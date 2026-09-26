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

FGameplayAttribute MCPGas::FindAttributeAcrossSets(const FString& Name, FString* OutSetName)
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
	// Reports parameters its handlers never read (#1057).
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("gas"));

	// #1057: a handler registered with a spec declares its parameters here and
	// nowhere else; the TS surface for it is generated from a recording of these.
	// The create_* handlers are contract-exempt: the contract test's name and
	// package path would reach AssetTools.CreateAsset.
	using EType = EMCPParamType;
	auto ActorLabel = []()
	{
		return MCPParam::Optional(TEXT("actorLabel"), EType::String, TEXT("Live actor label, internal name or object path. Pass this or actorPath"));
	};
	auto ActorPath = []()
	{
		return MCPParam::Optional(TEXT("actorPath"), EType::String, TEXT("Full actor object path. The unambiguous selector, and it wins over actorLabel when both are given"));
	};
	auto World = []()
	{
		return MCPParam::Optional(TEXT("world"), EType::String, TEXT("Runtime world scope: auto (default) | pie | editor"));
	};
	auto AbilityClass = [](const TCHAR* Description)
	{
		return MCPParam::Required(TEXT("abilityClass"), EType::String, Description);
	};
	auto BlueprintName = []()
	{
		return MCPParam::Required(TEXT("name"), EType::String, TEXT("Blueprint name"));
	};
	auto BlueprintFolder = [](const TCHAR* Description)
	{
		return MCPParam::Optional(TEXT("packagePath"), EType::String, Description);
	};
	auto BlueprintOnConflict = []()
	{
		return MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("skip (default, returns the existing asset) or error when the asset already exists"));
	};
	const FMCPSpecRules CreatesBlueprint = MCPSpec::ContractExempt(
		TEXT("Creates, compiles and saves a Blueprint under the contract values; nothing it reads fails first"));

	Registry.RegisterHandler(TEXT("create_gameplay_effect"), &CreateGameplayEffect, {
		BlueprintName(), BlueprintFolder(TEXT("Content folder (default /Game/GAS/Effects)")), BlueprintOnConflict(),
		MCPParam::Optional(TEXT("durationPolicy"), EType::String, TEXT("Echoed back as durationPolicy (default Instant); it is not written onto the effect")),
	}, CreatesBlueprint);
	Registry.RegisterHandler(TEXT("get_gas_info"), &GetGasInfo, {
		MCPParam::Required(TEXT("blueprintPath"), EType::String, TEXT("Blueprint asset path to inspect")),
	});
	Registry.RegisterHandler(TEXT("create_gameplay_ability"), &CreateGameplayAbility, {
		BlueprintName(), BlueprintFolder(TEXT("Content folder (default /Game/GAS/Abilities)")), BlueprintOnConflict(),
	}, CreatesBlueprint);
	Registry.RegisterHandler(TEXT("create_attribute_set"), &CreateAttributeSet, {
		BlueprintName(), BlueprintFolder(TEXT("Content folder (default /Game/GAS/Attributes)")), BlueprintOnConflict(),
	}, CreatesBlueprint);
	Registry.RegisterHandler(TEXT("create_gameplay_cue"), &CreateGameplayCue, {
		BlueprintName(), BlueprintFolder(TEXT("Content folder (default /Game/GAS/Cues)")), BlueprintOnConflict(),
		MCPParam::Optional(TEXT("cueType"), EType::String, TEXT("Static (default, GameplayCueNotify_Static) | Actor (GameplayCueNotify_Actor)")),
	}, CreatesBlueprint);
	Registry.RegisterHandler(TEXT("add_ability_system_component"), &AddAbilitySystemComponent, {
		MCPParam::Required(TEXT("blueprintPath"), EType::String, TEXT("Blueprint asset path")),
		MCPParam::Optional(TEXT("componentName"), EType::String, TEXT("Name of the AbilitySystemComponent (default AbilitySystemComp)")),
	});
	Registry.RegisterHandler(TEXT("add_attribute"), &AddAttribute, {
		MCPParam::Required(TEXT("attributeSetPath"), EType::String, TEXT("AttributeSet Blueprint asset path")),
		MCPParam::Required(TEXT("attributeName"), EType::String, TEXT("FGameplayAttributeData variable to add")),
	});
	Registry.RegisterHandler(TEXT("set_ability_tags"), &SetAbilityTags, {
		MCPParam::Required(TEXT("abilityPath"), EType::String, TEXT("GameplayAbility Blueprint asset path")),
		MCPParam::Optional(TEXT("ability_tags"), EType::Array, TEXT("AbilityTags container, written whole")).Items(EType::String),
		MCPParam::Optional(TEXT("cancel_abilities_with_tag"), EType::Array, TEXT("CancelAbilitiesWithTag container, written whole")).Items(EType::String),
		MCPParam::Optional(TEXT("block_abilities_with_tag"), EType::Array, TEXT("BlockAbilitiesWithTag container, written whole")).Items(EType::String),
		MCPParam::Optional(TEXT("activation_required_tags"), EType::Array, TEXT("ActivationRequiredTags container, written whole")).Items(EType::String),
		MCPParam::Optional(TEXT("activation_blocked_tags"), EType::Array, TEXT("ActivationBlockedTags container, written whole")).Items(EType::String),
	});
	Registry.RegisterHandler(TEXT("set_effect_modifier"), &SetEffectModifier, {
		MCPParam::Required(TEXT("effectPath"), EType::String, TEXT("GameplayEffect Blueprint asset path")),
		MCPParam::Required(TEXT("attribute"), EType::String, TEXT("Attribute to modify: SetName.Attribute, or a unique attribute name")),
		MCPParam::Optional(TEXT("operation"), EType::String, TEXT("Additive (default) | Multiplicative | Division | Override")),
		MCPParam::Optional(TEXT("magnitude"), EType::Number, TEXT("Static magnitude (default 0)")),
	});
	Registry.RegisterHandler(TEXT("set_asc_defaults"), &SetAscDefaults, {
		MCPParam::Required(TEXT("blueprintPath"), EType::String, TEXT("Blueprint asset path carrying the AbilitySystemComponent")),
		MCPParam::Required(TEXT("attributeSet"), EType::String, TEXT("AttributeSet content path or class name")).Alias(TEXT("attributeSetPath")),
		MCPParam::Optional(TEXT("componentName"), EType::String, TEXT("AbilitySystemComponent to wire (default: the first one)")),
		MCPParam::Optional(TEXT("initDataTable"), EType::String, TEXT("DataTable of starting attribute values")),
	});
	Registry.RegisterHandler(TEXT("apply_effect"), &ApplyEffect, {
		ActorLabel(),
		ActorPath(),
		MCPParam::Required(TEXT("effectClass"), EType::String, TEXT("GameplayEffect content path or class name")).Alias(TEXT("effectPath")),
		MCPParam::Optional(TEXT("level"), EType::Number, TEXT("Effect level (default 1)")),
		MCPParam::Optional(TEXT("setByCaller"), EType::Object, TEXT("SetByCaller magnitudes keyed by gameplay tag or name")),
		World(),
	});
	Registry.RegisterHandler(TEXT("remove_effect"), &RemoveEffect, {
		ActorLabel(),
		ActorPath(),
		MCPParam::Optional(TEXT("effectHandle"), EType::String, TEXT("Active-effect handle apply_effect reported. Removes exactly that effect")),
		MCPParam::Optional(TEXT("effectClass"), EType::String, TEXT("GameplayEffect content path or class name. Removes every active effect of that class")).Alias(TEXT("effectPath")),
		MCPParam::Optional(TEXT("stacksToRemove"), EType::Integer, TEXT("Stacks to take off (default -1, the whole effect)")),
		World(),
	});
	Registry.RegisterHandler(TEXT("set_attribute"), &SetAttribute, {
		ActorLabel(),
		ActorPath(),
		MCPParam::Required(TEXT("attribute"), EType::String, TEXT("Attribute name: Health or SetName.Health")),
		MCPParam::Required(TEXT("value"), EType::Number, TEXT("New base value")),
		World(),
	});
	Registry.RegisterHandler(TEXT("get_attribute"), &GetAttribute, {
		ActorLabel(),
		ActorPath(),
		MCPParam::Optional(TEXT("attribute"), EType::String, TEXT("Attribute to read. Omit to list every attribute")),
		World(),
	});
	Registry.RegisterHandler(TEXT("init_asc"), &InitAsc, {
		ActorLabel(),
		ActorPath(),
		MCPParam::Optional(TEXT("attributeSet"), EType::String, TEXT("AttributeSet content path or class name to make sure is registered")),
		World(),
	});
	Registry.RegisterHandler(TEXT("get_asc_state"), &GetAscState, {
		ActorLabel(),
		ActorPath(),
		World(),
	});
	Registry.RegisterHandler(TEXT("get_live_attribute_value"), &GetLiveAttributeValue, {
		ActorLabel(),
		ActorPath(),
		MCPParam::Required(TEXT("attributeSet"), EType::String, TEXT("AttributeSet content path or class name")),
		MCPParam::Required(TEXT("attribute"), EType::String, TEXT("Attribute property name, or Set.Property")),
		MCPParam::Optional(TEXT("registerOwnerSets"), EType::Boolean, TEXT("Register the actor's own attribute sets on its ASC when it has none, the way BeginPlay would (default true)")),
		World(),
	});
	Registry.RegisterHandler(TEXT("set_live_attribute_value"), &SetLiveAttributeValue, {
		ActorLabel(),
		ActorPath(),
		MCPParam::Required(TEXT("attributeSet"), EType::String, TEXT("AttributeSet content path or class name")),
		MCPParam::Required(TEXT("attribute"), EType::String, TEXT("Attribute property name, or Set.Property")),
		MCPParam::Required(TEXT("value"), EType::Number, TEXT("Value to write")),
		MCPParam::Optional(TEXT("valueType"), EType::String, TEXT("current (default, writes the attribute data in place) | base (writes through the ASC aggregator)")),
		MCPParam::Optional(TEXT("registerOwnerSets"), EType::Boolean, TEXT("Register the actor's own attribute sets on its ASC when it has none, the way BeginPlay would (default true)")),
		World(),
	});
	Registry.RegisterHandler(TEXT("grant_ability"), &GrantAbility, {
		ActorLabel(),
		ActorPath(),
		AbilityClass(TEXT("GameplayAbility Blueprint path, generated class path, or native class name")),
		MCPParam::Optional(TEXT("level"), EType::Number, TEXT("Ability level (default 1)")),
		MCPParam::Optional(TEXT("inputId"), EType::Integer, TEXT("InputID for the spec (default -1, unbound)")),
		World(),
	});
	Registry.RegisterHandler(TEXT("revoke_ability"), &RevokeAbility, {
		ActorLabel(),
		ActorPath(),
		AbilityClass(TEXT("GameplayAbility class to revoke")),
		World(),
	});
	Registry.RegisterHandler(TEXT("get_active_effects"), &GetActiveEffects, {
		ActorLabel(),
		ActorPath(),
		World(),
	});
	Registry.RegisterHandler(TEXT("trace_ability_activation"), &TraceAbilityActivation, {
		ActorLabel(),
		ActorPath(),
		AbilityClass(TEXT("GameplayAbility class to trace")),
		MCPParam::Optional(TEXT("activate"), EType::Boolean, TEXT("Also call TryActivateAbility, to prove the verdict (default false)")),
		World(),
	});
	Registry.RegisterHandler(TEXT("add_loose_gameplay_tag"), &AddLooseGameplayTag, {
		ActorLabel(),
		ActorPath(),
		MCPParam::Required(TEXT("tag"), EType::String, TEXT("A registered gameplay tag")),
		MCPParam::Optional(TEXT("count"), EType::Integer, TEXT("References to add, at least 1 (default 1)")),
		World(),
	});
	Registry.RegisterHandler(TEXT("remove_loose_gameplay_tag"), &RemoveLooseGameplayTag, {
		ActorLabel(),
		ActorPath(),
		MCPParam::Required(TEXT("tag"), EType::String, TEXT("A registered gameplay tag")),
		MCPParam::Optional(TEXT("count"), EType::Integer, TEXT("References to remove, at least 1 (default 1)")),
		World(),
	});

	// Input binding, cues and the attribute audit (GasHandlers_Abilities.cpp).
	Registry.RegisterHandler(TEXT("bind_ability_input"), &BindAbilityInput, {
		ActorLabel(),
		ActorPath(),
		AbilityClass(TEXT("Granted GameplayAbility class to bind")),
		MCPParam::Required(TEXT("inputId"), EType::Integer, TEXT("Input id to bind; -1 leaves the ability unbound")),
		World(),
	});
	Registry.RegisterHandler(TEXT("clear_ability_input"), &ClearAbilityInput, {
		ActorLabel(),
		ActorPath(),
		AbilityClass(TEXT("Granted GameplayAbility class to unbind")),
		World(),
	});
	Registry.RegisterHandler(TEXT("send_ability_input"), &SendAbilityInput, {
		ActorLabel(),
		ActorPath(),
		MCPParam::Optional(TEXT("inputEvent"), EType::String, TEXT("pressed (default) | released | confirm | cancel. pressed and released address an inputId; confirm and cancel take none")),
		MCPParam::Optional(TEXT("inputId"), EType::Integer, TEXT("Input id to send pressed or released to")),
		MCPParam::Optional(TEXT("abilityClass"), EType::String, TEXT("Granted ability whose bound input id to use instead of inputId")),
		World(),
	});
	Registry.RegisterHandler(TEXT("add_effect_cue"), &AddEffectCue, {
		MCPParam::Required(TEXT("effectPath"), EType::String, TEXT("GameplayEffect Blueprint asset path")).Alias(TEXT("effectClass")),
		MCPParam::Required(TEXT("cueTag"), EType::String, TEXT("Registered GameplayCue tag, under the GameplayCue root")),
		MCPParam::Optional(TEXT("minLevel"), EType::Number, TEXT("Lowest effect level this cue covers, used to normalise the magnitude")),
		MCPParam::Optional(TEXT("maxLevel"), EType::Number, TEXT("Highest effect level this cue covers")),
		MCPParam::Optional(TEXT("magnitudeAttribute"), EType::String, TEXT("Attribute the cue takes its magnitude from (SetName.Attribute), instead of the effect level")),
	});
	Registry.RegisterHandler(TEXT("remove_effect_cue"), &RemoveEffectCue, {
		MCPParam::Required(TEXT("effectPath"), EType::String, TEXT("GameplayEffect Blueprint asset path")).Alias(TEXT("effectClass")),
		MCPParam::Required(TEXT("cueTag"), EType::String, TEXT("GameplayCue tag to unlink. May be one that is no longer registered")),
	});
	Registry.RegisterHandler(TEXT("validate_cue_coverage"), &ValidateCueCoverage, {
		MCPParam::Optional(TEXT("directory"), EType::String, TEXT("Content path to scan (default /Game)")),
		MCPParam::Optional(TEXT("effectPath"), EType::String, TEXT("Audit this one GameplayEffect instead of scanning")).Alias(TEXT("effectClass")),
		MCPParam::Optional(TEXT("maxEffects"), EType::Integer, TEXT("Cap on effect classes scanned (default 500, max 5000)")),
	});
	// Named audit_attributes, not audit_attribute_set: the read/mutate lexicon
	// takes a mutate verb anywhere in an action name and "set" is one, so the
	// longer spelling would have been gated as a write it never performs.
	Registry.RegisterHandler(TEXT("audit_attributes"), &AuditAttributeSet, {
		MCPParam::Optional(TEXT("attributeSet"), EType::String, TEXT("AttributeSet content path or class name. Pass this, a live actor, or both")),
		ActorLabel(),
		ActorPath(),
		MCPParam::Optional(TEXT("probeClamping"), EType::Boolean, TEXT("Measure an existing clamp by driving the set's own PreAttributeChange. Needs a live registered set (default false)")),
		World(),
	});

	// Snapshot and diff (GasHandlers_Snapshot.cpp).
	Registry.RegisterHandler(TEXT("capture_gas_state"), &CaptureGasState, {
		ActorLabel(),
		ActorPath(),
		MCPParam::Optional(TEXT("snapshotId"), EType::String, TEXT("Id to store the snapshot under (generated when omitted)")),
		MCPParam::Optional(TEXT("compareWith"), EType::String, TEXT("Earlier snapshot id to diff this capture against")),
		MCPParam::Optional(TEXT("registerOwnerSets"), EType::Boolean, TEXT("Register the actor's own attribute sets on its ASC when it has none, the way BeginPlay would (default true)")),
		World(),
	});
	Registry.RegisterHandler(TEXT("compare_gas_states"), &CompareGasStates, {
		MCPParam::Optional(TEXT("beforeId"), EType::String, TEXT("Snapshot id of the earlier reading. Pass this or beforeSnapshot")),
		MCPParam::Optional(TEXT("beforeSnapshot"), EType::Object, TEXT("The earlier snapshot object itself")),
		MCPParam::Optional(TEXT("afterId"), EType::String, TEXT("Snapshot id of the later reading. Pass this or afterSnapshot")),
		MCPParam::Optional(TEXT("afterSnapshot"), EType::Object, TEXT("The later snapshot object itself")),
	});
	Registry.RegisterHandler(TEXT("list_gas_snapshots"), &ListGasSnapshots, {
		MCPParam::Optional(TEXT("actorPath"), EType::String, TEXT("Only snapshots of this actor object path")),
		MCPParam::Optional(TEXT("includeSnapshots"), EType::Boolean, TEXT("Return the full snapshot bodies rather than a summary row each")),
	});
	Registry.RegisterHandler(TEXT("delete_gas_snapshot"), &DeleteGasSnapshot, {
		MCPParam::Required(TEXT("snapshotId"), EType::String, TEXT("Snapshot id to drop")),
	});
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

	REQUIRE_ASSET(UBlueprint, Blueprint, BlueprintPath);
	if (!Blueprint->GeneratedClass)
	{
		return MCPError(FString::Printf(TEXT("Blueprint has no generated class: %s"), *BlueprintPath));
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
	// Read before anything can fail (#1057).
	FString CompName = OptionalString(Params, TEXT("componentName"), TEXT("AbilitySystemComp"));

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

	// param name -> FGameplayTagContainer UPROPERTY on UGameplayAbility.
	const TArray<TPair<FString, FString>> TagMap = {
		{TEXT("ability_tags"), TEXT("AbilityTags")},
		{TEXT("cancel_abilities_with_tag"), TEXT("CancelAbilitiesWithTag")},
		{TEXT("block_abilities_with_tag"), TEXT("BlockAbilitiesWithTag")},
		{TEXT("activation_required_tags"), TEXT("ActivationRequiredTags")},
		{TEXT("activation_blocked_tags"), TEXT("ActivationBlockedTags")},
	};

	// Every container is read before the CDO load can fail (#1057). A null entry
	// is a container the caller did not pass.
	TArray<const TArray<TSharedPtr<FJsonValue>>*> PassedArrays;
	for (const TPair<FString, FString>& Entry : TagMap)
	{
		const TArray<TSharedPtr<FJsonValue>>* TagArray = nullptr;
		PassedArrays.Add(TryGetArrayParam(Params, *Entry.Key, TagArray) ? TagArray : nullptr);
	}

	TSharedPtr<FJsonValue> CdoErr;
	UObject* CDO = LoadBlueprintCDO<UObject>(AbilityPath, CdoErr);
	if (!CDO) return CdoErr;

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

	for (int32 EntryIndex = 0; EntryIndex < TagMap.Num(); ++EntryIndex)
	{
		const TPair<FString, FString>& Entry = TagMap[EntryIndex];
		const TArray<TSharedPtr<FJsonValue>>* TagArray = PassedArrays[EntryIndex];
		if (!TagArray) continue;

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
	const FGameplayAttribute GAttr = MCPGas::FindAttributeAcrossSets(Attribute, &ResolvedSetName);
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
	if (auto Err = RequireString(Params, TEXT("attributeSet"), AttrSetSpec)) return Err;

	// Read before anything can fail (#1057).
	const FString CompName = OptionalString(Params, TEXT("componentName"));
	const FString TablePath = OptionalString(Params, TEXT("initDataTable"));

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BPPath));
	if (!BP) return MCPError(FString::Printf(TEXT("Blueprint not found: %s"), *BPPath));

	UClass* ASCClass = FindObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.AbilitySystemComponent"));
	if (!ASCClass) return MCPError(TEXT("AbilitySystemComponent not found. Enable GameplayAbilities plugin."));

	UClass* AttrSetClass = MCPGas::ResolveClassDerivingFrom(AttrSetSpec, UAttributeSet::StaticClass());
	if (!AttrSetClass) return MCPError(FString::Printf(TEXT("AttributeSet class not found: %s"), *AttrSetSpec));

	// Find the ASC component template on the blueprint's construction script.
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
