#include "ProjectHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

// Native-class creation: same API as File -> New C++ Class in the editor
#include "AddToProjectConfig.h"
#include "GameProjectUtils.h"
#include "GameProjectGenerationModule.h"
#include "ModuleDescriptor.h" // FModuleContextInfo + EHostType

// Live Coding — Win64 only, guarded by PLATFORM_WINDOWS
#if PLATFORM_WINDOWS
#include "ILiveCodingModule.h"
#endif

namespace
{
	/** Convert EHostType enum to a short string for JSON output. */
	FString HostTypeToString(EHostType::Type Type)
	{
		switch (Type)
		{
			case EHostType::Runtime:                    return TEXT("Runtime");
			case EHostType::RuntimeNoCommandlet:        return TEXT("RuntimeNoCommandlet");
			case EHostType::RuntimeAndProgram:          return TEXT("RuntimeAndProgram");
			case EHostType::CookedOnly:                 return TEXT("CookedOnly");
			case EHostType::UncookedOnly:               return TEXT("UncookedOnly");
			case EHostType::Developer:                  return TEXT("Developer");
			case EHostType::DeveloperTool:              return TEXT("DeveloperTool");
			case EHostType::Editor:                     return TEXT("Editor");
			case EHostType::EditorNoCommandlet:         return TEXT("EditorNoCommandlet");
			case EHostType::EditorAndProgram:           return TEXT("EditorAndProgram");
			case EHostType::Program:                    return TEXT("Program");
			case EHostType::ServerOnly:                 return TEXT("ServerOnly");
			case EHostType::ClientOnly:                 return TEXT("ClientOnly");
			case EHostType::ClientOnlyNoCommandlet:     return TEXT("ClientOnlyNoCommandlet");
			default:                                    return TEXT("Unknown");
		}
	}

	/** Resolve a parent class path to a UClass*.
	 *  Accepts: "/Script/Engine.Actor", "Actor" (short native name), or
	 *  a BP path "/Game/.../BP_Foo.BP_Foo_C". Returns nullptr if unresolved. */
	const UClass* ResolveParentClass(const FString& InClass)
	{
		if (InClass.IsEmpty()) return UObject::StaticClass();

		// Direct path form
		if (UClass* Cls = LoadClass<UObject>(nullptr, *InClass))
		{
			return Cls;
		}
		if (UClass* Cls = LoadObject<UClass>(nullptr, *InClass))
		{
			return Cls;
		}

		// Try common native short-names first
		if (UClass* Cls = FindFirstObjectSafe<UClass>(*InClass))
		{
			return Cls;
		}

		// Try "/Script/<Module>.<Name>" for common modules if a short name
		// was provided.
		static const TCHAR* CommonModules[] = { TEXT("Engine"), TEXT("CoreUObject"), TEXT("UMG"), TEXT("GameplayAbilities") };
		for (const TCHAR* Mod : CommonModules)
		{
			FString Candidate = FString::Printf(TEXT("/Script/%s.%s"), Mod, *InClass);
			if (UClass* Cls = LoadClass<UObject>(nullptr, *Candidate))
			{
				return Cls;
			}
			if (UClass* Cls = LoadObject<UClass>(nullptr, *Candidate))
			{
				return Cls;
			}
		}

		return nullptr;
	}
}

void FProjectHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	// create_cpp_class triggers AddCodeToProject which regenerates IDE
	// project files + kicks a Live Coding compile. Both are synchronous
	// game-thread work and easily exceed the 30-second default.
	Registry.RegisterHandlerWithTimeout(TEXT("create_cpp_class"), &CreateCppClass, 300.0f);
	Registry.RegisterHandler(TEXT("list_project_modules"), &ListProjectModules);
	// live_coding_compile(wait=true) can also exceed 30s on a full rebuild.
	Registry.RegisterHandlerWithTimeout(TEXT("live_coding_compile"), &LiveCodingCompile, 300.0f);
	Registry.RegisterHandler(TEXT("live_coding_status"), &LiveCodingStatus);
}

// ─── create_cpp_class ────────────────────────────────────────────────
// Creates a new native UCLASS in the project by calling the same engine
// API that backs the editor's "File -> New C++ Class" dialog
// (GameProjectUtils::AddCodeToProject). Generated .h/.cpp come from the
// engine templates — byte-for-byte identical to what a human gets via
// the dialog. Agents can then call write_cpp_file to fill in additional
// UPROPERTYs/UFUNCTIONs and build_project to compile.
//
// Params:
//   className    required. Must be C++-valid. Prefix handled by
//                FNewClassInfo based on parent (A for Actor, U for UObject,
//                etc.) — omit the prefix.
//   parentClass  optional. Path or short name. Defaults to UObject.
//                Examples: "Actor", "ActorComponent", "/Script/UMG.UserWidget".
//   moduleName   optional. Target game module. Defaults to the project's
//                first module (usually the primary runtime module).
//   classDomain  optional. "public" | "private" | "classes". Default "public".
//   subPath      optional. Nested folder under public/private — e.g.
//                "Gameplay/Abilities". Default: root of the domain folder.
TSharedPtr<FJsonValue> FProjectHandlers::CreateCppClass(const TSharedPtr<FJsonObject>& Params)
{
	FString ClassName;
	if (auto Err = RequireString(Params, TEXT("className"), ClassName)) return Err;

	const FString ParentClassStr = OptionalString(Params, TEXT("parentClass"), TEXT(""));
	const FString TargetModule   = OptionalString(Params, TEXT("moduleName"), TEXT(""));
	const FString ClassDomain    = OptionalString(Params, TEXT("classDomain"), TEXT("public")).ToLower();
	const FString SubPath        = OptionalString(Params, TEXT("subPath"), TEXT(""));

	const UClass* ParentClass = ResolveParentClass(ParentClassStr);
	if (!ParentClass)
	{
		return MCPError(FString::Printf(
			TEXT("Could not resolve parentClass '%s' — pass a /Script/<Module>.<Class> path or a loaded native class name"),
			*ParentClassStr));
	}

	// Discover project modules and pick one.
	FGameProjectGenerationModule& GPM = FGameProjectGenerationModule::Get();
	const TArray<FModuleContextInfo>& Modules = GPM.GetCurrentProjectModules();
	if (Modules.Num() == 0)
	{
		return MCPError(TEXT("Project has no C++ modules to add the class to. Add at least one code file through the editor first, or create a module manually."));
	}

	// Copy by value — AddCodeToProject_Internal may reset/repopulate the
	// module cache, which would invalidate any reference we held into
	// the engine's TArray<FModuleContextInfo>.
	FModuleContextInfo SelectedModule;
	bool bFoundModule = false;
	if (!TargetModule.IsEmpty())
	{
		for (const FModuleContextInfo& M : Modules)
		{
			if (M.ModuleName == TargetModule)
			{
				SelectedModule = M;
				bFoundModule = true;
				break;
			}
		}
		if (!bFoundModule)
		{
			TArray<FString> Names;
			for (const FModuleContextInfo& M : Modules) Names.Add(M.ModuleName);
			return MCPError(FString::Printf(
				TEXT("moduleName '%s' not found. Available: [%s]"),
				*TargetModule, *FString::Join(Names, TEXT(", "))));
		}
	}
	else
	{
		SelectedModule = Modules[0];
	}

	// Build the target class path. GameProjectUtils expects a directory
	// path under the module's source root, ending with a trailing slash.
	FString DomainFolder;
	if (ClassDomain == TEXT("private")) DomainFolder = TEXT("Private");
	else if (ClassDomain == TEXT("classes")) DomainFolder = TEXT("Classes");
	else DomainFolder = TEXT("Public");

	// Build the target path relative to ModuleSourcePath so path-normalisation
	// mismatches (forward vs back slashes, trailing slash, relative-root)
	// never cause IsValidSourcePath to reject the destination. The engine
	// normalises via FPaths::ConvertRelativePathToFull + trailing slash;
	// we mirror that.
	FString NewClassPath = SelectedModule.ModuleSourcePath; // always trailing /
	NewClassPath /= DomainFolder;
	if (!SubPath.IsEmpty())
	{
		NewClassPath /= SubPath;
	}
	NewClassPath /= TEXT("");                                 // ensure trailing /
	FPaths::NormalizeDirectoryName(NewClassPath);
	NewClassPath = FPaths::ConvertRelativePathToFull(NewClassPath);
	if (!NewClassPath.EndsWith(TEXT("/")) && !NewClassPath.EndsWith(TEXT("\\")))
	{
		NewClassPath += TEXT("/");
	}

	// Validate the class name against the chosen module.
	FText ValidationFail;
	if (!GameProjectUtils::IsValidClassNameForCreation(ClassName, SelectedModule, /*DisallowedHeaders=*/{}, ValidationFail))
	{
		return MCPError(FString::Printf(
			TEXT("Invalid class name '%s': %s"),
			*ClassName, *ValidationFail.ToString()));
	}
	if (!GameProjectUtils::IsValidBaseClassForCreation(ParentClass, SelectedModule))
	{
		return MCPError(FString::Printf(
			TEXT("Class '%s' is not a valid base for module '%s' (check module dependencies)"),
			*ParentClass->GetName(), *SelectedModule.ModuleName));
	}

	// Hand off to the engine. Returns Succeeded | InvalidInput |
	// FailedToAddCode | FailedToHotReload. FailedToHotReload means the
	// files were written but live-coding/hot-reload couldn't pick up the
	// new UCLASS — a full editor restart + UBT build is needed.
	FString OutHeader, OutCpp;
	FText OutFail;
	GameProjectUtils::EReloadStatus OutReload = GameProjectUtils::EReloadStatus::NotReloaded;
	const FNewClassInfo ParentInfo(ParentClass);

	const GameProjectUtils::EAddCodeToProjectResult ResultCode =
		GameProjectUtils::AddCodeToProject(
			ClassName,
			NewClassPath,
			SelectedModule,
			ParentInfo,
			/*DisallowedHeaders=*/{},
			OutHeader,
			OutCpp,
			OutFail,
			OutReload);

	const bool bFilesWritten =
		ResultCode == GameProjectUtils::EAddCodeToProjectResult::Succeeded ||
		ResultCode == GameProjectUtils::EAddCodeToProjectResult::FailedToHotReload;

	if (!bFilesWritten)
	{
		const TCHAR* CodeStr =
			ResultCode == GameProjectUtils::EAddCodeToProjectResult::InvalidInput     ? TEXT("InvalidInput") :
			ResultCode == GameProjectUtils::EAddCodeToProjectResult::FailedToAddCode  ? TEXT("FailedToAddCode") :
			TEXT("Unknown");
		// Include the paths in the error so the agent can diagnose path
		// mismatches without another round-trip.
		return MCPError(FString::Printf(
			TEXT("AddCodeToProject failed (%s): %s [module='%s', moduleSourcePath='%s', newClassPath='%s']"),
			CodeStr,
			*OutFail.ToString(),
			*SelectedModule.ModuleName,
			*SelectedModule.ModuleSourcePath,
			*NewClassPath));
	}

	const bool bReloaded = (OutReload == GameProjectUtils::EReloadStatus::Reloaded);
	const bool bNeedsRestart =
		(ResultCode == GameProjectUtils::EAddCodeToProjectResult::FailedToHotReload) ||
		(!bReloaded);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("className"), ClassName);
	Result->SetStringField(TEXT("parentClass"), ParentClass->GetPathName());
	Result->SetStringField(TEXT("moduleName"), SelectedModule.ModuleName);
	Result->SetStringField(TEXT("headerPath"), OutHeader);
	Result->SetStringField(TEXT("cppPath"), OutCpp);
	Result->SetStringField(TEXT("reloadStatus"), bReloaded ? TEXT("reloaded") : TEXT("not_reloaded"));
	Result->SetBoolField(TEXT("needsEditorRestart"), bNeedsRestart);
	if (bNeedsRestart)
	{
		Result->SetStringField(TEXT("note"),
			TEXT("Files written but the new UCLASS is not yet live. Trigger build_project (or live_coding_compile for existing-class edits) and restart the editor for UE to register the new type."));
	}
	return MCPResult(Result);
}

// ─── list_project_modules ────────────────────────────────────────────
// Enumerates the current project's native modules (name, host type,
// source path). Feed moduleName from here into create_cpp_class.
TSharedPtr<FJsonValue> FProjectHandlers::ListProjectModules(const TSharedPtr<FJsonObject>& /*Params*/)
{
	FGameProjectGenerationModule& GPM = FGameProjectGenerationModule::Get();
	const TArray<FModuleContextInfo>& Modules = GPM.GetCurrentProjectModules();

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FModuleContextInfo& M : Modules)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), M.ModuleName);
		Obj->SetStringField(TEXT("hostType"), HostTypeToString(M.ModuleType));
		Obj->SetStringField(TEXT("sourcePath"), M.ModuleSourcePath);
		Arr.Add(MakeShared<FJsonValueObject>(Obj));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("modules"), Arr);
	Result->SetNumberField(TEXT("count"), Arr.Num());
	return MCPResult(Result);
}

// ─── live_coding_compile ─────────────────────────────────────────────
// Triggers Live Coding compile (equivalent to Ctrl+Alt+F11 in the editor).
// Live Coding hot-patches method bodies of EXISTING classes without an
// editor restart — fast iteration on UFUNCTION implementations. It does
// NOT reliably pick up brand-new UCLASSes; use build_project +
// editor restart for that.
//
// Params:
//   wait    optional bool, default false. If true, blocks until compile
//           completes and returns the final result. If false, fires and
//           returns immediately with "in_progress".
TSharedPtr<FJsonValue> FProjectHandlers::LiveCodingCompile(const TSharedPtr<FJsonObject>& Params)
{
#if PLATFORM_WINDOWS
	ILiveCodingModule* Live = FModuleManager::GetModulePtr<ILiveCodingModule>(TEXT("LiveCoding"));
	if (!Live)
	{
		return MCPError(TEXT("LiveCoding module not loaded. Enable Live Coding in Editor Preferences or run build_project instead."));
	}

	if (!Live->IsEnabledByDefault() && !Live->IsEnabledForSession())
	{
		Live->EnableForSession(true);
	}

	if (!Live->CanEnableForSession())
	{
		return MCPError(FString::Printf(
			TEXT("Live Coding cannot be enabled: %s"), *Live->GetEnableErrorText().ToString()));
	}

	const bool bWait = OptionalBool(Params, TEXT("wait"), false);
	const ELiveCodingCompileFlags Flags =
		bWait ? ELiveCodingCompileFlags::WaitForCompletion : ELiveCodingCompileFlags::None;

	ELiveCodingCompileResult CompileResult = ELiveCodingCompileResult::NotStarted;
	const bool bAccepted = Live->Compile(Flags, &CompileResult);

	auto CompileResultString = [](ELiveCodingCompileResult R) -> FString
	{
		switch (R)
		{
			case ELiveCodingCompileResult::Success:            return TEXT("success");
			case ELiveCodingCompileResult::NoChanges:          return TEXT("no_changes");
			case ELiveCodingCompileResult::InProgress:         return TEXT("in_progress");
			case ELiveCodingCompileResult::CompileStillActive: return TEXT("already_compiling");
			case ELiveCodingCompileResult::NotStarted:         return TEXT("not_started");
			case ELiveCodingCompileResult::Failure:            return TEXT("failure");
			case ELiveCodingCompileResult::Cancelled:          return TEXT("cancelled");
			default:                                           return TEXT("unknown");
		}
	};

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("accepted"), bAccepted);
	Result->SetBoolField(TEXT("waited"), bWait);
	Result->SetStringField(TEXT("result"), CompileResultString(CompileResult));
	return MCPResult(Result);
#else
	return MCPError(TEXT("Live Coding is only available on Windows. Use build_project on other platforms."));
#endif
}

// ─── live_coding_status ──────────────────────────────────────────────
// Reports Live Coding availability. Useful for an agent to decide between
// live_coding_compile (fast iteration) and build_project (full UBT build).
TSharedPtr<FJsonValue> FProjectHandlers::LiveCodingStatus(const TSharedPtr<FJsonObject>& /*Params*/)
{
#if PLATFORM_WINDOWS
	ILiveCodingModule* Live = FModuleManager::GetModulePtr<ILiveCodingModule>(TEXT("LiveCoding"));
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("available"), Live != nullptr);
	if (Live)
	{
		Result->SetBoolField(TEXT("enabledByDefault"), Live->IsEnabledByDefault());
		Result->SetBoolField(TEXT("enabledForSession"), Live->IsEnabledForSession());
		Result->SetBoolField(TEXT("canEnableForSession"), Live->CanEnableForSession());
		Result->SetBoolField(TEXT("started"), Live->HasStarted());
		Result->SetBoolField(TEXT("compiling"), Live->IsCompiling());
		Result->SetStringField(TEXT("enableError"), Live->GetEnableErrorText().ToString());
	}
	return MCPResult(Result);
#else
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("available"), false);
	Result->SetStringField(TEXT("note"), TEXT("Live Coding is Windows-only."));
	return MCPResult(Result);
#endif
}
