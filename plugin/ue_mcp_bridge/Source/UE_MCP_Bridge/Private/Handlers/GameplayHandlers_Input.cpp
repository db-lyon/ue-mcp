// Split from GameplayHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FGameplayHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in GameplayHandlers.cpp::RegisterHandlers.

#include "GameplayHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/TopLevelAssetPath.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Components/ActorComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNode_StateMachine.h"
#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Subsystems/SubsystemBlueprintLibrary.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/WorldSubsystem.h"


TSharedPtr<FJsonValue> FGameplayHandlers::CreateInputAction(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Input"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (auto Existing = MCPCheckAssetExists(PackagePath, Name, OnConflict, TEXT("InputAction")))
	{
		return Existing;
	}

	UClass* InputActionClass = FindObject<UClass>(nullptr, TEXT("/Script/EnhancedInput.InputAction"));
	if (!InputActionClass)
	{
		return MCPError(TEXT("InputAction class not found. Enable EnhancedInput plugin."));
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, InputActionClass, nullptr);
	if (!NewAsset)
	{
		return MCPError(TEXT("Failed to create InputAction"));
	}

	// Apply valueType if provided
	FString ValueTypeStr = OptionalString(Params, TEXT("valueType"));
	if (!ValueTypeStr.IsEmpty())
	{
		EInputActionValueType DesiredType = EInputActionValueType::Boolean;
		bool bValidType = true;

		if (ValueTypeStr.Equals(TEXT("Boolean"), ESearchCase::IgnoreCase) || ValueTypeStr == TEXT("Digital"))
		{
			DesiredType = EInputActionValueType::Boolean;
		}
		else if (ValueTypeStr.Equals(TEXT("Axis1D"), ESearchCase::IgnoreCase) || ValueTypeStr.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
		{
			DesiredType = EInputActionValueType::Axis1D;
		}
		else if (ValueTypeStr.Equals(TEXT("Axis2D"), ESearchCase::IgnoreCase) || ValueTypeStr.Equals(TEXT("Vector2D"), ESearchCase::IgnoreCase))
		{
			DesiredType = EInputActionValueType::Axis2D;
		}
		else if (ValueTypeStr.Equals(TEXT("Axis3D"), ESearchCase::IgnoreCase) || ValueTypeStr.Equals(TEXT("Vector"), ESearchCase::IgnoreCase))
		{
			DesiredType = EInputActionValueType::Axis3D;
		}
		else
		{
			bValidType = false;
		}

		if (bValidType)
		{
			UInputAction* InputAction = Cast<UInputAction>(NewAsset);
			if (InputAction)
			{
				InputAction->ValueType = DesiredType;
			}
		}
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	MCPSetDeleteAssetRollback(Result, NewAsset->GetPathName());

	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FGameplayHandlers::CreateInputMappingContext(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Input"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	if (auto Existing = MCPCheckAssetExists(PackagePath, Name, OnConflict, TEXT("InputMappingContext")))
	{
		return Existing;
	}

	UClass* IMCClass = FindObject<UClass>(nullptr, TEXT("/Script/EnhancedInput.InputMappingContext"));
	if (!IMCClass)
	{
		return MCPError(TEXT("InputMappingContext class not found. Enable EnhancedInput plugin."));
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, IMCClass, nullptr);
	if (!NewAsset)
	{
		return MCPError(TEXT("Failed to create InputMappingContext"));
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	MCPSetDeleteAssetRollback(Result, NewAsset->GetPathName());

	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FGameplayHandlers::SetupEnhancedInput(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	FString MappingContextPath;
	if (auto Err = RequireString(Params, TEXT("mappingContextPath"), MappingContextPath)) return Err;

	int32 Priority = OptionalInt(Params, TEXT("priority"), 0);

	REQUIRE_EDITOR_WORLD(World);

	// Find actor by label
	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		if ((*ActorIt)->GetActorLabel() == ActorLabel)
		{
			FoundActor = *ActorIt;
			break;
		}
	}

	if (!FoundActor)
	{
		return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));
	}

	// Load the mapping context asset
	UInputMappingContext* MappingContext = LoadObject<UInputMappingContext>(nullptr, *MappingContextPath);
	if (!MappingContext)
	{
		return MCPError(FString::Printf(TEXT("InputMappingContext not found: %s"), *MappingContextPath));
	}

	// Check if the actor has an EnhancedInputComponent
	UEnhancedInputComponent* InputComp = FoundActor->FindComponentByClass<UEnhancedInputComponent>();
	if (!InputComp)
	{
		return MCPError(TEXT("Actor does not have an EnhancedInputComponent"));
	}

	// Optionally bind input actions from params
	TArray<TSharedPtr<FJsonValue>> BoundActions;
	const TArray<TSharedPtr<FJsonValue>>* ActionsArray = nullptr;
	if (Params->TryGetArrayField(TEXT("actions"), ActionsArray))
	{
		for (const auto& ActionVal : *ActionsArray)
		{
			const TSharedPtr<FJsonObject>* ActionObj = nullptr;
			if (ActionVal->TryGetObject(ActionObj))
			{
				FString ActionPath;
				if ((*ActionObj)->TryGetStringField(TEXT("actionPath"), ActionPath))
				{
					UInputAction* InputAction = LoadObject<UInputAction>(nullptr, *ActionPath);
					if (InputAction)
					{
						TSharedPtr<FJsonObject> BoundAction = MakeShared<FJsonObject>();
						BoundAction->SetStringField(TEXT("actionPath"), ActionPath);
						BoundAction->SetStringField(TEXT("actionName"), InputAction->GetName());
						BoundActions.Add(MakeShared<FJsonValueObject>(BoundAction));
					}
				}
			}
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("mappingContext"), MappingContext->GetName());
	Result->SetNumberField(TEXT("priority"), Priority);
	Result->SetArrayField(TEXT("boundActions"), BoundActions);

	return MCPResult(Result);
}


// ─────────────────────────────────────────────────────────────
// #57 / #60  read_imc — Read InputMappingContext mappings
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FGameplayHandlers::ReadImc(const TSharedPtr<FJsonObject>& Params)
{
	FString ImcPath;
	if (auto Err = RequireString(Params, TEXT("imcPath"), ImcPath)) return Err;

	UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, *ImcPath);
	if (!IMC)
	{
		return MCPError(FString::Printf(TEXT("InputMappingContext not found: %s"), *ImcPath));
	}

	auto Result = MCPSuccess();

	TArray<TSharedPtr<FJsonValue>> MappingsArr;
	const TArray<FEnhancedActionKeyMapping>& Mappings = IMC->GetMappings();
	for (const FEnhancedActionKeyMapping& Mapping : Mappings)
	{
		TSharedPtr<FJsonObject> MObj = MakeShared<FJsonObject>();
		MObj->SetStringField(TEXT("inputAction"), Mapping.Action ? Mapping.Action->GetPathName() : TEXT("None"));
		MObj->SetStringField(TEXT("inputActionName"), Mapping.Action ? Mapping.Action->GetName() : TEXT("None"));
		MObj->SetStringField(TEXT("key"), Mapping.Key.GetFName().ToString());

		// Triggers
		TArray<TSharedPtr<FJsonValue>> TriggersArr;
		for (const TObjectPtr<UInputTrigger>& Trigger : Mapping.Triggers)
		{
			if (Trigger)
			{
				TriggersArr.Add(MakeShared<FJsonValueString>(Trigger->GetClass()->GetName()));
			}
		}
		MObj->SetArrayField(TEXT("triggers"), TriggersArr);

		// Modifiers
		TArray<TSharedPtr<FJsonValue>> ModifiersArr;
		for (const TObjectPtr<UInputModifier>& Modifier : Mapping.Modifiers)
		{
			if (Modifier)
			{
				ModifiersArr.Add(MakeShared<FJsonValueString>(Modifier->GetClass()->GetName()));
			}
		}
		MObj->SetArrayField(TEXT("modifiers"), ModifiersArr);

		MappingsArr.Add(MakeShared<FJsonValueObject>(MObj));
	}

	Result->SetStringField(TEXT("imcPath"), IMC->GetPathName());
	Result->SetStringField(TEXT("imcName"), IMC->GetName());
	Result->SetArrayField(TEXT("mappings"), MappingsArr);
	Result->SetNumberField(TEXT("count"), MappingsArr.Num());

	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// #57 / #60  add_imc_mapping — Add key mapping to an IMC
// ─────────────────────────────────────────────────────────────


// ─────────────────────────────────────────────────────────────
// #57 / #60  add_imc_mapping — Add key mapping to an IMC
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FGameplayHandlers::AddImcMapping(const TSharedPtr<FJsonObject>& Params)
{
	FString ImcPath;
	if (auto Err = RequireString(Params, TEXT("imcPath"), ImcPath)) return Err;

	FString InputActionPath;
	if (auto Err = RequireString(Params, TEXT("inputActionPath"), InputActionPath)) return Err;

	FString KeyName;
	if (auto Err = RequireString(Params, TEXT("key"), KeyName)) return Err;

	UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, *ImcPath);
	if (!IMC)
	{
		return MCPError(FString::Printf(TEXT("InputMappingContext not found: %s"), *ImcPath));
	}

	UInputAction* InputAction = LoadObject<UInputAction>(nullptr, *InputActionPath);
	if (!InputAction)
	{
		return MCPError(FString::Printf(TEXT("InputAction not found: %s"), *InputActionPath));
	}

	FKey Key(*KeyName);
	if (!Key.IsValid())
	{
		return MCPError(FString::Printf(TEXT("Invalid key name: %s"), *KeyName));
	}

	// Idempotency: mapping with (action, key) already present?
	for (const FEnhancedActionKeyMapping& M : IMC->GetMappings())
	{
		if (M.Action == InputAction && M.Key == Key)
		{
			auto Existed = MCPSuccess();
			MCPSetExisted(Existed);
			Existed->SetStringField(TEXT("imcPath"), IMC->GetPathName());
			Existed->SetStringField(TEXT("inputAction"), InputAction->GetPathName());
			Existed->SetStringField(TEXT("key"), KeyName);
			return MCPResult(Existed);
		}
	}

	// Create the mapping and add it
	FEnhancedActionKeyMapping NewMapping;
	NewMapping.Action = InputAction;
	NewMapping.Key = Key;

	IMC->MapKey(InputAction, Key);

	// Mark dirty — caller can use asset(save) to persist (#197 fix: SavePackage crash)
	UPackage* Pkg = IMC->GetOutermost();
	if (Pkg)
	{
		Pkg->MarkPackageDirty();
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("imcPath"), IMC->GetPathName());
	Result->SetStringField(TEXT("inputAction"), InputAction->GetPathName());
	Result->SetStringField(TEXT("key"), KeyName);

	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// #75  set_mapping_modifiers — Add modifiers/triggers to an IMC mapping
//      Creates UObject subobjects with IMC as outer so they serialize.
// ─────────────────────────────────────────────────────────────


// ─────────────────────────────────────────────────────────────
// #75  set_mapping_modifiers — Add modifiers/triggers to an IMC mapping
//      Creates UObject subobjects with IMC as outer so they serialize.
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FGameplayHandlers::SetMappingModifiers(const TSharedPtr<FJsonObject>& Params)
{
	FString ImcPath;
	if (auto Err = RequireString(Params, TEXT("imcPath"), ImcPath)) return Err;

	UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, *ImcPath);
	if (!IMC)
	{
		return MCPError(FString::Printf(TEXT("InputMappingContext not found: %s"), *ImcPath));
	}

	int32 MappingIndex = OptionalInt(Params, TEXT("mappingIndex"), 0);
	TArray<FEnhancedActionKeyMapping>& Mappings = const_cast<TArray<FEnhancedActionKeyMapping>&>(IMC->GetMappings());
	if (!Mappings.IsValidIndex(MappingIndex))
	{
		return MCPError(FString::Printf(TEXT("Mapping index %d out of range (count: %d)"), MappingIndex, Mappings.Num()));
	}

	FEnhancedActionKeyMapping& Mapping = Mappings[MappingIndex];

	// ── Modifiers ──
	const TArray<TSharedPtr<FJsonValue>>* ModifiersArr = nullptr;
	if (Params->TryGetArrayField(TEXT("modifiers"), ModifiersArr) && ModifiersArr)
	{
		Mapping.Modifiers.Empty();
		for (const auto& ModVal : *ModifiersArr)
		{
			const TSharedPtr<FJsonObject>* ModObj = nullptr;
			if (!ModVal->TryGetObject(ModObj) || !ModObj) continue;

			FString TypeName;
			(*ModObj)->TryGetStringField(TEXT("type"), TypeName);
			if (TypeName.IsEmpty()) continue;

			// Resolve class: try multiple patterns (#169 fix)
			// "DeadZone" → UInputModifierDeadZone, "Negate" → UInputModifierNegate, etc.
			UClass* ModClass = nullptr;
			{
				TArray<FString> Candidates;
				if (TypeName.StartsWith(TEXT("UInputModifier")) || TypeName.StartsWith(TEXT("InputModifier")))
				{
					Candidates.Add(TypeName);
				}
				else
				{
					Candidates.Add(TEXT("UInputModifier") + TypeName);
					Candidates.Add(TEXT("InputModifier") + TypeName);
					Candidates.Add(TypeName);
				}
				for (const FString& Cand : Candidates)
				{
					ModClass = FindClassByShortName(Cand);
					if (ModClass && ModClass->IsChildOf(UInputModifier::StaticClass())) break;
					ModClass = nullptr;
				}
			}
			if (!ModClass)
			{
				continue; // skip unknown modifier types
			}

			// Create with IMC as outer — this is the key fix for #75
			UInputModifier* Modifier = NewObject<UInputModifier>(IMC, ModClass);

			// Set properties via reflection
			for (const auto& Pair : (*ModObj)->Values)
			{
				if (Pair.Key == TEXT("type")) continue;

				FProperty* Prop = ModClass->FindPropertyByName(FName(*Pair.Key));
				if (!Prop) continue;

				void* PropAddr = Prop->ContainerPtrToValuePtr<void>(Modifier);

				if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
				{
					double Val = 0;
					Pair.Value->TryGetNumber(Val);
					FloatProp->SetPropertyValue(PropAddr, (float)Val);
				}
				else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
				{
					double Val = 0;
					Pair.Value->TryGetNumber(Val);
					DoubleProp->SetPropertyValue(PropAddr, Val);
				}
				else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
				{
					bool Val = false;
					Pair.Value->TryGetBool(Val);
					BoolProp->SetPropertyValue(PropAddr, Val);
				}
				else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
				{
					FString EnumStr;
					if (Pair.Value->TryGetString(EnumStr))
					{
						int64 EnumVal = EnumProp->GetEnum()->GetValueByNameString(EnumStr);
						if (EnumVal != INDEX_NONE)
						{
							EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(PropAddr, EnumVal);
						}
					}
				}
				else if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
				{
					if (ByteProp->Enum)
					{
						FString EnumStr;
						if (Pair.Value->TryGetString(EnumStr))
						{
							int64 EnumVal = ByteProp->Enum->GetValueByNameString(EnumStr);
							if (EnumVal != INDEX_NONE)
							{
								ByteProp->SetPropertyValue(PropAddr, (uint8)EnumVal);
							}
						}
					}
					else
					{
						double Val = 0;
						Pair.Value->TryGetNumber(Val);
						ByteProp->SetPropertyValue(PropAddr, (uint8)Val);
					}
				}
			}

			Mapping.Modifiers.Add(Modifier);
		}
	}

	// ── Triggers ──
	const TArray<TSharedPtr<FJsonValue>>* TriggersArr = nullptr;
	if (Params->TryGetArrayField(TEXT("triggers"), TriggersArr) && TriggersArr)
	{
		Mapping.Triggers.Empty();
		for (const auto& TrigVal : *TriggersArr)
		{
			const TSharedPtr<FJsonObject>* TrigObj = nullptr;
			if (!TrigVal->TryGetObject(TrigObj) || !TrigObj) continue;

			FString TypeName;
			(*TrigObj)->TryGetStringField(TEXT("type"), TypeName);
			if (TypeName.IsEmpty()) continue;

			// Resolve trigger class: try multiple patterns (#169 fix)
			UClass* TrigClass = nullptr;
			{
				TArray<FString> Candidates;
				if (TypeName.StartsWith(TEXT("UInputTrigger")) || TypeName.StartsWith(TEXT("InputTrigger")))
				{
					Candidates.Add(TypeName);
				}
				else
				{
					Candidates.Add(TEXT("UInputTrigger") + TypeName);
					Candidates.Add(TEXT("InputTrigger") + TypeName);
					Candidates.Add(TypeName);
				}
				for (const FString& Cand : Candidates)
				{
					TrigClass = FindClassByShortName(Cand);
					if (TrigClass && TrigClass->IsChildOf(UInputTrigger::StaticClass())) break;
					TrigClass = nullptr;
				}
			}
			if (!TrigClass)
			{
				continue;
			}

			UInputTrigger* Trigger = NewObject<UInputTrigger>(IMC, TrigClass);

			// Set properties via reflection (same pattern as modifiers)
			for (const auto& Pair : (*TrigObj)->Values)
			{
				if (Pair.Key == TEXT("type")) continue;

				FProperty* Prop = TrigClass->FindPropertyByName(FName(*Pair.Key));
				if (!Prop) continue;

				void* PropAddr = Prop->ContainerPtrToValuePtr<void>(Trigger);

				if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
				{
					double Val = 0;
					Pair.Value->TryGetNumber(Val);
					FloatProp->SetPropertyValue(PropAddr, (float)Val);
				}
				else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
				{
					double Val = 0;
					Pair.Value->TryGetNumber(Val);
					DoubleProp->SetPropertyValue(PropAddr, Val);
				}
				else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
				{
					bool Val = false;
					Pair.Value->TryGetBool(Val);
					BoolProp->SetPropertyValue(PropAddr, Val);
				}
			}

			Mapping.Triggers.Add(Trigger);
		}
	}

	// Mark dirty — caller can use asset(save) to persist (#197 fix)
	UPackage* Pkg = IMC->GetOutermost();
	if (Pkg)
	{
		Pkg->MarkPackageDirty();
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("imcPath"), IMC->GetPathName());
	Result->SetNumberField(TEXT("mappingIndex"), MappingIndex);
	Result->SetNumberField(TEXT("modifierCount"), Mapping.Modifiers.Num());
	Result->SetNumberField(TEXT("triggerCount"), Mapping.Triggers.Num());
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// #158  remove_imc_mapping / set_imc_mapping_key / set_imc_mapping_action
// ─────────────────────────────────────────────────────────────
namespace ImcEdit_Internal
{
	static int32 ResolveMappingIndex(UInputMappingContext* IMC, const TSharedPtr<FJsonObject>& Params, FString& OutError)
	{
		const TArray<FEnhancedActionKeyMapping>& Mappings = IMC->GetMappings();

		int32 Idx = INDEX_NONE;
		double NumIdx = 0;
		if (Params->TryGetNumberField(TEXT("mappingIndex"), NumIdx))
		{
			Idx = static_cast<int32>(NumIdx);
			if (!Mappings.IsValidIndex(Idx))
			{
				OutError = FString::Printf(TEXT("Mapping index %d out of range (count %d)"), Idx, Mappings.Num());
				return INDEX_NONE;
			}
			return Idx;
		}

		FString ActionPath, KeyName;
		const bool bHasAction = Params->TryGetStringField(TEXT("inputActionPath"), ActionPath) && !ActionPath.IsEmpty();
		const bool bHasKey    = Params->TryGetStringField(TEXT("key"), KeyName) && !KeyName.IsEmpty();
		if (!bHasAction && !bHasKey)
		{
			OutError = TEXT("Provide mappingIndex or (inputActionPath + key) to identify the mapping.");
			return INDEX_NONE;
		}

		UInputAction* Action = bHasAction ? LoadObject<UInputAction>(nullptr, *ActionPath) : nullptr;
		FKey Key = bHasKey ? FKey(*KeyName) : FKey();

		for (int32 i = 0; i < Mappings.Num(); ++i)
		{
			const FEnhancedActionKeyMapping& M = Mappings[i];
			if (bHasAction && M.Action != Action) continue;
			if (bHasKey && M.Key != Key) continue;
			return i;
		}

		OutError = TEXT("No mapping matched the given inputActionPath/key.");
		return INDEX_NONE;
	}

	static bool SaveImc(UInputMappingContext* IMC)
	{
		UPackage* Pkg = IMC->GetOutermost();
		if (!Pkg) return false;
		// Mark dirty only — caller can use asset(save) to persist (#197 fix)
		Pkg->MarkPackageDirty();
		return true;
	}
}


TSharedPtr<FJsonValue> FGameplayHandlers::RemoveImcMapping(const TSharedPtr<FJsonObject>& Params)
{
	FString ImcPath;
	if (auto Err = RequireString(Params, TEXT("imcPath"), ImcPath)) return Err;

	UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, *ImcPath);
	if (!IMC)
	{
		return MCPError(FString::Printf(TEXT("InputMappingContext not found: %s"), *ImcPath));
	}

	FString ResolveError;
	int32 Idx = ImcEdit_Internal::ResolveMappingIndex(IMC, Params, ResolveError);
	if (Idx == INDEX_NONE)
	{
		return MCPError(ResolveError);
	}

	TArray<FEnhancedActionKeyMapping>& Mappings = const_cast<TArray<FEnhancedActionKeyMapping>&>(IMC->GetMappings());
	const FEnhancedActionKeyMapping Removed = Mappings[Idx];
	Mappings.RemoveAt(Idx);

	ImcEdit_Internal::SaveImc(IMC);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("imcPath"), IMC->GetPathName());
	Result->SetNumberField(TEXT("mappingIndex"), Idx);
	Result->SetStringField(TEXT("removedInputAction"), Removed.Action ? Removed.Action->GetPathName() : TEXT("None"));
	Result->SetStringField(TEXT("removedKey"), Removed.Key.GetFName().ToString());
	Result->SetNumberField(TEXT("count"), Mappings.Num());
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FGameplayHandlers::SetImcMappingKey(const TSharedPtr<FJsonObject>& Params)
{
	FString ImcPath;
	if (auto Err = RequireString(Params, TEXT("imcPath"), ImcPath)) return Err;

	FString NewKeyName;
	if (auto Err = RequireString(Params, TEXT("newKey"), NewKeyName)) return Err;

	UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, *ImcPath);
	if (!IMC)
	{
		return MCPError(FString::Printf(TEXT("InputMappingContext not found: %s"), *ImcPath));
	}

	FKey NewKey(*NewKeyName);
	if (!NewKey.IsValid())
	{
		return MCPError(FString::Printf(TEXT("Invalid key name: %s"), *NewKeyName));
	}

	// Selector: mappingIndex | inputActionPath | key (current key). ResolveMappingIndex handles combinations.
	FString ResolveError;
	int32 Idx = ImcEdit_Internal::ResolveMappingIndex(IMC, Params, ResolveError);
	if (Idx == INDEX_NONE)
	{
		return MCPError(ResolveError);
	}

	TArray<FEnhancedActionKeyMapping>& Mappings = const_cast<TArray<FEnhancedActionKeyMapping>&>(IMC->GetMappings());
	const FKey PrevKey = Mappings[Idx].Key;
	Mappings[Idx].Key = NewKey;

	ImcEdit_Internal::SaveImc(IMC);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("imcPath"), IMC->GetPathName());
	Result->SetNumberField(TEXT("mappingIndex"), Idx);
	Result->SetStringField(TEXT("previousKey"), PrevKey.GetFName().ToString());
	Result->SetStringField(TEXT("newKey"), NewKey.GetFName().ToString());
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FGameplayHandlers::SetImcMappingAction(const TSharedPtr<FJsonObject>& Params)
{
	FString ImcPath;
	if (auto Err = RequireString(Params, TEXT("imcPath"), ImcPath)) return Err;

	FString NewActionPath;
	if (auto Err = RequireString(Params, TEXT("newInputActionPath"), NewActionPath)) return Err;

	UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, *ImcPath);
	if (!IMC)
	{
		return MCPError(FString::Printf(TEXT("InputMappingContext not found: %s"), *ImcPath));
	}

	UInputAction* NewAction = LoadObject<UInputAction>(nullptr, *NewActionPath);
	if (!NewAction)
	{
		return MCPError(FString::Printf(TEXT("InputAction not found: %s"), *NewActionPath));
	}

	// Selector: mappingIndex | key | inputActionPath (current action).
	FString ResolveError;
	int32 Idx = ImcEdit_Internal::ResolveMappingIndex(IMC, Params, ResolveError);
	if (Idx == INDEX_NONE)
	{
		return MCPError(ResolveError);
	}

	TArray<FEnhancedActionKeyMapping>& Mappings = const_cast<TArray<FEnhancedActionKeyMapping>&>(IMC->GetMappings());
	const UInputAction* PrevAction = Mappings[Idx].Action;
	Mappings[Idx].Action = NewAction;

	ImcEdit_Internal::SaveImc(IMC);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("imcPath"), IMC->GetPathName());
	Result->SetNumberField(TEXT("mappingIndex"), Idx);
	Result->SetStringField(TEXT("previousInputAction"), PrevAction ? PrevAction->GetPathName() : TEXT("None"));
	Result->SetStringField(TEXT("newInputAction"), NewAction->GetPathName());
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// #54 / #89 / #90  inspect_pie — PIE runtime actor inspection
// ─────────────────────────────────────────────────────────────


// ─────────────────────────────────────────────────────────────
// #54 / #89 / #90  inspect_pie — PIE runtime actor inspection
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FGameplayHandlers::InspectPie(const TSharedPtr<FJsonObject>& Params)
{
	// Get PIE world
	FWorldContext* PIEContext = GEditor->GetPIEWorldContext();
	if (!PIEContext || !PIEContext->World())
	{
		return MCPError(TEXT("No PIE world available. Is Play-In-Editor running?"));
	}

	UWorld* PIEWorld = PIEContext->World();

	FString ActorLabel;
	bool bHasLabel = Params->TryGetStringField(TEXT("actorLabel"), ActorLabel) && !ActorLabel.IsEmpty();

	if (!bHasLabel)
	{
		// List all actors in PIE world
		TArray<TSharedPtr<FJsonValue>> ActorsArr;
		for (TActorIterator<AActor> It(PIEWorld); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor || !IsValid(Actor)) continue;

			TSharedPtr<FJsonObject> AObj = MakeShared<FJsonObject>();
			AObj->SetStringField(TEXT("name"), Actor->GetName());
			AObj->SetStringField(TEXT("label"), Actor->GetActorLabel());
			AObj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

			FVector Loc = Actor->GetActorLocation();
			TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
			LocObj->SetNumberField(TEXT("x"), Loc.X);
			LocObj->SetNumberField(TEXT("y"), Loc.Y);
			LocObj->SetNumberField(TEXT("z"), Loc.Z);
			AObj->SetObjectField(TEXT("location"), LocObj);

			ActorsArr.Add(MakeShared<FJsonValueObject>(AObj));
		}

		auto Result = MCPSuccess();
		Result->SetArrayField(TEXT("actors"), ActorsArr);
		Result->SetNumberField(TEXT("count"), ActorsArr.Num());
		return MCPResult(Result);
	}

	// Find specific actor by label
	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(PIEWorld); It; ++It)
	{
		if ((*It)->GetActorLabel() == ActorLabel || (*It)->GetName() == ActorLabel)
		{
			FoundActor = *It;
			break;
		}
	}

	if (!FoundActor)
	{
		return MCPError(FString::Printf(TEXT("Actor not found in PIE: %s"), *ActorLabel));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("name"), FoundActor->GetName());
	Result->SetStringField(TEXT("label"), FoundActor->GetActorLabel());
	Result->SetStringField(TEXT("class"), FoundActor->GetClass()->GetName());

	FVector Loc = FoundActor->GetActorLocation();
	TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
	LocObj->SetNumberField(TEXT("x"), Loc.X);
	LocObj->SetNumberField(TEXT("y"), Loc.Y);
	LocObj->SetNumberField(TEXT("z"), Loc.Z);
	Result->SetObjectField(TEXT("location"), LocObj);

	// Components
	TArray<TSharedPtr<FJsonValue>> CompsArr;
	TArray<UActorComponent*> Components;
	FoundActor->GetComponents(Components);
	for (UActorComponent* Comp : Components)
	{
		if (!Comp) continue;

		TSharedPtr<FJsonObject> CObj = MakeShared<FJsonObject>();
		CObj->SetStringField(TEXT("name"), Comp->GetName());
		CObj->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
		CObj->SetBoolField(TEXT("active"), Comp->IsActive());

		// For scene components, include transform
		if (USceneComponent* SceneComp = Cast<USceneComponent>(Comp))
		{
			FVector CLoc = SceneComp->GetComponentLocation();
			TSharedPtr<FJsonObject> CLocObj = MakeShared<FJsonObject>();
			CLocObj->SetNumberField(TEXT("x"), CLoc.X);
			CLocObj->SetNumberField(TEXT("y"), CLoc.Y);
			CLocObj->SetNumberField(TEXT("z"), CLoc.Z);
			CObj->SetObjectField(TEXT("worldLocation"), CLocObj);
		}

		// For primitive components, include physics/collision info
		if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Comp))
		{
			CObj->SetBoolField(TEXT("simulatingPhysics"), PrimComp->IsSimulatingPhysics());
			CObj->SetStringField(TEXT("collisionProfile"), PrimComp->GetCollisionProfileName().ToString());
		}

		CompsArr.Add(MakeShared<FJsonValueObject>(CObj));
	}
	Result->SetArrayField(TEXT("components"), CompsArr);

	// Input bindings — check for EnhancedInputComponent
	UEnhancedInputComponent* InputComp = FoundActor->FindComponentByClass<UEnhancedInputComponent>();
	Result->SetBoolField(TEXT("hasEnhancedInput"), InputComp != nullptr);

	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// #26  get_pie_anim_state — PIE anim instance state
// ─────────────────────────────────────────────────────────────


// ─────────────────────────────────────────────────────────────
// #26  get_pie_anim_state — PIE anim instance state
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FGameplayHandlers::GetPieAnimState(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	// Get PIE world
	FWorldContext* PIEContext = GEditor->GetPIEWorldContext();
	if (!PIEContext || !PIEContext->World())
	{
		return MCPError(TEXT("No PIE world available. Is Play-In-Editor running?"));
	}

	UWorld* PIEWorld = PIEContext->World();

	// Find actor
	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(PIEWorld); It; ++It)
	{
		if ((*It)->GetActorLabel() == ActorLabel || (*It)->GetName() == ActorLabel)
		{
			FoundActor = *It;
			break;
		}
	}

	if (!FoundActor)
	{
		return MCPError(FString::Printf(TEXT("Actor not found in PIE: %s"), *ActorLabel));
	}

	// Find SkeletalMeshComponent
	USkeletalMeshComponent* SkelMesh = FoundActor->FindComponentByClass<USkeletalMeshComponent>();
	if (!SkelMesh)
	{
		return MCPError(TEXT("Actor does not have a SkeletalMeshComponent"));
	}

	// Get AnimInstance
	UAnimInstance* AnimInst = SkelMesh->GetAnimInstance();
	if (!AnimInst)
	{
		return MCPError(TEXT("No AnimInstance on the SkeletalMeshComponent"));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorName"), FoundActor->GetName());
	Result->SetStringField(TEXT("animClass"), AnimInst->GetClass()->GetName());

	// Current montage
	UAnimMontage* CurrentMontage = AnimInst->GetCurrentActiveMontage();
	Result->SetStringField(TEXT("currentMontage"), CurrentMontage ? CurrentMontage->GetName() : TEXT("None"));
	if (CurrentMontage)
	{
		Result->SetNumberField(TEXT("montagePosition"), AnimInst->Montage_GetPosition(CurrentMontage));
		Result->SetBoolField(TEXT("montageIsPlaying"), AnimInst->Montage_IsPlaying(CurrentMontage));
	}

	// State machine info — use the anim class interface to enumerate machines
	TArray<TSharedPtr<FJsonValue>> StatesArr;
	if (const IAnimClassInterface* AnimClassInterface = IAnimClassInterface::GetFromClass(AnimInst->GetClass()))
	{
		const TArray<FBakedAnimationStateMachine>& BakedMachines = AnimClassInterface->GetBakedStateMachines();
		for (int32 MachineIdx = 0; MachineIdx < BakedMachines.Num(); ++MachineIdx)
		{
			const FBakedAnimationStateMachine& BakedMachine = BakedMachines[MachineIdx];
			TSharedPtr<FJsonObject> SMObj = MakeShared<FJsonObject>();
			SMObj->SetStringField(TEXT("machineName"), BakedMachine.MachineName.ToString());
			SMObj->SetNumberField(TEXT("machineIndex"), MachineIdx);
			SMObj->SetNumberField(TEXT("stateCount"), BakedMachine.States.Num());

			// Try to get current state from the instance
			const FAnimNode_StateMachine* SM = AnimInst->GetStateMachineInstance(MachineIdx);
			if (SM)
			{
				int32 StateIdx = SM->GetCurrentState();
				SMObj->SetNumberField(TEXT("currentStateIndex"), StateIdx);
				if (BakedMachine.States.IsValidIndex(StateIdx))
				{
					SMObj->SetStringField(TEXT("currentStateName"), BakedMachine.States[StateIdx].StateName.ToString());
				}
			}

			StatesArr.Add(MakeShared<FJsonValueObject>(SMObj));
		}
	}
	Result->SetArrayField(TEXT("stateMachines"), StatesArr);

	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// #139 — Read arbitrary UPROPERTY values on a PIE AnimInstance
// Params:
//   actorLabel: required — find the actor in PIE by label or name
//   propertyNames: optional array of property names to read; if omitted
//                  returns ALL UPROPERTY values on the AnimInstance CDO.
// ─────────────────────────────────────────────────────────────


// ─────────────────────────────────────────────────────────────
// #139 — Read arbitrary UPROPERTY values on a PIE AnimInstance
// Params:
//   actorLabel: required — find the actor in PIE by label or name
//   propertyNames: optional array of property names to read; if omitted
//                  returns ALL UPROPERTY values on the AnimInstance CDO.
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FGameplayHandlers::GetPieAnimProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	UWorld* PIEWorld = GetPIEWorld();
	if (!PIEWorld)
	{
		return MCPError(TEXT("No PIE world available. Is Play-In-Editor running?"));
	}

	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(PIEWorld); It; ++It)
	{
		if ((*It)->GetActorLabel() == ActorLabel || (*It)->GetName() == ActorLabel)
		{
			FoundActor = *It;
			break;
		}
	}
	if (!FoundActor) return MCPError(FString::Printf(TEXT("Actor not found in PIE: %s"), *ActorLabel));

	USkeletalMeshComponent* SkelMesh = FoundActor->FindComponentByClass<USkeletalMeshComponent>();
	if (!SkelMesh) return MCPError(TEXT("Actor has no SkeletalMeshComponent"));
	UAnimInstance* AnimInst = SkelMesh->GetAnimInstance();
	if (!AnimInst) return MCPError(TEXT("No AnimInstance on the SkeletalMeshComponent"));

	TArray<FString> RequestedNames;
	const TArray<TSharedPtr<FJsonValue>>* NamesArr = nullptr;
	if (Params->TryGetArrayField(TEXT("propertyNames"), NamesArr) && NamesArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *NamesArr)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S)) RequestedNames.Add(S);
		}
	}

	auto ExportOne = [AnimInst](FProperty* Prop) -> FString
	{
		FString ValueStr;
		const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(AnimInst);
		Prop->ExportText_Direct(ValueStr, ValuePtr, ValuePtr, AnimInst, PPF_None);
		return ValueStr;
	};

	TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
	if (RequestedNames.Num() > 0)
	{
		for (const FString& Name : RequestedNames)
		{
			FProperty* Prop = AnimInst->GetClass()->FindPropertyByName(*Name);
			if (!Prop)
			{
				Props->SetStringField(Name, TEXT("<not found>"));
				continue;
			}
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("type"), Prop->GetCPPType());
			Entry->SetStringField(TEXT("value"), ExportOne(Prop));
			Props->SetObjectField(Name, Entry);
		}
	}
	else
	{
		for (TFieldIterator<FProperty> It(AnimInst->GetClass()); It; ++It)
		{
			FProperty* Prop = *It;
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("type"), Prop->GetCPPType());
			Entry->SetStringField(TEXT("value"), ExportOne(Prop));
			Props->SetObjectField(Prop->GetName(), Entry);
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("animClass"), AnimInst->GetClass()->GetName());
	Result->SetObjectField(TEXT("properties"), Props);
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// #139 — Read a subsystem's UPROPERTY values (GameInstance / World / LocalPlayer)
// Params:
//   subsystemClass: class path or short name of the subsystem to read
//   scope: "game" (GameInstance, default), "world", "engine", "localplayer"
//   propertyNames: optional array; omit for all UPROPERTYs
//   actorLabel: required only for localplayer scope (to locate the owning PlayerController)
// ─────────────────────────────────────────────────────────────


// ─────────────────────────────────────────────────────────────
// #139 — Read a subsystem's UPROPERTY values (GameInstance / World / LocalPlayer)
// Params:
//   subsystemClass: class path or short name of the subsystem to read
//   scope: "game" (GameInstance, default), "world", "engine", "localplayer"
//   propertyNames: optional array; omit for all UPROPERTYs
//   actorLabel: required only for localplayer scope (to locate the owning PlayerController)
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FGameplayHandlers::GetPieSubsystemState(const TSharedPtr<FJsonObject>& Params)
{
	FString SubsystemClassName;
	if (auto Err = RequireString(Params, TEXT("subsystemClass"), SubsystemClassName)) return Err;

	UClass* SubClass = nullptr;
	if (SubsystemClassName.Contains(TEXT("/")) || SubsystemClassName.Contains(TEXT(".")))
	{
		SubClass = LoadObject<UClass>(nullptr, *SubsystemClassName);
	}
	if (!SubClass)
	{
		SubClass = FindClassByShortName(SubsystemClassName);
	}
	if (!SubClass) return MCPError(FString::Printf(TEXT("Subsystem class not found: %s"), *SubsystemClassName));

	const FString Scope = OptionalString(Params, TEXT("scope"), TEXT("game")).ToLower();

	UWorld* PIEWorld = GetPIEWorld();
	if (!PIEWorld) return MCPError(TEXT("No PIE world available. Is Play-In-Editor running?"));

	USubsystem* Subsystem = nullptr;
	if (Scope == TEXT("engine"))
	{
		Subsystem = GEngine ? GEngine->GetEngineSubsystemBase(SubClass) : nullptr;
	}
	else if (Scope == TEXT("world"))
	{
		Subsystem = PIEWorld->GetSubsystemBase(SubClass);
	}
	else if (Scope == TEXT("localplayer"))
	{
		ULocalPlayer* LP = PIEWorld->GetFirstLocalPlayerFromController();
		if (!LP) return MCPError(TEXT("No LocalPlayer in PIE world"));
		Subsystem = LP->GetSubsystemBase(SubClass);
	}
	else // game (default)
	{
		UGameInstance* GI = PIEWorld->GetGameInstance();
		if (!GI) return MCPError(TEXT("No GameInstance in PIE world"));
		Subsystem = GI->GetSubsystemBase(SubClass);
	}

	if (!Subsystem) return MCPError(FString::Printf(TEXT("Subsystem not found: %s (scope=%s)"), *SubsystemClassName, *Scope));

	TArray<FString> RequestedNames;
	const TArray<TSharedPtr<FJsonValue>>* NamesArr = nullptr;
	if (Params->TryGetArrayField(TEXT("propertyNames"), NamesArr) && NamesArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *NamesArr)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S)) RequestedNames.Add(S);
		}
	}

	auto ExportOne = [Subsystem](FProperty* Prop) -> FString
	{
		FString ValueStr;
		const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Subsystem);
		Prop->ExportText_Direct(ValueStr, ValuePtr, ValuePtr, Subsystem, PPF_None);
		return ValueStr;
	};

	TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
	if (RequestedNames.Num() > 0)
	{
		for (const FString& Name : RequestedNames)
		{
			FProperty* Prop = Subsystem->GetClass()->FindPropertyByName(*Name);
			if (!Prop)
			{
				Props->SetStringField(Name, TEXT("<not found>"));
				continue;
			}
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("type"), Prop->GetCPPType());
			Entry->SetStringField(TEXT("value"), ExportOne(Prop));
			Props->SetObjectField(Name, Entry);
		}
	}
	else
	{
		for (TFieldIterator<FProperty> It(Subsystem->GetClass()); It; ++It)
		{
			FProperty* Prop = *It;
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("type"), Prop->GetCPPType());
			Entry->SetStringField(TEXT("value"), ExportOne(Prop));
			Props->SetObjectField(Prop->GetName(), Entry);
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("subsystemClass"), Subsystem->GetClass()->GetPathName());
	Result->SetStringField(TEXT("scope"), Scope);
	Result->SetObjectField(TEXT("properties"), Props);
	return MCPResult(Result);
}

// ─── #124 read_behavior_tree_graph ──────────────────────────────────


// ─────────────────────────────────────────────────────────────
// #186  apply_damage_in_pie — Apply damage to a PIE actor via UGameplayStatics
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FGameplayHandlers::ApplyDamageInPie(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	double BaseDamage = OptionalNumber(Params, TEXT("baseDamage"), 10.0);
	FString DamageTypeClassName = OptionalString(Params, TEXT("damageTypeClass"), TEXT(""));

	// Get PIE world
	FWorldContext* PIEContext = GEditor->GetPIEWorldContext();
	if (!PIEContext || !PIEContext->World())
	{
		return MCPError(TEXT("No PIE world available. Is Play-In-Editor running?"));
	}
	UWorld* PIEWorld = PIEContext->World();

	// Find actor by label or name
	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(PIEWorld); It; ++It)
	{
		if ((*It)->GetActorLabel() == ActorLabel || (*It)->GetName() == ActorLabel)
		{
			TargetActor = *It;
			break;
		}
	}

	if (!TargetActor)
	{
		return MCPError(FString::Printf(TEXT("Actor not found in PIE: %s"), *ActorLabel));
	}

	// Resolve damage type
	TSubclassOf<UDamageType> DamageTypeClass = UDamageType::StaticClass();
	if (!DamageTypeClassName.IsEmpty())
	{
		UClass* FoundClass = FindClassByShortName(DamageTypeClassName);
		if (FoundClass && FoundClass->IsChildOf(UDamageType::StaticClass()))
		{
			DamageTypeClass = FoundClass;
		}
	}

	// We need an instigator — use the first player controller as event instigator
	APlayerController* PC = PIEWorld->GetFirstPlayerController();
	AActor* DamageCauser = PC ? PC->GetPawn() : nullptr;
	AController* InstigatorController = PC;

	// Apply damage
	float ActualDamage = UGameplayStatics::ApplyDamage(
		TargetActor,
		static_cast<float>(BaseDamage),
		InstigatorController,
		DamageCauser,
		DamageTypeClass
	);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorClass"), TargetActor->GetClass()->GetName());
	Result->SetNumberField(TEXT("baseDamage"), BaseDamage);
	Result->SetNumberField(TEXT("actualDamage"), static_cast<double>(ActualDamage));
	Result->SetStringField(TEXT("damageType"), DamageTypeClass->GetName());
	return MCPResult(Result);
}
