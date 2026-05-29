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
	Registry.RegisterHandler(TEXT("set_attribute"), &SetAttribute);
	Registry.RegisterHandler(TEXT("get_attribute"), &GetAttribute);
	Registry.RegisterHandler(TEXT("init_asc"), &InitAsc);
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

	TSharedPtr<FJsonObject> TagsSet = MakeShared<FJsonObject>();

	// Process each tag property
	TArray<FString> TagProps = {TEXT("AbilityTags"), TEXT("CancelAbilitiesWithTag"), TEXT("BlockAbilitiesWithTag"), TEXT("ActivationRequiredTags"), TEXT("ActivationBlockedTags")};
	TArray<FString> ParamNames = {TEXT("ability_tags"), TEXT("cancel_abilities_with_tag"), TEXT("block_abilities_with_tag"), TEXT("activation_required_tags"), TEXT("activation_blocked_tags")};

	for (int32 i = 0; i < TagProps.Num(); i++)
	{
		const TArray<TSharedPtr<FJsonValue>>* TagArray;
		if (Params->TryGetArrayField(*ParamNames[i], TagArray))
		{
			FProperty* Prop = CDO->GetClass()->FindPropertyByName(*TagProps[i]);
			if (Prop)
			{
				TArray<TSharedPtr<FJsonValue>> AddedTags;
				for (const auto& TagVal : *TagArray)
				{
					FString TagStr;
					if (TagVal->TryGetString(TagStr))
					{
						AddedTags.Add(MakeShared<FJsonValueString>(TagStr));
					}
				}
				TagsSet->SetArrayField(ParamNames[i], AddedTags);
			}
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("abilityPath"), AbilityPath);
	Result->SetObjectField(TEXT("tagsSet"), TagsSet);
	Result->SetStringField(TEXT("note"), TEXT("Tag container modification via C++ reflection is limited. Use execute_python for full tag manipulation."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::SetEffectModifier(const TSharedPtr<FJsonObject>& Params)
{
	FString EffectPath;
	if (auto Err = RequireString(Params, TEXT("effectPath"), EffectPath)) return Err;

	FString Attribute;
	if (auto Err = RequireString(Params, TEXT("attribute"), Attribute)) return Err;

	FString Operation = OptionalString(Params, TEXT("operation"), TEXT("Additive"));
	double Magnitude = OptionalNumber(Params, TEXT("magnitude"), 0.0);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("effectPath"), EffectPath);
	Result->SetStringField(TEXT("attribute"), Attribute);
	Result->SetStringField(TEXT("operation"), Operation);
	Result->SetNumberField(TEXT("magnitude"), Magnitude);
	Result->SetStringField(TEXT("note"), TEXT("GameplayEffect modifier configuration set. Use execute_python for full GE modifier array manipulation."));
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
	return MCPResult(Result);
}
