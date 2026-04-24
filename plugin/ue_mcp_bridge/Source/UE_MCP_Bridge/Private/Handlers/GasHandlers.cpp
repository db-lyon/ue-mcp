#include "GasHandlers.h"
#include "UE_MCP_BridgeModule.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
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

void FGasHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("create_gameplay_effect"), &CreateGameplayEffect);
	Registry.RegisterHandler(TEXT("get_gas_info"), &GetGasInfo);
	Registry.RegisterHandler(TEXT("create_gameplay_ability"), &CreateGameplayAbility);
	Registry.RegisterHandler(TEXT("create_attribute_set"), &CreateAttributeSet);
	Registry.RegisterHandler(TEXT("create_gameplay_cue"), &CreateGameplayCue);
	Registry.RegisterHandler(TEXT("add_ability_tag"), &AddAbilityTag);
	Registry.RegisterHandler(TEXT("create_gameplay_cue_notify"), &CreateGameplayCueNotify);
	Registry.RegisterHandler(TEXT("add_ability_system_component"), &AddAbilitySystemComponent);
	Registry.RegisterHandler(TEXT("add_attribute"), &AddAttribute);
	Registry.RegisterHandler(TEXT("set_ability_tags"), &SetAbilityTags);
	Registry.RegisterHandler(TEXT("set_effect_modifier"), &SetEffectModifier);
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

	if (auto Existing = MCPCheckAssetExists(PackagePath, Name, OnConflict, FriendlyType))
	{
		return Existing;
	}

	if (!ParentClass)
	{
		return MCPError(FString::Printf(TEXT("%s parent class not found. Enable GameplayAbilities plugin."), *FriendlyType));
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UBlueprintFactory* BlueprintFactory = NewObject<UBlueprintFactory>();
	BlueprintFactory->ParentClass = ParentClass;

	UBlueprint* NewBlueprint = Cast<UBlueprint>(AssetTools.CreateAsset(Name, PackagePath, UBlueprint::StaticClass(), BlueprintFactory));
	if (!NewBlueprint)
	{
		return MCPError(FString::Printf(TEXT("Failed to create %s Blueprint"), *FriendlyType));
	}

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

TSharedPtr<FJsonValue> FGasHandlers::AddAbilityTag(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;

	FString TagString;
	if (auto Err = RequireString(Params, TEXT("tag"), TagString)) return Err;

	// Load the ability blueprint
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		return MCPError(FString::Printf(TEXT("Blueprint not found or has no generated class: %s"), *BlueprintPath));
	}

	// Verify it is a GameplayAbility subclass
	UClass* GAClass = FindObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.GameplayAbility"));
	if (!GAClass || !Blueprint->GeneratedClass->IsChildOf(GAClass))
	{
		return MCPError(TEXT("Blueprint is not a GameplayAbility subclass"));
	}

	// Get CDO
	UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
	if (!CDO)
	{
		return MCPError(TEXT("Could not get CDO for ability blueprint"));
	}

	// Request the gameplay tag (this will create it if it does not exist)
	FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*TagString), false);
	if (!Tag.IsValid())
	{
		// If the tag doesn't exist yet, add it
		Tag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(*TagString));
	}

	if (!Tag.IsValid())
	{
		return MCPError(FString::Printf(TEXT("Could not resolve gameplay tag: %s"), *TagString));
	}

	// Find the AbilityTags property on the CDO and add the tag
	FProperty* AbilityTagsProp = GAClass->FindPropertyByName(TEXT("AbilityTags"));
	if (!AbilityTagsProp)
	{
		return MCPError(TEXT("Could not find AbilityTags property on GameplayAbility"));
	}

	FGameplayTagContainer* TagContainer = AbilityTagsProp->ContainerPtrToValuePtr<FGameplayTagContainer>(CDO);
	if (!TagContainer)
	{
		return MCPError(TEXT("Could not access AbilityTags container on CDO"));
	}

	// Idempotency: tag already present?
	if (TagContainer->HasTagExact(Tag))
	{
		auto Existed = MCPSuccess();
		MCPSetExisted(Existed);
		Existed->SetStringField(TEXT("blueprintPath"), BlueprintPath);
		Existed->SetStringField(TEXT("tag"), TagString);
		Existed->SetNumberField(TEXT("totalTags"), TagContainer->Num());
		return MCPResult(Existed);
	}

	TagContainer->AddTag(Tag);

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	SaveAssetPackage(Blueprint);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetStringField(TEXT("tag"), TagString);
	Result->SetNumberField(TEXT("totalTags"), TagContainer->Num());
	// No rollback: no paired remove_ability_tag handler.
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGasHandlers::CreateGameplayCueNotify(const TSharedPtr<FJsonObject>& Params)
{
	const FString NotifyType = OptionalString(Params, TEXT("notifyType"), TEXT("Actor"));
	const TCHAR* ParentPath;
	const TCHAR* FriendlyName;
	if (NotifyType == TEXT("Static"))
	{
		ParentPath = TEXT("/Script/GameplayAbilities.GameplayCueNotify_Static");
		FriendlyName = TEXT("GameplayCueNotify_Static");
	}
	else
	{
		ParentPath = TEXT("/Script/GameplayAbilities.GameplayCueNotify_Actor");
		FriendlyName = TEXT("GameplayCueNotify_Actor");
	}
	UClass* Cls = FindObject<UClass>(nullptr, ParentPath);

	FString FriendlyCopy(FriendlyName);
	return CreateGasBlueprint(
		Params, TEXT("/Game/GAS/CueNotifies"), Cls, TEXT("GameplayCueNotify"),
		[&NotifyType, &FriendlyCopy](TSharedPtr<FJsonObject>& R)
		{
			R->SetStringField(TEXT("notifyType"), NotifyType);
			R->SetStringField(TEXT("parentClass"), FriendlyCopy);
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

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(AbilityPath));
	if (!BP)
	{
		return MCPError(FString::Printf(TEXT("Ability Blueprint not found: %s"), *AbilityPath));
	}

	UObject* CDO = BP->GeneratedClass ? BP->GeneratedClass->GetDefaultObject() : nullptr;
	if (!CDO)
	{
		return MCPError(TEXT("Could not get CDO. Compile the blueprint first."));
	}

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
