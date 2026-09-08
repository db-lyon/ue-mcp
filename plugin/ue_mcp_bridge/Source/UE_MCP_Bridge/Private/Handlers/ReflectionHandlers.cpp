#include "ReflectionHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include "HandlerPagination.h"
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
	// T1: per-instance writable schema (ReflectionHandlers_Schema.cpp).
	Registry.RegisterHandler(TEXT("reflect_instance"), &ReflectInstance);
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

	// T3: paged. A project with its plugins enabled reports several hundred
	// modules, and the whole list is rarely what the caller wanted.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_loaded_modules|filter=%s|loadedOnly=%d"), *Filter, bLoadedOnly ? 1 : 0),
			/*DefaultLimit*/ 500, /*MaxLimit*/ 5000, Page))
	{
		return Err;
	}

	TArray<FModuleStatus> Statuses;
	FModuleManager::Get().QueryModules(Statuses);

	TArray<MCPPagination::FPageRow> Rows;
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
		// The module name is the page anchor: unique, and stable across two
		// enumerations even when modules load in between.
		Rows.Add({ S.Name, MakeShared<FJsonValueObject>(E) });
	}

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("totalLoaded"), LoadedCount);
	Result->SetNumberField(TEXT("totalModules"), Statuses.Num());
	MCPPagination::EmitPage(Page, Rows, TEXT("modules"), Result);
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

	// T3: paged. This used to stop at `limit` matches and say nothing about
	// what it had cut, so a caller filtering on Actor read the first 100
	// classes as if they were all of them.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_classes|parentFilter=%s"), *ParentFilter),
			/*DefaultLimit*/ 100, /*MaxLimit*/ 1000, Page))
	{
		return Err;
	}

	auto Result = MCPSuccess();

	if (!ParentFilter.IsEmpty())
	{
		UClass* ParentClass = FindClass(ParentFilter);
		if (!ParentClass)
		{
			return MCPClassNotFoundError(ParentFilter, TEXT("parentFilter"));
		}

		TArray<MCPPagination::FPageRow> Rows;
		for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
		{
			UClass* Class = *ClassIt;
			if (Class && Class->IsChildOf(ParentClass))
			{
				TSharedPtr<FJsonObject> ClassObj = MakeShared<FJsonObject>();
				ClassObj->SetStringField(TEXT("name"), Class->GetName());
				ClassObj->SetStringField(TEXT("path"), Class->GetPathName());
				if (Class->GetSuperClass())
				{
					ClassObj->SetStringField(TEXT("parent"), Class->GetSuperClass()->GetName());
				}
				// The class PATH is the page anchor, not the short name: two
				// modules may each define a class called Settings, and a page
				// boundary has to name exactly one of them.
				Rows.Add({ Class->GetPathName(), MakeShared<FJsonValueObject>(ClassObj) });
			}
		}
		// TObjectIterator walks the object hash, whose order is not a contract,
		// so the rows are sorted before paging. Without it the same page can
		// come back in a different order between two calls, and the anchor
		// would report a change that is only the enumeration reshuffling.
		Rows.Sort([](const MCPPagination::FPageRow& A, const MCPPagination::FPageRow& B)
			{ return A.Id < B.Id; });
		Result->SetStringField(TEXT("parentFilter"), ParentFilter);
		MCPPagination::EmitPage(Page, Rows, TEXT("classes"), Result);
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

		TArray<MCPPagination::FPageRow> Rows;
		for (const FString& ClassName : CommonClasses)
		{
			UClass* Class = FindClass(ClassName);
			if (Class)
			{
				TSharedPtr<FJsonObject> ClassObj = MakeShared<FJsonObject>();
				ClassObj->SetStringField(TEXT("name"), Class->GetName());
				ClassObj->SetStringField(TEXT("path"), Class->GetPathName());
				if (Class->GetSuperClass())
				{
					ClassObj->SetStringField(TEXT("parent"), Class->GetSuperClass()->GetName());
				}
				Rows.Add({ Class->GetPathName(), MakeShared<FJsonValueObject>(ClassObj) });
			}
		}
		// Authored order, so this list is deliberately NOT sorted: it is a
		// curated starting point rather than an enumeration, and alphabetising
		// it would bury Actor under AnimInstance.
		Result->SetStringField(TEXT("note"), TEXT("Showing common base classes. Use parentFilter to find derived classes."));
		MCPPagination::EmitPage(Page, Rows, TEXT("classes"), Result);
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FReflectionHandlers::ListGameplayTags(const TSharedPtr<FJsonObject>& Params)
{
	FString FilterPrefix = OptionalString(Params, TEXT("filter"));

	// T3: paged. A project that has adopted gameplay tags seriously carries
	// thousands of them, and the unpaged list was the largest read on this
	// category.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_gameplay_tags|filter=%s"), *FilterPrefix),
			/*DefaultLimit*/ 500, /*MaxLimit*/ 5000, Page))
	{
		return Err;
	}

	auto Result = MCPSuccess();

	UGameplayTagsManager& TagsManager = UGameplayTagsManager::Get();
	FGameplayTagContainer AllTags;
	TagsManager.RequestAllGameplayTags(AllTags, false);

	TArray<FString> TagStrings;
	for (const FGameplayTag& Tag : AllTags)
	{
		FString TagString = Tag.ToString();
		if (FilterPrefix.IsEmpty() || TagString.StartsWith(FilterPrefix))
		{
			TagStrings.Add(MoveTemp(TagString));
		}
	}
	// Sorted before paging, as it always was: the tag container enumerates in
	// registration order, which moves when a tag is added, and a page anchor
	// needs a stable sequence to resume into.
	TagStrings.Sort();

	TArray<MCPPagination::FPageRow> Rows;
	Rows.Reserve(TagStrings.Num());
	for (const FString& TagString : TagStrings)
	{
		Rows.Add({ TagString, MakeShared<FJsonValueString>(TagString) });
	}

	Result->SetStringField(TEXT("filter"), FilterPrefix.IsEmpty() ? TEXT("(all)") : FilterPrefix);
	MCPPagination::EmitPage(Page, Rows, TEXT("tags"), Result);

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

	const FString SectionName = TEXT("/Script/GameplayTags.GameplayTagsSettings");
	const FString Section = FString::Printf(TEXT("[%s]"), *SectionName);
	FString Entry = FString::Printf(TEXT("+GameplayTagList=(Tag=\"%s\",DevComment=\"%s\")"), *Tag, *Comment);

	// The identity of a tag entry is the TAG, not the whole line. Keying the
	// duplicate check on the full Entry text matched only when the DevComment
	// was byte-identical too, so calling twice with a different comment (or
	// with a comment after one without) appended a SECOND +GameplayTagList line
	// for the same tag and reported it as created.
	//
	// A raw substring scan over the whole file is not enough either, and a
	// hand-edited ini carries both ways of defeating it. A COMMENTED-OUT
	// declaration (`;+GameplayTagList=(Tag="Ability.Fire",...)`) contains the
	// key, so a whole-file scan reports the tag as existing and never writes it,
	// leaving it undeclared as far as the engine is concerned. And an ini is
	// tolerant of spacing, so `Tag = "Ability.Fire"` does not contain
	// `Tag="Ability.Fire"` and the duplicate line comes straight back.
	//
	// So the scan is per line, and each line is first reduced to what the ini
	// parser would actually read: its comment tail is cut off (StripLineComment
	// below), and a line that is nothing but a comment is then empty and is
	// skipped along with the other markers the parser ignores (see the marker
	// note inside the loop). What survives is compared with EVERY whitespace
	// character
	// removed, tabs included. FString::RemoveSpacesInline is not that: it is
	// documented and implemented as removing the SPACE CHARACTER ONLY, so a
	// tab-aligned `Tag<TAB>=<TAB>"Ability.Fire"` survived it and the duplicate
	// line came straight back. TrimStartAndEnd already deals with leading and
	// trailing tabs, which is exactly not where the problem is: the tabs that
	// matter sit inside the entry. A gameplay tag name can never contain
	// whitespace, so stripping it cannot merge two distinct tags; the stripped
	// copy is used only for the comparison and never written back.
	//
	// Case is handled explicitly on every comparison below rather than left to
	// the defaults, because the defaults disagree with each other:
	// FString::Contains defaults to ESearchCase::IgnoreCase and FString::Equals
	// defaults to ESearchCase::CaseSensitive. Those line numbers are
	// UnrealString.h.inl:1162, 1177, 1191 and 1271 in the 5.8 tree this plugin
	// builds against, and the file moves between versions - the same four
	// declarations sit further down in 5.7.4, where Equals is at 1318 - so read
	// them by name, not by number. IgnoreCase is what this handler wants
	// throughout, since a gameplay tag compares as an FName and FName is
	// case-insensitive, and an ini section name is looked up on an FString map
	// key whose operator== is Stricmp.
	auto StripWhitespace = [](const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());
		for (const TCHAR C : In)
		{
			if (!FChar::IsWhitespace(C)) Out.AppendChar(C);
		}
		return Out;
	};
	const FString TagKey = FString::Printf(TEXT("Tag=\"%s\""), *Tag);
	const FString CompactTagKey = StripWhitespace(TagKey);

	// `//` is a comment marker in an Unreal ini. This scan used to skip only ';'
	// and '#', so a hand-edited `// +GameplayTagList=(Tag="Ability.Fire",...)`
	// was invisible to the engine and yet matched the tag key here, and the
	// handler reported the tag already declared and wrote nothing - leaving it
	// undeclared, which is the exact failure the commented-out-line note above
	// says this scan exists to prevent.
	//
	// FConfigFile reads every line through FParse::LineExtended with
	// ELineExtendedFlags::SwallowDoubleSlashComments set
	// (ConfigCacheIni.cpp:1847-1851). That flag makes LineExtended set bIgnore
	// on an unquoted `//` (Parse.cpp:1244-1250) and then drop every remaining
	// character of the line rather than appending it (:1286-1297), so the ini
	// parser sees the line already truncated at the `//`.
	//
	// Truncating here rather than skipping the line is what makes both halves
	// right: a line that is only a comment becomes empty and is skipped, and a
	// real declaration carrying a trailing `// note` keeps its declaration.
	// Section headers get the same treatment for the same reason, and they have
	// to, because the parser tests the brackets AFTER LineExtended has already
	// removed the comment: `[Section] // note` IS a section header to Unreal.
	//
	// Quote state is tracked because it has to be. DevComment="see http://x"
	// carries a `//` inside quotes that is not a comment, and LineExtended
	// toggles bIsQuoted on every `"` and steps over a backslash-escaped `\"` or
	// `\\` inside a quoted run (:1280-1284) rather than letting it toggle.
	//
	// Read in a 5.7.4 source tree. For 5.8, which is what this plugin builds
	// against, the enumerator survives verbatim -
	// FParse::ELineExtendedFlags::SwallowDoubleSlashComments (Parse.h:95-112) -
	// and Epic's own 5.8 Config/Mac/DataDrivenPlatformInfo.ini:58 carries a
	// bare `// Reenable ...` line inside a section. The implementing body is a
	// 5.7.4 read; it is not shipped with the launcher install.
	auto StripLineComment = [](const FString& In)
	{
		bool bInQuote = false;
		for (int32 i = 0; i < In.Len(); ++i)
		{
			if (bInQuote && In[i] == TEXT('\\') && i + 1 < In.Len()
				&& (In[i + 1] == TEXT('"') || In[i + 1] == TEXT('\\')))
			{
				++i;
				continue;
			}
			if (In[i] == TEXT('"'))
			{
				bInQuote = !bInQuote;
				continue;
			}
			if (!bInQuote && In[i] == TEXT('/') && i + 1 < In.Len() && In[i + 1] == TEXT('/'))
			{
				return In.Left(i);
			}
		}
		return In;
	};

	// The section header is recognised the way FConfigFile's own parser
	// recognises one, which is deliberately NOT the whitespace-stripped
	// comparison the tag scan uses. ProcessInputFileContents strips TRAILING
	// whitespace from the line and then requires the first character to be '['
	// and the last to be ']' (ConfigCacheIni.cpp:1874-1902). So a line carrying
	// anything after the closing bracket is not a section header to Unreal
	// either, and a header written `[ Name ]` names a DIFFERENT section, one
	// whose name has the spaces in it. Stripped full-line equality would have
	// accepted that second form and spliced the entry into a section the engine
	// never reads under the name this handler means. Leading whitespace is not
	// tolerated for the same reason: the parser tests the raw first character.
	// A trailing `// note` is tolerated, and has to be, because the parser tests
	// the brackets on a line LineExtended has already truncated at the comment.
	// The caller passes a line StripLineComment has already cut, which is what
	// the parameter name records, so `[Name] // note` arrives as `[Name] ` and
	// the TrimEnd below finishes the job.
	auto IsSectionHeader = [&SectionName](const FString& CommentStrippedLine)
	{
		FString Line = CommentStrippedLine;
		Line.TrimEndInline();
		if (Line.Len() < 2 || Line[0] != TEXT('[') || Line[Line.Len() - 1] != TEXT(']')) return false;
		return Line.Mid(1, Line.Len() - 2).Equals(SectionName, ESearchCase::IgnoreCase);
	};

	FString FileContent;
	bool bTagAlreadyDeclared = false;
	if (FFileHelper::LoadFileToString(FileContent, *TagFile))
	{
		// Walked by offset rather than split into an array of lines, because
		// the write below has to be an INSERTION into the original text. The
		// previous shape rejoined with FString::Join(Lines, LineEnd), and a
		// join rewrites EVERY terminator in the file to the single separator
		// that was detected: a hand-edited ini with mixed endings came back
		// wholly converted, which is precisely the unrelated config diff this
		// code goes out of its way not to produce. Only the inserted line is
		// written now; every existing byte is left where it was.
		int32 SectionInsertAt = INDEX_NONE;  // offset just past the header line
		FString SectionLineEnd;              // that header line's own terminator
		int32 Cursor = 0;
		bool bAtEnd = false;
		while (!bAtEnd && Cursor <= FileContent.Len())
		{
			int32 LineEnd = Cursor;
			while (LineEnd < FileContent.Len()
				&& FileContent[LineEnd] != TEXT('\r')
				&& FileContent[LineEnd] != TEXT('\n'))
			{
				++LineEnd;
			}
			int32 TermLen = 0;
			if (LineEnd < FileContent.Len())
			{
				TermLen = (FileContent[LineEnd] == TEXT('\r')
					&& LineEnd + 1 < FileContent.Len()
					&& FileContent[LineEnd + 1] == TEXT('\n')) ? 2 : 1;
			}
			else
			{
				bAtEnd = true;
			}

			// Comment tail off first, exactly as FParse::LineExtended does it,
			// so everything below sees the line the ini parser would see.
			const FString Line = StripLineComment(FileContent.Mid(Cursor, LineEnd - Cursor));
			const FString Trimmed = Line.TrimStartAndEnd();
			// ';' is the other comment marker the ini parser honours: it skips a
			// line whose first character is ';' (ConfigCacheIni.cpp:1944-1946).
			// '#' is not a comment marker there - `#Foo=Bar` parses as a key
			// literally named "#Foo" - which is why a '#'-prefixed
			// GameplayTagList line declares nothing and is skipped here too.
			if (!Trimmed.IsEmpty() && !Trimmed.StartsWith(TEXT(";")) && !Trimmed.StartsWith(TEXT("#")))
			{
				if (StripWhitespace(Trimmed).Contains(CompactTagKey, ESearchCase::IgnoreCase))
				{
					bTagAlreadyDeclared = true;
					break;
				}
				if (SectionInsertAt == INDEX_NONE && IsSectionHeader(Line))
				{
					SectionInsertAt = LineEnd + TermLen;
					SectionLineEnd = FileContent.Mid(LineEnd, TermLen);
				}
			}
			Cursor = LineEnd + TermLen;
		}

		if (!bTagAlreadyDeclared)
		{
			// The separator the file already uses, for the text this call adds.
			// Silently converting a CRLF ini to LF, or appending LF lines onto a
			// CRLF file, would be an unrelated change landing in someone's
			// config diff.
			const FString FileLineEnd = FileContent.Contains(TEXT("\r\n")) ? TEXT("\r\n") : TEXT("\n");
			if (SectionInsertAt == INDEX_NONE)
			{
				FileContent += FileLineEnd + FileLineEnd + Section + FileLineEnd + Entry + FileLineEnd;
			}
			else
			{
				// Straight after the header line, reusing THAT line's own
				// terminator so the new entry matches its neighbours. A header
				// sitting on the file's last line has no terminator to copy, so
				// the file default goes in front of the entry instead.
				FileContent.InsertAt(SectionInsertAt, SectionLineEnd.IsEmpty()
					? FileLineEnd + Entry
					: Entry + SectionLineEnd);
			}
		}
	}
	else
	{
		FileContent = Section + TEXT("\n") + Entry + TEXT("\n");
	}

	// Idempotency: this tag is already declared, so there is nothing to write.
	// A differing DevComment is deliberately NOT treated as a change: rewriting
	// it would mean editing the caller's ini in place, which this handler has
	// never done and which is not what "create" was asked for.
	if (bTagAlreadyDeclared)
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("unchanged"), true);
		Result->SetStringField(TEXT("method"), TEXT("ini_append"));
		Result->SetStringField(TEXT("note"),
			TEXT("DefaultGameplayTags.ini already declares this tag, so the file was not rewritten. Any comment "
			     "passed on this call was not applied: edit the existing GameplayTagList entry to change it."));
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), TEXT("Nothing was written, so there is nothing to undo."));
		return MCPResult(Result);
	}

	if (FFileHelper::SaveStringToFile(FileContent, *TagFile))
	{
		MCPSetCreated(Result);
		Result->SetBoolField(TEXT("unchanged"), false);
		Result->SetStringField(TEXT("method"), TEXT("ini_append"));
		Result->SetStringField(TEXT("note"), TEXT("Restart editor to pick up new tag"));

		// The bridge registers no action that removes a gameplay tag: the tag
		// surface is list_gameplay_tags and this one. Nothing is named as an
		// inverse, because nothing would answer to the name.
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), TEXT(
			"No action removes a gameplay tag. This appended a GameplayTagList entry to "
			"Config/DefaultGameplayTags.ini, and undoing it means deleting that line from the file by hand or "
			"through the Gameplay Tags editor."));
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

	// The list as it stands, read before the clear loop destroys it. This is
	// the payload of the inverse call: set_enum_entries replaces the whole list,
	// so handing it the previous list back is its own exact undo. Index
	// NumEnums()-1 is the auto _MAX sentinel, which is regenerated and is not an
	// authored entry.
	TArray<TSharedPtr<FJsonValue>> PreviousEntries;
	for (int32 i = 0; i < Enum->NumEnums() - 1; ++i)
	{
		TSharedPtr<FJsonObject> Was = MakeShared<FJsonObject>();
		Was->SetStringField(TEXT("name"), Enum->GetNameStringByIndex(i));
		Was->SetStringField(TEXT("displayName"), Enum->GetDisplayNameTextByIndex(i).ToString());
		PreviousEntries.Add(MakeShared<FJsonValueObject>(Was));
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
	Result->SetArrayField(TEXT("previousEntries"), PreviousEntries);

	// This action replaces the whole list, so replaying it with the previous
	// list is the inverse. It is lossy in one named way: a user-defined enum's
	// underlying enumerator names are minted by the engine, so restoring the
	// list restores the display names and their order, not the raw names that
	// were there before.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetArrayField(TEXT("entries"), PreviousEntries);
	MCPSetRollback(Result, TEXT("set_enum_entries"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"), TEXT(
		"Replaying set_enum_entries with previousEntries restores the display names and their order. The underlying "
		"enumerator names are minted by FEnumEditorUtils rather than written by this handler, so anything that "
		"stored a raw enumerator name may not rebind, and enum values already saved on assets resolve by index."));
	return MCPResult(Result);
}
