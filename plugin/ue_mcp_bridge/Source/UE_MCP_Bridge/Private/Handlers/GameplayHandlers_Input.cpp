// Split from GameplayHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FGameplayHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in GameplayHandlers.cpp::RegisterHandlers.

#include "GameplayHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
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
#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
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

	UClass* InputActionClass = FindObject<UClass>(nullptr, TEXT("/Script/EnhancedInput.InputAction"));
	if (!InputActionClass)
	{
		return MCPError(TEXT("InputAction class not found. Enable EnhancedInput plugin."));
	}

	auto Created = MCPCreateAssetIdempotent<UObject>(Name, PackagePath, OnConflict, TEXT("InputAction"), InputActionClass, nullptr);
	if (Created.EarlyReturn) return Created.EarlyReturn;
	UObject* NewAsset = Created.Asset;

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

	UClass* IMCClass = FindObject<UClass>(nullptr, TEXT("/Script/EnhancedInput.InputMappingContext"));
	if (!IMCClass)
	{
		return MCPError(TEXT("InputMappingContext class not found. Enable EnhancedInput plugin."));
	}

	auto Created = MCPCreateAssetIdempotent<UObject>(Name, PackagePath, OnConflict, TEXT("InputMappingContext"), IMCClass, nullptr);
	if (Created.EarlyReturn) return Created.EarlyReturn;

	UEditorAssetLibrary::SaveAsset(Created.Asset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), Created.Asset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	MCPSetDeleteAssetRollback(Result, Created.Asset->GetPathName());

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
// #604  get_applied_imcs — read a live PIE player's applied Input Mapping
// Contexts (this is a runtime READ; PIE input injection lives in pie-studio).
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FGameplayHandlers::GetAppliedImcs(const TSharedPtr<FJsonObject>& Params)
{
	UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
	if (!World) return MCPError(TEXT("No PIE world available. Start Play-In-Editor first."));

	const int32 PlayerIndex = OptionalInt(Params, TEXT("playerIndex"), 0);
	APlayerController* PC = UGameplayStatics::GetPlayerController(World, PlayerIndex);
	if (!PC) return MCPError(FString::Printf(TEXT("No PlayerController at index %d"), PlayerIndex));

	ULocalPlayer* LP = PC->GetLocalPlayer();
	if (!LP) return MCPError(TEXT("PlayerController has no LocalPlayer"));

	UEnhancedInputLocalPlayerSubsystem* Sub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!Sub) return MCPError(TEXT("EnhancedInputLocalPlayerSubsystem not available (project may not use Enhanced Input)"));

	UEnhancedPlayerInput* EPI = Sub->GetPlayerInput();
	if (!EPI) return MCPError(TEXT("No EnhancedPlayerInput on the local player"));

	// GetAppliedInputContextData() is protected, so read the UPROPERTY map via
	// reflection: keys are UInputMappingContext object refs, values are the
	// FAppliedInputContextData struct (Priority, RegistrationCount).
	TArray<TSharedPtr<FJsonValue>> Contexts;
	FMapProperty* MapProp = CastField<FMapProperty>(EPI->GetClass()->FindPropertyByName(TEXT("AppliedInputContextData")));
	if (MapProp)
	{
		FObjectPropertyBase* KeyObjProp = CastField<FObjectPropertyBase>(MapProp->KeyProp);
		FStructProperty* ValStruct = CastField<FStructProperty>(MapProp->ValueProp);
		FIntProperty* PriorityProp = ValStruct ? CastField<FIntProperty>(ValStruct->Struct->FindPropertyByName(TEXT("Priority"))) : nullptr;
		FIntProperty* RegProp = ValStruct ? CastField<FIntProperty>(ValStruct->Struct->FindPropertyByName(TEXT("RegistrationCount"))) : nullptr;
		FScriptMapHelper Helper(MapProp, MapProp->ContainerPtrToValuePtr<void>(EPI));
		for (int32 i = 0; i < Helper.GetMaxIndex(); ++i)
		{
			if (!Helper.IsValidIndex(i)) continue;
			UObject* IMC = KeyObjProp ? KeyObjProp->GetObjectPropertyValue(Helper.GetKeyPtr(i)) : nullptr;
			if (!IMC) continue;
			void* ValPtr = Helper.GetValuePtr(i);
			TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
			C->SetStringField(TEXT("imc"), IMC->GetPathName());
			C->SetStringField(TEXT("name"), IMC->GetName());
			if (PriorityProp) C->SetNumberField(TEXT("priority"), PriorityProp->GetPropertyValue_InContainer(ValPtr));
			if (RegProp) C->SetNumberField(TEXT("registrationCount"), RegProp->GetPropertyValue_InContainer(ValPtr));
			Contexts.Add(MakeShared<FJsonValueObject>(C));
		}
	}

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("playerIndex"), PlayerIndex);
	Result->SetArrayField(TEXT("appliedContexts"), Contexts);
	Result->SetNumberField(TEXT("count"), Contexts.Num());
	return MCPResult(Result);
}

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

			// #649: accept either {type:"<ShortName>", <props>} or
			// {class:"/Script/Module.Class", properties:{...}}.
			FString ClassPath;
			(*ModObj)->TryGetStringField(TEXT("class"), ClassPath);
			UClass* ModClass = nullptr;
			if (!ClassPath.IsEmpty())
			{
				ModClass = LoadClass<UInputModifier>(nullptr, *ClassPath);
				if (!ModClass) ModClass = LoadObject<UClass>(nullptr, *ClassPath);
				if (!ModClass) ModClass = FindClassByShortName(ClassPath);
				if (ModClass && !ModClass->IsChildOf(UInputModifier::StaticClass())) ModClass = nullptr;
			}

			FString TypeName;
			(*ModObj)->TryGetStringField(TEXT("type"), TypeName);
			if (ClassPath.IsEmpty() && TypeName.IsEmpty()) continue;

			// Resolve class: try multiple patterns (#169 fix)
			// "DeadZone" → UInputModifierDeadZone, "Negate" → UInputModifierNegate, etc.
			if (!ModClass && !TypeName.IsEmpty())
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

			// #649: property source is a nested `properties` object when
			// provided, else the modifier object's own sibling keys.
			const TSharedPtr<FJsonObject>* PropsObj = nullptr;
			const bool bUseNestedProps = (*ModObj)->TryGetObjectField(TEXT("properties"), PropsObj) && PropsObj && (*PropsObj).IsValid();
			const auto& PropSource = bUseNestedProps ? (*PropsObj)->Values : (*ModObj)->Values;

			// Set properties via reflection
			for (const auto& Pair : PropSource)
			{
				if (Pair.Key == TEXT("type") || Pair.Key == TEXT("class") || Pair.Key == TEXT("properties")) continue;

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
	// #725: triggers previously accepted only {type:"Hold"}; the more obvious
	// {class:"/Script/EnhancedInput.InputTriggerHold"} was rejected. Worse, an
	// unresolvable shape could leave a NULL entry in Mapping.Triggers, which
	// trips AssetCheck on save. Build into a temp array, accept class OR type
	// (mirroring modifiers, #649), apply nested "properties", and never append
	// a null; report any failed specs instead of silently corrupting the asset.
	TArray<TSharedPtr<FJsonValue>> FailedTriggers;
	const TArray<TSharedPtr<FJsonValue>>* TriggersArr = nullptr;
	if (Params->TryGetArrayField(TEXT("triggers"), TriggersArr) && TriggersArr)
	{
		TArray<TObjectPtr<UInputTrigger>> NewTriggers;
		for (const auto& TrigVal : *TriggersArr)
		{
			const TSharedPtr<FJsonObject>* TrigObj = nullptr;
			if (!TrigVal->TryGetObject(TrigObj) || !TrigObj)
			{
				FailedTriggers.Add(MakeShared<FJsonValueString>(TEXT("(non-object trigger entry)")));
				continue;
			}

			FString ClassPath;
			(*TrigObj)->TryGetStringField(TEXT("class"), ClassPath);
			UClass* TrigClass = nullptr;
			if (!ClassPath.IsEmpty())
			{
				TrigClass = LoadClass<UInputTrigger>(nullptr, *ClassPath);
				if (!TrigClass) TrigClass = LoadObject<UClass>(nullptr, *ClassPath);
				if (!TrigClass) TrigClass = FindClassByShortName(ClassPath);
				if (TrigClass && !TrigClass->IsChildOf(UInputTrigger::StaticClass())) TrigClass = nullptr;
			}

			FString TypeName;
			(*TrigObj)->TryGetStringField(TEXT("type"), TypeName);
			if (!TrigClass && !TypeName.IsEmpty())
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
				FailedTriggers.Add(MakeShared<FJsonValueString>(ClassPath.IsEmpty() ? TypeName : ClassPath));
				continue;
			}

			UInputTrigger* Trigger = NewObject<UInputTrigger>(IMC, TrigClass);
			if (!Trigger)
			{
				FailedTriggers.Add(MakeShared<FJsonValueString>(ClassPath.IsEmpty() ? TypeName : ClassPath));
				continue;
			}

			// Set properties via reflection. Accept both top-level fields and a
			// nested "properties" object (same as modifiers).
			auto ApplyTriggerProps = [&](const TSharedPtr<FJsonObject>& Obj)
			{
				for (const auto& Pair : Obj->Values)
				{
					if (Pair.Key == TEXT("type") || Pair.Key == TEXT("class") || Pair.Key == TEXT("properties")) continue;
					FProperty* Prop = TrigClass->FindPropertyByName(FName(*Pair.Key));
					if (!Prop) continue;
					void* PropAddr = Prop->ContainerPtrToValuePtr<void>(Trigger);
					if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
					{
						double Val = 0; Pair.Value->TryGetNumber(Val);
						FloatProp->SetPropertyValue(PropAddr, (float)Val);
					}
					else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
					{
						double Val = 0; Pair.Value->TryGetNumber(Val);
						DoubleProp->SetPropertyValue(PropAddr, Val);
					}
					else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
					{
						bool Val = false; Pair.Value->TryGetBool(Val);
						BoolProp->SetPropertyValue(PropAddr, Val);
					}
					else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
					{
						double Val = 0; Pair.Value->TryGetNumber(Val);
						IntProp->SetPropertyValue(PropAddr, (int32)Val);
					}
				}
			};
			ApplyTriggerProps(*TrigObj);
			const TSharedPtr<FJsonObject>* PropsObj = nullptr;
			if ((*TrigObj)->TryGetObjectField(TEXT("properties"), PropsObj) && PropsObj && (*PropsObj).IsValid())
			{
				ApplyTriggerProps(*PropsObj);
			}

			NewTriggers.Add(Trigger);
		}

		Mapping.Triggers = NewTriggers;
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
	if (FailedTriggers.Num() > 0)
	{
		// #725: tell the caller which trigger specs did not resolve rather than
		// silently dropping them (or, worse, leaving a null entry behind).
		Result->SetArrayField(TEXT("failedTriggers"), FailedTriggers);
	}
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

