#include "GasHandlers.h"
#include "UE_MCP_BridgeModule.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/Blueprint.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/BlueprintFactory.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "EdGraphSchema_K2.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "Engine/DataTable.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "ScalableFloat.h"
#include "GameplayTagsManager.h"

// Resolve a FGameplayAttribute by name across every loaded AttributeSet
// subclass. Accepts a bare property name ("Health") or a qualified
// "SetClassSubstring.Attribute". Returns an invalid attribute on miss.
// OutSetName reports the attribute set class this resolved against. A BARE
// attribute name is answered by the first matching set TObjectIterator reaches,
// and iterator order is not stable, so anything that has to name the attribute
// back - a rollback record above all - must carry the qualified form or it can
// land on a different set's attribute of the same name on the next call.
static FGameplayAttribute FindGameplayAttributeByName(const FString& Name, FString* OutSetName = nullptr)
{
	FString SetFilter, AttrName = Name;
	if (Name.Contains(TEXT(".")))
	{
		Name.Split(TEXT("."), &SetFilter, &AttrName);
	}
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* C = *It;
		if (C == UAttributeSet::StaticClass() || !C->IsChildOf(UAttributeSet::StaticClass())) continue;
		if (!SetFilter.IsEmpty() && !C->GetName().Contains(SetFilter)) continue;
		for (TFieldIterator<FProperty> P(C); P; ++P)
		{
			FStructProperty* SP = CastField<FStructProperty>(*P);
			if (SP && SP->Struct == FGameplayAttributeData::StaticStruct() && SP->GetName() == AttrName)
			{
				if (OutSetName) *OutSetName = C->GetName();
				return FGameplayAttribute(SP);
			}
		}
	}
	return FGameplayAttribute();
}

void FGasHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("create_gameplay_effect"), &CreateGameplayEffect);
	Registry.RegisterHandler(TEXT("get_gas_info"), &GetGasInfo);
	Registry.RegisterHandler(TEXT("create_gameplay_ability"), &CreateGameplayAbility);
	Registry.RegisterHandler(TEXT("create_attribute_set"), &CreateAttributeSet);
	Registry.RegisterHandler(TEXT("create_gameplay_cue"), &CreateGameplayCue);
	Registry.RegisterHandler(TEXT("add_ability_system_component"), &AddAbilitySystemComponent);
	Registry.RegisterHandler(TEXT("add_attribute"), &AddAttribute);
	Registry.RegisterHandler(TEXT("set_ability_tags"), &SetAbilityTags);
	Registry.RegisterHandler(TEXT("set_effect_modifier"), &SetEffectModifier);
	Registry.RegisterHandler(TEXT("set_asc_defaults"), &SetAscDefaults);
	Registry.RegisterHandler(TEXT("apply_effect"), &ApplyEffect);
	Registry.RegisterHandler(TEXT("remove_effect"), &RemoveEffect);
	Registry.RegisterHandler(TEXT("set_attribute"), &SetAttribute);
	Registry.RegisterHandler(TEXT("get_attribute"), &GetAttribute);
	Registry.RegisterHandler(TEXT("init_asc"), &InitAsc);
	Registry.RegisterHandler(TEXT("get_asc_state"), &GetAscState);
	Registry.RegisterHandler(TEXT("get_live_attribute_value"), &GetLiveAttributeValue);
	Registry.RegisterHandler(TEXT("set_live_attribute_value"), &SetLiveAttributeValue);
	Registry.RegisterHandler(TEXT("grant_ability"), &GrantAbility);
	Registry.RegisterHandler(TEXT("revoke_ability"), &RevokeAbility);
	Registry.RegisterHandler(TEXT("get_active_effects"), &GetActiveEffects);
	Registry.RegisterHandler(TEXT("trace_ability_activation"), &TraceAbilityActivation);

	// Input binding, cues and the attribute audit (GasHandlers_Abilities.cpp).
	Registry.RegisterHandler(TEXT("bind_ability_input"), &BindAbilityInput);
	Registry.RegisterHandler(TEXT("clear_ability_input"), &ClearAbilityInput);
	Registry.RegisterHandler(TEXT("send_ability_input"), &SendAbilityInput);
	Registry.RegisterHandler(TEXT("add_effect_cue"), &AddEffectCue);
	Registry.RegisterHandler(TEXT("remove_effect_cue"), &RemoveEffectCue);
	Registry.RegisterHandler(TEXT("validate_cue_coverage"), &ValidateCueCoverage);
	// Named audit_attributes, not audit_attribute_set: the read/mutate lexicon
	// takes a mutate verb anywhere in an action name and "set" is one, so the
	// longer spelling would have been gated as a write it never performs.
	Registry.RegisterHandler(TEXT("audit_attributes"), &AuditAttributeSet);

	// Snapshot and diff (GasHandlers_Snapshot.cpp).
	Registry.RegisterHandler(TEXT("capture_gas_state"), &CaptureGasState);
	Registry.RegisterHandler(TEXT("compare_gas_states"), &CompareGasStates);
	Registry.RegisterHandler(TEXT("list_gas_snapshots"), &ListGasSnapshots);
	Registry.RegisterHandler(TEXT("delete_gas_snapshot"), &DeleteGasSnapshot);
}

TSharedPtr<FJsonValue> FGasHandlers::CreateGasBlueprint(
	const TSharedPtr<FJsonObject>& Params,
	const FString& DefaultPackagePath,
	UClass* ParentClass,
	const FString& FriendlyType,
	TFunction<void(TSharedPtr<FJsonObject>&)> ExtraResultFields)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), DefaultPackagePath);
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (!ParentClass)
	{
		return MCPError(FString::Printf(TEXT("%s parent class not found. Enable GameplayAbilities plugin."), *FriendlyType));
	}

	UBlueprintFactory* BlueprintFactory = NewObject<UBlueprintFactory>();
	BlueprintFactory->ParentClass = ParentClass;

	auto Created = MCPCreateAssetIdempotent<UBlueprint>(Name, PackagePath, OnConflict, FriendlyType, BlueprintFactory);
	if (Created.EarlyReturn) return Created.EarlyReturn;
	UBlueprint* NewBlueprint = Created.Asset;

	NewBlueprint->ParentClass = ParentClass;
	FKismetEditorUtilities::CompileBlueprint(NewBlueprint);

	SaveAssetPackage(NewBlueprint);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), NewBlueprint->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	if (ExtraResultFields) ExtraResultFields(Result);
	MCPSetDeleteAssetRollback(Result, NewBlueprint->GetPathName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::CreateGameplayEffect(const TSharedPtr<FJsonObject>& Params)
{
	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] CreateGameplayEffect called"));

	const FString DurationPolicy = OptionalString(Params, TEXT("durationPolicy"), TEXT("Instant"));
	UClass* Cls = FindObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.GameplayEffect"));

	return CreateGasBlueprint(
		Params, TEXT("/Game/GAS/Effects"), Cls, TEXT("GameplayEffect"),
		[&DurationPolicy](TSharedPtr<FJsonObject>& R)
		{
			R->SetStringField(TEXT("durationPolicy"), DurationPolicy);
		});
}

TSharedPtr<FJsonValue> FGasHandlers::GetGasInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		// Return success with empty info rather than crashing
		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
		Result->SetBoolField(TEXT("hasGasComponents"), false);
		Result->SetStringField(TEXT("info"), TEXT("Blueprint not found or has no generated class"));
		return MCPResult(Result);
	}

	UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
	if (!CDO)
	{
		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
		Result->SetBoolField(TEXT("hasGasComponents"), false);
		Result->SetStringField(TEXT("info"), TEXT("No CDO available"));
		return MCPResult(Result);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetStringField(TEXT("className"), Blueprint->GeneratedClass->GetName());
	Result->SetStringField(TEXT("parentClass"), Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT("None"));

	// Check for GAS-related components
	bool bHasGasComponents = false;
	TArray<TSharedPtr<FJsonValue>> ComponentArray;

	// Check if the class has an AbilitySystemComponent
	UClass* ASCClass = FindObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.AbilitySystemComponent"));
	if (ASCClass && CDO->IsA(AActor::StaticClass()))
	{
		AActor* ActorCDO = Cast<AActor>(CDO);
		if (ActorCDO)
		{
			TArray<UActorComponent*> Components;
			ActorCDO->GetComponents(Components);
			for (UActorComponent* Comp : Components)
			{
				if (Comp && Comp->IsA(ASCClass))
				{
					bHasGasComponents = true;
					TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
					CompObj->SetStringField(TEXT("name"), Comp->GetName());
					CompObj->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
					ComponentArray.Add(MakeShared<FJsonValueObject>(CompObj));
				}
			}
		}
	}

	Result->SetBoolField(TEXT("hasGasComponents"), bHasGasComponents);
	Result->SetArrayField(TEXT("gasComponents"), ComponentArray);

	// Check if this is a GameplayEffect subclass
	UClass* GEClass = FindObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.GameplayEffect"));
	Result->SetBoolField(TEXT("isGameplayEffect"), GEClass && Blueprint->GeneratedClass->IsChildOf(GEClass));

	// Check if this is a GameplayAbility subclass
	UClass* GAClass = FindObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.GameplayAbility"));
	Result->SetBoolField(TEXT("isGameplayAbility"), GAClass && Blueprint->GeneratedClass->IsChildOf(GAClass));

	// Check if this is an AttributeSet subclass
	UClass* AttrSetClass = FindObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.AttributeSet"));
	Result->SetBoolField(TEXT("isAttributeSet"), AttrSetClass && Blueprint->GeneratedClass->IsChildOf(AttrSetClass));

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::CreateGameplayAbility(const TSharedPtr<FJsonObject>& Params)
{
	UClass* Cls = FindObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.GameplayAbility"));
	return CreateGasBlueprint(Params, TEXT("/Game/GAS/Abilities"), Cls, TEXT("GameplayAbility"));
}

TSharedPtr<FJsonValue> FGasHandlers::CreateAttributeSet(const TSharedPtr<FJsonObject>& Params)
{
	UClass* Cls = FindObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.AttributeSet"));
	return CreateGasBlueprint(Params, TEXT("/Game/GAS/Attributes"), Cls, TEXT("AttributeSet"));
}

TSharedPtr<FJsonValue> FGasHandlers::CreateGameplayCue(const TSharedPtr<FJsonObject>& Params)
{
	const FString CueType = OptionalString(Params, TEXT("cueType"), TEXT("Static"));
	const TCHAR* ParentPath = CueType == TEXT("Actor")
		? TEXT("/Script/GameplayAbilities.GameplayCueNotify_Actor")
		: TEXT("/Script/GameplayAbilities.GameplayCueNotify_Static");
	UClass* Cls = FindObject<UClass>(nullptr, ParentPath);

	return CreateGasBlueprint(
		Params, TEXT("/Game/GAS/Cues"), Cls, TEXT("GameplayCue"),
		[&CueType](TSharedPtr<FJsonObject>& R)
		{
			R->SetStringField(TEXT("cueType"), CueType);
		});
}

TSharedPtr<FJsonValue> FGasHandlers::AddAbilitySystemComponent(const TSharedPtr<FJsonObject>& Params)
{
	FString BPPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BPPath)) return Err;

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BPPath));
	if (!BP)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *BPPath));
	}

	UClass* ASCClass = FindObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.AbilitySystemComponent"));
	if (!ASCClass)
	{
		return MCPError(TEXT("AbilitySystemComponent not found. Enable GameplayAbilities plugin."));
	}

	FString CompName = OptionalString(Params, TEXT("componentName"), TEXT("AbilitySystemComp"));

	// Idempotency: existing ASC on the blueprint?
	if (BP->SimpleConstructionScript)
	{
		const FName CompFName(*CompName);
		for (USCS_Node* N : BP->SimpleConstructionScript->GetAllNodes())
		{
			if (!N || !N->ComponentTemplate) continue;
			if (N->ComponentTemplate->GetClass() == ASCClass || N->GetVariableName() == CompFName)
			{
				auto Existed = MCPSuccess();
				MCPSetExisted(Existed);
				Existed->SetStringField(TEXT("blueprintPath"), BPPath);
				Existed->SetStringField(TEXT("component"), N->GetVariableName().ToString());
				return MCPResult(Existed);
			}
		}
	}

	USCS_Node* NewNode = BP->SimpleConstructionScript->CreateNode(ASCClass, *CompName);
	if (NewNode)
	{
		BP->SimpleConstructionScript->AddNode(NewNode);
		FKismetEditorUtilities::CompileBlueprint(BP);

		SaveAssetPackage(BP);
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("blueprintPath"), BPPath);
	Result->SetStringField(TEXT("component"), CompName);

	// Rollback: remove_component
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), BPPath);
	Payload->SetStringField(TEXT("componentName"), CompName);
	MCPSetRollback(Result, TEXT("remove_component"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::AddAttribute(const TSharedPtr<FJsonObject>& Params)
{
	FString BPPath;
	if (auto Err = RequireString(Params, TEXT("attributeSetPath"), BPPath)) return Err;

	FString AttrName;
	if (auto Err = RequireString(Params, TEXT("attributeName"), AttrName)) return Err;

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BPPath));
	if (!BP)
	{
		return MCPError(FString::Printf(TEXT("AttributeSet Blueprint not found: %s"), *BPPath));
	}

	// Idempotency: member variable with this name already present?
	const FName AttrFName(*AttrName);
	for (const FBPVariableDescription& V : BP->NewVariables)
	{
		if (V.VarName == AttrFName)
		{
			auto Existed = MCPSuccess();
			MCPSetExisted(Existed);
			Existed->SetStringField(TEXT("attributeSetPath"), BPPath);
			Existed->SetStringField(TEXT("attributeName"), AttrName);
			return MCPResult(Existed);
		}
	}

	// Add a FGameplayAttributeData variable
	FEdGraphPinType PinType;
	PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	UScriptStruct* AttrStruct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/GameplayAbilities.GameplayAttributeData"));
	if (AttrStruct)
	{
		PinType.PinSubCategoryObject = AttrStruct;
	}

	FBlueprintEditorUtils::AddMemberVariable(BP, AttrFName, PinType);
	FKismetEditorUtilities::CompileBlueprint(BP);

	SaveAssetPackage(BP);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("attributeSetPath"), BPPath);
	Result->SetStringField(TEXT("attributeName"), AttrName);

	// Rollback: delete_variable
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), BPPath);
	Payload->SetStringField(TEXT("name"), AttrName);
	MCPSetRollback(Result, TEXT("delete_variable"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::SetAbilityTags(const TSharedPtr<FJsonObject>& Params)
{
	FString AbilityPath;
	if (auto Err = RequireString(Params, TEXT("abilityPath"), AbilityPath)) return Err;

	TSharedPtr<FJsonValue> CdoErr;
	UObject* CDO = LoadBlueprintCDO<UObject>(AbilityPath, CdoErr);
	if (!CDO) return CdoErr;

	// param name -> FGameplayTagContainer UPROPERTY on UGameplayAbility.
	const TArray<TPair<FString, FString>> TagMap = {
		{TEXT("ability_tags"), TEXT("AbilityTags")},
		{TEXT("cancel_abilities_with_tag"), TEXT("CancelAbilitiesWithTag")},
		{TEXT("block_abilities_with_tag"), TEXT("BlockAbilitiesWithTag")},
		{TEXT("activation_required_tags"), TEXT("ActivationRequiredTags")},
		{TEXT("activation_blocked_tags"), TEXT("ActivationBlockedTags")},
	};

	TSharedPtr<FJsonObject> Applied = MakeShared<FJsonObject>();
	// The tags each container held before this call, keyed by the SAME param
	// name, so the record is a straight replay of this action with the previous
	// values. Only the keys this call actually writes are captured, because a
	// key the caller did not pass was not touched and must not be rewritten.
	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	TArray<FString> Unsupported;
	// Two different questions: bAnyApplied is "did the caller name a container
	// this handler writes", bAnyChanged is "did any of those containers end up
	// holding something different". Only the second one is idempotency.
	bool bAnyApplied = false;
	bool bAnyChanged = false;

	for (const TPair<FString, FString>& Entry : TagMap)
	{
		const TArray<TSharedPtr<FJsonValue>>* TagArray = nullptr;
		if (!Params->TryGetArrayField(*Entry.Key, TagArray) || !TagArray) continue;

		FStructProperty* Prop = CastField<FStructProperty>(CDO->GetClass()->FindPropertyByName(*Entry.Value));
		if (!Prop || Prop->Struct != FGameplayTagContainer::StaticStruct())
		{
			// Engine-version drift (e.g. AbilityTags deprecated): report, don't fake.
			Unsupported.Add(Entry.Value);
			continue;
		}

		FGameplayTagContainer* Container = Prop->ContainerPtrToValuePtr<FGameplayTagContainer>(CDO);
		// Read before the Reset below, or the record restores an empty container
		// instead of what was there. The copy is also what the change check at
		// the end of the loop compares against, so "unchanged" means the tags
		// really are the same rather than merely that the caller passed a
		// container this handler recognises.
		const FGameplayTagContainer PreviousContainer = *Container;
		{
			TArray<TSharedPtr<FJsonValue>> PreviousTags;
			for (const FGameplayTag& Tag : PreviousContainer)
			{
				PreviousTags.Add(MakeShared<FJsonValueString>(Tag.GetTagName().ToString()));
			}
			RollbackPayload->SetArrayField(Entry.Key, PreviousTags);
		}
		Container->Reset();
		TArray<TSharedPtr<FJsonValue>> Wrote;
		for (const TSharedPtr<FJsonValue>& TagVal : *TagArray)
		{
			FString TagStr;
			if (!TagVal.IsValid() || !TagVal->TryGetString(TagStr)) continue;
			const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*TagStr), /*ErrorIfNotFound*/ false);
			if (Tag.IsValid())
			{
				Container->AddTag(Tag);
				Wrote.Add(MakeShared<FJsonValueString>(TagStr));
			}
			else
			{
				Wrote.Add(MakeShared<FJsonValueString>(TagStr + TEXT(" (unregistered tag - skipped)")));
			}
		}
		Applied->SetArrayField(Entry.Key, Wrote);
		bAnyApplied = true;
		// HasAllExact both ways is set equality for a tag container, which is
		// what "did this write change anything" actually asks.
		if (!(Container->HasAllExact(PreviousContainer) && PreviousContainer.HasAllExact(*Container)))
		{
			bAnyChanged = true;
		}
	}

	if (bAnyApplied)
	{
		CDO->MarkPackageDirty();
		SaveAssetPackage(CDO);
	}

	auto Result = MCPSuccess();
	if (bAnyChanged) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("updated"), false);
	Result->SetBoolField(TEXT("unchanged"), !bAnyChanged);
	Result->SetBoolField(TEXT("containersWritten"), bAnyApplied);
	Result->SetStringField(TEXT("abilityPath"), AbilityPath);
	Result->SetObjectField(TEXT("applied"), Applied);
	if (Unsupported.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> U;
		for (const FString& S : Unsupported) U.Add(MakeShared<FJsonValueString>(S));
		Result->SetArrayField(TEXT("unsupportedProperties"), U);
	}

	if (bAnyChanged)
	{
		// Each container is written whole (Reset then re-add), so the inverse is
		// the same call carrying the tags that were in it. Every restored tag was
		// registered at read time, so nothing in the record can be skipped as an
		// unknown tag the way an authored list can.
		RollbackPayload->SetStringField(TEXT("abilityPath"), AbilityPath);
		MCPSetRollback(Result, TEXT("set_ability_tags"), RollbackPayload);
		Result->SetBoolField(TEXT("rollbackLossy"), false);
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), bAnyApplied
			? TEXT("Every container this call wrote ended up holding exactly the tags it already held, so nothing "
				   "changed and there is nothing to undo.")
			: TEXT("No tag container was written, so there is nothing to undo. Pass one of ability_tags, "
				   "cancel_abilities_with_tag, block_abilities_with_tag, activation_required_tags or "
				   "activation_blocked_tags to change something."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::SetEffectModifier(const TSharedPtr<FJsonObject>& Params)
{
	FString EffectPath;
	if (auto Err = RequireString(Params, TEXT("effectPath"), EffectPath)) return Err;

	FString Attribute;
	if (auto Err = RequireString(Params, TEXT("attribute"), Attribute)) return Err;

	const FString Operation = OptionalString(Params, TEXT("operation"), TEXT("Additive"));
	const double Magnitude = OptionalNumber(Params, TEXT("magnitude"), 0.0);

	TSharedPtr<FJsonValue> CdoErr;
	UGameplayEffect* GE = LoadBlueprintCDO<UGameplayEffect>(EffectPath, CdoErr);
	if (!GE) return CdoErr;

	FString ResolvedSetName;
	const FGameplayAttribute GAttr = FindGameplayAttributeByName(Attribute, &ResolvedSetName);
	if (!GAttr.IsValid())
	{
		return MCPError(FString::Printf(
			TEXT("Attribute not found: %s. Use 'SetName.Attribute' (or a unique attribute name); the AttributeSet must be compiled/loaded."), *Attribute));
	}

	const FString Op = Operation.ToLower();
	EGameplayModOp::Type ModOp;
	if (Op == TEXT("additive") || Op == TEXT("add")) ModOp = EGameplayModOp::Additive;
	else if (Op == TEXT("multiplicative") || Op == TEXT("multiply") || Op == TEXT("multiplicitive")) ModOp = EGameplayModOp::Multiplicitive;
	else if (Op == TEXT("division") || Op == TEXT("divide")) ModOp = EGameplayModOp::Division;
	else if (Op == TEXT("override")) ModOp = EGameplayModOp::Override;
	else return MCPError(FString::Printf(TEXT("Unknown operation '%s' (Additive|Multiplicative|Division|Override)"), *Operation));

	// Update an existing modifier for the same attribute+op, else append one.
	bool bUpdated = false;
	// The magnitude the existing modifier held, read before it is overwritten.
	// Only meaningful when this call updates rather than appends, and only when
	// the magnitude is the ScalableFloat form this action writes.
	float PreviousMagnitude = 0.f;
	bool bPreviousMagnitudeReadable = false;
	bool bPreviousMagnitudeLevelDependent = false;
	for (FGameplayModifierInfo& M : GE->Modifiers)
	{
		if (M.Attribute == GAttr && M.ModifierOp == ModOp)
		{
			// GetStaticMagnitudeIfPossible answers for ANY ScalableFloat,
			// including one bound to a curve table, and hands back that curve's
			// value at the level asked for. Writing it back as a plain constant
			// would silently replace the binding, so the magnitude is probed at
			// three levels and only a value that does not move with level is
			// treated as a constant this action can restore.
			float AtLevel1 = 0.f, AtLevel2 = 0.f, AtLevel10 = 0.f;
			if (M.ModifierMagnitude.GetStaticMagnitudeIfPossible(1.f, AtLevel1)
				&& M.ModifierMagnitude.GetStaticMagnitudeIfPossible(2.f, AtLevel2)
				&& M.ModifierMagnitude.GetStaticMagnitudeIfPossible(10.f, AtLevel10))
			{
				PreviousMagnitude = AtLevel1;
				bPreviousMagnitudeReadable = true;
				bPreviousMagnitudeLevelDependent = (AtLevel2 != AtLevel1) || (AtLevel10 != AtLevel1);
			}
			M.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(static_cast<float>(Magnitude)));
			bUpdated = true;
			break;
		}
	}
	if (!bUpdated)
	{
		FGameplayModifierInfo ModInfo;
		ModInfo.Attribute = GAttr;
		ModInfo.ModifierOp = ModOp;
		ModInfo.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(static_cast<float>(Magnitude)));
		GE->Modifiers.Add(ModInfo);
	}

	GE->MarkPackageDirty();
	SaveAssetPackage(GE);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	// Qualified with the set it resolved against, so the inverse below and any
	// caller echoing this back address the same attribute rather than the first
	// same-named one the class iterator happens to reach next time.
	const FString QualifiedAttribute = ResolvedSetName.IsEmpty()
		? GAttr.GetName() : (ResolvedSetName + TEXT(".") + GAttr.GetName());
	Result->SetStringField(TEXT("effectPath"), EffectPath);
	Result->SetStringField(TEXT("attribute"), GAttr.GetName());
	Result->SetStringField(TEXT("qualifiedAttribute"), QualifiedAttribute);
	Result->SetStringField(TEXT("attributeSet"), ResolvedSetName);
	Result->SetStringField(TEXT("operation"), Operation);
	Result->SetNumberField(TEXT("magnitude"), Magnitude);
	Result->SetBoolField(TEXT("replacedExisting"), bUpdated);
	Result->SetNumberField(TEXT("modifierCount"), GE->Modifiers.Num());

	if (bUpdated && bPreviousMagnitudeReadable && !bPreviousMagnitudeLevelDependent)
	{
		// Overwriting a magnitude inverts to writing the old one back, keyed on
		// the same attribute and operation this call matched on.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("effectPath"), EffectPath);
		Payload->SetStringField(TEXT("attribute"), QualifiedAttribute);
		Payload->SetStringField(TEXT("operation"), Operation);
		Payload->SetNumberField(TEXT("magnitude"), PreviousMagnitude);
		MCPSetRollback(Result, TEXT("set_effect_modifier"), Payload);
		// The value comes back, but as a plain ScalableFloat constant. A
		// modifier that had been authored as a curve-table lookup whose value
		// happens not to move with level would come back as that constant, with
		// the binding gone.
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The previous magnitude is restored as a plain ScalableFloat constant, which is the only form this "
				 "action writes. It was read at effect levels 1, 2 and 10 and did not move, so no level scaling is "
				 "lost; a curve-table binding that is flat across those levels would still be replaced by the "
				 "constant. Read the effect's Modifiers with asset(get_property) first if the binding matters."));
	}
	else
	{
		// Three cases, each honest about why no record is emitted.
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), !bUpdated
			? TEXT("This APPENDED a new modifier, and no action removes a modifier from a GameplayEffect, so there is "
				   "no inverse call. Writing a magnitude of zero would leave the modifier in place rather than undo it. "
				   "Delete and recreate the effect, or edit its Modifiers array directly, to drop one.")
			: (bPreviousMagnitudeReadable
				? TEXT("The magnitude that was overwritten CHANGES WITH EFFECT LEVEL - a curve-table-backed "
					   "ScalableFloat, read at levels 1, 2 and 10 and different at each. This action writes a single "
					   "constant, so restoring it would replace the curve binding with one number and silently break "
					   "every other level. No inverse is emitted; recover the binding from source control or read it "
					   "with asset(get_property).")
				: TEXT("The modifier that was overwritten did not carry a static magnitude - it was attribute-based, "
					   "SetByCaller or a custom calculation - and this action only writes the ScalableFloat form, so "
					   "the previous magnitude cannot be expressed as an inverse call. Read the effect's Modifiers "
					   "with asset(get_property) before overwriting one of those.")));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::SetAscDefaults(const TSharedPtr<FJsonObject>& Params)
{
	FString BPPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BPPath)) return Err;

	FString AttrSetSpec;
	if (auto Err = RequireStringAlt(Params, TEXT("attributeSet"), TEXT("attributeSetPath"), AttrSetSpec)) return Err;

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BPPath));
	if (!BP) return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *BPPath));

	UClass* ASCClass = FindObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.AbilitySystemComponent"));
	if (!ASCClass) return MCPError(TEXT("AbilitySystemComponent not found. Enable GameplayAbilities plugin."));

	// Resolve the AttributeSet class from a content path (BP generated class) or
	// a native short name.
	UClass* AttrSetClass = nullptr;
	{
		UClass* AttrBase = UAttributeSet::StaticClass();
		auto Ok = [AttrBase](UClass* C) { return C && C->IsChildOf(AttrBase); };
		if (AttrSetSpec.Contains(TEXT("/")))
		{
			if (UClass* C = LoadObject<UClass>(nullptr, *AttrSetSpec); Ok(C)) AttrSetClass = C;
			if (!AttrSetClass)
			{
				FString AssetName;
				AttrSetSpec.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
				if (UClass* C = LoadObject<UClass>(nullptr, *(AttrSetSpec + TEXT(".") + AssetName + TEXT("_C"))); Ok(C)) AttrSetClass = C;
			}
			if (!AttrSetClass)
			{
				if (UBlueprint* SetBP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(AttrSetSpec)))
				{
					if (Ok(SetBP->GeneratedClass)) AttrSetClass = SetBP->GeneratedClass;
				}
			}
		}
		else if (UClass* C = FindClassByShortName(AttrSetSpec); Ok(C))
		{
			AttrSetClass = C;
		}
	}
	if (!AttrSetClass) return MCPError(FString::Printf(TEXT("AttributeSet class not found: %s"), *AttrSetSpec));

	// Find the ASC component template on the blueprint's construction script.
	const FString CompName = OptionalString(Params, TEXT("componentName"));
	UAbilitySystemComponent* ASCTemplate = nullptr;
	FString ResolvedComp;
	if (BP->SimpleConstructionScript)
	{
		for (USCS_Node* N : BP->SimpleConstructionScript->GetAllNodes())
		{
			if (!N || !N->ComponentTemplate || !N->ComponentTemplate->IsA(ASCClass)) continue;
			if (!CompName.IsEmpty() && N->GetVariableName() != FName(*CompName)) continue;
			ASCTemplate = Cast<UAbilitySystemComponent>(N->ComponentTemplate);
			ResolvedComp = N->GetVariableName().ToString();
			break;
		}
	}
	if (!ASCTemplate)
	{
		return MCPError(TEXT("No AbilitySystemComponent on the blueprint - run add_ability_system_component first"));
	}

	// Optional init DataTable (production path: starting values at ASC init).
	UDataTable* InitTable = nullptr;
	const FString TablePath = OptionalString(Params, TEXT("initDataTable"));
	if (!TablePath.IsEmpty())
	{
		InitTable = LoadObject<UDataTable>(nullptr, *TablePath);
		if (!InitTable) return MCPError(FString::Printf(TEXT("initDataTable not found: %s"), *TablePath));
	}

	// Idempotency: already wired for this attribute set?
	for (const FAttributeDefaults& D : ASCTemplate->DefaultStartingData)
	{
		if (D.Attributes == AttrSetClass)
		{
			auto Existed = MCPSuccess();
			MCPSetExisted(Existed);
			Existed->SetStringField(TEXT("blueprintPath"), BPPath);
			Existed->SetStringField(TEXT("component"), ResolvedComp);
			Existed->SetStringField(TEXT("attributeSet"), AttrSetClass->GetName());
			return MCPResult(Existed);
		}
	}

	FAttributeDefaults Def;
	Def.Attributes = AttrSetClass;
	Def.DefaultStartingTable = InitTable;
	ASCTemplate->DefaultStartingData.Add(Def);

	FKismetEditorUtilities::CompileBlueprint(BP);
	SaveAssetPackage(BP);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("blueprintPath"), BPPath);
	Result->SetStringField(TEXT("component"), ResolvedComp);
	Result->SetStringField(TEXT("attributeSet"), AttrSetClass->GetName());
	if (InitTable) Result->SetStringField(TEXT("initDataTable"), InitTable->GetPathName());
	Result->SetStringField(TEXT("note"),
		TEXT("Attribute set wired to the ASC's DefaultStartingData. If attributes aren't live at runtime, call gas(action=\"init_asc\", attributeSet=...) after PIE starts."));
	// No inverse. This appends an FAttributeDefaults entry to the ASC component
	// template's DefaultStartingData, and no action removes one: the array is
	// only reachable through this call, which adds. blueprint(remove_component)
	// would delete the whole AbilitySystemComponent, which undoes far more than
	// this did whenever the component was there first.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("No action removes an entry from an AbilitySystemComponent's DefaultStartingData, so wiring an attribute "
			 "set to it has no inverse call. Removing the whole component would undo more than this call did. Edit the "
			 "component template's DefaultStartingData directly if the entry has to go."));
	return MCPResult(Result);
}
