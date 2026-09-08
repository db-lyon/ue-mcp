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
#include "HandlerJsonProperty.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Engine.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectIterator.h"
#include "EnhancedInputSubsystemInterface.h"
#include "ScopedTransaction.h"


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
// #57 / #60  read_imc - Read InputMappingContext mappings
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
// #57 / #60  add_imc_mapping - Add key mapping to an IMC
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

	// Mark dirty - caller can use asset(save) to persist (#197 fix: SavePackage crash)
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

	// The inverse drops the mapping this call added, named by the same
	// (action, key) pair rather than by index, because a later add or remove
	// would have moved the index. remove_imc_mapping resolves exactly that pair.
	// Nothing is lost: the mapping was created here with no modifiers or
	// triggers on it.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("imcPath"), IMC->GetPathName());
	Payload->SetStringField(TEXT("inputActionPath"), InputAction->GetPathName());
	Payload->SetStringField(TEXT("key"), KeyName);
	MCPSetRollback(Result, TEXT("remove_imc_mapping"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), false);

	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// #75  set_mapping_modifiers - Add modifiers/triggers to an IMC mapping
//      Creates UObject subobjects with IMC as outer so they serialize.
// ─────────────────────────────────────────────────────────────


// ─────────────────────────────────────────────────────────────
// #75  set_mapping_modifiers - Add modifiers/triggers to an IMC mapping
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

	// Both instanced lists as one comparable string: every object's class path
	// followed by every one of its property values, exported. Class paths alone
	// would report "unchanged" for a call that retuned a Hold threshold on the
	// same trigger class, which is exactly the write a caller most often
	// repeats. A list the caller did not pass contributes identically to both
	// readings, so it cancels out and only what was written can move this.
	// Written as a lambda rather than a file-local helper: this module is a
	// unity build, and a second .cpp defining the same free function in the
	// same blob is a redefinition.
	const auto DescribeInstancedList = [](const auto& Objects) -> FString
	{
		FString Digest;
		for (const auto& Entry : Objects)
		{
			// Taken as a raw UObject* rather than left as the TObjectPtr the
			// array holds: ContainerPtrToValuePtr wants a void*, and going
			// through the smart pointer's conversion operator on the way there
			// is a needless place for the overload to pick something else.
			const UObject* Object = Entry;
			if (!Object) { Digest += TEXT("|<null>"); continue; }
			Digest += TEXT("|") + Object->GetClass()->GetPathName();
			for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
			{
				FString Exported;
				It->ExportTextItem_Direct(
					Exported, It->ContainerPtrToValuePtr<void>(Object), nullptr, nullptr, PPF_None);
				Digest += TEXT("=") + Exported;
			}
		}
		return Digest;
	};
	const FString ListsBefore =
		DescribeInstancedList(Mapping.Modifiers) + TEXT("//") + DescribeInstancedList(Mapping.Triggers);

	// The classes currently on the mapping, captured before either list is
	// emptied, in the {class: "<path>"} shape this same action accepts. Only
	// the list this call is about to replace is captured: a list the caller did
	// not pass is left alone, so rewriting it would be a change, not an undo.
	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	bool bCapturedAnyList = false;
	int32 PreviousConfiguredObjects = 0;
	{
		auto CaptureClasses = [&](const TCHAR* ParamName, const auto& Objects)
		{
			if (!Params->HasField(ParamName)) return;
			TArray<TSharedPtr<FJsonValue>> Entries;
			for (const auto& Object : Objects)
			{
				if (!Object) continue;
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("class"), Object->GetClass()->GetPathName());
				Entries.Add(MakeShared<FJsonValueObject>(Entry));
				++PreviousConfiguredObjects;
			}
			RollbackPayload->SetArrayField(ParamName, Entries);
			bCapturedAnyList = true;
		};
		CaptureClasses(TEXT("modifiers"), Mapping.Modifiers);
		CaptureClasses(TEXT("triggers"), Mapping.Triggers);
	}

	// #725 reported unresolvable TRIGGER specs but the modifier loop below
	// still dropped an unknown class, and an unknown property key on a
	// resolved one, without a word: the call returned success for a modifier
	// that was never added, or was added unconfigured. Both are now collected
	// and reported. The loop still continues rather than failing the call, so
	// this is additive: nothing that used to succeed now errors.
	TArray<TSharedPtr<FJsonValue>> FailedModifiers;
	TArray<TSharedPtr<FJsonValue>> IgnoredProperties;

	// ── Modifiers ──
	const TArray<TSharedPtr<FJsonValue>>* ModifiersArr = nullptr;
	if (Params->TryGetArrayField(TEXT("modifiers"), ModifiersArr) && ModifiersArr)
	{
		Mapping.Modifiers.Empty();
		for (const auto& ModVal : *ModifiersArr)
		{
			const TSharedPtr<FJsonObject>* ModObj = nullptr;
			if (!ModVal->TryGetObject(ModObj) || !ModObj)
			{
				FailedModifiers.Add(MakeShared<FJsonValueString>(TEXT("(non-object modifier entry)")));
				continue;
			}

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
			if (ClassPath.IsEmpty() && TypeName.IsEmpty())
			{
				FailedModifiers.Add(MakeShared<FJsonValueString>(TEXT("(modifier entry with neither class nor type)")));
				continue;
			}

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
				FailedModifiers.Add(MakeShared<FJsonValueString>(ClassPath.IsEmpty() ? TypeName : ClassPath));
				continue; // unknown modifier type, reported in failedModifiers
			}

			// Create with IMC as outer - this is the key fix for #75
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
				if (!Prop)
				{
					IgnoredProperties.Add(MakeShared<FJsonValueString>(
						ModClass->GetName() + TEXT(".") + FString(Pair.Key)));
					continue;
				}

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

	// Mark dirty - caller can use asset(save) to persist (#197 fix)
	UPackage* Pkg = IMC->GetOutermost();
	if (Pkg)
	{
		Pkg->MarkPackageDirty();
	}

	// The same reading, taken off the same two lists after the write. Rebuilt
	// objects of the same classes carrying the same property values produce the
	// same string, so a replayed call reports that it moved nothing.
	const FString ListsAfter =
		DescribeInstancedList(Mapping.Modifiers) + TEXT("//") + DescribeInstancedList(Mapping.Triggers);
	const bool bListsChanged = ListsAfter != ListsBefore;

	auto Result = MCPSuccess();
	if (bListsChanged) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("updated"), false);
	Result->SetBoolField(TEXT("unchanged"), !bListsChanged);
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
	if (FailedModifiers.Num() > 0)
	{
		Result->SetArrayField(TEXT("failedModifiers"), FailedModifiers);
	}
	if (IgnoredProperties.Num() > 0)
	{
		// A property key that names nothing on the resolved class was applied
		// to nothing. modifierCount still counts the modifier, so without this
		// the result reads as a full success.
		Result->SetArrayField(TEXT("ignoredProperties"), IgnoredProperties);
	}

	if (bCapturedAnyList && bListsChanged)
	{
		// Replaying this action with the classes that were there. The modifier
		// and trigger objects are rebuilt from their class defaults, so this
		// restores WHICH ones were on the mapping, not how they were tuned.
		RollbackPayload->SetStringField(TEXT("imcPath"), IMC->GetPathName());
		RollbackPayload->SetNumberField(TEXT("mappingIndex"), MappingIndex);
		MCPSetRollback(Result, TEXT("set_mapping_modifiers"), RollbackPayload);
		Result->SetBoolField(TEXT("rollbackLossy"), PreviousConfiguredObjects > 0);
		if (PreviousConfiguredObjects > 0)
		{
			Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
				TEXT("The %d modifier/trigger class(es) that were on this mapping are restored, each rebuilt at its "
					 "CLASS DEFAULTS. Any property that had been tuned on them - a DeadZone threshold, a Hold time - "
					 "is not carried, because the record holds classes rather than per-object property values. Read "
					 "them with gameplay(read_imc) first if the tuning matters."),
				PreviousConfiguredObjects));
		}
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), bCapturedAnyList
			? TEXT("The lists this call rebuilt hold exactly the same classes with exactly the same property values "
				   "they held before, so nothing changed and there is nothing to undo.")
			: TEXT("Neither 'modifiers' nor 'triggers' was passed, so neither list was replaced and there is nothing "
				   "to undo."));
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
		// Mark dirty only - caller can use asset(save) to persist (#197 fix)
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
		// Idempotent replay: a rollback that runs after the mapping is already
		// gone must not fail the flow, because "no such mapping" is the state
		// the caller asked for. Only the "nothing matched a selector" case is a
		// no-op; an out-of-range mappingIndex or a missing selector is still a
		// bad call and still errors.
		FString ActionPath, KeyName;
		const bool bNamedPair =
			!Params->HasField(TEXT("mappingIndex"))
			&& ((Params->TryGetStringField(TEXT("inputActionPath"), ActionPath) && !ActionPath.IsEmpty())
				|| (Params->TryGetStringField(TEXT("key"), KeyName) && !KeyName.IsEmpty()));
		if (bNamedPair)
		{
			auto Noop = MCPSuccess();
			Noop->SetStringField(TEXT("imcPath"), IMC->GetPathName());
			if (!ActionPath.IsEmpty()) Noop->SetStringField(TEXT("inputActionPath"), ActionPath);
			if (!KeyName.IsEmpty()) Noop->SetStringField(TEXT("key"), KeyName);
			Noop->SetBoolField(TEXT("alreadyDeleted"), true);
			Noop->SetBoolField(TEXT("updated"), false);
			Noop->SetNumberField(TEXT("count"), IMC->GetMappings().Num());
			Noop->SetBoolField(TEXT("rollbackPossible"), false);
			Noop->SetStringField(TEXT("rollbackNote"),
				TEXT("No mapping matched, so nothing was removed and there is nothing to restore."));
			return MCPResult(Noop);
		}
		return MCPError(ResolveError);
	}

	TArray<FEnhancedActionKeyMapping>& Mappings = const_cast<TArray<FEnhancedActionKeyMapping>&>(IMC->GetMappings());
	const FEnhancedActionKeyMapping Removed = Mappings[Idx];
	// Read before the removal: what add_imc_mapping cannot put back.
	const int32 RemovedModifiers = Removed.Modifiers.Num();
	const int32 RemovedTriggers = Removed.Triggers.Num();
	Mappings.RemoveAt(Idx);

	ImcEdit_Internal::SaveImc(IMC);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("imcPath"), IMC->GetPathName());
	Result->SetNumberField(TEXT("mappingIndex"), Idx);
	Result->SetBoolField(TEXT("alreadyDeleted"), false);
	Result->SetStringField(TEXT("removedInputAction"), Removed.Action ? Removed.Action->GetPathName() : TEXT("None"));
	Result->SetStringField(TEXT("removedKey"), Removed.Key.GetFName().ToString());
	Result->SetNumberField(TEXT("removedModifiers"), RemovedModifiers);
	Result->SetNumberField(TEXT("removedTriggers"), RemovedTriggers);
	Result->SetNumberField(TEXT("count"), Mappings.Num());

	if (Removed.Action)
	{
		// Re-mapping the same (action, key) pair is the inverse. The mapping is
		// APPENDED, so its index changes, and add_imc_mapping creates a bare
		// mapping - anything hanging off this one does not come back.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("imcPath"), IMC->GetPathName());
		Payload->SetStringField(TEXT("inputActionPath"), Removed.Action->GetPathName());
		Payload->SetStringField(TEXT("key"), Removed.Key.GetFName().ToString());
		MCPSetRollback(Result, TEXT("add_imc_mapping"), Payload);
		const bool bLossy = RemovedModifiers > 0 || RemovedTriggers > 0 || Idx != Mappings.Num();
		Result->SetBoolField(TEXT("rollbackLossy"), bLossy);
		if (bLossy)
		{
			Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
				TEXT("The action and key come back, but the mapping is re-added at the END of the list (index %d, not "
					 "%d), and the %d modifier(s) and %d trigger(s) it carried are NOT restored - add_imc_mapping "
					 "creates a bare mapping. Re-apply them with gameplay(set_mapping_modifiers)."),
				Mappings.Num(), Idx, RemovedModifiers, RemovedTriggers));
		}
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The removed mapping had no InputAction on it, and gameplay(add_imc_mapping) requires one, so there "
				 "is no call that puts an action-less mapping back."));
	}
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
	const bool bChanged = PrevKey != NewKey;
	Mappings[Idx].Key = NewKey;

	ImcEdit_Internal::SaveImc(IMC);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("imcPath"), IMC->GetPathName());
	Result->SetNumberField(TEXT("mappingIndex"), Idx);
	Result->SetStringField(TEXT("previousKey"), PrevKey.GetFName().ToString());
	Result->SetStringField(TEXT("newKey"), NewKey.GetFName().ToString());
	Result->SetBoolField(TEXT("unchanged"), !bChanged);
	if (bChanged) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("updated"), false);

	if (bChanged)
	{
		// Writing the previous key back. Addressed by mappingIndex, which this
		// call did not move, rather than by the key - which is the thing that
		// changed and would no longer select the same mapping.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("imcPath"), IMC->GetPathName());
		Payload->SetNumberField(TEXT("mappingIndex"), Idx);
		Payload->SetStringField(TEXT("newKey"), PrevKey.GetFName().ToString());
		MCPSetRollback(Result, TEXT("set_imc_mapping_key"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), false);
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The mapping already used this key, so nothing changed and there is nothing to undo."));
	}
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
	const bool bChanged = PrevAction != NewAction;
	Mappings[Idx].Action = NewAction;

	ImcEdit_Internal::SaveImc(IMC);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("imcPath"), IMC->GetPathName());
	Result->SetNumberField(TEXT("mappingIndex"), Idx);
	Result->SetStringField(TEXT("previousInputAction"), PrevAction ? PrevAction->GetPathName() : TEXT("None"));
	Result->SetStringField(TEXT("newInputAction"), NewAction->GetPathName());
	Result->SetBoolField(TEXT("unchanged"), !bChanged);
	if (bChanged) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("updated"), false);

	if (bChanged && PrevAction)
	{
		// Writing the previous action back, addressed by the index this call did
		// not move. Only emitted when there WAS a previous action: the inverse
		// requires a loadable newInputActionPath, and "None" is not one.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("imcPath"), IMC->GetPathName());
		Payload->SetNumberField(TEXT("mappingIndex"), Idx);
		Payload->SetStringField(TEXT("newInputActionPath"), PrevAction->GetPathName());
		MCPSetRollback(Result, TEXT("set_imc_mapping_action"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), false);
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), bChanged
			? TEXT("The mapping had no InputAction before this call, and set_imc_mapping_action requires a loadable "
				   "newInputActionPath, so there is no call that puts it back to none.")
			: TEXT("The mapping already used this InputAction, so nothing changed and there is nothing to undo."));
	}
	return MCPResult(Result);
}


// ═════════════════════════════════════════════════════════════
// Enhanced Input depth (V11).
//
// AUDIT, before anything below was written. Already shipping, and NOT rebuilt:
//
//   create_input_action / create_input_mapping_context  (:47, :115)
//   read_imc / list_imc_mappings                        (:147)
//   add_imc_mapping                                     (:205)
//   set_mapping_modifiers                               (:281) - the MAPPING's
//                                                       triggers and modifiers
//   remove_imc_mapping / set_imc_mapping_key /
//   set_imc_mapping_action                              (:622, :656, :699)
//   get_applied_imcs / get_input_mapping_contexts       GameplayHandlers_InputRuntime.cpp:155
//   list_input_assets                                   GameplayHandlers.cpp:603
//   K2Node_EnhancedInputAction in Blueprint graphs      BlueprintHandlers_Graph.cpp:155/589
//
// So context and action creation, mapping CRUD, per-mapping trigger and
// modifier authoring, applied-context readback and Blueprint event nodes were
// all already covered. Runtime input INJECTION is covered too, in pie-studio.
//
// What was genuinely missing, and is what this section adds:
//
//   * Reading an InputAction back at all. Nothing reported its ValueType, its
//     consumption flags, or the instanced triggers and modifiers hanging off
//     it, so an authored action could not be verified.
//   * The ACTION's own Triggers and Modifiers arrays, as distinct from a
//     mapping's. asset(set_property) cannot author them: both are Instanced
//     TArray<TObjectPtr<...>>, and the JSON property setter only ever assigns
//     an object it can LOAD FROM A PATH. It has no instantiate-from-class
//     form, so a property write can neither mint nor destroy the subobjects
//     these arrays hold.
//   * Applying and removing a mapping context on a LIVE player. The read half
//     already existed and had no write half, so an agent could see which
//     contexts were applied and could not change them without running game
//     code. AddMappingContext / RemoveMappingContext are engine calls on the
//     local player subsystem, not properties.
//   * Reading an action's live value. FInputActionInstance is transient
//     per-player state, and GetActionValue / FindActionInstanceData are plain
//     C++ methods, not UFUNCTIONs, so editor(invoke_function) cannot reach
//     them. This is the only way to prove input actually arrived.
//   * An audit. The failure mode Enhanced Input is known for is a 2D action
//     driven by 1D keys with no Swizzle modifier: it compiles, it binds, and
//     it silently reports zero forever.
//
// NOT built, deliberately, because a property write already reaches it:
// ValueType, ActionDescription, bTriggerWhenPaused, bConsumeInput,
// bConsumesActionAndAxisMappings, bReserveAllMappings, AccumulationBehavior,
// TriggerEventsThatConsumeLegacyKeys and every tunable on an existing trigger
// or modifier instance. read_input_action returns the objectPath of each of
// those instances; aim editor(set_property) at it.
// ═════════════════════════════════════════════════════════════
namespace InputDepth_Internal
{
	/** Human-facing name for an action's value type. */
	static FString ValueTypeName(EInputActionValueType Type)
	{
		switch (Type)
		{
		case EInputActionValueType::Boolean: return TEXT("Boolean");
		case EInputActionValueType::Axis1D:  return TEXT("Axis1D");
		case EInputActionValueType::Axis2D:  return TEXT("Axis2D");
		case EInputActionValueType::Axis3D:  return TEXT("Axis3D");
		default: break;
		}
		return TEXT("Unknown");
	}

	/** Concrete, authorable subclasses of a base this editor has loaded. An
	 *  error that names only the bad value leaves the caller guessing; this is
	 *  what turns it into a correctable one. */
	static TArray<FString> LoadedHelperClassNames(UClass* Base, int32 Max = 40)
	{
		TArray<FString> Names;
		if (!Base) return Names;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (Names.Num() >= Max) break;
			UClass* Class = *It;
			if (!Class || Class == Base || !Class->IsChildOf(Base)) continue;
			if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) continue;
			const FString Name = Class->GetName();
			if (Name.StartsWith(TEXT("SKEL_")) || Name.StartsWith(TEXT("REINST_")) || Name.StartsWith(TEXT("TRASHCLASS_"))) continue;
			Names.Add(Name);
		}
		Names.Sort();
		return Names;
	}

	/** Resolve one {type:"Hold"} or {class:"/Script/..."} spec to a concrete
	 *  subclass of Base. Both shapes are accepted because set_mapping_modifiers
	 *  already accepts both, and a caller should not have to learn two
	 *  grammars for the same object. */
	static UClass* ResolveHelperClass(
		const TSharedPtr<FJsonObject>& Spec, UClass* Base, const TCHAR* Prefix, FString& OutError)
	{
		FString ClassPath;
		Spec->TryGetStringField(TEXT("class"), ClassPath);
		FString TypeName;
		Spec->TryGetStringField(TEXT("type"), TypeName);
		if (ClassPath.IsEmpty() && TypeName.IsEmpty())
		{
			OutError = FString::Printf(TEXT("needs class or type; valid type values: %s"),
				*FString::Join(LoadedHelperClassNames(Base), TEXT(", ")));
			return nullptr;
		}

		TArray<FString> Candidates;
		if (!ClassPath.IsEmpty()) Candidates.Add(ClassPath);
		if (!TypeName.IsEmpty())
		{
			Candidates.Add(FString(TEXT("U")) + Prefix + TypeName);
			Candidates.Add(FString(Prefix) + TypeName);
			Candidates.Add(TypeName);
		}
		for (const FString& Candidate : Candidates)
		{
			UClass* Found = MCPResolveClassOfType(Candidate, Base);
			if (Found && !Found->HasAnyClassFlags(CLASS_Abstract)) return Found;
		}

		OutError = FString::Printf(TEXT("'%s' is not a concrete %s subclass; valid: %s"),
			ClassPath.IsEmpty() ? *TypeName : *ClassPath,
			*Base->GetName(),
			*FString::Join(LoadedHelperClassNames(Base), TEXT(", ")));
		return nullptr;
	}

	/** One trigger or modifier as a {class, properties} spec: exactly the shape
	 *  set_action_triggers takes, with every value in UE export-text form,
	 *  which the JSON property setter re-imports. This is what makes the
	 *  rollback an exact restore rather than an approximation. */
	static TSharedPtr<FJsonObject> HelperToSpec(const UObject* Helper)
	{
		TSharedPtr<FJsonObject> Spec = MakeShared<FJsonObject>();
		if (!Helper) return Spec;
		Spec->SetStringField(TEXT("class"), Helper->GetClass()->GetPathName());

		TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(Helper->GetClass()); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property) continue;
			// Only WRITABLE editable fields belong in a replayable spec. A
			// VisibleAnywhere or transient field would turn the undo into a
			// second failure.
			if (!Property->HasAnyPropertyFlags(CPF_Edit)) continue;
			if (Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient)) continue;
			if (MCPPropertyIsFixedArray(Property)) continue;
			Properties->SetField(Property->GetName(), MCPExportPropertyValue(Property, Helper));
		}
		Spec->SetObjectField(TEXT("properties"), Properties);
		return Spec;
	}

	/** A comparable identity for one instance: its class plus every editable
	 *  value. Two arrays with equal signatures mean the call changed nothing,
	 *  which is what makes a repeat safe AND able to say so. */
	static FString HelperSignature(const UObject* Helper)
	{
		if (!Helper) return TEXT("null");
		TArray<FString> Parts;
		Parts.Add(Helper->GetClass()->GetPathName());
		for (TFieldIterator<FProperty> It(Helper->GetClass()); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit)) continue;
			if (Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient)) continue;
			FString Text;
			Property->ExportTextItem_Direct(
				Text, Property->ContainerPtrToValuePtr<void>(Helper), nullptr, nullptr, PPF_None);
			Parts.Add(Property->GetName() + TEXT("=") + Text);
		}
		Parts.Sort();
		return FString::Join(Parts, TEXT("|"));
	}

	/** Write a spec's properties onto an instance. Accepts either a nested
	 *  "properties" object or the spec's own sibling keys, mirroring
	 *  set_mapping_modifiers. Unlike that handler, an unknown or unconvertible
	 *  property is an ERROR rather than a skip: silently dropping a
	 *  HoldTimeThreshold and returning success is how a trigger ends up
	 *  configured differently from what the caller asked for. */
	static bool ApplyHelperProperties(UObject* Helper, const TSharedPtr<FJsonObject>& Spec, FString& OutError)
	{
		const TSharedPtr<FJsonObject>* Nested = nullptr;
		const bool bNested = Spec->TryGetObjectField(TEXT("properties"), Nested) && Nested && (*Nested).IsValid();
		const auto& Source = bNested ? (*Nested)->Values : Spec->Values;
		for (const auto& Pair : Source)
		{
			// UE 5.8 stores JSON object keys in shared string storage; copy the
			// key before passing it to the FString-based property-path helper.
			const FString Key(Pair.Key);
			if (!bNested && (Key == TEXT("type") || Key == TEXT("class") || Key == TEXT("properties"))) continue;
			FString Error;
			if (!MCPJsonProperty::SetDottedPropertyFromJson(Helper, Key, Pair.Value, Error))
			{
				OutError = FString::Printf(TEXT("property '%s': %s"), *Key, *Error);
				return false;
			}
		}
		return true;
	}

	/** Build one array of trigger/modifier instances from its specs.
	 *
	 *  Every entry is resolved and configured before the caller assigns
	 *  anything, so a bad entry at position nine cannot leave the first eight
	 *  applied. Instances are constructed under Outer (the owning asset) so
	 *  they serialize with it; on failure they are simply never referenced by
	 *  a UPROPERTY and are collected, leaving the asset untouched. */
	static TSharedPtr<FJsonValue> BuildHelpers(
		const TArray<TSharedPtr<FJsonValue>>& Specs,
		UClass* Base,
		const TCHAR* Prefix,
		const TCHAR* FieldName,
		UObject* Outer,
		TArray<UObject*>& OutInstances)
	{
		OutInstances.Reset();
		for (int32 Index = 0; Index < Specs.Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* SpecObj = nullptr;
			if (!Specs[Index].IsValid() || !Specs[Index]->TryGetObject(SpecObj) || !SpecObj || !(*SpecObj).IsValid())
			{
				return MCPError(FString::Printf(
					TEXT("%s[%d] must be an object, either {type:\"Hold\", HoldTimeThreshold:0.5} or {class:\"/Script/EnhancedInput.InputTriggerHold\", properties:{HoldTimeThreshold:0.5}}"),
					FieldName, Index));
			}

			FString Error;
			UClass* Class = ResolveHelperClass(*SpecObj, Base, Prefix, Error);
			if (!Class)
			{
				return MCPError(FString::Printf(TEXT("%s[%d]: %s"), FieldName, Index, *Error));
			}

			UObject* Instance = NewObject<UObject>(Outer, Class, NAME_None, RF_Transactional);
			if (!Instance)
			{
				return MCPError(FString::Printf(TEXT("%s[%d]: failed to construct %s"), FieldName, Index, *Class->GetPathName()));
			}
			OutInstances.Add(Instance);

			if (!ApplyHelperProperties(Instance, *SpecObj, Error))
			{
				return MCPError(FString::Printf(TEXT("%s[%d] (%s) %s"), FieldName, Index, *Class->GetName(), *Error));
			}
		}
		return nullptr;
	}

	/** The live player whose Enhanced Input state an action targets. */
	struct FResolvedPlayer
	{
		APlayerController* Controller = nullptr;
		UEnhancedInputLocalPlayerSubsystem* Subsystem = nullptr;
		UEnhancedPlayerInput* PlayerInput = nullptr;
		int32 PieInstance = INDEX_NONE;
		int32 PlayerIndex = 0;
		FString WorldPath;
	};

	/** Resolve pieInstance/playerIndex to a local player's Enhanced Input
	 *  subsystem, naming what to do next in every failure case rather than
	 *  reporting an empty state that reads as "nothing is applied". */
	static TSharedPtr<FJsonValue> ResolveLocalPlayer(const TSharedPtr<FJsonObject>& Params, FResolvedPlayer& Out)
	{
		if (!GEngine) return MCPError(TEXT("Engine not available"));

		int32 RequestedInstance = INDEX_NONE;
		double RawInstance = 0.0;
		const bool bHasInstance = Params->TryGetNumberField(TEXT("pieInstance"), RawInstance);
		if (bHasInstance) RequestedInstance = FMath::RoundToInt(RawInstance);
		const int32 RequestedPlayer = OptionalInt(Params, TEXT("playerIndex"), 0);

		int32 WorldsSeen = 0;
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.WorldType != EWorldType::PIE && Ctx.WorldType != EWorldType::Game) continue;
			if (bHasInstance && Ctx.PIEInstance != RequestedInstance) continue;
			UWorld* World = Ctx.World();
			if (!World) continue;
			++WorldsSeen;

			int32 Index = 0;
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It, ++Index)
			{
				if (Index != RequestedPlayer) continue;
				APlayerController* PC = It->Get();
				if (!PC) continue;

				ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
				if (!LocalPlayer)
				{
					return MCPError(FString::Printf(
						TEXT("Player %d in PIE instance %d is a remote controller with no LocalPlayer, so it has no Enhanced Input subsystem. That state lives on the owning client: target that instance with pieInstance. See editor(list_pie_instances)."),
						RequestedPlayer, Ctx.PIEInstance));
				}

				UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
				if (!Subsystem)
				{
					return MCPError(TEXT("Enhanced Input subsystem is not active for this local player. Check that the EnhancedInput plugin is enabled and that Project Settings, Input, Default Player Input Class is EnhancedPlayerInput."));
				}

				Out.Controller = PC;
				Out.Subsystem = Subsystem;
				Out.PlayerInput = Subsystem->GetPlayerInput();
				Out.PieInstance = Ctx.PIEInstance;
				Out.PlayerIndex = RequestedPlayer;
				Out.WorldPath = World->GetPathName();
				return nullptr;
			}
		}

		if (WorldsSeen == 0)
		{
			return MCPError(bHasInstance
				? FString::Printf(TEXT("No running PIE world with instance %d. List them with editor(list_pie_instances), or start PIE with editor(play_in_editor)."), RequestedInstance)
				: FString(TEXT("PIE is not running, and Enhanced Input state only exists on a live player. Start it with editor(play_in_editor).")));
		}
		return MCPError(FString::Printf(
			TEXT("No player controller at playerIndex %d in the matching PIE world. Read the player list with gameplay(get_applied_imcs)."),
			RequestedPlayer));
	}

	/** Add the pieInstance/playerIndex a rollback has to target back onto its
	 *  payload, so an undo lands on the same player the call did. */
	static void AddPlayerTargetTo(TSharedPtr<FJsonObject> Payload, const FResolvedPlayer& Player)
	{
		Payload->SetNumberField(TEXT("pieInstance"), Player.PieInstance);
		Payload->SetNumberField(TEXT("playerIndex"), Player.PlayerIndex);
	}

	/** One trigger/modifier instance described for a read: class, the
	 *  objectPath editor(set_property) writes at, and its current values. */
	static TSharedPtr<FJsonObject> DescribeHelper(const UObject* Helper, int32 Index)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("index"), Index);
		if (!Helper)
		{
			Obj->SetStringField(TEXT("classPath"), TEXT("None"));
			// A null entry is not cosmetic: it fails asset validation on save.
			Obj->SetBoolField(TEXT("null"), true);
			return Obj;
		}
		Obj->SetStringField(TEXT("classPath"), Helper->GetClass()->GetPathName());
		Obj->SetStringField(TEXT("className"), Helper->GetClass()->GetName());
		Obj->SetStringField(TEXT("objectPath"), Helper->GetPathName());

		TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(Helper->GetClass()); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit)) continue;
			if (Property->HasAnyPropertyFlags(CPF_Transient)) continue;
			Properties->SetField(Property->GetName(), MCPExportPropertyValue(Property, Helper));
		}
		Obj->SetObjectField(TEXT("properties"), Properties);
		return Obj;
	}
}

// ─────────────────────────────────────────────────────────────
// read_input_action - the verification half authoring never had
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FGameplayHandlers::ReadInputAction(const TSharedPtr<FJsonObject>& Params)
{
	using namespace InputDepth_Internal;

	FString ActionPath;
	if (auto Err = RequireString(Params, TEXT("inputActionPath"), ActionPath)) return Err;

	UInputAction* Action = LoadAssetByPath<UInputAction>(ActionPath);
	if (!Action) return MCPAssetLoadError(ActionPath, TEXT("InputAction"));

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("inputActionPath"), Action->GetPathName());
	Result->SetStringField(TEXT("name"), Action->GetName());
	// The whole action is a plain UObject, so every scalar below is reachable
	// with editor(set_property) at this path. No typed setters are shipped.
	Result->SetStringField(TEXT("objectPath"), Action->GetPathName());
	Result->SetStringField(TEXT("valueType"), ValueTypeName(Action->ValueType));
	Result->SetStringField(TEXT("actionDescription"), Action->ActionDescription.ToString());
	Result->SetBoolField(TEXT("triggerWhenPaused"), Action->bTriggerWhenPaused);
	Result->SetBoolField(TEXT("consumeInput"), Action->bConsumeInput);
	Result->SetBoolField(TEXT("consumesActionAndAxisMappings"), Action->bConsumesActionAndAxisMappings);
	Result->SetBoolField(TEXT("reserveAllMappings"), Action->bReserveAllMappings);
	Result->SetNumberField(TEXT("triggerEventsThatConsumeLegacyKeys"), Action->TriggerEventsThatConsumeLegacyKeys);
	if (const FProperty* Accumulation = Action->GetClass()->FindPropertyByName(TEXT("AccumulationBehavior")))
	{
		Result->SetField(TEXT("accumulationBehavior"), MCPExportPropertyValue(Accumulation, Action));
	}

	// PlayerMappableKeySettings is protected, and FProperty reflection ignores
	// C++ access specifiers, so reading it here needs no accessor.
	FString SettingsPath = TEXT("None");
	if (const FObjectPropertyBase* SettingsProperty =
			CastField<FObjectPropertyBase>(Action->GetClass()->FindPropertyByName(TEXT("PlayerMappableKeySettings"))))
	{
		if (const UObject* Settings = SettingsProperty->GetObjectPropertyValue_InContainer(Action))
		{
			SettingsPath = Settings->GetPathName();
		}
	}
	Result->SetStringField(TEXT("playerMappableKeySettings"), SettingsPath);

	TArray<TSharedPtr<FJsonValue>> Triggers;
	for (int32 Index = 0; Index < Action->Triggers.Num(); ++Index)
	{
		Triggers.Add(MakeShared<FJsonValueObject>(DescribeHelper(Action->Triggers[Index], Index)));
	}
	TArray<TSharedPtr<FJsonValue>> Modifiers;
	for (int32 Index = 0; Index < Action->Modifiers.Num(); ++Index)
	{
		Modifiers.Add(MakeShared<FJsonValueObject>(DescribeHelper(Action->Modifiers[Index], Index)));
	}
	Result->SetArrayField(TEXT("triggers"), Triggers);
	Result->SetNumberField(TEXT("triggerCount"), Triggers.Num());
	Result->SetArrayField(TEXT("modifiers"), Modifiers);
	Result->SetNumberField(TEXT("modifierCount"), Modifiers.Num());
	Result->SetStringField(TEXT("note"),
		TEXT("These triggers and modifiers are the ACTION's own, applied after every mapping's. Each entry carries an objectPath: tune it with editor(set_property), and add or remove entries with set_action_triggers. read_imc reports the per-mapping arrays instead."));
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// set_action_triggers - author the instanced arrays a property write cannot
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FGameplayHandlers::SetActionTriggers(const TSharedPtr<FJsonObject>& Params)
{
	using namespace InputDepth_Internal;

	FString ActionPath;
	if (auto Err = RequireString(Params, TEXT("inputActionPath"), ActionPath)) return Err;

	UInputAction* Action = LoadAssetByPath<UInputAction>(ActionPath);
	if (!Action) return MCPAssetLoadError(ActionPath, TEXT("InputAction"));

	const TArray<TSharedPtr<FJsonValue>>* TriggerSpecs = nullptr;
	const bool bHasTriggers = Params->TryGetArrayField(TEXT("triggers"), TriggerSpecs) && TriggerSpecs != nullptr;
	const TArray<TSharedPtr<FJsonValue>>* ModifierSpecs = nullptr;
	const bool bHasModifiers = Params->TryGetArrayField(TEXT("modifiers"), ModifierSpecs) && ModifierSpecs != nullptr;
	const bool bClear = OptionalBool(Params, TEXT("clear"), false);

	if (!bHasTriggers && !bHasModifiers && !bClear)
	{
		return MCPError(TEXT("Pass triggers and/or modifiers to author, or clear=true to empty both arrays. An omitted array is left alone unless clear is true; a supplied array REPLACES that array wholesale."));
	}

	// Prior state, captured before anything is built. This is both the
	// rollback payload and the answer to "did this call change anything".
	TArray<TSharedPtr<FJsonValue>> PriorTriggerSpecs;
	TArray<TSharedPtr<FJsonValue>> PriorModifierSpecs;
	TArray<FString> PriorTriggerSig;
	TArray<FString> PriorModifierSig;
	for (const TObjectPtr<UInputTrigger>& Trigger : Action->Triggers)
	{
		PriorTriggerSpecs.Add(MakeShared<FJsonValueObject>(HelperToSpec(Trigger)));
		PriorTriggerSig.Add(HelperSignature(Trigger));
	}
	for (const TObjectPtr<UInputModifier>& Modifier : Action->Modifiers)
	{
		PriorModifierSpecs.Add(MakeShared<FJsonValueObject>(HelperToSpec(Modifier)));
		PriorModifierSig.Add(HelperSignature(Modifier));
	}

	// Build BOTH arrays completely before either is assigned, so a bad entry
	// in modifiers cannot leave the new triggers already applied.
	TArray<UObject*> NewTriggers;
	TArray<UObject*> NewModifiers;
	if (bHasTriggers)
	{
		if (auto Err = BuildHelpers(*TriggerSpecs, UInputTrigger::StaticClass(), TEXT("InputTrigger"), TEXT("triggers"), Action, NewTriggers))
		{
			return Err;
		}
	}
	if (bHasModifiers)
	{
		if (auto Err = BuildHelpers(*ModifierSpecs, UInputModifier::StaticClass(), TEXT("InputModifier"), TEXT("modifiers"), Action, NewModifiers))
		{
			return Err;
		}
	}

	const bool bWriteTriggers = bHasTriggers || bClear;
	const bool bWriteModifiers = bHasModifiers || bClear;

	TArray<FString> NextTriggerSig = PriorTriggerSig;
	if (bWriteTriggers)
	{
		NextTriggerSig.Reset();
		for (const UObject* Instance : NewTriggers) NextTriggerSig.Add(HelperSignature(Instance));
	}
	TArray<FString> NextModifierSig = PriorModifierSig;
	if (bWriteModifiers)
	{
		NextModifierSig.Reset();
		for (const UObject* Instance : NewModifiers) NextModifierSig.Add(HelperSignature(Instance));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("inputActionPath"), Action->GetPathName());

	if (NextTriggerSig == PriorTriggerSig && NextModifierSig == PriorModifierSig)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetNumberField(TEXT("triggerCount"), Action->Triggers.Num());
		Result->SetNumberField(TEXT("modifierCount"), Action->Modifiers.Num());
		Result->SetStringField(TEXT("rollbackUnavailable"),
			TEXT("the action already carried exactly these triggers and modifiers; nothing changed"));
		return MCPResult(Result);
	}

	const bool bShouldActuallyTransact = GEditor != nullptr;
	FScopedTransaction Transaction(
		NSLOCTEXT("UEMCP", "SetInputActionTriggers", "Set Input Action Triggers and Modifiers"),
		bShouldActuallyTransact);
	Action->Modify();
	if (bWriteTriggers)
	{
		Action->Triggers.Reset();
		for (UObject* Instance : NewTriggers) Action->Triggers.Add(Cast<UInputTrigger>(Instance));
	}
	if (bWriteModifiers)
	{
		Action->Modifiers.Reset();
		for (UObject* Instance : NewModifiers) Action->Modifiers.Add(Cast<UInputModifier>(Instance));
	}

#if WITH_EDITOR
	// A running player caches the instanced triggers off this action, and the
	// engine tracks which actions changed theirs so it can rebuild those
	// caches. Writing the array without going through PostEditChange leaves
	// that bookkeeping stale, so an already-applied context keeps the old
	// trigger behaviour with no error anywhere.
	Action->PostEditChange();
#endif
	Action->MarkPackageDirty();

	MCPSetExisted(Result);
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("unchanged"), false);
	Result->SetNumberField(TEXT("triggerCount"), Action->Triggers.Num());
	Result->SetNumberField(TEXT("modifierCount"), Action->Modifiers.Num());
	Result->SetBoolField(TEXT("triggersReplaced"), bWriteTriggers);
	Result->SetBoolField(TEXT("modifiersReplaced"), bWriteModifiers);

	FString SaveReason;
	if (!SaveAssetPackageChecked(Action, SaveReason))
	{
		Transaction.Cancel();
		return MCPError(FString::Printf(
			TEXT("Authored the triggers and modifiers but could not save %s: %s"), *Action->GetPathName(), *SaveReason));
	}

	// Both arrays go into the payload whether or not this call wrote both, so
	// the undo restores the exact prior state rather than half of it.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("inputActionPath"), Action->GetPathName());
	Payload->SetArrayField(TEXT("triggers"), PriorTriggerSpecs);
	Payload->SetArrayField(TEXT("modifiers"), PriorModifierSpecs);
	MCPSetRollback(Result, TEXT("set_action_triggers"), Payload);
	Result->SetBoolField(TEXT("rollbackAvailable"), true);
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// apply_mapping_context / remove_mapping_context - the write half of
// get_applied_imcs, on a live player
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FGameplayHandlers::ApplyMappingContext(const TSharedPtr<FJsonObject>& Params)
{
	using namespace InputDepth_Internal;

	FString ContextPath;
	if (auto Err = RequireString(Params, TEXT("mappingContext"), ContextPath)) return Err;

	UInputMappingContext* IMC = LoadAssetByPath<UInputMappingContext>(ContextPath);
	if (!IMC) return MCPAssetLoadError(ContextPath, TEXT("InputMappingContext"));

	const int32 Priority = OptionalInt(Params, TEXT("priority"), 0);

	FResolvedPlayer Player;
	if (auto Err = ResolveLocalPlayer(Params, Player)) return Err;

	int32 ExistingPriority = 0;
	const bool bAlready = Player.Subsystem->HasMappingContext(IMC, ExistingPriority);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("mappingContext"), IMC->GetPathName());
	Result->SetNumberField(TEXT("priority"), Priority);
	Result->SetNumberField(TEXT("pieInstance"), Player.PieInstance);
	Result->SetNumberField(TEXT("playerIndex"), Player.PlayerIndex);
	Result->SetStringField(TEXT("worldPath"), Player.WorldPath);
	// This changes a live player, not an asset. Nothing is written to disk and
	// nothing survives the end of the session.
	Result->SetStringField(TEXT("scope"),
		TEXT("runtime: affects the live player only and is gone when PIE stops"));

	if (bAlready && ExistingPriority == Priority)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("alreadyApplied"), true);
		Result->SetStringField(TEXT("rollbackUnavailable"),
			TEXT("the context was already applied at this priority; nothing changed"));
		return MCPResult(Result);
	}

	FModifyContextOptions Options;
	// Apply synchronously. The default defers the control-mapping rebuild to
	// the end of the frame, which reads as a silent no-op to anything that
	// verifies with get_applied_imcs in the same turn.
	Options.bForceImmediately = true;
	Player.Subsystem->AddMappingContext(IMC, Priority, Options);

	Result->SetBoolField(TEXT("alreadyApplied"), false);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("mappingContext"), IMC->GetPathName());
	AddPlayerTargetTo(Payload, Player);

	if (bAlready)
	{
		// It was applied at another priority, so the undo is a re-apply at the
		// old one rather than a removal.
		MCPSetExisted(Result);
		MCPSetUpdated(Result);
		Result->SetNumberField(TEXT("previousPriority"), ExistingPriority);
		Payload->SetNumberField(TEXT("priority"), ExistingPriority);
		MCPSetRollback(Result, TEXT("apply_mapping_context"), Payload);
	}
	else
	{
		MCPSetCreated(Result);
		MCPSetRollback(Result, TEXT("remove_mapping_context"), Payload);
	}
	Result->SetBoolField(TEXT("rollbackAvailable"), true);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::RemoveMappingContext(const TSharedPtr<FJsonObject>& Params)
{
	using namespace InputDepth_Internal;

	FString ContextPath;
	if (auto Err = RequireString(Params, TEXT("mappingContext"), ContextPath)) return Err;

	UInputMappingContext* IMC = LoadAssetByPath<UInputMappingContext>(ContextPath);
	if (!IMC) return MCPAssetLoadError(ContextPath, TEXT("InputMappingContext"));

	FResolvedPlayer Player;
	if (auto Err = ResolveLocalPlayer(Params, Player)) return Err;

	int32 ExistingPriority = 0;
	const bool bApplied = Player.Subsystem->HasMappingContext(IMC, ExistingPriority);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("mappingContext"), IMC->GetPathName());
	Result->SetNumberField(TEXT("pieInstance"), Player.PieInstance);
	Result->SetNumberField(TEXT("playerIndex"), Player.PlayerIndex);
	Result->SetStringField(TEXT("worldPath"), Player.WorldPath);
	Result->SetStringField(TEXT("scope"),
		TEXT("runtime: affects the live player only and is gone when PIE stops"));

	if (!bApplied)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("alreadyAbsent"), true);
		Result->SetStringField(TEXT("rollbackUnavailable"),
			TEXT("the context was not applied to this player; nothing changed"));
		return MCPResult(Result);
	}

	FModifyContextOptions Options;
	Options.bForceImmediately = true;
	Player.Subsystem->RemoveMappingContext(IMC, Options);

	MCPSetExisted(Result);
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("alreadyAbsent"), false);
	Result->SetNumberField(TEXT("removedPriority"), ExistingPriority);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("mappingContext"), IMC->GetPathName());
	Payload->SetNumberField(TEXT("priority"), ExistingPriority);
	AddPlayerTargetTo(Payload, Player);
	MCPSetRollback(Result, TEXT("apply_mapping_context"), Payload);
	Result->SetBoolField(TEXT("rollbackAvailable"), true);
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// get_action_value - did the input actually arrive
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FGameplayHandlers::GetActionValue(const TSharedPtr<FJsonObject>& Params)
{
	using namespace InputDepth_Internal;

	FResolvedPlayer Player;
	if (auto Err = ResolveLocalPlayer(Params, Player)) return Err;
	if (!Player.PlayerInput)
	{
		return MCPError(TEXT("The local player has no UEnhancedPlayerInput yet. It is created on the first input tick, so step PIE forward and retry."));
	}

	const FString ActionPath = OptionalString(Params, TEXT("inputActionPath"));

	TArray<const UInputAction*> Wanted;
	if (!ActionPath.IsEmpty())
	{
		UInputAction* Action = LoadAssetByPath<UInputAction>(ActionPath);
		if (!Action) return MCPAssetLoadError(ActionPath, TEXT("InputAction"));
		Wanted.Add(Action);
	}
	else
	{
		// ActionInstanceData is protected, but the player's applied mappings
		// name every action it currently has bound, which is the same set.
		TConstArrayView<FEnhancedActionKeyMapping> Mappings;
#if UE_MCP_HAS_5_8_API
		Mappings = Player.PlayerInput->GetEnhancedActionMappingsView();
#else
		// GetEnhancedActionMappingsView() arrived in 5.8 and is the only public
		// route to this list: GetEnhancedActionMappings() is protected. The
		// backing EnhancedActionMappings is a UPROPERTY though, and FProperty
		// reflection ignores C++ access specifiers, so the same array is still
		// readable. Same route gameplay(get_applied_imcs) takes to the applied
		// contexts.
		if (FArrayProperty* MappingsProp = CastField<FArrayProperty>(
				Player.PlayerInput->GetClass()->FindPropertyByName(TEXT("EnhancedActionMappings"))))
		{
			Mappings = *MappingsProp->ContainerPtrToValuePtr<TArray<FEnhancedActionKeyMapping>>(Player.PlayerInput);
		}
#endif
		TSet<const UInputAction*> Seen;
		for (const FEnhancedActionKeyMapping& Mapping : Mappings)
		{
			const UInputAction* Action = Mapping.Action;
			if (Action && !Seen.Contains(Action))
			{
				Seen.Add(Action);
				Wanted.Add(Action);
			}
		}
	}

	const UEnum* TriggerEventEnum = StaticEnum<ETriggerEvent>();
	TArray<TSharedPtr<FJsonValue>> Actions;
	for (const UInputAction* Action : Wanted)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("inputActionPath"), Action->GetPathName());
		Entry->SetStringField(TEXT("name"), Action->GetName());
		Entry->SetStringField(TEXT("valueType"), ValueTypeName(Action->ValueType));

		const FInputActionValue Value = Player.PlayerInput->GetActionValue(Action);
		TSharedPtr<FJsonObject> ValueObj = MakeShared<FJsonObject>();
		ValueObj->SetNumberField(TEXT("x"), Value[0]);
		ValueObj->SetNumberField(TEXT("y"), Value[1]);
		ValueObj->SetNumberField(TEXT("z"), Value[2]);
		Entry->SetObjectField(TEXT("value"), ValueObj);
		Entry->SetNumberField(TEXT("magnitude"), Value.GetMagnitude());
		Entry->SetBoolField(TEXT("nonZero"), Value.IsNonZero());
		Entry->SetStringField(TEXT("valueText"), Value.ToString());

		const FInputActionInstance* Instance = Player.PlayerInput->FindActionInstanceData(Action);
		Entry->SetBoolField(TEXT("hasInstanceData"), Instance != nullptr);
		if (Instance)
		{
			const ETriggerEvent Event = Instance->GetTriggerEvent();
			Entry->SetStringField(TEXT("triggerEvent"), TriggerEventEnum
				? TriggerEventEnum->GetNameStringByValue(static_cast<int64>(Event))
				: FString::FromInt(static_cast<int32>(Event)));
			Entry->SetNumberField(TEXT("elapsedProcessedTime"), Instance->GetElapsedTime());
			Entry->SetNumberField(TEXT("elapsedTriggeredTime"), Instance->GetTriggeredTime());
			Entry->SetNumberField(TEXT("lastTriggeredWorldTime"), Instance->GetLastTriggeredWorldTime());
		}
		else
		{
			// No instance data means no applied mapping context has ever bound
			// this action for this player, which is a different failure from
			// "bound but reading zero".
			Entry->SetStringField(TEXT("note"),
				TEXT("No per-player instance data: no applied mapping context binds this action for this player. Check gameplay(get_applied_imcs)."));
		}

		TArray<FString> Keys;
		for (const FKey& Key : Player.Subsystem->QueryKeysMappedToAction(Action))
		{
			Keys.Add(Key.GetFName().ToString());
		}
		Entry->SetArrayField(TEXT("mappedKeys"), MCPStringListToJson(Keys));

		Actions.Add(MakeShared<FJsonValueObject>(Entry));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("actions"), Actions);
	Result->SetNumberField(TEXT("actionCount"), Actions.Num());
	Result->SetNumberField(TEXT("pieInstance"), Player.PieInstance);
	Result->SetNumberField(TEXT("playerIndex"), Player.PlayerIndex);
	Result->SetStringField(TEXT("worldPath"), Player.WorldPath);
	// The engine zeroes the reported value outside a Triggered event, so a
	// held axis read on the wrong frame is zero by design, not by failure.
	Result->SetStringField(TEXT("note"),
		TEXT("value is zero whenever triggerEvent is not Triggered, by engine design, so read triggerEvent before concluding the input did not arrive."));
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// validate_input - the silent failures Enhanced Input is known for
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FGameplayHandlers::ValidateInput(const TSharedPtr<FJsonObject>& Params)
{
	using namespace InputDepth_Internal;

	const FString SingleImc = OptionalString(Params, TEXT("imcPath"));
	const FString Directory = OptionalString(Params, TEXT("directory"), TEXT("/Game"));
	const bool bRecursive = OptionalBool(Params, TEXT("recursive"), true);
	const int32 Limit = FMath::Clamp(OptionalInt(Params, TEXT("limit"), 200), 1, 2000);

	TArray<UInputMappingContext*> Contexts;
	TArray<FString> Skipped;
	bool bSweptWholeProject = false;

	if (!SingleImc.IsEmpty())
	{
		UInputMappingContext* IMC = LoadAssetByPath<UInputMappingContext>(SingleImc);
		if (!IMC) return MCPAssetLoadError(SingleImc, TEXT("InputMappingContext"));
		Contexts.Add(IMC);
	}
	else
	{
		bSweptWholeProject = true;
		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> Assets;
		AR.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/EnhancedInput"), TEXT("InputMappingContext")), Assets, true);
		for (const FAssetData& Asset : Assets)
		{
			const FString PackagePath = Asset.PackagePath.ToString();
			const bool bInScope = bRecursive
				? (PackagePath == Directory || PackagePath.StartsWith(Directory + TEXT("/")))
				: (PackagePath == Directory);
			if (!bInScope) continue;
			if (Contexts.Num() >= Limit)
			{
				Skipped.Add(Asset.GetObjectPathString());
				continue;
			}
			if (UInputMappingContext* IMC = Cast<UInputMappingContext>(Asset.GetAsset()))
			{
				Contexts.Add(IMC);
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> Problems;
	int32 ErrorCount = 0;
	int32 WarningCount = 0;
	auto AddProblem = [&Problems, &ErrorCount, &WarningCount](
		const TCHAR* Severity, const TCHAR* Code, const FString& Message,
		const FString& Asset, int32 MappingIndex)
	{
		TSharedPtr<FJsonObject> Problem = MakeShared<FJsonObject>();
		Problem->SetStringField(TEXT("severity"), Severity);
		Problem->SetStringField(TEXT("code"), Code);
		Problem->SetStringField(TEXT("message"), Message);
		Problem->SetStringField(TEXT("asset"), Asset);
		if (MappingIndex >= 0) Problem->SetNumberField(TEXT("mappingIndex"), MappingIndex);
		Problems.Add(MakeShared<FJsonValueObject>(Problem));
		if (FCString::Strcmp(Severity, TEXT("error")) == 0) ++ErrorCount; else ++WarningCount;
	};

	// SwizzleAxis is the stock modifier that moves the single component a 1D
	// key produces into Y or Z. A project modifier can do the same, and there
	// is no way to know what it does, so anything outside the EnhancedInput
	// module counts as a possible widener rather than being reported wrongly.
	UClass* SwizzleClass = MCPResolveClassOfType(
		TEXT("/Script/EnhancedInput.InputModifierSwizzleAxis"), UInputModifier::StaticClass());
	const UPackage* StockModule = UInputModifier::StaticClass()->GetOutermost();
	auto CountWideners = [SwizzleClass, StockModule](const TArray<TObjectPtr<UInputModifier>>& Modifiers) -> int32
	{
		int32 Count = 0;
		for (const TObjectPtr<UInputModifier>& Modifier : Modifiers)
		{
			if (!Modifier) continue;
			if (SwizzleClass && Modifier->IsA(SwizzleClass)) { ++Count; continue; }
			if (Modifier->GetClass()->GetOutermost() != StockModule) ++Count;
		}
		return Count;
	};

	TSet<const UInputAction*> MappedActions;
	int32 MappingsChecked = 0;

	for (UInputMappingContext* IMC : Contexts)
	{
		const FString ImcPath = IMC->GetPathName();
		const TArray<FEnhancedActionKeyMapping>& Mappings = IMC->GetMappings();
		if (Mappings.Num() == 0)
		{
			AddProblem(TEXT("warning"), TEXT("empty_context"),
				TEXT("This mapping context has no key mappings, so applying it binds nothing"), ImcPath, -1);
		}

		TSet<FString> SeenPairs;
		for (int32 Index = 0; Index < Mappings.Num(); ++Index)
		{
			const FEnhancedActionKeyMapping& Mapping = Mappings[Index];
			++MappingsChecked;

			if (!Mapping.Action)
			{
				AddProblem(TEXT("error"), TEXT("null_action"),
					FString::Printf(TEXT("Mapping %d has no InputAction; it binds the key to nothing and fails asset validation on save"), Index),
					ImcPath, Index);
				continue;
			}
			MappedActions.Add(Mapping.Action);

			if (!Mapping.Key.IsValid())
			{
				AddProblem(TEXT("error"), TEXT("invalid_key"),
					FString::Printf(TEXT("Mapping %d for %s has key '%s', which is not a registered FKey, so nothing will ever drive it. Correct it with set_imc_mapping_key"),
						Index, *Mapping.Action->GetName(), *Mapping.Key.GetFName().ToString()),
					ImcPath, Index);
			}

			const FString Pair = Mapping.Action->GetPathName() + TEXT("|") + Mapping.Key.GetFName().ToString();
			if (SeenPairs.Contains(Pair))
			{
				AddProblem(TEXT("warning"), TEXT("duplicate_mapping"),
					FString::Printf(TEXT("Mapping %d repeats %s on %s within this context; the duplicate accumulates into the same action value"),
						Index, *Mapping.Action->GetName(), *Mapping.Key.GetFName().ToString()),
					ImcPath, Index);
			}
			SeenPairs.Add(Pair);

			for (int32 T = 0; T < Mapping.Triggers.Num(); ++T)
			{
				if (!Mapping.Triggers[T])
				{
					AddProblem(TEXT("error"), TEXT("null_trigger"),
						FString::Printf(TEXT("Mapping %d has a null entry at Triggers[%d]; asset validation rejects it on save. Rewrite the list with set_mapping_modifiers"), Index, T),
						ImcPath, Index);
				}
			}
			for (int32 M = 0; M < Mapping.Modifiers.Num(); ++M)
			{
				if (!Mapping.Modifiers[M])
				{
					AddProblem(TEXT("error"), TEXT("null_modifier"),
						FString::Printf(TEXT("Mapping %d has a null entry at Modifiers[%d]; asset validation rejects it on save. Rewrite the list with set_mapping_modifiers"), Index, M),
						ImcPath, Index);
				}
			}

			// The quiet one. A 2D or 3D action driven by a key that produces a
			// single component reads zero on every axis but X forever, with no
			// warning anywhere, unless a modifier moves the value across.
			const EInputActionValueType KeyType = FInputActionValue::GetValueTypeFromKey(Mapping.Key);
			const EInputActionValueType ActionType = Mapping.Action->ValueType;
			if (static_cast<int32>(KeyType) < static_cast<int32>(ActionType))
			{
				const int32 Wideners = CountWideners(Mapping.Modifiers) + CountWideners(Mapping.Action->Modifiers);
				if (Wideners == 0)
				{
					AddProblem(TEXT("warning"), TEXT("axis_width_mismatch"),
						FString::Printf(TEXT("Mapping %d drives %s (%s) with key %s (%s) and no Swizzle Input Axis Values modifier on the mapping or the action, so every component past X stays zero. This is the classic silent WASD failure. Add one with set_mapping_modifiers or set_action_triggers"),
							Index, *Mapping.Action->GetName(), *ValueTypeName(ActionType),
							*Mapping.Key.GetFName().ToString(), *ValueTypeName(KeyType)),
						ImcPath, Index);
				}
			}
		}
	}

	// An action nothing maps is only meaningful when the whole project was
	// swept. Reporting it after validating one context would flag every other
	// action in the project.
	TArray<TSharedPtr<FJsonValue>> UnmappedActions;
	if (bSweptWholeProject)
	{
		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> ActionAssets;
		AR.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/EnhancedInput"), TEXT("InputAction")), ActionAssets, true);
		for (const FAssetData& Asset : ActionAssets)
		{
			const FString PackagePath = Asset.PackagePath.ToString();
			const bool bInScope = bRecursive
				? (PackagePath == Directory || PackagePath.StartsWith(Directory + TEXT("/")))
				: (PackagePath == Directory);
			if (!bInScope) continue;
			// Each of these LOADS the asset, so honour the same cap the
			// context sweep uses rather than pulling a whole project into
			// memory for a read.
			if (UnmappedActions.Num() >= Limit)
			{
				Skipped.Add(Asset.GetObjectPathString());
				continue;
			}
			const UInputAction* Action = Cast<UInputAction>(Asset.GetAsset());
			if (!Action || MappedActions.Contains(Action)) continue;
			UnmappedActions.Add(MakeShared<FJsonValueString>(Action->GetPathName()));
			AddProblem(TEXT("warning"), TEXT("unmapped_action"),
				FString::Printf(TEXT("%s is mapped by no InputMappingContext under %s, so nothing can ever trigger it. Bind it with add_imc_mapping"), *Action->GetName(), *Directory),
				Action->GetPathName(), -1);
		}
	}

	TArray<FString> ScannedPaths;
	for (const UInputMappingContext* IMC : Contexts) ScannedPaths.Add(IMC->GetPathName());

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("scanned"), MCPStringListToJson(ScannedPaths));
	Result->SetNumberField(TEXT("contextCount"), Contexts.Num());
	Result->SetNumberField(TEXT("mappingsChecked"), MappingsChecked);
	Result->SetArrayField(TEXT("unmappedActions"), UnmappedActions);
	Result->SetArrayField(TEXT("problems"), Problems);
	Result->SetNumberField(TEXT("problemCount"), Problems.Num());
	Result->SetNumberField(TEXT("errorCount"), ErrorCount);
	Result->SetNumberField(TEXT("warningCount"), WarningCount);
	Result->SetBoolField(TEXT("valid"), ErrorCount == 0);
	if (Skipped.Num() > 0)
	{
		Result->SetArrayField(TEXT("skippedOverLimit"), MCPStringListToJson(Skipped));
		Result->SetBoolField(TEXT("truncated"), true);
	}
	return MCPResult(Result);
}
