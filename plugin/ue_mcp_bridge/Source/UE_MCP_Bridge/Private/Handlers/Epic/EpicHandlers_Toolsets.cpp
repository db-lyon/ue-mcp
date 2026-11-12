// Turning Epic's registered toolsets off and on (#1827).
//
// The registry's own switch is its block and allow lists: a toolset whose name
// a block pattern matches, or that a non-empty allow list does not match, is
// disabled, which hides it from the schemas and makes ExecuteTool refuse it.
// The lists exist twice. UToolsetRegistrySettings holds them in config, and the
// process-wide FToolsetRegistry copies them once when it is built and keeps its
// own copy after that, so an edit to the settings alone reaches the next editor
// session and not this one. Both halves are written here: the settings object
// through the same save the Project Settings dialog uses, and the live registry
// through FToolsetRegistry::AddBlockedName and its siblings, which re-filter
// every toolset and broadcast the registry's change event.
//
// The settings class is reached by reflection. The registry methods are
// exported from the ToolsetRegistry module, which this module never links (the
// bridge has to load in projects that leave the plugin off), so they are looked
// up in the loaded DLL by their decorated names, as EpicHandlers_MCP.cpp does
// for the MCP plugin's request id. Every one of them takes and returns only
// pointers, references, an FString or a TFunctionRef, so a plain function
// pointer with the object as its first argument calls it with the same x64
// convention the member function was compiled with. The names are identical in
// 5.8 and 5.9. In 5.9 FToolsetRegistry::Get() hands back the registry; in 5.8 the
// registry is a member of UToolsetRegistrySubsystem, read through its header.

#include "EpicHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerCheckpoint.h"
#include "HandlerSourceControl.h"

#include "Editor.h"
#include "EditorSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Templates/Function.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

#if WITH_UEMCP_TOOLSET_REGISTRY_SUBSYSTEM
#include "ToolsetRegistry/ToolsetRegistrySubsystem.h"
#endif

namespace UEMCPEpicToolsets
{
	static const TCHAR* const RegistryModule = TEXT("ToolsetRegistry");
	static const TCHAR* const SettingsClassPath = TEXT("/Script/ToolsetRegistry.ToolsetRegistrySettings");

	/** Stands in for UE::ToolsetRegistry::FToolset, only ever handled by reference. */
	struct FOpaqueToolset;
	using FToolsetVisitor = TFunctionRef<void(const FString&, const FOpaqueToolset&)>;

	struct FRegistryApi
	{
		void* Registry = nullptr;
		void (*AddBlockedName)(void*, const FString&) = nullptr;
		void (*RemoveBlockedName)(void*, const FString&) = nullptr;
		void (*AddAllowedName)(void*, const FString&) = nullptr;
		void (*RemoveAllowedName)(void*, const FString&) = nullptr;
		const TArray<FString>& (*GetBlockedNames)(const void*) = nullptr;
		const TArray<FString>& (*GetAllowedNames)(const void*) = nullptr;
		void (*ForEachToolset)(const void*, FToolsetVisitor) = nullptr;
		bool (*IsEnabled)(const FOpaqueToolset*) = nullptr;
	};

	/** The live registry and its exported methods, or an error code and message saying why not. */
	static bool ResolveApi(FRegistryApi& Out, FString& OutCode, FString& OutError)
	{
#if !UE_MCP_HAS_5_8_API
		OutCode = TEXT("unsupported_engine_version");
		OutError = TEXT("Toolset toggles require Unreal Engine 5.8 or newer, where the ToolsetRegistry plugin first ships");
		return false;
#elif !(PLATFORM_WINDOWS && defined(_MSC_VER))
		OutCode = TEXT("unsupported_platform");
		OutError = TEXT("Toolset toggles reach the registry's exported methods by their MSVC names, so they run only in a Win64 editor");
		return false;
#else
		if (!FModuleManager::Get().IsModuleLoaded(RegistryModule))
		{
			OutCode = TEXT("PLUGIN_NOT_AVAILABLE");
			OutError = TEXT("Epic ToolsetRegistry not available. Requires UE 5.8+ with the ToolsetRegistry plugin enabled in the project.");
			return false;
		}
		const FString File = FModuleManager::Get().GetModuleFilename(RegistryModule);
		void* Handle = File.IsEmpty() ? nullptr : FPlatformProcess::GetDllHandle(*File);
		if (!Handle)
		{
			OutCode = TEXT("PLUGIN_NOT_AVAILABLE");
			OutError = FString::Printf(TEXT("The ToolsetRegistry module is loaded but its library (%s) could not be opened"), *File);
			return false;
		}
		auto Find = [Handle](const TCHAR* Name) { return FPlatformProcess::GetDllExport(Handle, Name); };
		Out.AddBlockedName = reinterpret_cast<decltype(Out.AddBlockedName)>(Find(
			TEXT("?AddBlockedName@FToolsetRegistry@ToolsetRegistry@UE@@QEAAXAEBVFString@@@Z")));
		Out.RemoveBlockedName = reinterpret_cast<decltype(Out.RemoveBlockedName)>(Find(
			TEXT("?RemoveBlockedName@FToolsetRegistry@ToolsetRegistry@UE@@QEAAXAEBVFString@@@Z")));
		Out.AddAllowedName = reinterpret_cast<decltype(Out.AddAllowedName)>(Find(
			TEXT("?AddAllowedName@FToolsetRegistry@ToolsetRegistry@UE@@QEAAXAEBVFString@@@Z")));
		Out.RemoveAllowedName = reinterpret_cast<decltype(Out.RemoveAllowedName)>(Find(
			TEXT("?RemoveAllowedName@FToolsetRegistry@ToolsetRegistry@UE@@QEAAXAEBVFString@@@Z")));
		Out.GetBlockedNames = reinterpret_cast<decltype(Out.GetBlockedNames)>(Find(
			TEXT("?GetBlockedNames@FToolsetRegistry@ToolsetRegistry@UE@@QEBAAEBV?$TArray@VFString@@V?$TSizedDefaultAllocator@$0CA@@@@@XZ")));
		Out.GetAllowedNames = reinterpret_cast<decltype(Out.GetAllowedNames)>(Find(
			TEXT("?GetAllowedNames@FToolsetRegistry@ToolsetRegistry@UE@@QEBAAEBV?$TArray@VFString@@V?$TSizedDefaultAllocator@$0CA@@@@@XZ")));
		Out.ForEachToolset = reinterpret_cast<decltype(Out.ForEachToolset)>(Find(
			TEXT("?ForEachToolset@FToolsetRegistry@ToolsetRegistry@UE@@QEBAXV?$TFunctionRef@$$A6AXAEBVFString@@AEBVFToolset@ToolsetRegistry@UE@@@Z@@@Z")));
		Out.IsEnabled = reinterpret_cast<decltype(Out.IsEnabled)>(Find(
			TEXT("?IsEnabled@FToolset@ToolsetRegistry@UE@@QEBA_NXZ")));

#if UE_MCP_HAS_5_9_API
		using FGet = void* (*)();
		if (FGet Get = reinterpret_cast<FGet>(Find(TEXT("?Get@FToolsetRegistry@ToolsetRegistry@UE@@SAPEAV123@XZ"))))
		{
			Out.Registry = Get();
		}
#elif WITH_UEMCP_TOOLSET_REGISTRY_SUBSYSTEM
		if (GEditor)
		{
			if (UClass* SubsystemClass = FindObject<UClass>(nullptr, TEXT("/Script/ToolsetRegistry.ToolsetRegistrySubsystem")))
			{
				// The member is plain C++, so its address comes from the header's
				// layout; nothing here calls into the module.
				if (UEditorSubsystem* Subsystem = GEditor->GetEditorSubsystemBase(SubsystemClass))
				{
					Out.Registry = &static_cast<UToolsetRegistrySubsystem*>(Subsystem)->ToolsetRegistry;
				}
			}
		}
#endif
		const bool bAll = Out.Registry && Out.AddBlockedName && Out.RemoveBlockedName && Out.AddAllowedName
			&& Out.RemoveAllowedName && Out.GetBlockedNames && Out.GetAllowedNames && Out.ForEachToolset && Out.IsEnabled;
		if (!bAll)
		{
			OutCode = TEXT("unsupported_engine_version");
			OutError = Out.Registry
				? TEXT("This engine's ToolsetRegistry does not export the block and allow list methods toolset toggles drive")
				: TEXT("The ToolsetRegistry module is loaded but its process-wide registry could not be reached on this engine version");
			return false;
		}
		return true;
#endif
	}

	/** One registered toolset by name, with whether the registry has it enabled. */
	struct FToolsetEntry
	{
		FString Name;
		bool bEnabled = false;
	};

	/** The toolset name an exact pattern written by this file names, or empty for any other pattern. */
	static FString ExactPatternName(const FString& Pattern)
	{
		static const FString Head = TEXT("/^\\Q");
		static const FString Tail = TEXT("\\E$/");
		if (Pattern.Len() <= Head.Len() + Tail.Len() || !Pattern.StartsWith(Head, ESearchCase::CaseSensitive)
			|| !Pattern.EndsWith(Tail, ESearchCase::CaseSensitive)) return FString();
		return Pattern.Mid(Head.Len(), Pattern.Len() - Head.Len() - Tail.Len());
	}

	/** Every toolset name the registry has listed as enabled since this editor loaded the bridge. Game thread only. */
	static TSet<FString>& SeenNames()
	{
		static TSet<FString> Seen;
		return Seen;
	}

	// The registry enumerates enabled toolsets only (ForEachToolset, Find and the
	// schemas all skip a disabled one), so a disabled toolset is known here by
	// one of two other routes: it was listed as enabled earlier in this session,
	// or one of the exact patterns this file writes names it, which also holds
	// across a restart because the pattern is saved. A toolset that a pattern
	// written by someone else hid before the bridge first looked stays unlisted.
	static TArray<FToolsetEntry> ListToolsets(const FRegistryApi& Api, const TArray<FString>& Patterns)
	{
		TArray<FToolsetEntry> Out;
		TSet<FString> Enabled;
		Api.ForEachToolset(Api.Registry, [&Out, &Api, &Enabled](const FString& Name, const FOpaqueToolset& Toolset)
		{
			FToolsetEntry Entry;
			Entry.Name = Name;
			Entry.bEnabled = Api.IsEnabled(&Toolset);
			Enabled.Add(Name);
			Out.Add(MoveTemp(Entry));
		});
		SeenNames().Append(Enabled);
		TSet<FString> Disabled;
		for (const FString& Name : SeenNames())
		{
			if (!Enabled.Contains(Name)) Disabled.Add(Name);
		}
		for (const FString& Pattern : Patterns)
		{
			const FString Name = ExactPatternName(Pattern);
			if (!Name.IsEmpty() && !Enabled.Contains(Name)) Disabled.Add(Name);
		}
		for (const FString& Name : Disabled)
		{
			FToolsetEntry Entry;
			Entry.Name = Name;
			Entry.bEnabled = false;
			Out.Add(MoveTemp(Entry));
		}
		Out.Sort([](const FToolsetEntry& A, const FToolsetEntry& B) { return A.Name < B.Name; });
		return Out;
	}

	/** The registry's pattern rule: /.../ is a regular expression, anything else a case-insensitive substring. */
	static bool PatternMatches(const FString& Pattern, const FString& Name)
	{
		if (Pattern.Len() > 2 && Pattern.StartsWith(TEXT("/")) && Pattern.EndsWith(TEXT("/")))
		{
			const FRegexPattern Regex(Pattern.Mid(1, Pattern.Len() - 2));
			FRegexMatcher Matcher(Regex, Name);
			return Matcher.FindNext();
		}
		return !Pattern.IsEmpty() && Name.Contains(Pattern, ESearchCase::IgnoreCase);
	}

	static TArray<FString> MatchingPatterns(const TArray<FString>& Patterns, const FString& Name)
	{
		TArray<FString> Out;
		for (const FString& Pattern : Patterns)
		{
			if (PatternMatches(Pattern, Name)) Out.Add(Pattern);
		}
		return Out;
	}

	/** The pattern this handler writes for one toolset: its whole name and nothing else. */
	static FString ExactPattern(const FString& ToolsetName)
	{
		return FString::Printf(TEXT("/^\\Q%s\\E$/"), *ToolsetName);
	}

	static TArray<TSharedPtr<FJsonValue>> StringValues(const TArray<FString>& Strings)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& S : Strings) Out.Add(MakeShared<FJsonValueString>(S));
		return Out;
	}

	// ── The settings half ────────────────────────────────────────────────────

	static UObject* SettingsObject()
	{
		UClass* Class = FindObject<UClass>(nullptr, SettingsClassPath);
		return Class ? Class->GetDefaultObject() : nullptr;
	}

	static TArray<FString>* SettingsArray(UObject* Settings, const TCHAR* Property)
	{
		if (!Settings) return nullptr;
		FArrayProperty* Prop = CastField<FArrayProperty>(Settings->GetClass()->FindPropertyByName(Property));
		if (!Prop || !Prop->Inner->IsA<FStrProperty>()) return nullptr;
		return Prop->ContainerPtrToValuePtr<TArray<FString>>(Settings);
	}

	static void AddSettingsPatterns(TSharedPtr<FJsonObject> Res, UObject* Settings)
	{
		if (TArray<FString>* Blocked = SettingsArray(Settings, TEXT("BlockedNames")))
		{
			Res->SetArrayField(TEXT("settingsBlockedNames"), StringValues(*Blocked));
		}
		if (TArray<FString>* Allowed = SettingsArray(Settings, TEXT("AllowedNames")))
		{
			Res->SetArrayField(TEXT("settingsAllowedNames"), StringValues(*Allowed));
		}
		if (Settings)
		{
			Res->SetStringField(TEXT("configFile"), FPaths::ConvertRelativePathToFull(Settings->GetDefaultConfigFilename()));
			Res->SetStringField(TEXT("configSection"), Settings->GetClass()->GetPathName());
		}
	}

	/** Every pattern either half holds, live and saved, block and allow. */
	static TArray<FString> KnownPatterns(const FRegistryApi& Api)
	{
		TArray<FString> Out = Api.GetBlockedNames(Api.Registry);
		Out.Append(Api.GetAllowedNames(Api.Registry));
		UObject* Settings = SettingsObject();
		if (TArray<FString>* Blocked = SettingsArray(Settings, TEXT("BlockedNames"))) Out.Append(*Blocked);
		if (TArray<FString>* Allowed = SettingsArray(Settings, TEXT("AllowedNames"))) Out.Append(*Allowed);
		return Out;
	}

	/** True when a live pattern was not written here, so it may hide a toolset the registry will not enumerate. */
	static bool HasForeignPatterns(const TArray<FString>& Blocked, const TArray<FString>& Allowed)
	{
		for (const FString& P : Blocked) if (ExactPatternName(P).IsEmpty()) return true;
		for (const FString& P : Allowed) if (ExactPatternName(P).IsEmpty()) return true;
		return false;
	}

	static const TCHAR* const UnlistedNote =
		TEXT("The registry enumerates enabled toolsets only. A disabled toolset is listed when it was seen enabled earlier in this editor session "
		     "or when this action's own pattern names it; one hidden by another pattern before the bridge first looked is not listed.");

	static TSharedPtr<FJsonValue> Unavailable(const FString& Code, const FString& Error)
	{
		return MCPErrorWithCode(Code, Error);
	}
}


TSharedPtr<FJsonValue> FEpicHandlers::GetToolsetStates(const TSharedPtr<FJsonObject>& Params)
{
	const FString Filter = OptionalString(Params, TEXT("nameFilter"), TEXT(""));

	UEMCPEpicToolsets::FRegistryApi Api;
	FString Code, Error;
	if (!UEMCPEpicToolsets::ResolveApi(Api, Code, Error)) return UEMCPEpicToolsets::Unavailable(Code, Error);

	const TArray<FString> Blocked = Api.GetBlockedNames(Api.Registry);
	const TArray<FString> Allowed = Api.GetAllowedNames(Api.Registry);

	TArray<TSharedPtr<FJsonValue>> Out;
	int32 EnabledCount = 0;
	for (const UEMCPEpicToolsets::FToolsetEntry& Entry : UEMCPEpicToolsets::ListToolsets(Api, UEMCPEpicToolsets::KnownPatterns(Api)))
	{
		if (!Filter.IsEmpty() && !Entry.Name.Contains(Filter)) continue;
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Entry.Name);
		Obj->SetBoolField(TEXT("enabled"), Entry.bEnabled);
		if (Entry.bEnabled) ++EnabledCount;

		const TArray<FString> BlockedBy = UEMCPEpicToolsets::MatchingPatterns(Blocked, Entry.Name);
		const TArray<FString> AllowedBy = UEMCPEpicToolsets::MatchingPatterns(Allowed, Entry.Name);
		Obj->SetArrayField(TEXT("blockedBy"), UEMCPEpicToolsets::StringValues(BlockedBy));
		Obj->SetArrayField(TEXT("allowedBy"), UEMCPEpicToolsets::StringValues(AllowedBy));
		const bool bOutsideAllowList = Allowed.Num() > 0 && AllowedBy.Num() == 0;
		Obj->SetBoolField(TEXT("outsideAllowList"), bOutsideAllowList);
		if (!Entry.bEnabled)
		{
			// Neither list explains it when the toolset has no tools left to
			// offer: every tool filtered out, or withheld by a permission provider.
			Obj->SetStringField(TEXT("disabledBy"),
				BlockedBy.Num() > 0 ? TEXT("blockList") : bOutsideAllowList ? TEXT("allowList") : TEXT("noVisibleTools"));
		}
		Obj->SetBoolField(TEXT("toggledHere"),
			Blocked.Contains(UEMCPEpicToolsets::ExactPattern(Entry.Name)) || Allowed.Contains(UEMCPEpicToolsets::ExactPattern(Entry.Name)));
		Out.Add(MakeShared<FJsonValueObject>(Obj));
	}

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetNumberField(TEXT("toolsetCount"), Out.Num());
	Res->SetNumberField(TEXT("enabledCount"), EnabledCount);
	Res->SetArrayField(TEXT("toolsets"), Out);
	Res->SetArrayField(TEXT("blockedNames"), UEMCPEpicToolsets::StringValues(Blocked));
	Res->SetArrayField(TEXT("allowedNames"), UEMCPEpicToolsets::StringValues(Allowed));
	UEMCPEpicToolsets::AddSettingsPatterns(Res, UEMCPEpicToolsets::SettingsObject());
	if (UEMCPEpicToolsets::HasForeignPatterns(Blocked, Allowed))
	{
		Res->SetStringField(TEXT("unlistedNote"), UEMCPEpicToolsets::UnlistedNote);
	}
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FEpicHandlers::SetToolsetEnabled(const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* Requested = nullptr;
	if (TSharedPtr<FJsonValue> Err = RequireArray(Params, TEXT("toolsets"), Requested)) return Err;
	bool bEnable = true;
	if (!TryGetBoolParam(Params, TEXT("enabled"), bEnable))
	{
		return MCPError(TEXT("Missing required parameter 'enabled' (boolean)"));
	}

	TArray<FString> Names;
	for (const TSharedPtr<FJsonValue>& V : *Requested)
	{
		FString Name;
		if (!V.IsValid() || !V->TryGetString(Name) || Name.IsEmpty())
		{
			return MCPError(TEXT("'toolsets' must be an array of qualified toolset names"));
		}
		Names.AddUnique(Name);
	}
	if (Names.Num() == 0) return MCPError(TEXT("'toolsets' is empty; name at least one toolset"));

	UEMCPEpicToolsets::FRegistryApi Api;
	FString Code, Error;
	if (!UEMCPEpicToolsets::ResolveApi(Api, Code, Error)) return UEMCPEpicToolsets::Unavailable(Code, Error);

	UObject* Settings = UEMCPEpicToolsets::SettingsObject();
	TArray<FString>* SettingsBlocked = UEMCPEpicToolsets::SettingsArray(Settings, TEXT("BlockedNames"));
	TArray<FString>* SettingsAllowed = UEMCPEpicToolsets::SettingsArray(Settings, TEXT("AllowedNames"));
	if (!SettingsBlocked || !SettingsAllowed)
	{
		return MCPError(FString::Printf(TEXT("%s or its BlockedNames/AllowedNames lists were not found"), UEMCPEpicToolsets::SettingsClassPath));
	}

	TMap<FString, bool> Known;
	TArray<FString> KnownNames;
	for (const UEMCPEpicToolsets::FToolsetEntry& Entry : UEMCPEpicToolsets::ListToolsets(Api, UEMCPEpicToolsets::KnownPatterns(Api)))
	{
		Known.Add(Entry.Name, Entry.bEnabled);
		KnownNames.Add(Entry.Name);
	}
	TArray<FString> Unknown;
	for (const FString& Name : Names)
	{
		if (!Known.Contains(Name)) Unknown.Add(Name);
	}
	if (Unknown.Num() > 0)
	{
		TSharedPtr<FJsonObject> Err = MCPErrorObject(FString::Printf(
			TEXT("Unknown toolset name(s): %s. Toolset names are exact and case-sensitive; validNames lists every toolset known here. %s"),
			*FString::Join(Unknown, TEXT(", ")), UEMCPEpicToolsets::UnlistedNote));
		Err->SetStringField(TEXT("errorCode"), TEXT("unknown_toolset"));
		Err->SetArrayField(TEXT("unknown"), UEMCPEpicToolsets::StringValues(Unknown));
		Err->SetArrayField(TEXT("validNames"), UEMCPEpicToolsets::StringValues(KnownNames));
		return MCPResult(Err);
	}

	// Plan every change before making any, so a refusal leaves both halves untouched.
	const TArray<FString> LiveBlocked = Api.GetBlockedNames(Api.Registry);
	const TArray<FString> LiveAllowed = Api.GetAllowedNames(Api.Registry);
	TArray<FString> AddBlock, RemoveBlock, AddAllow;
	TArray<FString> Changing;
	for (const FString& Name : Names)
	{
		if (Known[Name] == bEnable) continue;
		const FString Exact = UEMCPEpicToolsets::ExactPattern(Name);
		if (!bEnable)
		{
			AddBlock.Add(Exact);
		}
		else
		{
			TArray<FString> OtherBlocks = UEMCPEpicToolsets::MatchingPatterns(LiveBlocked, Name);
			OtherBlocks.Remove(Exact);
			if (OtherBlocks.Num() > 0)
			{
				return MCPError(FString::Printf(
					TEXT("Toolset '%s' is blocked by the pattern(s) %s, which this action did not write and which may cover other toolsets too. "
					     "A block wins over the allow list, so edit ToolsetBlockedNames in Project Settings > Plugins > Toolset Registry. Nothing was changed."),
					*Name, *FString::Join(OtherBlocks, TEXT(", "))));
			}
			if (LiveBlocked.Contains(Exact)) RemoveBlock.Add(Exact);
			if (LiveAllowed.Num() > 0 && UEMCPEpicToolsets::MatchingPatterns(LiveAllowed, Name).Num() == 0) AddAllow.Add(Exact);
		}
		Changing.Add(Name);
	}

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	const FString ConfigFile = FPaths::ConvertRelativePathToFull(Settings->GetDefaultConfigFilename());

	if (Changing.Num() > 0)
	{
		if (FPaths::FileExists(ConfigFile) && IFileManager::Get().IsReadOnly(*ConfigFile))
		{
			FString CheckoutNote;
			MCPSourceControl::TryAutoCheckout(ConfigFile, CheckoutNote);
			if (IFileManager::Get().IsReadOnly(*ConfigFile))
			{
				return MCPError(FString::Printf(TEXT("%s is read-only, so the toolset setting cannot be saved. Nothing was changed. %s"),
					*ConfigFile, CheckoutNote.IsEmpty()
						? TEXT("Check it out, or turn on source_control(set_checkout_mode, mode=auto).") : *CheckoutNote));
			}
		}

		// The live registry: each call re-filters every toolset and broadcasts.
		for (const FString& P : RemoveBlock) Api.RemoveBlockedName(Api.Registry, P);
		for (const FString& P : AddAllow) Api.AddAllowedName(Api.Registry, P);
		for (const FString& P : AddBlock) Api.AddBlockedName(Api.Registry, P);

		// The settings: the same edits, so the next session starts this way. The
		// class is defaultconfig, so this is the save the settings dialog makes:
		// Config/DefaultGame.ini in 5.9, DefaultEditorPerProjectUserSettings.ini in 5.8.
		for (const FString& P : RemoveBlock) SettingsBlocked->Remove(P);
		for (const FString& P : AddAllow) SettingsAllowed->AddUnique(P);
		for (const FString& P : AddBlock) SettingsBlocked->AddUnique(P);
		MCPCheckpointRecordBeforeWrite(ConfigFile);
		const bool bPersisted = Settings->TryUpdateDefaultConfigFile();
		Res->SetBoolField(TEXT("persisted"), bPersisted);
		if (!bPersisted)
		{
			Res->SetStringField(TEXT("persistNote"), FString::Printf(
				TEXT("%s could not be written; the change holds in this session only"), *ConfigFile));
		}

		MCPSetUpdated(Res);
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("toolsets"), UEMCPEpicToolsets::StringValues(Changing));
		Payload->SetBoolField(TEXT("enabled"), !bEnable);
		MCPSetRollback(Res, TEXT("epic_set_toolset_enabled"), Payload);
	}
	else
	{
		Res->SetBoolField(TEXT("unchanged"), true);
	}

	// Read back what the registry now reports rather than echoing the plan.
	TMap<FString, bool> After;
	for (const UEMCPEpicToolsets::FToolsetEntry& Entry : UEMCPEpicToolsets::ListToolsets(Api, UEMCPEpicToolsets::KnownPatterns(Api))) After.Add(Entry.Name, Entry.bEnabled);
	TArray<TSharedPtr<FJsonValue>> Results;
	TArray<FString> NotApplied;
	for (const FString& Name : Names)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		const bool bNow = After.FindRef(Name);
		Obj->SetStringField(TEXT("name"), Name);
		Obj->SetBoolField(TEXT("previousEnabled"), Known[Name]);
		Obj->SetBoolField(TEXT("enabled"), bNow);
		Obj->SetBoolField(TEXT("changed"), bNow != Known[Name]);
		if (bNow != bEnable) NotApplied.Add(Name);
		Results.Add(MakeShared<FJsonValueObject>(Obj));
	}
	Res->SetArrayField(TEXT("toolsets"), Results);
	Res->SetNumberField(TEXT("changedCount"), Changing.Num());
	Res->SetArrayField(TEXT("blockedNames"), UEMCPEpicToolsets::StringValues(Api.GetBlockedNames(Api.Registry)));
	Res->SetArrayField(TEXT("allowedNames"), UEMCPEpicToolsets::StringValues(Api.GetAllowedNames(Api.Registry)));
	UEMCPEpicToolsets::AddSettingsPatterns(Res, Settings);
	if (NotApplied.Num() > 0)
	{
		// Enabled through both lists and still off: the toolset has no tool left
		// to offer (a per-tool pattern or a permission provider withholds them all).
		Res->SetBoolField(TEXT("success"), false);
		Res->SetStringField(TEXT("errorCode"), TEXT("toolset_state_not_applied"));
		Res->SetStringField(TEXT("error"), FString::Printf(
			TEXT("The registry still reports %s as %s after the lists were updated. A toolset with no visible tools stays disabled; "
			     "check epic(get_toolset_states) for the patterns that match its tools."),
			*FString::Join(NotApplied, TEXT(", ")), bEnable ? TEXT("disabled") : TEXT("enabled")));
	}
	return MCPResult(Res);
}

void FEpicHandlers::RegisterToolsetStateHandlers(FMCPHandlerRegistry& Registry)
{
	using EType = EMCPParamType;
	Registry.RegisterHandler(TEXT("epic_get_toolset_states"), &GetToolsetStates, {
		MCPParam::Optional(TEXT("nameFilter"), EType::String, TEXT("Case-sensitive substring of the qualified toolset name")),
	});
	Registry.RegisterHandler(TEXT("epic_set_toolset_enabled"), &SetToolsetEnabled, {
		MCPParam::Required(TEXT("toolsets"), EType::Array, TEXT("Exact qualified toolset names from epic_get_toolset_states, e.g. GASToolsets.AttributeSetToolset")).Items(EType::String),
		MCPParam::Required(TEXT("enabled"), EType::Boolean, TEXT("true turns the toolsets on, false turns them off")),
	});
}
