#include "EditorHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Editor/EditorEngine.h"
#include "Editor.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "IPythonScriptPlugin.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ConfigContext.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "LevelEditorViewport.h"
#include "UnrealClient.h"
#include "Slate/SceneViewport.h"
#include "HAL/PlatformMemory.h"
#include "Misc/App.h"
#include "Logging/MessageLog.h"
#include "HighResScreenshot.h"
#include "Misc/OutputDeviceRedirector.h"
#include "FileHelpers.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "EditorValidatorSubsystem.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"
#if PLATFORM_WINDOWS
#include "ILiveCodingModule.h"
#endif
#include "LevelEditorSubsystem.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "DesktopPlatformModule.h"
#include "HAL/PlatformProcess.h"
#include "Misc/MonitoredProcess.h"

void FEditorHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	// Install log capture ring buffer (#82)
	FMCPLogCapture::Get().Install();

	Registry.RegisterHandler(TEXT("execute_command"), &ExecuteCommand);
	Registry.RegisterHandler(TEXT("execute_python"), &ExecutePython);
	Registry.RegisterHandler(TEXT("set_property"), &SetProperty);
	Registry.RegisterHandler(TEXT("set_config"), &SetConfig);
	Registry.RegisterHandler(TEXT("read_config"), &ReadConfig);
	Registry.RegisterHandler(TEXT("get_viewport_info"), &GetViewportInfo);
	Registry.RegisterHandler(TEXT("get_editor_performance_stats"), &GetEditorPerformanceStats);
	Registry.RegisterHandler(TEXT("get_output_log"), &GetOutputLog);
	Registry.RegisterHandler(TEXT("search_log"), &SearchLog);
	Registry.RegisterHandler(TEXT("get_message_log"), &GetMessageLog);
	Registry.RegisterHandler(TEXT("get_build_status"), &GetBuildStatus);
	Registry.RegisterHandler(TEXT("pie_control"), &PieControl);
	Registry.RegisterHandler(TEXT("capture_screenshot"), &CaptureScreenshot);
	Registry.RegisterHandler(TEXT("set_viewport_camera"), &SetViewportCamera);
	Registry.RegisterHandler(TEXT("undo"), &Undo);
	Registry.RegisterHandler(TEXT("redo"), &Redo);
	Registry.RegisterHandler(TEXT("reload_handlers"), &ReloadHandlers);
	Registry.RegisterHandler(TEXT("save_asset"), &SaveAsset);
	Registry.RegisterHandler(TEXT("save_all"), &SaveAll);
	Registry.RegisterHandler(TEXT("get_crash_reports"), &GetCrashReports);
	Registry.RegisterHandler(TEXT("read_editor_log"), &ReadEditorLog);
	Registry.RegisterHandler(TEXT("pie_get_runtime_value"), &PieGetRuntimeValue);
	Registry.RegisterHandler(TEXT("build_lighting"), &BuildLighting);
	Registry.RegisterHandler(TEXT("build_all"), &BuildAll);
	Registry.RegisterHandler(TEXT("validate_assets"), &ValidateAssets);
	Registry.RegisterHandler(TEXT("cook_content"), &CookContent);
	Registry.RegisterHandler(TEXT("focus_viewport_on_actor"), &FocusViewportOnActor);
	Registry.RegisterHandler(TEXT("hot_reload"), &HotReload);
	Registry.RegisterHandler(TEXT("create_new_level"), &CreateNewLevel);
	Registry.RegisterHandler(TEXT("save_current_level"), &SaveCurrentLevel);
	Registry.RegisterHandler(TEXT("open_asset"), &OpenAsset);
	// Aliases for TS tool compatibility
	Registry.RegisterHandler(TEXT("get_runtime_value"), &PieGetRuntimeValue);
	// New handlers
	Registry.RegisterHandler(TEXT("run_stat_command"), &RunStatCommand);
	Registry.RegisterHandler(TEXT("set_scalability"), &SetScalability);
	Registry.RegisterHandler(TEXT("build_geometry"), &BuildGeometry);
	Registry.RegisterHandler(TEXT("build_hlod"), &BuildHlod);
	Registry.RegisterHandler(TEXT("list_crashes"), &ListCrashes);
	Registry.RegisterHandler(TEXT("get_crash_info"), &GetCrashInfo);
	Registry.RegisterHandler(TEXT("check_for_crashes"), &CheckForCrashes);
	// #14: Build project
	Registry.RegisterHandler(TEXT("build_project"), &BuildProject);
	// #49: Generate project files
	Registry.RegisterHandler(TEXT("generate_project_files"), &GenerateProjectFiles);
	// #126: fast-forward PIE game time
	Registry.RegisterHandler(TEXT("set_pie_time_scale"), &SetPieTimeScale);
}

TSharedPtr<FJsonValue> FEditorHandlers::ExecuteCommand(const TSharedPtr<FJsonObject>& Params)
{
	FString Command;
	if (auto Err = RequireString(Params, TEXT("command"), Command)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	UKismetSystemLibrary::ExecuteConsoleCommand(World, Command, nullptr);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("command"), Command);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::ExecutePython(const TSharedPtr<FJsonObject>& Params)
{
	FString Code;
	if (auto Err = RequireString(Params, TEXT("code"), Code)) return Err;

	IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
	if (!PythonPlugin || !PythonPlugin->IsPythonAvailable())
	{
		return MCPError(TEXT("Python scripting is not available"));
	}

	FPythonCommandEx PythonCommand;
	PythonCommand.Command = Code;
	PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
	PythonCommand.FileExecutionScope = EPythonFileExecutionScope::Public;

	bool bSuccess = PythonPlugin->ExecPythonCommandEx(PythonCommand);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bSuccess);
	Result->SetStringField(TEXT("result"), PythonCommand.CommandResult);

	TArray<TSharedPtr<FJsonValue>> LogArray;
	for (const FPythonLogOutputEntry& Entry : PythonCommand.LogOutput)
	{
		TSharedPtr<FJsonObject> LogEntry = MakeShared<FJsonObject>();
		LogEntry->SetStringField(TEXT("type"), LexToString(Entry.Type));
		LogEntry->SetStringField(TEXT("output"), Entry.Output);
		LogArray.Add(MakeShared<FJsonValueObject>(LogEntry));
	}
	Result->SetArrayField(TEXT("log_output"), LogArray);

	FString CombinedOutput;
	for (const FPythonLogOutputEntry& Entry : PythonCommand.LogOutput)
	{
		if (!CombinedOutput.IsEmpty()) CombinedOutput += TEXT("\n");
		CombinedOutput += Entry.Output;
	}
	Result->SetStringField(TEXT("output"), CombinedOutput);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::SetProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	// Load asset
	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	// Get property
	FProperty* Property = Asset->GetClass()->FindPropertyByName(*PropertyName);
	if (!Property)
	{
		return MCPError(FString::Printf(TEXT("Property not found: %s"), *PropertyName));
	}

	// Get value from params
	TSharedPtr<FJsonValue> ValueJsonRef = Params->TryGetField(TEXT("value"));
	if (!ValueJsonRef.IsValid())
	{
		return MCPError(TEXT("Missing 'value' parameter"));
	}

	// Set property value — use ImportText_Direct for full UE text format support (#29)
	// This handles nested struct arrays, FVector, FGameplayTag, TArray<>, etc.
	void* PropertyValue = Property->ContainerPtrToValuePtr<void>(Asset);

	FString ValueStr;
	if (ValueJsonRef->Type == EJson::String)
	{
		ValueStr = ValueJsonRef->AsString();
	}
	else if (ValueJsonRef->Type == EJson::Boolean)
	{
		ValueStr = ValueJsonRef->AsBool() ? TEXT("true") : TEXT("false");
	}
	else if (ValueJsonRef->Type == EJson::Number)
	{
		ValueStr = FString::SanitizeFloat(ValueJsonRef->AsNumber());
	}
	else
	{
		// For objects/arrays, serialize back to string for ImportText
		// This lets callers pass UE text format as a string value
		return MCPError(TEXT("Value must be a string (UE text format), number, or boolean. For complex types, pass UE text format as a string (e.g. '((Key=1,Value=\"Hello\"))' for struct arrays)."));
	}

	const TCHAR* ImportResult = Property->ImportText_Direct(*ValueStr, PropertyValue, Asset, PPF_None);
	if (!ImportResult)
	{
		return MCPError(FString::Printf(TEXT("ImportText failed for property '%s' with value '%s'. Check UE text format."), *PropertyName, *ValueStr));
	}

	Asset->MarkPackageDirty();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::SetConfig(const TSharedPtr<FJsonObject>& Params)
{
	FString ConfigName;
	if (!Params->TryGetStringField(TEXT("configName"), ConfigName))
	{
		Params->TryGetStringField(TEXT("configFile"), ConfigName);
	}
	FString Section;
	if (auto Err = RequireString(Params, TEXT("section"), Section)) return Err;
	FString Key;
	if (auto Err = RequireString(Params, TEXT("key"), Key)) return Err;
	FString Value;
	Params->TryGetStringField(TEXT("value"), Value);

	if (ConfigName.IsEmpty())
	{
		ConfigName = TEXT("DefaultEngine.ini");
	}
	else if (!ConfigName.EndsWith(TEXT(".ini")))
	{
		ConfigName = FString::Printf(TEXT("Default%s.ini"), *ConfigName);
	}

	FString ConfigDir = FPaths::ProjectConfigDir();
	FString IniPath = FPaths::Combine(ConfigDir, ConfigName);

	// Capture previous value for rollback and idempotency
	FString PrevValue;
	bool bHadPrev = GConfig->GetString(*Section, *Key, PrevValue, IniPath);
	if (bHadPrev && PrevValue == Value)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("configFile"), ConfigName);
		Noop->SetStringField(TEXT("section"), Section);
		Noop->SetStringField(TEXT("key"), Key);
		Noop->SetStringField(TEXT("value"), Value);
		return MCPResult(Noop);
	}

	GConfig->SetString(*Section, *Key, *Value, IniPath);
	GConfig->Flush(false, IniPath);

	// #106: GConfig->Flush sometimes does not persist newly-created sections
	// for DeveloperSettings-backed classes. Verify on disk; if the section or
	// key is missing, fall back to direct file write.
	auto VerifyOnDisk = [&]() -> bool
	{
		FString FileContents;
		if (!FFileHelper::LoadFileToString(FileContents, *IniPath))
		{
			return false;
		}
		const FString SectionHeader = FString::Printf(TEXT("[%s]"), *Section);
		int32 SectionIdx = FileContents.Find(SectionHeader);
		if (SectionIdx == INDEX_NONE) return false;
		// Find next section boundary
		int32 NextSection = FileContents.Find(TEXT("\n["), ESearchCase::CaseSensitive, ESearchDir::FromStart, SectionIdx + SectionHeader.Len());
		int32 EndIdx = NextSection == INDEX_NONE ? FileContents.Len() : NextSection;
		FString SectionBody = FileContents.Mid(SectionIdx, EndIdx - SectionIdx);
		return SectionBody.Contains(FString::Printf(TEXT("%s="), *Key));
	};

	bool bPersisted = VerifyOnDisk();
	if (!bPersisted)
	{
		// Direct-write fallback. Load file (create if missing), ensure section exists, upsert key=value.
		FString FileContents;
		if (!FFileHelper::LoadFileToString(FileContents, *IniPath))
		{
			FileContents = TEXT("");
		}

		TArray<FString> Lines;
		FileContents.ParseIntoArrayLines(Lines, /*CullEmpty*/ false);

		const FString SectionHeader = FString::Printf(TEXT("[%s]"), *Section);
		const FString KVLine = FString::Printf(TEXT("%s=%s"), *Key, *Value);

		int32 SectionIdx = INDEX_NONE;
		int32 SectionEnd = Lines.Num();
		for (int32 i = 0; i < Lines.Num(); ++i)
		{
			if (Lines[i].TrimStartAndEnd() == SectionHeader)
			{
				SectionIdx = i;
				SectionEnd = Lines.Num();
				for (int32 j = i + 1; j < Lines.Num(); ++j)
				{
					FString T = Lines[j].TrimStartAndEnd();
					if (T.StartsWith(TEXT("[")) && T.EndsWith(TEXT("]")))
					{
						SectionEnd = j;
						break;
					}
				}
				break;
			}
		}

		if (SectionIdx == INDEX_NONE)
		{
			if (Lines.Num() > 0 && !Lines.Last().TrimStartAndEnd().IsEmpty())
			{
				Lines.Add(TEXT(""));
			}
			Lines.Add(SectionHeader);
			Lines.Add(KVLine);
			Lines.Add(TEXT(""));
		}
		else
		{
			bool bReplaced = false;
			const FString KeyPrefix = FString::Printf(TEXT("%s="), *Key);
			for (int32 i = SectionIdx + 1; i < SectionEnd; ++i)
			{
				if (Lines[i].StartsWith(KeyPrefix))
				{
					Lines[i] = KVLine;
					bReplaced = true;
					break;
				}
			}
			if (!bReplaced)
			{
				int32 Insert = SectionEnd;
				while (Insert > SectionIdx + 1 && Lines[Insert - 1].TrimStartAndEnd().IsEmpty()) Insert--;
				Lines.Insert(KVLine, Insert);
			}
		}

		FString Out = FString::Join(Lines, TEXT("\n"));
		if (!Out.EndsWith(TEXT("\n"))) Out += TEXT("\n");
		FFileHelper::SaveStringToFile(Out, *IniPath);
		GConfig->LoadFile(IniPath);
		bPersisted = true;
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("configFile"), ConfigName);
	Result->SetStringField(TEXT("section"), Section);
	Result->SetStringField(TEXT("key"), Key);
	Result->SetStringField(TEXT("value"), Value);
	Result->SetBoolField(TEXT("persisted"), bPersisted);

	// Rollback: self-inverse with previous value (only if we had a previous value)
	if (bHadPrev)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("configFile"), ConfigName);
		Payload->SetStringField(TEXT("section"), Section);
		Payload->SetStringField(TEXT("key"), Key);
		Payload->SetStringField(TEXT("value"), PrevValue);
		MCPSetRollback(Result, TEXT("set_config"), Payload);
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::ReadConfig(const TSharedPtr<FJsonObject>& Params)
{
	FString ConfigName;
	if (!Params->TryGetStringField(TEXT("configFile"), ConfigName))
	{
		Params->TryGetStringField(TEXT("configName"), ConfigName);
	}
	FString Section;
	if (auto Err = RequireString(Params, TEXT("section"), Section)) return Err;
	FString Key;
	if (auto Err = RequireString(Params, TEXT("key"), Key)) return Err;

	if (ConfigName.IsEmpty())
	{
		ConfigName = TEXT("DefaultEngine.ini");
	}
	else if (!ConfigName.EndsWith(TEXT(".ini")))
	{
		ConfigName = FString::Printf(TEXT("Default%s.ini"), *ConfigName);
	}

	FString ConfigDir = FPaths::ProjectConfigDir();
	FString IniPath = FPaths::Combine(ConfigDir, ConfigName);

	FString Value;
	bool bFound = GConfig->GetString(*Section, *Key, Value, IniPath);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("configFile"), ConfigName);
	Result->SetStringField(TEXT("section"), Section);
	Result->SetStringField(TEXT("key"), Key);
	Result->SetBoolField(TEXT("found"), bFound);
	if (bFound)
	{
		Result->SetStringField(TEXT("value"), Value);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::GetViewportInfo(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor)
	{
		return MCPError(TEXT("Editor not available"));
	}

	FLevelEditorViewportClient* ViewportClient = GCurrentLevelEditingViewportClient;
	if (!ViewportClient)
	{
		// Try to get from level viewport clients list
		const TArray<FLevelEditorViewportClient*>& ViewportClients = GEditor->GetLevelViewportClients();
		if (ViewportClients.Num() > 0)
		{
			ViewportClient = ViewportClients[0];
		}
	}

	if (!ViewportClient)
	{
		return MCPError(TEXT("No viewport client available"));
	}

	FVector Location = ViewportClient->GetViewLocation();
	FRotator Rotation = ViewportClient->GetViewRotation();
	float FOV = ViewportClient->ViewFOV;

	auto Result = MCPSuccess();

	TSharedPtr<FJsonObject> LocationObj = MakeShared<FJsonObject>();
	LocationObj->SetNumberField(TEXT("x"), Location.X);
	LocationObj->SetNumberField(TEXT("y"), Location.Y);
	LocationObj->SetNumberField(TEXT("z"), Location.Z);
	Result->SetObjectField(TEXT("location"), LocationObj);

	TSharedPtr<FJsonObject> RotationObj = MakeShared<FJsonObject>();
	RotationObj->SetNumberField(TEXT("pitch"), Rotation.Pitch);
	RotationObj->SetNumberField(TEXT("yaw"), Rotation.Yaw);
	RotationObj->SetNumberField(TEXT("roll"), Rotation.Roll);
	Result->SetObjectField(TEXT("rotation"), RotationObj);

	Result->SetNumberField(TEXT("fov"), FOV);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::GetEditorPerformanceStats(const TSharedPtr<FJsonObject>& Params)
{
	// FPS from delta time
	double DeltaTime = FApp::GetDeltaTime();
	double FPS = (DeltaTime > 0.0) ? (1.0 / DeltaTime) : 0.0;

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("fps"), FPS);
	Result->SetNumberField(TEXT("deltaTime"), DeltaTime);

	// Memory stats
	FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
	TSharedPtr<FJsonObject> MemoryObj = MakeShared<FJsonObject>();
	MemoryObj->SetNumberField(TEXT("usedPhysical"), static_cast<double>(MemStats.UsedPhysical));
	MemoryObj->SetNumberField(TEXT("availablePhysical"), static_cast<double>(MemStats.AvailablePhysical));
	MemoryObj->SetNumberField(TEXT("usedVirtual"), static_cast<double>(MemStats.UsedVirtual));
	MemoryObj->SetNumberField(TEXT("availableVirtual"), static_cast<double>(MemStats.AvailableVirtual));
	Result->SetObjectField(TEXT("memory"), MemoryObj);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::GetOutputLog(const TSharedPtr<FJsonObject>& Params)
{
	int32 MaxLines = OptionalInt(Params, TEXT("maxLines"), 100);
	FString Filter = OptionalString(Params, TEXT("filter"));
	FString Category = OptionalString(Params, TEXT("category"));

	// Read from ring-buffer log capture (#82)
	TArray<FMCPLogCapture::FMCPLogLine> RecentLines = FMCPLogCapture::Get().GetRecentLines(MaxLines * 2); // over-fetch for filtering

	TArray<TSharedPtr<FJsonValue>> LinesArray;
	for (const FMCPLogCapture::FMCPLogLine& Line : RecentLines)
	{
		if (!Filter.IsEmpty() && !Line.Message.Contains(Filter, ESearchCase::IgnoreCase))
		{
			continue;
		}
		if (!Category.IsEmpty() && !Line.Category.Contains(Category, ESearchCase::IgnoreCase))
		{
			continue;
		}

		TSharedPtr<FJsonObject> LineObj = MakeShared<FJsonObject>();
		LineObj->SetStringField(TEXT("message"), Line.Message);
		LineObj->SetStringField(TEXT("category"), Line.Category);
		LineObj->SetStringField(TEXT("verbosity"), Line.Verbosity);
		LinesArray.Add(MakeShared<FJsonValueObject>(LineObj));

		if (LinesArray.Num() >= MaxLines) break;
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("lines"), LinesArray);
	Result->SetNumberField(TEXT("lineCount"), LinesArray.Num());
	Result->SetNumberField(TEXT("maxLines"), MaxLines);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::SearchLog(const TSharedPtr<FJsonObject>& Params)
{
	FString Query;
	if (auto Err = RequireString(Params, TEXT("query"), Query)) return Err;

	int32 MaxResults = OptionalInt(Params, TEXT("maxResults"), 100);

	// Search ring-buffer log capture (#82)
	TArray<FMCPLogCapture::FMCPLogLine> Matches = FMCPLogCapture::Get().Search(Query, MaxResults);

	TArray<TSharedPtr<FJsonValue>> MatchesArray;
	for (const FMCPLogCapture::FMCPLogLine& Line : Matches)
	{
		TSharedPtr<FJsonObject> MatchObj = MakeShared<FJsonObject>();
		MatchObj->SetStringField(TEXT("message"), Line.Message);
		MatchObj->SetStringField(TEXT("category"), Line.Category);
		MatchObj->SetStringField(TEXT("verbosity"), Line.Verbosity);
		MatchesArray.Add(MakeShared<FJsonValueObject>(MatchObj));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("matches"), MatchesArray);
	Result->SetNumberField(TEXT("matchCount"), MatchesArray.Num());
	Result->SetStringField(TEXT("query"), Query);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::GetMessageLog(const TSharedPtr<FJsonObject>& Params)
{
	// FMessageLog does not expose a simple API to read back entries in C++.
	// Return success with an empty messages array as a baseline implementation.
	auto Result = MCPSuccess();
	TArray<TSharedPtr<FJsonValue>> MessagesArray;
	Result->SetArrayField(TEXT("messages"), MessagesArray);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::GetBuildStatus(const TSharedPtr<FJsonObject>& Params)
{
	// Basic build status - report as idle since we cannot easily query
	// the live compilation state from within the editor module.
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("status"), TEXT("idle"));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::PieControl(const TSharedPtr<FJsonObject>& Params)
{
	FString Action;
	if (auto Err = RequireString(Params, TEXT("action"), Action)) return Err;

	if (!GEditor)
	{
		return MCPError(TEXT("Editor not available"));
	}

	auto Result = MCPSuccess();

	if (Action == TEXT("status"))
	{
		bool bIsPlaying = (GEditor->PlayWorld != nullptr);
		Result->SetBoolField(TEXT("isPlaying"), bIsPlaying);
		Result->SetStringField(TEXT("action"), Action);
	}
	else if (Action == TEXT("start"))
	{
		if (GEditor->PlayWorld != nullptr)
		{
			return MCPError(TEXT("PIE session already active"));
		}

		FRequestPlaySessionParams SessionParams;
		GEditor->RequestPlaySession(SessionParams);
		Result->SetStringField(TEXT("action"), Action);
	}
	else if (Action == TEXT("stop"))
	{
		if (GEditor->PlayWorld == nullptr)
		{
			return MCPError(TEXT("No PIE session active"));
		}

		GEditor->RequestEndPlayMap();
		Result->SetStringField(TEXT("action"), Action);
	}
	else
	{
		return MCPError(FString::Printf(TEXT("Unknown action: %s. Expected 'status', 'start', or 'stop'"), *Action));
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::CaptureScreenshot(const TSharedPtr<FJsonObject>& Params)
{
	FString Filename;
	if (auto Err = RequireString(Params, TEXT("filename"), Filename)) return Err;

	// Ensure the filename has a proper extension
	if (!Filename.EndsWith(TEXT(".png")) && !Filename.EndsWith(TEXT(".jpg")) && !Filename.EndsWith(TEXT(".bmp")))
	{
		Filename += TEXT(".png");
	}

	// #64: Force the active level viewport to render even if the editor window is not focused
	if (!GEditor)
	{
		return MCPError(TEXT("Editor not available"));
	}

	FLevelEditorViewportClient* ViewportClient = GCurrentLevelEditingViewportClient;
	if (!ViewportClient)
	{
		const TArray<FLevelEditorViewportClient*>& ViewportClients = GEditor->GetLevelViewportClients();
		if (ViewportClients.Num() > 0)
		{
			ViewportClient = ViewportClients[0];
		}
	}

	if (!ViewportClient)
	{
		return MCPError(TEXT("No level viewport available for screenshot"));
	}

	// Force a viewport redraw to ensure we capture current state
	ViewportClient->Invalidate();

	// Make the viewport's output path explicit so FScreenshotRequest picks it up
	FString FullPath = Filename;
	if (!FPaths::IsRelative(Filename))
	{
		// Already absolute
	}
	else
	{
		FullPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), Filename);
	}

	// Request the screenshot
	FScreenshotRequest::RequestScreenshot(FullPath, false, false);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("filename"), FullPath);
	Result->SetStringField(TEXT("note"), TEXT("Screenshot queued. The file will be written asynchronously by the renderer."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::SetViewportCamera(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor)
	{
		return MCPError(TEXT("Editor not available"));
	}

	FLevelEditorViewportClient* ViewportClient = GCurrentLevelEditingViewportClient;
	if (!ViewportClient)
	{
		const TArray<FLevelEditorViewportClient*>& ViewportClients = GEditor->GetLevelViewportClients();
		if (ViewportClients.Num() > 0)
		{
			ViewportClient = ViewportClients[0];
		}
	}

	if (!ViewportClient)
	{
		return MCPError(TEXT("No viewport client available"));
	}

	// Set location if provided
	const TSharedPtr<FJsonObject>* LocationObj = nullptr;
	if (Params->TryGetObjectField(TEXT("location"), LocationObj) && LocationObj)
	{
		FVector Location;
		Location.X = (*LocationObj)->GetNumberField(TEXT("x"));
		Location.Y = (*LocationObj)->GetNumberField(TEXT("y"));
		Location.Z = (*LocationObj)->GetNumberField(TEXT("z"));
		ViewportClient->SetViewLocation(Location);
	}

	// Set rotation if provided
	const TSharedPtr<FJsonObject>* RotationObj = nullptr;
	if (Params->TryGetObjectField(TEXT("rotation"), RotationObj) && RotationObj)
	{
		FRotator Rotation;
		Rotation.Pitch = (*RotationObj)->GetNumberField(TEXT("pitch"));
		Rotation.Yaw = (*RotationObj)->GetNumberField(TEXT("yaw"));
		Rotation.Roll = (*RotationObj)->GetNumberField(TEXT("roll"));
		ViewportClient->SetViewRotation(Rotation);
	}

	auto Result = MCPSuccess();
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::Undo(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor)
	{
		return MCPError(TEXT("Editor not available"));
	}

	bool bSuccess = GEditor->UndoTransaction();
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bSuccess);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::Redo(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor)
	{
		return MCPError(TEXT("Editor not available"));
	}

	bool bSuccess = GEditor->RedoTransaction();
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bSuccess);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::ReloadHandlers(const TSharedPtr<FJsonObject>& Params)
{
	// No-op in C++ bridge - this was used in the Python bridge to reload Python handler modules.
	auto Result = MCPSuccess();
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::SaveAsset(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;

	bool bSuccess = UEditorAssetLibrary::SaveAsset(AssetPath);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetBoolField(TEXT("success"), bSuccess);
	if (!bSuccess)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to save asset: %s"), *AssetPath));
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::SaveAll(const TSharedPtr<FJsonObject>& Params)
{
	// Save all dirty packages using FEditorFileUtils
	bool bPromptUserToSave = false;
	bool bSaveMapPackages = true;
	bool bSaveContentPackages = true;
	bool bSuccess = FEditorFileUtils::SaveDirtyPackages(bPromptUserToSave, bSaveMapPackages, bSaveContentPackages);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bSuccess);
	Result->SetStringField(TEXT("message"), bSuccess ? TEXT("All dirty packages saved") : TEXT("Some packages may have failed to save"));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::GetCrashReports(const TSharedPtr<FJsonObject>& Params)
{
	FString CrashesDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Crashes"));
	IFileManager& FileManager = IFileManager::Get();

	TArray<TSharedPtr<FJsonValue>> CrashesArray;

	if (FileManager.DirectoryExists(*CrashesDir))
	{
		// Find all subdirectories in Crashes folder
		TArray<FString> CrashFolders;
		FileManager.FindFiles(CrashFolders, *FPaths::Combine(CrashesDir, TEXT("*")), false, true);

		for (const FString& FolderName : CrashFolders)
		{
			FString FolderPath = FPaths::Combine(CrashesDir, FolderName);

			TSharedPtr<FJsonObject> CrashObj = MakeShared<FJsonObject>();
			CrashObj->SetStringField(TEXT("folder"), FolderName);
			CrashObj->SetStringField(TEXT("path"), FolderPath);

			// Get folder timestamp
			FDateTime TimeStamp = FileManager.GetTimeStamp(*FolderPath);
			if (TimeStamp != FDateTime::MinValue())
			{
				CrashObj->SetStringField(TEXT("timestamp"), TimeStamp.ToString());
			}

			// List files inside the crash folder
			TArray<FString> CrashFiles;
			FileManager.FindFiles(CrashFiles, *FPaths::Combine(FolderPath, TEXT("*")), true, false);

			TArray<TSharedPtr<FJsonValue>> FilesArray;
			for (const FString& FileName : CrashFiles)
			{
				FilesArray.Add(MakeShared<FJsonValueString>(FileName));
			}
			CrashObj->SetArrayField(TEXT("files"), FilesArray);

			CrashesArray.Add(MakeShared<FJsonValueObject>(CrashObj));
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("crashesDir"), CrashesDir);
	Result->SetNumberField(TEXT("crashCount"), CrashesArray.Num());
	Result->SetArrayField(TEXT("crashes"), CrashesArray);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::ReadEditorLog(const TSharedPtr<FJsonObject>& Params)
{
	// Parameters
	int32 LastN = OptionalInt(Params, TEXT("lastN"), 100);
	FString Filter = OptionalString(Params, TEXT("filter"));

	// Locate the editor log file
	FString LogDir = FPaths::ProjectLogDir();
	FString LogFilePath = FPaths::Combine(LogDir, TEXT("Editor.log"));

	// If Editor.log doesn't exist, try the current log file
	if (!FPaths::FileExists(LogFilePath))
	{
		LogFilePath = FPaths::Combine(LogDir, FString(FApp::GetProjectName()) + TEXT(".log"));
	}

	if (!FPaths::FileExists(LogFilePath))
	{
		return MCPError(FString::Printf(TEXT("Editor log file not found in %s"), *LogDir));
	}

	// Read the log file into lines
	TArray<FString> AllLines;
	if (!FFileHelper::LoadFileToStringArray(AllLines, *LogFilePath))
	{
		return MCPError(TEXT("Failed to read editor log file"));
	}

	// Apply filter and take last N lines
	TArray<FString> ResultLines;
	if (Filter.IsEmpty())
	{
		// No filter - take the last N lines directly
		int32 StartIndex = FMath::Max(0, AllLines.Num() - LastN);
		for (int32 i = StartIndex; i < AllLines.Num(); ++i)
		{
			ResultLines.Add(AllLines[i]);
		}
	}
	else
	{
		// Filter lines (case-insensitive) then take last N
		FString FilterLower = Filter.ToLower();
		TArray<FString> FilteredLines;
		for (const FString& Line : AllLines)
		{
			if (Line.ToLower().Contains(FilterLower))
			{
				FilteredLines.Add(Line);
			}
		}
		int32 StartIndex = FMath::Max(0, FilteredLines.Num() - LastN);
		for (int32 i = StartIndex; i < FilteredLines.Num(); ++i)
		{
			ResultLines.Add(FilteredLines[i]);
		}
	}

	// Convert to JSON array
	TArray<TSharedPtr<FJsonValue>> LinesArray;
	for (const FString& Line : ResultLines)
	{
		LinesArray.Add(MakeShared<FJsonValueString>(Line));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("logFile"), LogFilePath);
	Result->SetNumberField(TEXT("lineCount"), ResultLines.Num());
	Result->SetNumberField(TEXT("totalLines"), AllLines.Num());
	Result->SetArrayField(TEXT("lines"), LinesArray);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::PieGetRuntimeValue(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor)
	{
		return MCPError(TEXT("Editor not available"));
	}

	// Check if PIE is active
	if (GEditor->PlayWorld == nullptr)
	{
		return MCPError(TEXT("PIE is not active. Start a PIE session first."));
	}

	FString ActorPath;
	if (!Params->TryGetStringField(TEXT("actorPath"), ActorPath))
	{
		// Also accept actorLabel as a fallback
		if (!Params->TryGetStringField(TEXT("actorLabel"), ActorPath))
		{
			return MCPError(TEXT("Missing 'actorPath' parameter"));
		}
	}

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	// Search for the actor in the PIE world
	UWorld* PIEWorld = GEditor->PlayWorld;
	AActor* TargetActor = nullptr;

	for (TActorIterator<AActor> It(PIEWorld); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor->GetName() == ActorPath ||
			Actor->GetActorLabel() == ActorPath ||
			Actor->GetPathName() == ActorPath)
		{
			TargetActor = Actor;
			break;
		}
	}

	if (!TargetActor)
	{
		// Collect available actor names for the error message
		TArray<TSharedPtr<FJsonValue>> AvailableActors;
		int32 Count = 0;
		for (TActorIterator<AActor> It(PIEWorld); It && Count < 20; ++It, ++Count)
		{
			AvailableActors.Add(MakeShared<FJsonValueString>((*It)->GetActorLabel()));
		}
		TSharedPtr<FJsonObject> ErrResult = MakeShared<FJsonObject>();
		ErrResult->SetBoolField(TEXT("success"), false);
		ErrResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor '%s' not found in PIE world"), *ActorPath));
		ErrResult->SetArrayField(TEXT("availableActors"), AvailableActors);
		return MCPResult(ErrResult);
	}

	// Find the property via reflection
	FProperty* Property = TargetActor->GetClass()->FindPropertyByName(*PropertyName);
	if (!Property)
	{
		return MCPError(FString::Printf(TEXT("Property '%s' not found on actor '%s'"), *PropertyName, *ActorPath));
	}

	// Read property value and serialize based on type
	auto Result = MCPSuccess();
	const void* PropertyValue = Property->ContainerPtrToValuePtr<void>(TargetActor);

	if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		FString Value = StrProp->GetPropertyValue(PropertyValue);
		Result->SetStringField(TEXT("value"), Value);
		Result->SetStringField(TEXT("type"), TEXT("String"));
	}
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		bool Value = BoolProp->GetPropertyValue(PropertyValue);
		Result->SetBoolField(TEXT("value"), Value);
		Result->SetStringField(TEXT("type"), TEXT("Bool"));
	}
	else if (FIntProperty* IntProp = CastField<FIntProperty>(Property))
	{
		int32 Value = IntProp->GetPropertyValue(PropertyValue);
		Result->SetNumberField(TEXT("value"), Value);
		Result->SetStringField(TEXT("type"), TEXT("Int"));
	}
	else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
	{
		float Value = FloatProp->GetPropertyValue(PropertyValue);
		Result->SetNumberField(TEXT("value"), Value);
		Result->SetStringField(TEXT("type"), TEXT("Float"));
	}
	else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Property))
	{
		double Value = DoubleProp->GetPropertyValue(PropertyValue);
		Result->SetNumberField(TEXT("value"), Value);
		Result->SetStringField(TEXT("type"), TEXT("Double"));
	}
	else if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		FName Value = NameProp->GetPropertyValue(PropertyValue);
		Result->SetStringField(TEXT("value"), Value.ToString());
		Result->SetStringField(TEXT("type"), TEXT("Name"));
	}
	else if (FTextProperty* TextProp = CastField<FTextProperty>(Property))
	{
		FText Value = TextProp->GetPropertyValue(PropertyValue);
		Result->SetStringField(TEXT("value"), Value.ToString());
		Result->SetStringField(TEXT("type"), TEXT("Text"));
	}
	else if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		// Handle common struct types: FVector, FRotator, FLinearColor
		if (StructProp->Struct == TBaseStructure<FVector>::Get())
		{
			const FVector* Vec = reinterpret_cast<const FVector*>(PropertyValue);
			TSharedPtr<FJsonObject> VecObj = MakeShared<FJsonObject>();
			VecObj->SetNumberField(TEXT("x"), Vec->X);
			VecObj->SetNumberField(TEXT("y"), Vec->Y);
			VecObj->SetNumberField(TEXT("z"), Vec->Z);
			Result->SetObjectField(TEXT("value"), VecObj);
			Result->SetStringField(TEXT("type"), TEXT("Vector"));
		}
		else if (StructProp->Struct == TBaseStructure<FRotator>::Get())
		{
			const FRotator* Rot = reinterpret_cast<const FRotator*>(PropertyValue);
			TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
			RotObj->SetNumberField(TEXT("pitch"), Rot->Pitch);
			RotObj->SetNumberField(TEXT("yaw"), Rot->Yaw);
			RotObj->SetNumberField(TEXT("roll"), Rot->Roll);
			Result->SetObjectField(TEXT("value"), RotObj);
			Result->SetStringField(TEXT("type"), TEXT("Rotator"));
		}
		else if (StructProp->Struct == TBaseStructure<FLinearColor>::Get())
		{
			const FLinearColor* Color = reinterpret_cast<const FLinearColor*>(PropertyValue);
			TSharedPtr<FJsonObject> ColorObj = MakeShared<FJsonObject>();
			ColorObj->SetNumberField(TEXT("r"), Color->R);
			ColorObj->SetNumberField(TEXT("g"), Color->G);
			ColorObj->SetNumberField(TEXT("b"), Color->B);
			ColorObj->SetNumberField(TEXT("a"), Color->A);
			Result->SetObjectField(TEXT("value"), ColorObj);
			Result->SetStringField(TEXT("type"), TEXT("LinearColor"));
		}
		else
		{
			// Generic struct: export to string
			FString ExportedValue;
			Property->ExportTextItem_Direct(ExportedValue, PropertyValue, nullptr, nullptr, PPF_None);
			Result->SetStringField(TEXT("value"), ExportedValue);
			Result->SetStringField(TEXT("type"), StructProp->Struct->GetName());
		}
	}
	else
	{
		// Fallback: export property value as string
		FString ExportedValue;
		Property->ExportTextItem_Direct(ExportedValue, PropertyValue, nullptr, nullptr, PPF_None);
		Result->SetStringField(TEXT("value"), ExportedValue);
		Result->SetStringField(TEXT("type"), Property->GetCPPType());
	}

	Result->SetStringField(TEXT("actorPath"), ActorPath);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	return MCPResult(Result);
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

TSharedPtr<FJsonValue> FEditorHandlers::FocusViewportOnActor(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	// Find the actor by label
	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == ActorLabel)
		{
			TargetActor = *It;
			break;
		}
	}

	if (!TargetActor)
	{
		return MCPError(FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel));
	}

	// Get the viewport client
	FLevelEditorViewportClient* ViewportClient = GCurrentLevelEditingViewportClient;
	if (!ViewportClient)
	{
		const TArray<FLevelEditorViewportClient*>& ViewportClients = GEditor->GetLevelViewportClients();
		if (ViewportClients.Num() > 0)
		{
			ViewportClient = ViewportClients[0];
		}
	}

	if (!ViewportClient)
	{
		return MCPError(TEXT("No viewport client available"));
	}

	// Focus on the actor's bounding box
	FBox ActorBounds = TargetActor->GetComponentsBoundingBox(true);
	if (ActorBounds.IsValid)
	{
		ViewportClient->FocusViewportOnBox(ActorBounds);
	}
	else
	{
		// Fallback: just move the camera to the actor's location
		FVector ActorLocation = TargetActor->GetActorLocation();
		FVector CameraOffset(0.0, -500.0, 200.0);
		ViewportClient->SetViewLocation(ActorLocation + CameraOffset);
		FRotator LookAt = (ActorLocation - (ActorLocation + CameraOffset)).Rotation();
		ViewportClient->SetViewRotation(LookAt);
	}

	FVector FinalLocation = ViewportClient->GetViewLocation();
	TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
	LocObj->SetNumberField(TEXT("x"), FinalLocation.X);
	LocObj->SetNumberField(TEXT("y"), FinalLocation.Y);
	LocObj->SetNumberField(TEXT("z"), FinalLocation.Z);

	auto Result = MCPSuccess();
	Result->SetObjectField(TEXT("viewLocation"), LocObj);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
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

TSharedPtr<FJsonValue> FEditorHandlers::CreateNewLevel(const TSharedPtr<FJsonObject>& Params)
{
	FString LevelPath = OptionalString(Params, TEXT("levelPath"));
	FString TemplateLevel = OptionalString(Params, TEXT("templateLevel"));

	ULevelEditorSubsystem* LevelEditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<ULevelEditorSubsystem>() : nullptr;
	if (!LevelEditorSubsystem)
	{
		return MCPError(TEXT("LevelEditorSubsystem not available"));
	}

	// Idempotency: level at LevelPath already exists?
	if (!LevelPath.IsEmpty() && UEditorAssetLibrary::DoesAssetExist(LevelPath))
	{
		const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
		if (OnConflict == TEXT("error"))
		{
			return MCPError(FString::Printf(TEXT("Level already exists: %s"), *LevelPath));
		}
		auto Existed = MCPSuccess();
		MCPSetExisted(Existed);
		Existed->SetStringField(TEXT("levelPath"), LevelPath);
		return MCPResult(Existed);
	}

	bool bSuccess = false;
	if (TemplateLevel.IsEmpty())
	{
		bSuccess = LevelEditorSubsystem->NewLevel(LevelPath);
	}
	else
	{
		bSuccess = LevelEditorSubsystem->NewLevelFromTemplate(LevelPath, TemplateLevel);
	}

	if (!bSuccess)
	{
		return MCPError(FString::Printf(TEXT("Failed to create new level at: %s"), *LevelPath));
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);

	// Get info about the new world
	UWorld* World = GetEditorWorld();
	if (World)
	{
		Result->SetStringField(TEXT("worldName"), World->GetName());
		Result->SetStringField(TEXT("worldPath"), World->GetPathName());
	}

	Result->SetStringField(TEXT("levelPath"), LevelPath);
	Result->SetStringField(TEXT("message"), TEXT("New level created"));
	if (!LevelPath.IsEmpty())
	{
		MCPSetDeleteAssetRollback(Result, LevelPath);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::SaveCurrentLevel(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	ULevelEditorSubsystem* LevelEditorSubsystem = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	if (!LevelEditorSubsystem)
	{
		return MCPError(TEXT("LevelEditorSubsystem not available"));
	}

	bool bSuccess = LevelEditorSubsystem->SaveCurrentLevel();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("levelName"), World->GetName());
	Result->SetStringField(TEXT("levelPath"), World->GetPathName());
	Result->SetBoolField(TEXT("success"), bSuccess);

	if (!bSuccess)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to save current level"));
	}
	else
	{
		Result->SetStringField(TEXT("message"), TEXT("Current level saved"));
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::OpenAsset(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Failed to load asset at '%s'"), *AssetPath));
	}

	if (!GEditor)
	{
		return MCPError(TEXT("GEditor not available"));
	}

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem)
	{
		return MCPError(TEXT("AssetEditorSubsystem not available"));
	}

	bool bOpened = AssetEditorSubsystem->OpenEditorForAsset(Asset);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("assetClass"), Asset->GetClass()->GetName());
	Result->SetBoolField(TEXT("success"), bOpened);
	if (!bOpened)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to open editor for '%s' (%s)"), *AssetPath, *Asset->GetClass()->GetName()));
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::RunStatCommand(const TSharedPtr<FJsonObject>& Params)
{
	FString Command = OptionalString(Params, TEXT("command"), TEXT("stat fps"));

	REQUIRE_EDITOR_WORLD(World);

	GEditor->Exec(World, *Command);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("command"), Command);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::SetScalability(const TSharedPtr<FJsonObject>& Params)
{
	FString Level = OptionalString(Params, TEXT("level"), TEXT("Epic"));

	int32 Idx = 3; // Default to Epic
	if (Level == TEXT("Low")) Idx = 0;
	else if (Level == TEXT("Medium")) Idx = 1;
	else if (Level == TEXT("High")) Idx = 2;
	else if (Level == TEXT("Epic")) Idx = 3;
	else if (Level == TEXT("Cinematic")) Idx = 4;

	REQUIRE_EDITOR_WORLD(World);

	TArray<FString> Commands = {
		FString::Printf(TEXT("sg.ViewDistanceQuality %d"), Idx),
		FString::Printf(TEXT("sg.AntiAliasingQuality %d"), Idx),
		FString::Printf(TEXT("sg.ShadowQuality %d"), Idx),
		FString::Printf(TEXT("sg.GlobalIlluminationQuality %d"), Idx),
		FString::Printf(TEXT("sg.ReflectionQuality %d"), Idx),
		FString::Printf(TEXT("sg.PostProcessQuality %d"), Idx),
		FString::Printf(TEXT("sg.TextureQuality %d"), Idx),
		FString::Printf(TEXT("sg.EffectsQuality %d"), Idx),
		FString::Printf(TEXT("sg.FoliageQuality %d"), Idx),
		FString::Printf(TEXT("sg.ShadingQuality %d"), Idx),
	};

	for (const FString& Cmd : Commands)
	{
		GEditor->Exec(World, *Cmd);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("level"), Level);
	return MCPResult(Result);
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

TSharedPtr<FJsonValue> FEditorHandlers::ListCrashes(const TSharedPtr<FJsonObject>& Params)
{
	FString CrashesDir = FPaths::ProjectSavedDir() / TEXT("Crashes");

	TArray<TSharedPtr<FJsonValue>> CrashArray;
	IFileManager& FileManager = IFileManager::Get();

	TArray<FString> CrashFolders;
	FileManager.FindFiles(CrashFolders, *(CrashesDir / TEXT("*")), false, true);

	for (const FString& Folder : CrashFolders)
	{
		TSharedPtr<FJsonObject> CrashObj = MakeShared<FJsonObject>();
		FString FullPath = CrashesDir / Folder;
		CrashObj->SetStringField(TEXT("folder"), Folder);
		CrashObj->SetStringField(TEXT("path"), FullPath);

		FFileStatData StatData = FileManager.GetStatData(*FullPath);
		if (StatData.bIsValid)
		{
			CrashObj->SetNumberField(TEXT("modified"), StatData.ModificationTime.ToUnixTimestamp());
		}

		TArray<FString> Files;
		FileManager.FindFiles(Files, *(FullPath / TEXT("*")), true, false);
		TArray<TSharedPtr<FJsonValue>> FileArray;
		for (const FString& File : Files)
		{
			FileArray.Add(MakeShared<FJsonValueString>(File));
		}
		CrashObj->SetArrayField(TEXT("files"), FileArray);
		CrashArray.Add(MakeShared<FJsonValueObject>(CrashObj));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("crashesDir"), CrashesDir);
	Result->SetNumberField(TEXT("crashCount"), CrashArray.Num());
	Result->SetArrayField(TEXT("crashes"), CrashArray);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::GetCrashInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString CrashFolder;
	if (auto Err = RequireString(Params, TEXT("crashFolder"), CrashFolder)) return Err;

	FString CrashPath = FPaths::ProjectSavedDir() / TEXT("Crashes") / CrashFolder;
	if (!IFileManager::Get().DirectoryExists(*CrashPath))
	{
		auto Result = MCPSuccess();
		Result->SetBoolField(TEXT("available"), false);
		Result->SetStringField(TEXT("note"), FString::Printf(TEXT("Crash folder not found: %s"), *CrashFolder));
		return MCPResult(Result);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("folder"), CrashFolder);
	Result->SetStringField(TEXT("path"), CrashPath);

	TSharedPtr<FJsonObject> FilesObj = MakeShared<FJsonObject>();
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(CrashPath / TEXT("*")), true, false);

	for (const FString& File : Files)
	{
		TSharedPtr<FJsonObject> FileInfo = MakeShared<FJsonObject>();
		FString FilePath = CrashPath / File;
		FFileStatData StatData = IFileManager::Get().GetStatData(*FilePath);
		if (StatData.bIsValid)
		{
			FileInfo->SetNumberField(TEXT("size"), StatData.FileSize);
			FileInfo->SetNumberField(TEXT("modified"), StatData.ModificationTime.ToUnixTimestamp());
		}

		// Read text files
		if (File.EndsWith(TEXT(".log")) || File.EndsWith(TEXT(".txt")) || File.EndsWith(TEXT(".xml")) || File.EndsWith(TEXT(".json")))
		{
			FString Content;
			if (FFileHelper::LoadFileToString(Content, *FilePath))
			{
				// Limit content to 50KB
				if (Content.Len() > 50000)
				{
					Content = Content.Left(50000) + TEXT("\n... [truncated]");
				}
				FileInfo->SetStringField(TEXT("content"), Content);
			}
		}
		FilesObj->SetObjectField(File, FileInfo);
	}

	Result->SetObjectField(TEXT("files"), FilesObj);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::CheckForCrashes(const TSharedPtr<FJsonObject>& Params)
{
	FString CrashesDir = FPaths::ProjectSavedDir() / TEXT("Crashes");

	TArray<TSharedPtr<FJsonValue>> RecentCrashes;
	IFileManager& FileManager = IFileManager::Get();
	FDateTime Now = FDateTime::UtcNow();
	FDateTime Threshold = Now - FTimespan::FromHours(24);

	TArray<FString> CrashFolders;
	FileManager.FindFiles(CrashFolders, *(CrashesDir / TEXT("*")), false, true);

	for (const FString& Folder : CrashFolders)
	{
		FString FullPath = CrashesDir / Folder;
		FFileStatData StatData = FileManager.GetStatData(*FullPath);
		if (StatData.bIsValid && StatData.ModificationTime > Threshold)
		{
			TSharedPtr<FJsonObject> CrashObj = MakeShared<FJsonObject>();
			CrashObj->SetStringField(TEXT("folder"), Folder);
			CrashObj->SetStringField(TEXT("path"), FullPath);
			CrashObj->SetNumberField(TEXT("timestamp"), StatData.ModificationTime.ToUnixTimestamp());
			RecentCrashes.Add(MakeShared<FJsonValueObject>(CrashObj));
		}
	}

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("recentCrashCount"), RecentCrashes.Num());
	Result->SetArrayField(TEXT("recentCrashes"), RecentCrashes);
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

// #126: Fast-forward PIE game time. Raises WorldSettings dilation caps and calls SetGlobalTimeDilation.
TSharedPtr<FJsonValue> FEditorHandlers::SetPieTimeScale(const TSharedPtr<FJsonObject>& Params)
{
	double Factor = 1.0;
	if (!Params->TryGetNumberField(TEXT("factor"), Factor))
	{
		return MCPError(TEXT("Missing 'factor' (number) parameter"));
	}
	if (Factor <= 0.0)
	{
		return MCPError(TEXT("'factor' must be > 0"));
	}

	UWorld* World = GetPIEWorld();
	if (!World)
	{
		return MCPError(TEXT("No PIE/Game world active — start PIE first"));
	}

	AWorldSettings* WS = World->GetWorldSettings();
	if (!WS)
	{
		return MCPError(TEXT("WorldSettings not available on PIE world"));
	}

	// Raise dilation caps so Factor isn't clamped.
	const float CapHigh = FMath::Max(1000.0f, (float)Factor * 2.0f);
	WS->MaxGlobalTimeDilation = FMath::Max(WS->MaxGlobalTimeDilation, CapHigh);
	WS->MinGlobalTimeDilation = FMath::Min(WS->MinGlobalTimeDilation, 0.0001f);

	UGameplayStatics::SetGlobalTimeDilation(World, (float)Factor);

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("factor"), Factor);
	Result->SetNumberField(TEXT("maxCap"), WS->MaxGlobalTimeDilation);
	Result->SetNumberField(TEXT("minCap"), WS->MinGlobalTimeDilation);
	Result->SetStringField(TEXT("world"), World->GetName());
	return MCPResult(Result);
}
