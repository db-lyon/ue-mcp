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
#include "ILiveCodingModule.h"
#include "Kismet/KismetSystemLibrary.h"
#include "EditorValidatorSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

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
	FString Directory = OptionalString(Params, TEXT("directory"), TEXT("/Game/"));

	// Try to use the EditorValidatorSubsystem if available
	UEditorValidatorSubsystem* ValidatorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorValidatorSubsystem>() : nullptr;

	if (ValidatorSubsystem)
	{
		// Use the DataValidation console command for broad validation
		if (GEditor && GEditor->GetEditorWorldContext().World())
		{
			FString Command = FString::Printf(TEXT("DataValidation.ValidateAssets %s"), *Directory);
			UKismetSystemLibrary::ExecuteConsoleCommand(
				GEditor->GetEditorWorldContext().World(),
				Command,
				nullptr
			);
		}

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("directory"), Directory);
		Result->SetStringField(TEXT("message"), TEXT("Asset validation triggered via EditorValidatorSubsystem"));
		return MCPResult(Result);
	}
	else
	{
		// Fallback: trigger via console command
		if (GEditor && GEditor->GetEditorWorldContext().World())
		{
			UKismetSystemLibrary::ExecuteConsoleCommand(
				GEditor->GetEditorWorldContext().World(),
				TEXT("DataValidation.ValidateAssets"),
				nullptr
			);
		}

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("directory"), Directory);
		Result->SetStringField(TEXT("message"), TEXT("Asset validation triggered via console command"));
		return MCPResult(Result);
	}
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
