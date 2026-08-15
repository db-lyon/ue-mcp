// Split from EditorHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FEditorHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in EditorHandlers.cpp::RegisterHandlers.

#include "EditorHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "FileHelpers.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformFile.h"
#include "Modules/ModuleManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#if PLATFORM_WINDOWS
#include "ILiveCodingModule.h"
#endif
#include "Kismet/KismetSystemLibrary.h"
#include "EditorValidatorSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

+namespace
{
	static FString MCPDataValidationResultToString(const EDataValidationResult Result)
	{
		switch (Result)
		{
		case EDataValidationResult::Valid:
			return TEXT("valid");
		case EDataValidationResult::Invalid:
			return TEXT("invalid");
		case EDataValidationResult::NotValidated:
		default:
			return TEXT("notValidated");
		}
	}

	static TArray<TSharedPtr<FJsonValue>> MCPValidationTextArray(const TArray<FText>& Texts)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		Values.Reserve(Texts.Num());
		for (const FText& Text : Texts)
		{
			Values.Add(MakeShared<FJsonValueString>(Text.ToString()));
		}
		return Values;
	}

	static TArray<TSharedPtr<FJsonValue>> MCPValidationMessageArray(const TArray<TSharedRef<FTokenizedMessage>>& Messages)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		Values.Reserve(Messages.Num());
		for (const TSharedRef<FTokenizedMessage>& Message : Messages)
		{
			Values.Add(MakeShared<FJsonValueString>(Message->ToText().ToString()));
		}
		return Values;
	}

	static bool MCPResolveExplicitValidationAsset(
		IAssetRegistry& AssetRegistry,
		const FString& RequestedPath,
		FAssetData& OutAsset,
		FString& OutError)
	{
		const FString TrimmedPath = RequestedPath.TrimStartAndEnd();
		if (TrimmedPath.IsEmpty())
		{
			OutError = TEXT("asset path is empty");
			return false;
		}

		if (TrimmedPath.Contains(TEXT(".")) || TrimmedPath.Contains(TEXT(":")))
		{
			OutAsset = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(TrimmedPath));
			if (!OutAsset.IsValid())
			{
				OutError = FString::Printf(TEXT("asset object path was not found: %s"), *TrimmedPath);
				return false;
			}
			return true;
		}

		TArray<FAssetData> PackageAssets;
		AssetRegistry.GetAssetsByPackageName(FName(*TrimmedPath), PackageAssets);
		PackageAssets.Sort([](const FAssetData& A, const FAssetData& B)
		{
			return A.GetObjectPathString() < B.GetObjectPathString();
		});

		if (PackageAssets.Num() == 0)
		{
			OutError = FString::Printf(TEXT("asset package path was not found: %s"), *TrimmedPath);
			return false;
		}
		if (PackageAssets.Num() != 1)
		{
			OutError = FString::Printf(TEXT("asset package path is ambiguous (%d assets): %s"), PackageAssets.Num(), *TrimmedPath);
			return false;
		}

		OutAsset = PackageAssets[0];
		if (!OutAsset.IsValid())
		{
			OutError = FString::Printf(TEXT("asset package path resolved to invalid asset data: %s"), *TrimmedPath);
			return false;
		}
		return true;
	}
}

TSharedPtr<FJsonValue> FEditorHandlers::BuildLighting(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	FString Quality = OptionalString(Params, TEXT("quality"), TEXT("Preview"));

	// Map quality string to console command
	FString Command;
	if (Quality == TEXT("Preview"))
	{
		Command = TEXT("BUILD LIGHTING QUALITY=Preview");
	}
	else if (Quality == TEXT("Medium"))
	{
		Command = TEXT("BUILD LIGHTING QUALITY=Medium");
	}
	else if (Quality == TEXT("High"))
	{
		Command = TEXT("BUILD LIGHTING QUALITY=High");
	}
	else if (Quality == TEXT("Production"))
	{
		Command = TEXT("BUILD LIGHTING QUALITY=Production");
	}
	else
	{
		Command = TEXT("BUILD LIGHTING QUALITY=Preview");
	}

	UKismetSystemLibrary::ExecuteConsoleCommand(World, Command, nullptr);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("quality"), Quality);
	Result->SetStringField(TEXT("command"), Command);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Lighting build triggered (%s)"), *Quality));
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FEditorHandlers::BuildAll(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	// Execute full build: geometry, lighting, and paths
	UKismetSystemLibrary::ExecuteConsoleCommand(World, TEXT("MAP REBUILD"), nullptr);
	UKismetSystemLibrary::ExecuteConsoleCommand(World, TEXT("BUILD LIGHTING"), nullptr);
	UKismetSystemLibrary::ExecuteConsoleCommand(World, TEXT("RebuildNavigation"), nullptr);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("message"), TEXT("Build All triggered (geometry + lighting + navigation)"));
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FEditorHandlers::ValidateAssets(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor)
	{
		return MCPError(TEXT("Editor not available"));
	}

	UEditorValidatorSubsystem* ValidatorSubsystem = GEditor->GetEditorSubsystem<UEditorValidatorSubsystem>();
	if (!ValidatorSubsystem)
	{
		return MCPError(TEXT("EditorValidatorSubsystem is not available"));
	}

	const bool bHasAssetPaths = Params->HasField(TEXT("assetPaths"));
	const bool bHasAssetPath = Params->HasField(TEXT("assetPath"));
	const bool bHasDirectory = Params->HasField(TEXT("directory"));
	if ((bHasAssetPaths || bHasAssetPath) && bHasDirectory)
	{
		return MCPError(TEXT("Specify either assetPaths/assetPath or directory, not both"));
	}
	if (bHasAssetPaths && bHasAssetPath)
	{
		return MCPError(TEXT("Specify only one of assetPaths or assetPath"));
	}

	FAssetRegistryModule* AssetRegistryModule = FModuleManager::LoadModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry"));
	if (!AssetRegistryModule || !AssetRegistryModule->IsValid())
	{
		return MCPError(TEXT("AssetRegistry is not available"));
	}
	IAssetRegistry* AssetRegistry = AssetRegistryModule->TryGet();
	if (!AssetRegistry)
	{
		return MCPError(TEXT("AssetRegistry is shutting down"));
	}

	TArray<FAssetData> AssetDataList;
	FString SelectionMode;
	FString Selection;
	if (bHasAssetPaths || bHasAssetPath)
	{
		TArray<FString> RequestedPaths;
		if (bHasAssetPaths)
		{
			const TArray<TSharedPtr<FJsonValue>>* PathValues = nullptr;
			if (!Params->TryGetArrayField(TEXT("assetPaths"), PathValues) || !PathValues || PathValues->Num() == 0)
			{
				return MCPError(TEXT("assetPaths must be a non-empty array of exact asset paths"));
			}
			RequestedPaths.Reserve(PathValues->Num());
			for (const TSharedPtr<FJsonValue>& PathValue : *PathValues)
			{
				FString RequestedPath;
				if (!PathValue.IsValid() || !PathValue->TryGetString(RequestedPath) || RequestedPath.TrimStartAndEnd().IsEmpty())
				{
					return MCPError(TEXT("Every assetPaths entry must be a non-empty string"));
				}
				RequestedPaths.Add(RequestedPath);
			}
		}
		else
		{
			FString RequestedPath;
			if (!Params->TryGetStringField(TEXT("assetPath"), RequestedPath) || RequestedPath.TrimStartAndEnd().IsEmpty())
			{
				return MCPError(TEXT("assetPath must be a non-empty exact package or object path"));
			}
			RequestedPaths.Add(RequestedPath);
		}

		TSet<FString> SeenObjectPaths;
		for (const FString& RequestedPath : RequestedPaths)
		{
			FAssetData ResolvedAsset;
			FString ResolveError;
			if (!MCPResolveExplicitValidationAsset(*AssetRegistry, RequestedPath, ResolvedAsset, ResolveError))
			{
				return MCPError(ResolveError);
			}

			const FString ObjectPath = ResolvedAsset.GetObjectPathString();
			if (!SeenObjectPaths.Contains(ObjectPath))
			{
				SeenObjectPaths.Add(ObjectPath);
				AssetDataList.Add(ResolvedAsset);
			}
		}
		SelectionMode = TEXT("explicit");
		Selection = FString::Join(RequestedPaths, TEXT(","));
	}
	else
	{
		const FString Directory = OptionalString(Params, TEXT("directory"), TEXT("/Game/")).TrimStartAndEnd();
		if (Directory.IsEmpty() || !Directory.StartsWith(TEXT("/")))
		{
			return MCPError(TEXT("directory must be a non-empty package path such as /Game/"));
		}

		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*Directory));
		Filter.bRecursivePaths = true;
		if (!AssetRegistry->GetAssets(Filter, AssetDataList))
		{
			return MCPError(FString::Printf(TEXT("AssetRegistry could not enumerate directory: %s"), *Directory));
		}
		SelectionMode = TEXT("directory");
		Selection = Directory;
	}

	AssetDataList.Sort([](const FAssetData& A, const FAssetData& B)
	{
		return A.GetObjectPathString() < B.GetObjectPathString();
	});

	FValidateAssetsSettings Settings;
	Settings.ValidationUsecase = IsRunningCommandlet() ? EDataValidationUsecase::Commandlet : EDataValidationUsecase::Manual;
	Settings.bCollectPerAssetDetails = true;
	Settings.bShowIfNoFailures = false;
	Settings.bSilent = true;
	FValidateAssetsResults ValidationResults;
	ValidatorSubsystem->ValidateAssetsWithSettings(AssetDataList, Settings, ValidationResults);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("selectionMode"), SelectionMode);
	Result->SetStringField(TEXT("selection"), Selection);
	if (SelectionMode == TEXT("directory"))
	{
		// Preserve the legacy response field for { directory: ... } callers.
		Result->SetStringField(TEXT("directory"), Selection);
	}
	Result->SetStringField(TEXT("validationUsecase"), Settings.ValidationUsecase == EDataValidationUsecase::Commandlet ? TEXT("commandlet") : TEXT("manual"));
	Result->SetNumberField(TEXT("requested"), ValidationResults.NumRequested);
	Result->SetNumberField(TEXT("checked"), ValidationResults.NumChecked);
	Result->SetNumberField(TEXT("valid"), ValidationResults.NumValid);
	Result->SetNumberField(TEXT("invalid"), ValidationResults.NumInvalid);
	Result->SetNumberField(TEXT("skipped"), ValidationResults.NumSkipped);
	Result->SetNumberField(TEXT("warnings"), ValidationResults.NumWarnings);
	Result->SetNumberField(TEXT("unableToValidate"), ValidationResults.NumUnableToValidate);
	Result->SetNumberField(TEXT("externalObjects"), ValidationResults.NumExternalObjects);
	Result->SetBoolField(TEXT("assetLimitReached"), ValidationResults.bAssetLimitReached);
	const FString OverallResult = ValidationResults.NumInvalid > 0
		? TEXT("invalid")
		: (ValidationResults.NumUnableToValidate > 0 || ValidationResults.NumRequested == 0 || ValidationResults.NumChecked == 0
			? TEXT("notValidated")
			: (ValidationResults.NumWarnings > 0 ? TEXT("warning") : TEXT("valid")));
	Result->SetStringField(TEXT("result"), OverallResult);

	TArray<FString> DetailKeys;
	ValidationResults.AssetsDetails.GetKeys(DetailKeys);
	DetailKeys.Sort();
	TArray<TSharedPtr<FJsonValue>> DetailsArray;
	DetailsArray.Reserve(DetailKeys.Num());
	for (const FString& DetailKey : DetailKeys)
	{
		const FValidateAssetsDetails* Details = ValidationResults.AssetsDetails.Find(DetailKey);
		if (!Details)
		{
			continue;
		}
		auto DetailObject = MakeShared<FJsonObject>();
		DetailObject->SetStringField(TEXT("objectPath"), DetailKey);
		DetailObject->SetStringField(TEXT("packageName"), Details->PackageName.ToString());
		DetailObject->SetStringField(TEXT("assetName"), Details->AssetName.ToString());
		DetailObject->SetStringField(TEXT("result"), MCPDataValidationResultToString(Details->Result));
		DetailObject->SetArrayField(TEXT("errors"), MCPValidationTextArray(Details->ValidationErrors));
		DetailObject->SetArrayField(TEXT("warnings"), MCPValidationTextArray(Details->ValidationWarnings));
		DetailObject->SetArrayField(TEXT("messages"), MCPValidationMessageArray(Details->ValidationMessages));
		DetailsArray.Add(MakeShared<FJsonValueObject>(DetailObject));
	}
	Result->SetArrayField(TEXT("assets"), DetailsArray);
	Result->SetArrayField(TEXT("validatorMessages"), MCPValidationMessageArray(ValidationResults.ValidatorMessages));
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Validated %d assets"), ValidationResults.NumRequested));
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FEditorHandlers::CookContent(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	FString Platform = OptionalString(Params, TEXT("platform"), TEXT("Windows"));

	FString Command = FString::Printf(TEXT("CookOnTheFly -TargetPlatform=%s"), *Platform);
	UKismetSystemLibrary::ExecuteConsoleCommand(World, Command, nullptr);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("platform"), Platform);
	Result->SetStringField(TEXT("command"), Command);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Cook triggered for %s"), *Platform));
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FEditorHandlers::HotReload(const TSharedPtr<FJsonObject>& Params)
{
#if PLATFORM_WINDOWS
	ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
	if (LiveCoding && LiveCoding->IsEnabledForSession())
	{
		if (LiveCoding->IsCompiling())
		{
			auto Result = MCPSuccess();
			Result->SetStringField(TEXT("message"), TEXT("Live Coding compile already in progress"));
			return MCPResult(Result);
		}

		LiveCoding->EnableByDefault(true);
		LiveCoding->Compile();
		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("message"), TEXT("Live Coding compile triggered"));
		return MCPResult(Result);
	}
	else
#endif
	{
		// Live Coding not available (or not on Windows) - fall back to console command
		UWorld* World = GetEditorWorld();
		if (World)
		{
			UKismetSystemLibrary::ExecuteConsoleCommand(World, TEXT("LiveCoding.Compile"), nullptr);
			auto Result = MCPSuccess();
			Result->SetStringField(TEXT("message"), TEXT("Hot reload triggered via console command (Live Coding module not active in session)"));
			return MCPResult(Result);
		}
		else
		{
			return MCPError(TEXT("Neither Live Coding module nor editor world available for hot reload"));
		}
	}
}


TSharedPtr<FJsonValue> FEditorHandlers::BuildGeometry(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	GEditor->Exec(World, TEXT("MAP REBUILD"));

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("message"), TEXT("Geometry rebuild triggered"));
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FEditorHandlers::BuildHlod(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	GEditor->Exec(World, TEXT("BuildHLOD"));

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("message"), TEXT("HLOD build triggered"));
	return MCPResult(Result);
}


// #14: Build project via UnrealBuildTool
TSharedPtr<FJsonValue> FEditorHandlers::BuildProject(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor)
	{
		return MCPError(TEXT("Editor not available"));
	}

	FString Configuration = OptionalString(Params, TEXT("configuration"), TEXT("Development"));
	FString Platform = OptionalString(Params, TEXT("platform"), TEXT("Win64"));
	bool bClean = OptionalBool(Params, TEXT("clean"), false);

	// Build the project by invoking the engine's build tool
	// Use the project path from the running editor
	FString ProjectPath = FPaths::GetProjectFilePath();
	if (ProjectPath.IsEmpty())
	{
		return MCPError(TEXT("No project file path available"));
	}

	// Find UnrealBuildTool
	FString EngineDir = FPaths::EngineDir();
	FString UBTPath;

#if PLATFORM_WINDOWS
	UBTPath = FPaths::Combine(EngineDir, TEXT("Binaries"), TEXT("DotNET"), TEXT("UnrealBuildTool"), TEXT("UnrealBuildTool.exe"));
	if (!FPaths::FileExists(UBTPath))
	{
		// Try legacy path
		UBTPath = FPaths::Combine(EngineDir, TEXT("Binaries"), TEXT("DotNET"), TEXT("UnrealBuildTool.exe"));
	}
#else
	UBTPath = FPaths::Combine(EngineDir, TEXT("Binaries"), TEXT("DotNET"), TEXT("UnrealBuildTool"), TEXT("UnrealBuildTool"));
#endif

	if (!FPaths::FileExists(UBTPath))
	{
		return MCPError(FString::Printf(TEXT("UnrealBuildTool not found at '%s'"), *UBTPath));
	}

	// Build the command line
	FString ProjectName = FPaths::GetBaseFilename(ProjectPath);
	FString Args = FString::Printf(
		TEXT("%sEditor %s %s -Project=\"%s\" -WaitMutex -FromMsBuild"),
		*ProjectName, *Platform, *Configuration, *ProjectPath);

	if (bClean)
	{
		Args += TEXT(" -Clean");
	}

	// Launch the process asynchronously
	FProcHandle ProcHandle = FPlatformProcess::CreateProc(
		*UBTPath, *Args, true, false, false, nullptr, 0, nullptr, nullptr);

	if (!ProcHandle.IsValid())
	{
		return MCPError(TEXT("Failed to launch UnrealBuildTool"));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("ubtPath"), UBTPath);
	Result->SetStringField(TEXT("args"), Args);
	Result->SetStringField(TEXT("configuration"), Configuration);
	Result->SetStringField(TEXT("platform"), Platform);
	Result->SetStringField(TEXT("note"), TEXT("Build launched asynchronously. Check output log for progress."));
	return MCPResult(Result);
}

// #49: Generate VS project files


// #49: Generate VS project files
TSharedPtr<FJsonValue> FEditorHandlers::GenerateProjectFiles(const TSharedPtr<FJsonObject>& Params)
{
	FString ProjectPath = FPaths::GetProjectFilePath();
	if (ProjectPath.IsEmpty())
	{
		return MCPError(TEXT("No project file path available"));
	}

	// Find the GenerateProjectFiles script
	FString EngineDir = FPaths::EngineDir();
	FString ScriptPath;
#if PLATFORM_WINDOWS
	ScriptPath = FPaths::Combine(EngineDir, TEXT("Build"), TEXT("BatchFiles"), TEXT("GenerateProjectFiles.bat"));
#elif PLATFORM_MAC
	ScriptPath = FPaths::Combine(EngineDir, TEXT("Build"), TEXT("BatchFiles"), TEXT("Mac"), TEXT("GenerateProjectFiles.sh"));
#else
	ScriptPath = FPaths::Combine(EngineDir, TEXT("Build"), TEXT("BatchFiles"), TEXT("Linux"), TEXT("GenerateProjectFiles.sh"));
#endif

	if (!FPaths::FileExists(ScriptPath))
	{
		// Alternative: use UnrealBuildTool directly with -projectfiles flag
		FString UBTPath;
#if PLATFORM_WINDOWS
		UBTPath = FPaths::Combine(EngineDir, TEXT("Binaries"), TEXT("DotNET"), TEXT("UnrealBuildTool"), TEXT("UnrealBuildTool.exe"));
		if (!FPaths::FileExists(UBTPath))
		{
			UBTPath = FPaths::Combine(EngineDir, TEXT("Binaries"), TEXT("DotNET"), TEXT("UnrealBuildTool.exe"));
		}
#else
		UBTPath = FPaths::Combine(EngineDir, TEXT("Binaries"), TEXT("DotNET"), TEXT("UnrealBuildTool"), TEXT("UnrealBuildTool"));
#endif
		if (!FPaths::FileExists(UBTPath))
		{
			return MCPError(TEXT("Neither GenerateProjectFiles script nor UnrealBuildTool found"));
		}

		FString Args = FString::Printf(TEXT("-projectfiles -project=\"%s\" -game -rocket -progress"), *ProjectPath);
		FProcHandle ProcHandle = FPlatformProcess::CreateProc(
			*UBTPath, *Args, true, false, false, nullptr, 0, nullptr, nullptr);

		if (!ProcHandle.IsValid())
		{
			return MCPError(TEXT("Failed to launch UnrealBuildTool for project file generation"));
		}

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("tool"), UBTPath);
		Result->SetStringField(TEXT("args"), Args);
		Result->SetStringField(TEXT("projectPath"), ProjectPath);
		Result->SetStringField(TEXT("note"), TEXT("Project file generation launched. Check output log for progress."));
		return MCPResult(Result);
	}
	else
	{
		FString Args = FString::Printf(TEXT("-project=\"%s\" -game"), *ProjectPath);
		FProcHandle ProcHandle = FPlatformProcess::CreateProc(
			*ScriptPath, *Args, true, false, false, nullptr, 0, nullptr, nullptr);

		if (!ProcHandle.IsValid())
		{
			return MCPError(TEXT("Failed to launch GenerateProjectFiles"));
		}

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("tool"), ScriptPath);
		Result->SetStringField(TEXT("args"), Args);
		Result->SetStringField(TEXT("projectPath"), ProjectPath);
		Result->SetStringField(TEXT("note"), TEXT("Project file generation launched. Check output log for progress."));
		return MCPResult(Result);
	}
}
