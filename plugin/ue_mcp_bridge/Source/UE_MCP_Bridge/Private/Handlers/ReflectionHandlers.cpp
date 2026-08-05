#include "ReflectionHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Engine.h"
#include "Engine/UserDefinedEnum.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Kismet2/EnumEditorUtils.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/Factory.h"
#include "EditorAssetLibrary.h"
#include "GameplayTagsManager.h"
#include "GameplayTagsSettings.h"
#include "GameplayTagContainer.h"
#include "Misc/ConfigCacheIni.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "JsonSerializer.h"
#include "JsonObjectConverter.h"
#include "GameFramework/SaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

void FReflectionHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("reflect_class"), &ReflectClass);
	Registry.RegisterHandler(TEXT("reflect_struct"), &ReflectStruct);
	Registry.RegisterHandler(TEXT("reflect_enum"), &ReflectEnum);
	Registry.RegisterHandler(TEXT("list_classes"), &ListClasses);
	Registry.RegisterHandler(TEXT("list_gameplay_tags"), &ListGameplayTags);
	Registry.RegisterHandler(TEXT("create_gameplay_tag"), &CreateGameplayTag);
	Registry.RegisterHandler(TEXT("create_enum"), &CreateEnum);
	Registry.RegisterHandler(TEXT("set_enum_entries"), &SetEnumEntries);
	// #689: load-state probes.
	Registry.RegisterHandler(TEXT("is_class_loaded"), &IsClassLoaded);
	Registry.RegisterHandler(TEXT("is_module_loaded"), &IsModuleLoaded);
	Registry.RegisterHandler(TEXT("list_loaded_modules"), &ListLoadedModules);
	Registry.RegisterHandler(TEXT("inspect_save_game"), &InspectSaveGame);
}

// #689: report whether a UClass is currently loaded, and (separately) whether
// it merely exists (is loadable). Also reports the owning module and its load
// state so a caller can tell "not loaded yet" from "doesn't exist".
TSharedPtr<FJsonValue> FReflectionHandlers::IsClassLoaded(const TSharedPtr<FJsonObject>& Params)
{
	FString ClassName;
	if (auto Err = RequireStringAlt(Params, TEXT("className"), TEXT("class"), ClassName)) return Err;

	// FindClass does NOT load - a hit means the class is already in memory.
	UClass* Found = FindClass(ClassName);
	bool bLoaded = Found != nullptr;
	bool bExists = bLoaded;

	// If not loaded, see whether it could be loaded (exists on disk / in a
	// not-yet-loaded module) without leaving it loaded is not possible for
	// natives; for a path-form we can attempt a load to answer "exists".
	UClass* Resolved = Found;
	if (!bLoaded && ClassName.Contains(TEXT(".")))
	{
		Resolved = LoadObject<UClass>(nullptr, *ClassName);
		if (!Resolved) Resolved = LoadClass<UObject>(nullptr, *ClassName);
		if (Resolved) { bExists = true; /* loaded as a side effect */ }
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("className"), ClassName);
	Result->SetBoolField(TEXT("loaded"), bLoaded);
	Result->SetBoolField(TEXT("exists"), bExists);
	if (Resolved)
	{
		Result->SetStringField(TEXT("resolvedPath"), Resolved->GetPathName());
		if (UPackage* Pkg = Resolved->GetOutermost())
		{
			FString PkgName = Pkg->GetName(); // e.g. /Script/Engine
			Result->SetStringField(TEXT("package"), PkgName);
			if (PkgName.StartsWith(TEXT("/Script/")))
			{
				const FString ModuleName = PkgName.RightChop(8);
				Result->SetStringField(TEXT("module"), ModuleName);
				Result->SetBoolField(TEXT("moduleLoaded"), FModuleManager::Get().IsModuleLoaded(FName(*ModuleName)));
			}
		}
	}
	return MCPResult(Result);
}

// #689: report whether a named module is currently loaded.
TSharedPtr<FJsonValue> FReflectionHandlers::IsModuleLoaded(const TSharedPtr<FJsonObject>& Params)
{
	FString ModuleName;
	if (auto Err = RequireStringAlt(Params, TEXT("moduleName"), TEXT("module"), ModuleName)) return Err;
	const bool bLoaded = FModuleManager::Get().IsModuleLoaded(FName(*ModuleName));

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("moduleName"), ModuleName);
	Result->SetBoolField(TEXT("loaded"), bLoaded);
	return MCPResult(Result);
}

// #689: enumerate modules with their runtime load state. Optional 'filter'
// substring (case-insensitive) narrows the list.
TSharedPtr<FJsonValue> FReflectionHandlers::ListLoadedModules(const TSharedPtr<FJsonObject>& Params)
{
	const FString Filter = OptionalString(Params, TEXT("filter"));
	const bool bLoadedOnly = OptionalBool(Params, TEXT("loadedOnly"), false);

	TArray<FModuleStatus> Statuses;
	FModuleManager::Get().QueryModules(Statuses);

	TArray<TSharedPtr<FJsonValue>> Out;
	int32 LoadedCount = 0;
	for (const FModuleStatus& S : Statuses)
	{
		if (S.bIsLoaded) ++LoadedCount;
		if (bLoadedOnly && !S.bIsLoaded) continue;
		if (!Filter.IsEmpty() && !S.Name.Contains(Filter, ESearchCase::IgnoreCase)) continue;
		TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("name"), S.Name);
		E->SetBoolField(TEXT("loaded"), S.bIsLoaded);
		E->SetBoolField(TEXT("gameModule"), S.bIsGameModule);
		Out.Add(MakeShared<FJsonValueObject>(E));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("modules"), Out);
	Result->SetNumberField(TEXT("count"), Out.Num());
	Result->SetNumberField(TEXT("totalLoaded"), LoadedCount);
	Result->SetNumberField(TEXT("totalModules"), Statuses.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FReflectionHandlers::InspectSaveGame(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FString SlotName;
	if (auto Err = RequireString(Params, TEXT("slotName"), SlotName)) return Err;
	int32 UserIndex = 0;
	if (Params->HasField(TEXT("userIndex")))
	{
		double RawUserIndex = 0.0;
		if (!Params->TryGetNumberField(TEXT("userIndex"), RawUserIndex) ||
			!FMath::IsFinite(RawUserIndex) || RawUserIndex < 0.0 ||
			RawUserIndex > static_cast<double>(MAX_int32) || FMath::TruncToDouble(RawUserIndex) != RawUserIndex)
		{
			return MCPError(TEXT("userIndex must be a non-negative integer"));
		}
		UserIndex = static_cast<int32>(RawUserIndex);
	}

	if (SlotName.Len() > 128 || SlotName != SlotName.TrimStartAndEnd() ||
		SlotName == TEXT(".") || SlotName == TEXT("..") || SlotName.Contains(TEXT("..")) ||
		SlotName != FPaths::MakeValidFileName(SlotName))
	{
		return MCPError(TEXT("Invalid slotName: use a plain filename without path separators, traversal, or surrounding whitespace"));
	}
	if (!UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex))
	{
		return MCPError(FString::Printf(TEXT("Save game slot does not exist: %s (userIndex %d)"), *SlotName, UserIndex));
	}

	USaveGame* SaveGame = UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex);
	if (!SaveGame)
	{
		return MCPError(FString::Printf(TEXT("Failed to load save game slot: %s (userIndex %d)"), *SlotName, UserIndex));
	}

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Skipped;
	int32 PropertyCount = 0;
	for (TFieldIterator<FProperty> It(SaveGame->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FProperty* Property = *It;
		if (!Property || !Property->HasAnyPropertyFlags(CPF_SaveGame) ||
			Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))
		{
			continue;
		}

		const void* Value = Property->ContainerPtrToValuePtr<void>(SaveGame);
		TSharedPtr<FJsonValue> JsonValue = FJsonObjectConverter::UPropertyToJsonValue(
			Property, Value, 0, CPF_Transient | CPF_DuplicateTransient);
		// A property type the JSON converter cannot express (delegates, some
		// container key types) must not abort the whole inspection. Report it
		// and keep going so the caller still sees every readable value.
		if (!JsonValue.IsValid())
		{
			TSharedPtr<FJsonObject> SkipEntry = MakeShared<FJsonObject>();
			SkipEntry->SetStringField(TEXT("name"), Property->GetName());
			SkipEntry->SetStringField(TEXT("type"), Property->GetClass()->GetName());
			SkipEntry->SetStringField(TEXT("reason"), TEXT("Property type is not JSON-serializable"));
			Skipped.Add(MakeShared<FJsonValueObject>(SkipEntry));
			continue;
		}

		Properties->SetField(Property->GetName(), JsonValue);
		++PropertyCount;
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("slotName"), SlotName);
	Result->SetNumberField(TEXT("userIndex"), UserIndex);
	Result->SetStringField(TEXT("className"), SaveGame->GetClass()->GetName());
	Result->SetStringField(TEXT("classPath"), SaveGame->GetClass()->GetPathName());
	Result->SetNumberField(TEXT("propertyCount"), PropertyCount);
	Result->SetNumberField(TEXT("skippedCount"), Skipped.Num());
	Result->SetArrayField(TEXT("skippedProperties"), Skipped);
	Result->SetObjectField(TEXT("properties"), Properties);
	return MCPResult(Result);
}

namespace
{
	/**
	 * UHT compiles `///` doc comments into ToolTip metadata at build time.
	 * Returns the ToolTip if present, otherwise an empty string. Caller is
	 * responsible for omitting empty values from the JSON payload.
	 */
	FString ReadTooltip(const UField* Field)
	{
#if WITH_EDITOR
		if (!Field) return FString();
		// UField::GetMetaData("ToolTip") returns the raw text. Trimming
		// matches what the editor's tooltip panel does.
		const FString Raw = Field->GetMetaData(TEXT("ToolTip"));
		return Raw.TrimStartAndEnd();
#else
		return FString();
#endif
	}

	FString ReadPropertyTooltip(const FProperty* Prop)
	{
#if WITH_EDITOR
		if (!Prop) return FString();
		return Prop->GetMetaData(TEXT("ToolTip")).TrimStartAndEnd();
#else
		return FString();
#endif
	}

	void AddIfNonEmpty(TSharedPtr<FJsonObject> Obj, const TCHAR* Key, const FString& Value)
	{
		if (!Value.IsEmpty())
		{
			Obj->SetStringField(Key, Value);
		}
	}

	/**
	 * Encode the common EPropertyFlags bits the editor surfaces in its
	 * property panel + Blueprint nodes. Skips internal bookkeeping flags
	 * (CPF_NativeAccessSpecifier*, CPF_PersistentInstance, etc.) that aren't
	 * useful to AI callers reasoning about how to use a property.
	 */
	TArray<TSharedPtr<FJsonValue>> EncodePropertyFlags(const FProperty* Prop)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		if (!Prop) return Out;
		const EPropertyFlags F = Prop->PropertyFlags;
		auto Push = [&Out](const TCHAR* Name) { Out.Add(MakeShared<FJsonValueString>(Name)); };

		if (F & CPF_Edit)
		{
			if (F & CPF_DisableEditOnInstance) Push(TEXT("EditDefaultsOnly"));
			else if (F & CPF_DisableEditOnTemplate) Push(TEXT("EditInstanceOnly"));
			else Push(TEXT("EditAnywhere"));
		}
		if (F & CPF_EditConst)         Push(TEXT("VisibleAnywhere"));
		if (F & CPF_BlueprintVisible)
		{
			if (F & CPF_BlueprintReadOnly) Push(TEXT("BlueprintReadOnly"));
			else Push(TEXT("BlueprintReadWrite"));
		}
		if (F & CPF_Net)               Push(TEXT("Replicated"));
		if (F & CPF_RepNotify)         Push(TEXT("RepNotify"));
		if (F & CPF_Transient)         Push(TEXT("Transient"));
		if (F & CPF_Config)            Push(TEXT("Config"));
		if (F & CPF_GlobalConfig)      Push(TEXT("GlobalConfig"));
		if (F & CPF_SaveGame)          Push(TEXT("SaveGame"));
		if (F & CPF_Interp)            Push(TEXT("Interp"));
		if (F & CPF_AdvancedDisplay)   Push(TEXT("AdvancedDisplay"));
		if (F & CPF_Deprecated)        Push(TEXT("Deprecated"));
		if (F & CPF_NoClear)           Push(TEXT("NoClear"));
		if (F & CPF_ExposeOnSpawn)     Push(TEXT("ExposeOnSpawn"));
		return Out;
	}

	/**
	 * Function flags the editor cares about: how Blueprints can call it, how
	 * the network treats it, and whether it's an Exec console command.
	 */
	TArray<TSharedPtr<FJsonValue>> EncodeFunctionFlags(const UFunction* Func)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		if (!Func) return Out;
		const EFunctionFlags F = Func->FunctionFlags;
		auto Push = [&Out](const TCHAR* Name) { Out.Add(MakeShared<FJsonValueString>(Name)); };

		if (F & FUNC_BlueprintCallable) Push(TEXT("BlueprintCallable"));
		if (F & FUNC_BlueprintEvent)    Push(TEXT("BlueprintImplementableEvent"));
		if (F & FUNC_BlueprintPure)     Push(TEXT("BlueprintPure"));
		if (F & FUNC_Exec)              Push(TEXT("Exec"));
		if (F & FUNC_NetServer)         Push(TEXT("Server"));
		if (F & FUNC_NetClient)         Push(TEXT("Client"));
		if (F & FUNC_NetMulticast)      Push(TEXT("NetMulticast"));
		if (F & FUNC_NetReliable)       Push(TEXT("Reliable"));
		if (F & FUNC_Static)            Push(TEXT("Static"));
		return Out;
	}

	/**
	 * Build the per-property JSON block surfaced under a class/struct's
	 * `properties:` / `fields:` array. Includes name + type (the old
	 * contract) plus all metadata the editor exposes: tooltip, category,
	 * display name, edit-condition predicate, clamp range, and the flag
	 * names a UHEADER author would have typed.
	 */
	TSharedPtr<FJsonObject> SerializePropertyMeta(FProperty* Prop)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Prop->GetName());
		Obj->SetStringField(TEXT("type"), Prop->GetCPPType());
#if WITH_EDITOR
		AddIfNonEmpty(Obj, TEXT("tooltip"),       ReadPropertyTooltip(Prop));
		AddIfNonEmpty(Obj, TEXT("category"),      Prop->GetMetaData(TEXT("Category")));
		AddIfNonEmpty(Obj, TEXT("displayName"),   Prop->GetMetaData(TEXT("DisplayName")));
		AddIfNonEmpty(Obj, TEXT("editCondition"), Prop->GetMetaData(TEXT("EditCondition")));
		AddIfNonEmpty(Obj, TEXT("clampMin"),      Prop->GetMetaData(TEXT("ClampMin")));
		AddIfNonEmpty(Obj, TEXT("clampMax"),      Prop->GetMetaData(TEXT("ClampMax")));
		AddIfNonEmpty(Obj, TEXT("uiMin"),         Prop->GetMetaData(TEXT("UIMin")));
		AddIfNonEmpty(Obj, TEXT("uiMax"),         Prop->GetMetaData(TEXT("UIMax")));
		AddIfNonEmpty(Obj, TEXT("units"),         Prop->GetMetaData(TEXT("Units")));
#endif
		TArray<TSharedPtr<FJsonValue>> Flags = EncodePropertyFlags(Prop);
		if (Flags.Num() > 0)
		{
			Obj->SetArrayField(TEXT("flags"), Flags);
		}
		return Obj;
	}

	/**
	 * Build the per-function JSON block surfaced under a class's
	 * `functions:` array. Returns a structured params array (each with
	 * name + type + optional out/ref markers) and a separate returnType
	 * string when the function has a CPF_ReturnParm property.
	 */
	TSharedPtr<FJsonObject> SerializeFunctionMeta(UFunction* Func)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Func->GetName());
#if WITH_EDITOR
		AddIfNonEmpty(Obj, TEXT("tooltip"),     ReadTooltip(Func));
		AddIfNonEmpty(Obj, TEXT("category"),    Func->GetMetaData(TEXT("Category")));
		AddIfNonEmpty(Obj, TEXT("displayName"), Func->GetMetaData(TEXT("DisplayName")));
		AddIfNonEmpty(Obj, TEXT("keywords"),    Func->GetMetaData(TEXT("Keywords")));
#endif

		TArray<TSharedPtr<FJsonValue>> Params;
		FString ReturnType;
		for (TFieldIterator<FProperty> PIt(Func); PIt; ++PIt)
		{
			FProperty* P = *PIt;
			if (P->PropertyFlags & CPF_ReturnParm)
			{
				ReturnType = P->GetCPPType();
				continue;
			}
			TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
			ParamObj->SetStringField(TEXT("name"), P->GetName());
			ParamObj->SetStringField(TEXT("type"), P->GetCPPType());
			if (P->PropertyFlags & CPF_OutParm)       ParamObj->SetBoolField(TEXT("out"), true);
			if (P->PropertyFlags & CPF_ReferenceParm) ParamObj->SetBoolField(TEXT("byRef"), true);
			Params.Add(MakeShared<FJsonValueObject>(ParamObj));
		}
		Obj->SetArrayField(TEXT("params"), Params);
		if (!ReturnType.IsEmpty())
		{
			Obj->SetStringField(TEXT("returnType"), ReturnType);
		}
		else
		{
			Obj->SetStringField(TEXT("returnType"), TEXT("void"));
		}

		TArray<TSharedPtr<FJsonValue>> Flags = EncodeFunctionFlags(Func);
		if (Flags.Num() > 0)
		{
			Obj->SetArrayField(TEXT("flags"), Flags);
		}
		return Obj;
	}
}

TSharedPtr<FJsonValue> FReflectionHandlers::ReflectClass(const TSharedPtr<FJsonObject>& Params)
{
	FString ClassName;
	if (auto Err = RequireString(Params, TEXT("className"), ClassName)) return Err;

	UClass* Class = FindClass(ClassName);
	// #823: reflection is a read, so it is worth loading a class the caller
	// named by path but that is not in memory yet.
	if (!Class) Class = MCPResolveClass(ClassName, /*bAllowLoad*/ true);
	if (!Class)
	{
		return MCPClassNotFoundError(ClassName);
	}

	bool bIncludeInherited = OptionalBool(Params, TEXT("includeInherited"), false);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("className"), Class->GetName());
	AddIfNonEmpty(Result, TEXT("tooltip"), ReadTooltip(Class));
#if WITH_EDITOR
	AddIfNonEmpty(Result, TEXT("displayName"), Class->GetMetaData(TEXT("DisplayName")));
	AddIfNonEmpty(Result, TEXT("category"),    Class->GetMetaData(TEXT("Category")));
#endif

	if (Class->GetSuperClass())
	{
		Result->SetStringField(TEXT("parentClass"), Class->GetSuperClass()->GetName());
	}

	// Build parent chain
	TArray<TSharedPtr<FJsonValue>> ParentChain;
	UClass* Parent = Class->GetSuperClass();
	while (Parent)
	{
		ParentChain.Add(MakeShared<FJsonValueString>(Parent->GetName()));
		Parent = Parent->GetSuperClass();
	}
	Result->SetArrayField(TEXT("parentChain"), ParentChain);

	Result->SetBoolField(TEXT("isAbstract"), Class->HasAnyClassFlags(CLASS_Abstract));

	// Properties: name + type plus every metadata field the editor's property
	// panel renders - tooltip text, category, clamps, replication flags, etc.
	TArray<TSharedPtr<FJsonValue>> PropertiesArray;
	for (TFieldIterator<FProperty> PropIt(Class, bIncludeInherited ? EFieldIteratorFlags::IncludeSuper : EFieldIteratorFlags::ExcludeSuper); PropIt; ++PropIt)
	{
		PropertiesArray.Add(MakeShared<FJsonValueObject>(SerializePropertyMeta(*PropIt)));
	}
	Result->SetArrayField(TEXT("properties"), PropertiesArray);
	Result->SetNumberField(TEXT("propertyCount"), PropertiesArray.Num());

	// Functions: structured params + return type + tooltip + flags so callers
	// can see how to invoke each function without grepping the header.
	TArray<TSharedPtr<FJsonValue>> FunctionsArray;
	for (TFieldIterator<UFunction> FuncIt(Class, bIncludeInherited ? EFieldIteratorFlags::IncludeSuper : EFieldIteratorFlags::ExcludeSuper); FuncIt; ++FuncIt)
	{
		FunctionsArray.Add(MakeShared<FJsonValueObject>(SerializeFunctionMeta(*FuncIt)));
	}
	Result->SetArrayField(TEXT("functions"), FunctionsArray);
	Result->SetNumberField(TEXT("functionCount"), FunctionsArray.Num());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FReflectionHandlers::ReflectStruct(const TSharedPtr<FJsonObject>& Params)
{
	FString StructName;
	if (auto Err = RequireString(Params, TEXT("structName"), StructName)) return Err;

	UScriptStruct* Struct = FindStruct(StructName);
	if (!Struct)
	{
		return MCPError(FString::Printf(TEXT("Struct not found: %s"), *StructName));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("structName"), Struct->GetName());
	AddIfNonEmpty(Result, TEXT("tooltip"), ReadTooltip(Struct));
#if WITH_EDITOR
	AddIfNonEmpty(Result, TEXT("displayName"), Struct->GetMetaData(TEXT("DisplayName")));
#endif

	TArray<TSharedPtr<FJsonValue>> FieldsArray;
	for (TFieldIterator<FProperty> PropIt(Struct); PropIt; ++PropIt)
	{
		FieldsArray.Add(MakeShared<FJsonValueObject>(SerializePropertyMeta(*PropIt)));
	}
	Result->SetArrayField(TEXT("fields"), FieldsArray);
	Result->SetNumberField(TEXT("fieldCount"), FieldsArray.Num());

	return MCPResult(Result);
}

// Defined below, next to FindEnum.
static TArray<FString> SuggestEnumNames(const FString& EnumName, int32 MaxSuggestions = 8);

TSharedPtr<FJsonValue> FReflectionHandlers::ReflectEnum(const TSharedPtr<FJsonObject>& Params)
{
	FString EnumName;
	if (auto Err = RequireString(Params, TEXT("enumName"), EnumName)) return Err;

	UEnum* Enum = FindEnum(EnumName);
	if (!Enum)
	{
		const TArray<FString> Suggestions = SuggestEnumNames(EnumName);
		return MCPError(Suggestions.Num() > 0
			? FString::Printf(TEXT("Enum not found: %s. Close matches: %s"),
				*EnumName, *FString::Join(Suggestions, TEXT(", ")))
			: FString::Printf(TEXT("Enum not found: %s"), *EnumName));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("enumName"), Enum->GetName());
	Result->SetStringField(TEXT("enumPath"), Enum->GetPathName());
	Result->SetBoolField(TEXT("userDefined"), Enum->IsA<UUserDefinedEnum>());
	AddIfNonEmpty(Result, TEXT("tooltip"), ReadTooltip(Enum));

	TArray<TSharedPtr<FJsonValue>> ValuesArray;
	int32 NumEnums = Enum->NumEnums();
	for (int32 i = 0; i < NumEnums - 1; ++i) // -1 to exclude _MAX
	{
		FString EnumNameStr = Enum->GetNameStringByIndex(i);
		if (!EnumNameStr.IsEmpty() && !EnumNameStr.EndsWith(TEXT("_MAX")))
		{
			TSharedPtr<FJsonObject> ValueObj = MakeShared<FJsonObject>();
			ValueObj->SetStringField(TEXT("name"), EnumNameStr);
			ValueObj->SetNumberField(TEXT("value"), Enum->GetValueByIndex(i));
			ValueObj->SetStringField(TEXT("displayName"), Enum->GetDisplayNameTextByIndex(i).ToString());
#if WITH_EDITOR
			// Per-value tooltip - UEnum stores ToolTip metadata per enum
			// entry, addressable by index. Editor uses the same call to
			// populate the dropdown tooltips in Details panels.
			FString ValueTooltip = Enum->GetMetaData(TEXT("ToolTip"), i).TrimStartAndEnd();
			AddIfNonEmpty(ValueObj, TEXT("tooltip"), ValueTooltip);
#endif
			ValuesArray.Add(MakeShared<FJsonValueObject>(ValueObj));
		}
	}
	Result->SetArrayField(TEXT("values"), ValuesArray);
	Result->SetNumberField(TEXT("valueCount"), ValuesArray.Num());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FReflectionHandlers::ListClasses(const TSharedPtr<FJsonObject>& Params)
{
	FString ParentFilter = OptionalString(Params, TEXT("parentFilter"));

	int32 Limit = 100;
	Params->TryGetNumberField(TEXT("limit"), Limit);

	auto Result = MCPSuccess();

	if (!ParentFilter.IsEmpty())
	{
		UClass* ParentClass = FindClass(ParentFilter);
		if (!ParentClass)
		{
			return MCPClassNotFoundError(ParentFilter, TEXT("parentFilter"));
		}

		TArray<TSharedPtr<FJsonValue>> ClassesArray;
		for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
		{
			UClass* Class = *ClassIt;
			if (Class && Class->IsChildOf(ParentClass))
			{
				TSharedPtr<FJsonObject> ClassObj = MakeShared<FJsonObject>();
				ClassObj->SetStringField(TEXT("name"), Class->GetName());
				if (Class->GetSuperClass())
				{
					ClassObj->SetStringField(TEXT("parent"), Class->GetSuperClass()->GetName());
				}
				ClassesArray.Add(MakeShared<FJsonValueObject>(ClassObj));
				if (ClassesArray.Num() >= Limit)
				{
					break;
				}
			}
		}
		Result->SetStringField(TEXT("parentFilter"), ParentFilter);
		Result->SetArrayField(TEXT("classes"), ClassesArray);
		Result->SetNumberField(TEXT("count"), ClassesArray.Num());
	}
	else
	{
		// List common base classes
		TArray<FString> CommonClasses = {
			TEXT("Actor"), TEXT("Pawn"), TEXT("Character"), TEXT("PlayerController"), TEXT("GameModeBase"),
			TEXT("GameStateBase"), TEXT("PlayerState"), TEXT("HUD"), TEXT("ActorComponent"),
			TEXT("SceneComponent"), TEXT("PrimitiveComponent"), TEXT("StaticMeshComponent"),
			TEXT("SkeletalMeshComponent"), TEXT("CameraComponent"), TEXT("AudioComponent"),
			TEXT("LightComponent"), TEXT("UserWidget"), TEXT("AnimInstance"),
			TEXT("GameInstance"), TEXT("SaveGame"), TEXT("DataAsset"), TEXT("PrimaryDataAsset"),
			TEXT("BlueprintFunctionLibrary"), TEXT("DeveloperSettings"),
			TEXT("CheatManager"), TEXT("WorldSubsystem"), TEXT("GameInstanceSubsystem"),
			TEXT("LocalPlayerSubsystem"),
		};

		TArray<TSharedPtr<FJsonValue>> ClassesArray;
		for (const FString& ClassName : CommonClasses)
		{
			UClass* Class = FindClass(ClassName);
			if (Class)
			{
				TSharedPtr<FJsonObject> ClassObj = MakeShared<FJsonObject>();
				ClassObj->SetStringField(TEXT("name"), Class->GetName());
				if (Class->GetSuperClass())
				{
					ClassObj->SetStringField(TEXT("parent"), Class->GetSuperClass()->GetName());
				}
				ClassesArray.Add(MakeShared<FJsonValueObject>(ClassObj));
			}
		}
		Result->SetStringField(TEXT("note"), TEXT("Showing common base classes. Use parentFilter to find derived classes."));
		Result->SetArrayField(TEXT("classes"), ClassesArray);
		Result->SetNumberField(TEXT("count"), ClassesArray.Num());
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FReflectionHandlers::ListGameplayTags(const TSharedPtr<FJsonObject>& Params)
{
	FString FilterPrefix = OptionalString(Params, TEXT("filter"));

	auto Result = MCPSuccess();

	UGameplayTagsManager& TagsManager = UGameplayTagsManager::Get();
	FGameplayTagContainer AllTags;
	TagsManager.RequestAllGameplayTags(AllTags, false);

	TArray<TSharedPtr<FJsonValue>> TagsArray;
	for (const FGameplayTag& Tag : AllTags)
	{
		FString TagString = Tag.ToString();
		if (FilterPrefix.IsEmpty() || TagString.StartsWith(FilterPrefix))
		{
			TagsArray.Add(MakeShared<FJsonValueString>(TagString));
		}
	}

	TagsArray.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B) {
		return A->AsString() < B->AsString();
	});

	Result->SetStringField(TEXT("filter"), FilterPrefix.IsEmpty() ? TEXT("(all)") : FilterPrefix);
	Result->SetArrayField(TEXT("tags"), TagsArray);
	Result->SetNumberField(TEXT("count"), TagsArray.Num());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FReflectionHandlers::CreateGameplayTag(const TSharedPtr<FJsonObject>& Params)
{
	FString Tag;
	if (auto Err = RequireString(Params, TEXT("tag"), Tag)) return Err;

	FString Comment = OptionalString(Params, TEXT("comment"));

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("tag"), Tag);

	// Add tag via DefaultGameplayTags.ini (not AddNativeGameplayTag which asserts after init)
	FString ProjectDir = FPaths::ProjectDir();
	FString TagFile = FPaths::Combine(ProjectDir, TEXT("Config"), TEXT("DefaultGameplayTags.ini"));

	FString Section = TEXT("[/Script/GameplayTags.GameplayTagsSettings]");
	FString Entry = FString::Printf(TEXT("+GameplayTagList=(Tag=\"%s\",DevComment=\"%s\")"), *Tag, *Comment);

	FString FileContent;
	if (FFileHelper::LoadFileToString(FileContent, *TagFile))
	{
		if (!FileContent.Contains(Section))
		{
			FileContent += TEXT("\n\n") + Section + TEXT("\n") + Entry + TEXT("\n");
		}
		else if (!FileContent.Contains(Entry))
		{
			FileContent = FileContent.Replace(*Section, *(Section + TEXT("\n") + Entry));
		}
	}
	else
	{
		FileContent = Section + TEXT("\n") + Entry + TEXT("\n");
	}

	if (FFileHelper::SaveStringToFile(FileContent, *TagFile))
	{
		Result->SetStringField(TEXT("method"), TEXT("ini_append"));
		Result->SetStringField(TEXT("note"), TEXT("Restart editor to pick up new tag"));
		return MCPResult(Result);
	}

	return MCPError(TEXT("Could not add gameplay tag via available APIs"));
}

UClass* FReflectionHandlers::FindClass(const FString& ClassName)
{
	// #823: one shared resolution order for every string-to-UClass lookup in
	// the plugin. Non-loading on purpose: is_class_loaded reads a hit here as
	// "already in memory" and does its own load probe afterwards.
	return MCPResolveClass(ClassName, /*bAllowLoad*/ false);
}

UScriptStruct* FReflectionHandlers::FindStruct(const FString& StructName)
{
	// Try direct lookup (handles full paths like /Script/ModuleName.StructName)
	UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, *StructName);
	if (Struct)
	{
		return Struct;
	}

	// Short-name lookup via FindFirstObject (UE 5.6+ replacement for the
	// "any package" FindObject pattern). Tries the caller's spelling first,
	// then F-prefixed - matches the convention agents typically use ("Vector"
	// vs "FVector").
	const TArray<FString> Candidates = { StructName, TEXT("F") + StructName };
	for (const FString& Candidate : Candidates)
	{
		Struct = FindFirstObject<UScriptStruct>(*Candidate, EFindFirstObjectOptions::NativeFirst);
		if (Struct)
		{
			return Struct;
		}
	}

	return nullptr;
}

UEnum* FReflectionHandlers::FindEnum(const FString& EnumName)
{
	// Full-path lookup first: /Script/Engine.ECollisionChannel, etc.
	UEnum* Enum = FindObject<UEnum>(nullptr, *EnumName);
	if (Enum)
	{
		return Enum;
	}

	// #762: project enums were reported "not found" while Python could reach
	// them. Two gaps: NativeFirst biases the search away from content-defined
	// enums, and a UUserDefinedEnum that has not been loaded yet is not in the
	// object graph to find at all. Try the caller's spelling and the
	// E-prefixed UE convention, unbiased, then fall back to the asset registry
	// and load the asset before giving up.
	// NativeFirst FIRST: it is the deterministic tiebreak. Dropping it made a
	// project enum shadow a native one of the same name in object-hash order,
	// so reflect_enum could answer differently between editor sessions. Only
	// widen to an unbiased search once the native lookup has failed.
	const TArray<FString> Candidates = { EnumName, TEXT("E") + EnumName };
	for (const FString& Candidate : Candidates)
	{
		Enum = FindFirstObject<UEnum>(*Candidate, EFindFirstObjectOptions::NativeFirst);
		if (Enum)
		{
			return Enum;
		}
	}
	for (const FString& Candidate : Candidates)
	{
		Enum = FindFirstObject<UEnum>(*Candidate, EFindFirstObjectOptions::None);
		if (Enum)
		{
			return Enum;
		}
	}

	// Unloaded UUserDefinedEnum assets: resolve through the asset registry and
	// load, so a Blueprint enum behaves like a native one.
	if (FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry")))
	{
		IAssetRegistry& AssetRegistry =
			FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FAssetData> EnumAssets;
		AssetRegistry.GetAssetsByClass(UUserDefinedEnum::StaticClass()->GetClassPathName(), EnumAssets, true);
		for (const FAssetData& Asset : EnumAssets)
		{
			const FString AssetName = Asset.AssetName.ToString();
			if (AssetName == EnumName || AssetName == TEXT("E") + EnumName)
			{
				if (UEnum* Loaded = Cast<UEnum>(Asset.GetAsset()))
				{
					return Loaded;
				}
			}
		}
	}

	return nullptr;
}

/** Enum short names close to a failed lookup, so the error can name alternatives. */
static TArray<FString> SuggestEnumNames(const FString& EnumName, int32 MaxSuggestions)
{
	TArray<FString> Suggestions;
	const FString Needle = EnumName.ToLower();
	for (TObjectIterator<UEnum> It; It && Suggestions.Num() < MaxSuggestions; ++It)
	{
		// TObjectIterator only stops on live objects and its dereference is annotated
		// non-null, so a null test here is dead code that Clang rejects under -Werror.
		const FString Name = It->GetName();
		if (Name.ToLower().Contains(Needle))
		{
			Suggestions.AddUnique(Name);
		}
	}
	return Suggestions;
}

TSharedPtr<FJsonValue> FReflectionHandlers::SerializeProperty(FProperty* Prop, void* Data)
{
	// Basic property serialization - can be extended
	if (CastField<FStrProperty>(Prop))
	{
		return MakeShared<FJsonValueString>(CastField<FStrProperty>(Prop)->GetPropertyValue(Data));
	}
	else if (CastField<FIntProperty>(Prop))
	{
		return MakeShared<FJsonValueNumber>(CastField<FIntProperty>(Prop)->GetPropertyValue(Data));
	}
	else if (CastField<FFloatProperty>(Prop))
	{
		return MakeShared<FJsonValueNumber>(CastField<FFloatProperty>(Prop)->GetPropertyValue(Data));
	}
	else if (CastField<FBoolProperty>(Prop))
	{
		return MakeShared<FJsonValueBoolean>(CastField<FBoolProperty>(Prop)->GetPropertyValue(Data));
	}
	return MakeShared<FJsonValueString>(TEXT("(unserializable)"));
}

// ─── #274  create_enum  ────────────────────────────────────────────────
// Creates a UUserDefinedEnum asset and (optionally) populates entries.
TSharedPtr<FJsonValue> FReflectionHandlers::CreateEnum(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;
	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	UClass* FactoryClass = FindObject<UClass>(nullptr, TEXT("/Script/UnrealEd.EnumFactory"));
	if (!FactoryClass)
	{
		// fallback name some UE versions use
		FactoryClass = FindObject<UClass>(nullptr, TEXT("/Script/UnrealEd.UserDefinedEnumFactory"));
	}
	if (!FactoryClass)
	{
		return MCPError(TEXT("EnumFactory not found in /Script/UnrealEd"));
	}
	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);

	auto Created = MCPCreateAssetIdempotent<UUserDefinedEnum>(Name, PackagePath, OnConflict, TEXT("UserDefinedEnum"), Factory);
	if (Created.EarlyReturn) return Created.EarlyReturn;
	UUserDefinedEnum* Enum = Created.Asset;

	// Optional entries[] - array of strings or {name, displayName?}.
	const TArray<TSharedPtr<FJsonValue>>* EntriesArr = nullptr;
	int32 Added = 0;
	if (Params->TryGetArrayField(TEXT("entries"), EntriesArr) && EntriesArr)
	{
		for (const TSharedPtr<FJsonValue>& Entry : *EntriesArr)
		{
			FString EntryName;
			FString DisplayName;
			if (Entry->Type == EJson::String)
			{
				EntryName = Entry->AsString();
				DisplayName = EntryName;
			}
			else if (TSharedPtr<FJsonObject> Obj = Entry->AsObject())
			{
				Obj->TryGetStringField(TEXT("name"), EntryName);
				if (!Obj->TryGetStringField(TEXT("displayName"), DisplayName))
				{
					DisplayName = EntryName;
				}
			}
			if (EntryName.IsEmpty()) continue;
			FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(Enum);
			int32 NewIndex = Enum->NumEnums() - 2; // -1 is the auto MAX entry
			if (NewIndex >= 0)
			{
				FEnumEditorUtils::SetEnumeratorDisplayName(Enum, NewIndex, FText::FromString(DisplayName));
			}
			Added++;
		}
	}

	Enum->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(Enum->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), Enum->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetNumberField(TEXT("entriesAdded"), Added);
	MCPSetDeleteAssetRollback(Result, Enum->GetPathName());
	return MCPResult(Result);
}

// ─── #274  set_enum_entries  ───────────────────────────────────────────
// Replace the entry list on an existing UUserDefinedEnum.
TSharedPtr<FJsonValue> FReflectionHandlers::SetEnumEntries(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireString(Params, TEXT("assetPath"), AssetPath)) return Err;

	UUserDefinedEnum* Enum = Cast<UUserDefinedEnum>(LoadObject<UObject>(nullptr, *AssetPath));
	if (!Enum) return MCPError(FString::Printf(TEXT("UserDefinedEnum not found: %s"), *AssetPath));

	const TArray<TSharedPtr<FJsonValue>>* EntriesArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("entries"), EntriesArr) || !EntriesArr)
	{
		return MCPError(TEXT("Missing 'entries' (array of strings or {name, displayName})"));
	}

	// Clear existing entries (UE editor utils does this safely).
	while (Enum->NumEnums() > 1)
	{
		FEnumEditorUtils::RemoveEnumeratorFromUserDefinedEnum(Enum, 0);
	}

	int32 Added = 0;
	for (const TSharedPtr<FJsonValue>& Entry : *EntriesArr)
	{
		FString EntryName, DisplayName;
		if (Entry->Type == EJson::String)
		{
			EntryName = Entry->AsString();
			DisplayName = EntryName;
		}
		else if (TSharedPtr<FJsonObject> Obj = Entry->AsObject())
		{
			Obj->TryGetStringField(TEXT("name"), EntryName);
			if (!Obj->TryGetStringField(TEXT("displayName"), DisplayName)) DisplayName = EntryName;
		}
		if (EntryName.IsEmpty()) continue;
		FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(Enum);
		int32 NewIndex = Enum->NumEnums() - 2;
		if (NewIndex >= 0)
		{
			FEnumEditorUtils::SetEnumeratorDisplayName(Enum, NewIndex, FText::FromString(DisplayName));
		}
		Added++;
	}

	Enum->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(Enum->GetPathName());

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetNumberField(TEXT("entries"), Added);
	return MCPResult(Result);
}
