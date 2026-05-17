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
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Misc/OutputDeviceRedirector.h"
#include "FileHelpers.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "EditorValidatorSubsystem.h"
#include "SceneView.h"
#include "Components/PrimitiveComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Materials/MaterialInterface.h"
#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"
#if PLATFORM_WINDOWS
#include "ILiveCodingModule.h"
#endif
#include "LevelEditorSubsystem.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "DesktopPlatformModule.h"
#include "HAL/PlatformProcess.h"
#include "Misc/MonitoredProcess.h"
#include "HandlerJsonProperty.h"
#include "Engine/Blueprint.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

void FEditorHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	// Install log capture ring buffer (#82)
	FMCPLogCapture::Get().Install();

	Registry.RegisterHandler(TEXT("execute_command"), &ExecuteCommand);
	Registry.RegisterHandler(TEXT("execute_python"), &ExecutePython);
	Registry.RegisterHandler(TEXT("run_python_file"), &RunPythonFile);
	Registry.RegisterHandler(TEXT("set_property"), &SetProperty);
	Registry.RegisterHandler(TEXT("set_config"), &SetConfig);
	Registry.RegisterHandler(TEXT("get_viewport_info"), &GetViewportInfo);
	Registry.RegisterHandler(TEXT("hit_test_viewport_pixel"), &HitTestViewportPixel);
	Registry.RegisterHandler(TEXT("get_runtime_values"), &GetRuntimeValues);
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
	Registry.RegisterHandler(TEXT("save_dirty"), &SaveDirty);
	Registry.RegisterHandler(TEXT("list_dirty_packages"), &ListDirtyPackages);
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
	Registry.RegisterHandler(TEXT("capture_scene_png"), &CaptureScenePng);
	Registry.RegisterHandler(TEXT("get_pie_pawn"), &GetPiePawn);
	Registry.RegisterHandler(TEXT("invoke_function"), &InvokeFunction);
	Registry.RegisterHandler(TEXT("configure_pie"), &ConfigurePie);
	Registry.RegisterHandler(TEXT("get_pie_config"), &GetPieConfig);
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

// #142 — Run a Python script file on disk with __file__/__name__ context populated.
// Mirrors the execute_python return shape. Use this instead of execute_python
// when you want to invoke a checked-in .py file without wrapping it in `exec()`.
TSharedPtr<FJsonValue> FEditorHandlers::RunPythonFile(const TSharedPtr<FJsonObject>& Params)
{
	FString FilePath;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("filePath"), FilePath)) return Err;

	// Accept forward-slashes on Windows; FPlatformFileManager normalises them.
	if (!FPaths::FileExists(FilePath))
	{
		return MCPError(FString::Printf(TEXT("Python file not found: %s"), *FilePath));
	}

	IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
	if (!PythonPlugin || !PythonPlugin->IsPythonAvailable())
	{
		return MCPError(TEXT("Python scripting is not available"));
	}

	// Optional positional args to expose as sys.argv[1:].
	TArray<FString> ExtraArgs;
	const TArray<TSharedPtr<FJsonValue>>* ArgsArr = nullptr;
	if (Params->TryGetArrayField(TEXT("args"), ArgsArr) && ArgsArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *ArgsArr)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S)) ExtraArgs.Add(S);
		}
	}

	FPythonCommandEx PythonCommand;
	PythonCommand.Command = FilePath;
	PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
	PythonCommand.FileExecutionScope = EPythonFileExecutionScope::Public;
	for (const FString& A : ExtraArgs)
	{
		PythonCommand.Command += TEXT(" ") + A;
	}

	bool bSuccess = PythonPlugin->ExecPythonCommandEx(PythonCommand);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bSuccess);
	Result->SetStringField(TEXT("path"), FilePath);
	Result->SetStringField(TEXT("result"), PythonCommand.CommandResult);

	TArray<TSharedPtr<FJsonValue>> LogArray;
	FString CombinedOutput;
	for (const FPythonLogOutputEntry& Entry : PythonCommand.LogOutput)
	{
		TSharedPtr<FJsonObject> LogEntry = MakeShared<FJsonObject>();
		LogEntry->SetStringField(TEXT("type"), LexToString(Entry.Type));
		LogEntry->SetStringField(TEXT("output"), Entry.Output);
		LogArray.Add(MakeShared<FJsonValueObject>(LogEntry));
		if (!CombinedOutput.IsEmpty()) CombinedOutput += TEXT("\n");
		CombinedOutput += Entry.Output;
	}
	Result->SetArrayField(TEXT("log_output"), LogArray);
	Result->SetStringField(TEXT("output"), CombinedOutput);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::SetProperty(const TSharedPtr<FJsonObject>& Params)
{
	// #221/#230: TS schema documents `objectPath` but the dispatcher only
	// accepted `path`/`assetPath`. Take any of the three so callers using the
	// schema as written don't bounce off "missing required parameter".
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("objectPath"), AssetPath))
	{
		if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), AssetPath)) return Err;
	}

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	// Load asset (works for /Game/X.X full paths or short /Game/X paths).
	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		// Try the LoadAsset fallback so /Game/Foo (no .Foo suffix) resolves.
		Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	}
	if (!Asset)
	{
		return MCPError(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	// #210/#211: many data assets are edited via their CDO when assetPath is a
	// Blueprint generated class (e.g. /Game/Foo/BP_Bar.BP_Bar_C). If the loaded
	// object is a UClass, redirect to its default object so set-property writes
	// to the per-class defaults the user expects.
	if (UClass* Cls = Cast<UClass>(Asset))
	{
		Asset = Cls->GetDefaultObject();
	}
	else if (UBlueprint* BP = Cast<UBlueprint>(Asset))
	{
		if (BP->GeneratedClass) Asset = BP->GeneratedClass->GetDefaultObject();
	}

	FProperty* Property = Asset->GetClass()->FindPropertyByName(*PropertyName);
	if (!Property)
	{
		return MCPError(FString::Printf(TEXT("Property '%s' not found on %s"), *PropertyName, *Asset->GetClass()->GetName()));
	}

	TSharedPtr<FJsonValue> ValueJsonRef = Params->TryGetField(TEXT("value"));
	if (!ValueJsonRef.IsValid())
	{
		return MCPError(TEXT("Missing 'value' parameter"));
	}

	void* PropertyValue = Property->ContainerPtrToValuePtr<void>(Asset);
	Asset->Modify();

	// #210/#221: route through the recursive setter so JSON objects, arrays,
	// asset-path strings (FObjectProperty), and nested structs all apply
	// without callers having to pre-format UE text.
	FString SetErr;
	if (!MCPJsonProperty::SetJsonOnProperty(Property, PropertyValue, ValueJsonRef, SetErr))
	{
		return MCPError(FString::Printf(TEXT("Failed to set '%s': %s"), *PropertyName, *SetErr));
	}

	FPropertyChangedEvent ChangeEvent(Property);
	Asset->PostEditChangeProperty(ChangeEvent);
	Asset->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(Asset, /*bOnlyIfIsDirty=*/true);

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

// ---------------------------------------------------------------------------
// hit_test_viewport_pixel -- Ray-cast from a screen pixel through the active
// editor viewport and return the first hit (#418). Replaces the bespoke
// Python "build a ray, line trace, hope" workaround.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::HitTestViewportPixel(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor)
	{
		return MCPError(TEXT("Editor not available"));
	}

	double PixelX = 0, PixelY = 0;
	if (!Params->TryGetNumberField(TEXT("x"), PixelX) || !Params->TryGetNumberField(TEXT("y"), PixelY))
	{
		return MCPError(TEXT("Missing required parameters 'x' and 'y' (viewport pixel coordinates)"));
	}

	FLevelEditorViewportClient* ViewportClient = GCurrentLevelEditingViewportClient;
	if (!ViewportClient)
	{
		const TArray<FLevelEditorViewportClient*>& ViewportClients = GEditor->GetLevelViewportClients();
		if (ViewportClients.Num() > 0) ViewportClient = ViewportClients[0];
	}
	if (!ViewportClient || !ViewportClient->Viewport)
	{
		return MCPError(TEXT("No active editor viewport"));
	}

	// Viewport dimensions. Caller can override (e.g. when targeting a
	// screenshot pixel coordinate space that differs from the live viewport).
	FViewport* Viewport = ViewportClient->Viewport;
	const FIntPoint ViewportSize = Viewport->GetSizeXY();
	double Width = ViewportSize.X;
	double Height = ViewportSize.Y;
	Params->TryGetNumberField(TEXT("width"), Width);
	Params->TryGetNumberField(TEXT("height"), Height);
	if (Width <= 0 || Height <= 0)
	{
		return MCPError(FString::Printf(TEXT("Viewport size is zero (%dx%d) and no explicit width/height supplied. Focus the viewport, or pass width+height matching the screenshot used to pick the pixel."), ViewportSize.X, ViewportSize.Y));
	}

	// Build a SceneView matching the live viewport so DeprojectFVector2D uses
	// the actual projection matrix instead of guessing FOV/aspect.
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		Viewport, ViewportClient->GetScene(), ViewportClient->EngineShowFlags)
		.SetRealtimeUpdate(ViewportClient->IsRealtime()));
	FSceneView* SceneView = ViewportClient->CalcSceneView(&ViewFamily);
	if (!SceneView)
	{
		return MCPError(TEXT("Failed to construct SceneView for viewport"));
	}

	// If caller supplied width/height that differ from the actual viewport,
	// rescale the pixel into the viewport's coordinate space so the ray is
	// correct for the projection we built.
	const double SX = ViewportSize.X / Width;
	const double SY = ViewportSize.Y / Height;
	const FVector2D ScreenPos((float)(PixelX * SX), (float)(PixelY * SY));

	FVector RayOrigin, RayDirection;
	SceneView->DeprojectFVector2D(ScreenPos, RayOrigin, RayDirection);

	const double MaxDistance = OptionalNumber(Params, TEXT("maxDistance"), 200000.0);
	const FVector RayEnd = RayOrigin + RayDirection * MaxDistance;

	UWorld* World = ViewportClient->GetWorld();
	if (!World)
	{
		return MCPError(TEXT("No world for active viewport"));
	}

	FCollisionQueryParams Query(SCENE_QUERY_STAT(MCPHitTestViewportPixel), /*bTraceComplex*/ true);
	Query.bReturnPhysicalMaterial = true;
	Query.bReturnFaceIndex = true;

	// Optional ignore list by actor label.
	const TArray<TSharedPtr<FJsonValue>>* IgnoreArr = nullptr;
	if (Params->TryGetArrayField(TEXT("ignoreActors"), IgnoreArr) && IgnoreArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *IgnoreArr)
		{
			FString Label;
			if (!V->TryGetString(Label)) continue;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (It->GetActorLabel() == Label) { Query.AddIgnoredActor(*It); break; }
			}
		}
	}

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, RayOrigin, RayEnd, ECC_Visibility, Query);

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("hit"), bHit);
	TSharedPtr<FJsonObject> RayObj = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> OriginObj = MakeShared<FJsonObject>();
	OriginObj->SetNumberField(TEXT("x"), RayOrigin.X);
	OriginObj->SetNumberField(TEXT("y"), RayOrigin.Y);
	OriginObj->SetNumberField(TEXT("z"), RayOrigin.Z);
	RayObj->SetObjectField(TEXT("origin"), OriginObj);
	TSharedPtr<FJsonObject> DirObj = MakeShared<FJsonObject>();
	DirObj->SetNumberField(TEXT("x"), RayDirection.X);
	DirObj->SetNumberField(TEXT("y"), RayDirection.Y);
	DirObj->SetNumberField(TEXT("z"), RayDirection.Z);
	RayObj->SetObjectField(TEXT("direction"), DirObj);
	Result->SetObjectField(TEXT("ray"), RayObj);

	if (!bHit) return MCPResult(Result);

	AActor* HitActor = Hit.GetActor();
	UPrimitiveComponent* HitComp = Hit.GetComponent();
	if (HitActor) Result->SetStringField(TEXT("actorLabel"), HitActor->GetActorLabel());
	if (HitActor) Result->SetStringField(TEXT("actorClass"), HitActor->GetClass()->GetName());
	if (HitComp)
	{
		Result->SetStringField(TEXT("componentName"), HitComp->GetName());
		Result->SetStringField(TEXT("componentClass"), HitComp->GetClass()->GetName());
		const int32 MatIndex = Hit.FaceIndex >= 0 && HitComp->GetNumMaterials() > 0 ? 0 : -1;
		if (UMaterialInterface* Mat = (HitComp->GetNumMaterials() > 0 ? HitComp->GetMaterial(0) : nullptr))
		{
			Result->SetStringField(TEXT("materialPath"), Mat->GetPathName());
		}
	}

	auto WriteVec = [&](const TCHAR* Field, const FVector& V)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("x"), V.X);
		Obj->SetNumberField(TEXT("y"), V.Y);
		Obj->SetNumberField(TEXT("z"), V.Z);
		Result->SetObjectField(Field, Obj);
	};
	WriteVec(TEXT("location"), Hit.Location);
	WriteVec(TEXT("impactPoint"), Hit.ImpactPoint);
	WriteVec(TEXT("normal"), Hit.Normal);
	WriteVec(TEXT("impactNormal"), Hit.ImpactNormal);
	Result->SetNumberField(TEXT("distance"), Hit.Distance);
	Result->SetNumberField(TEXT("faceIndex"), Hit.FaceIndex);
	if (Hit.BoneName != NAME_None) Result->SetStringField(TEXT("boneName"), Hit.BoneName.ToString());
	if (Hit.PhysMaterial.IsValid()) Result->SetStringField(TEXT("physicalMaterial"), Hit.PhysMaterial->GetPathName());
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

	// #226: target=pie (or auto-detect when PIE is running) routes through
	// HighResShot in the active PIE world so we capture the player viewport
	// instead of whatever the editor camera was last looking at.
	const FString Target = OptionalString(Params, TEXT("target"), TEXT("auto")).ToLower();
	UWorld* PieWorld = nullptr;
	if (FWorldContext* PieCtx = GEditor->GetPIEWorldContext())
	{
		PieWorld = PieCtx->World();
	}
	const bool bUsePie = (Target == TEXT("pie")) || (Target == TEXT("auto") && PieWorld);

	if (bUsePie && PieWorld)
	{
		int32 Width = OptionalInt(Params, TEXT("width"), 1920);
		int32 Height = OptionalInt(Params, TEXT("height"), 1080);
		// Some callers pass a single 'resolution' (long edge); honour it as width.
		double ResolutionScalar = 0.0;
		if (Params->TryGetNumberField(TEXT("resolution"), ResolutionScalar) && ResolutionScalar > 0)
		{
			Width = (int32)ResolutionScalar;
			Height = (int32)(ResolutionScalar * 9.0 / 16.0);
		}
		const FString ConsoleCmd = FString::Printf(TEXT("HighResShot %dx%d"), Width, Height);
		GEngine->Exec(PieWorld, *ConsoleCmd);
		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("filename"), Filename);
		Result->SetStringField(TEXT("target"), TEXT("pie"));
		Result->SetStringField(TEXT("consoleCommand"), ConsoleCmd);
		Result->SetStringField(TEXT("note"), TEXT("HighResShot dispatched into PIE world; output lands in Saved/Screenshots/<map>/."));
		return MCPResult(Result);
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
	Result->SetStringField(TEXT("target"), TEXT("editor"));
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

// #378: drive UPackage::SavePackage directly on every dirty content package
// so callers get a per-package result map. set_class_default and friends
// occasionally leave packages dirty without persisting; this is the escape
// hatch that surfaces which packages actually wrote to disk.
TSharedPtr<FJsonValue> FEditorHandlers::SaveDirty(const TSharedPtr<FJsonObject>& Params)
{
	const bool bIncludeMaps = OptionalBool(Params, TEXT("includeMaps"), true);
	const bool bIncludeContent = OptionalBool(Params, TEXT("includeContent"), true);

	TArray<UPackage*> Dirty;
	for (TObjectIterator<UPackage> It; It; ++It)
	{
		UPackage* Pkg = *It;
		if (!Pkg || !Pkg->IsDirty()) continue;
		const FString Name = Pkg->GetName();
		const bool bIsMap = Pkg->ContainsMap();
		if (bIsMap && !bIncludeMaps) continue;
		if (!bIsMap && !bIncludeContent) continue;
		// Skip code modules + transient packages - only flush content packages
		// that live in mounted Content directories (have a resolvable .uasset
		// filename). Engine code packages like /Script/Engine should never be
		// touched by a content-save flush.
		if (Name.StartsWith(TEXT("/Script/"))) continue;
		if (Name.StartsWith(TEXT("/Temp/"))) continue;
		if (!FPackageName::IsValidLongPackageName(Name)) continue;
		Dirty.Add(Pkg);
	}

	TArray<TSharedPtr<FJsonValue>> Saved;
	TArray<TSharedPtr<FJsonValue>> Failed;

	for (UPackage* Pkg : Dirty)
	{
		const FString PackageName = Pkg->GetName();
		const FString Extension = Pkg->ContainsMap()
			? FPackageName::GetMapPackageExtension()
			: FPackageName::GetAssetPackageExtension();
		FString FileName;
		if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, FileName, Extension))
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("package"), PackageName);
			Entry->SetStringField(TEXT("error"), TEXT("could not resolve on-disk filename"));
			Failed.Add(MakeShared<FJsonValueObject>(Entry));
			continue;
		}
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.Error = GError;
		const bool bOk = UPackage::SavePackage(Pkg, nullptr, *FileName, SaveArgs);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("package"), PackageName);
		Entry->SetStringField(TEXT("file"), FileName);
		Entry->SetBoolField(TEXT("isMap"), Pkg->ContainsMap());
		if (bOk)
		{
			Saved.Add(MakeShared<FJsonValueObject>(Entry));
		}
		else
		{
			Entry->SetStringField(TEXT("error"), TEXT("SavePackage returned false"));
			Failed.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("dirtyCount"), Dirty.Num());
	Result->SetNumberField(TEXT("savedCount"), Saved.Num());
	Result->SetNumberField(TEXT("failedCount"), Failed.Num());
	Result->SetArrayField(TEXT("saved"), Saved);
	if (Failed.Num() > 0)
	{
		Result->SetArrayField(TEXT("failed"), Failed);
		Result->SetBoolField(TEXT("success"), false);
	}
	return MCPResult(Result);
}

// #340: list every dirty package (content + map) so callers can audit
// before flushing. Mirrors EditorLoadingAndSavingUtils.get_dirty_*_packages
// without the Python escape.
TSharedPtr<FJsonValue> FEditorHandlers::ListDirtyPackages(const TSharedPtr<FJsonObject>& Params)
{
	TArray<TSharedPtr<FJsonValue>> Content;
	TArray<TSharedPtr<FJsonValue>> Maps;
	for (TObjectIterator<UPackage> It; It; ++It)
	{
		UPackage* Pkg = *It;
		if (!Pkg || !Pkg->IsDirty()) continue;
		const FString Name = Pkg->GetName();
		if (Name.StartsWith(TEXT("/Script/"))) continue;
		if (Name.StartsWith(TEXT("/Temp/"))) continue;
		if (!FPackageName::IsValidLongPackageName(Name)) continue;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("package"), Name);
		if (Pkg->ContainsMap())
		{
			Maps.Add(MakeShared<FJsonValueObject>(Entry));
		}
		else
		{
			Content.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("contentCount"), Content.Num());
	Result->SetNumberField(TEXT("mapCount"), Maps.Num());
	Result->SetArrayField(TEXT("content"), Content);
	Result->SetArrayField(TEXT("maps"), Maps);
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

	// #224: treat templateLevel="Empty" / "None" / "" as "no template",
	// since callers reasonably read "Empty" as a sentinel for the empty
	// template. NewLevelFromTemplate("/Game/X", "Empty") otherwise tries to
	// load an asset literally named "Empty" and fails opaquely.
	const bool bHasTemplate = !TemplateLevel.IsEmpty()
		&& !TemplateLevel.Equals(TEXT("Empty"), ESearchCase::IgnoreCase)
		&& !TemplateLevel.Equals(TEXT("None"), ESearchCase::IgnoreCase);

	bool bSuccess = false;
	if (!bHasTemplate)
	{
		bSuccess = LevelEditorSubsystem->NewLevel(LevelPath);
	}
	else
	{
		bSuccess = LevelEditorSubsystem->NewLevelFromTemplate(LevelPath, TemplateLevel);
	}

	if (!bSuccess)
	{
		// #224: surface concrete reasons instead of a bare "Failed to create".
		FString Reason;
		if (LevelPath.IsEmpty())
		{
			Reason = TEXT("levelPath is required (e.g. \"/Game/Maps/MyLevel\")");
		}
		else if (!LevelPath.StartsWith(TEXT("/")))
		{
			Reason = FString::Printf(TEXT("levelPath must be a /Game/... mount point, got '%s'"), *LevelPath);
		}
		else if (bHasTemplate && !UEditorAssetLibrary::DoesAssetExist(TemplateLevel))
		{
			Reason = FString::Printf(TEXT("templateLevel asset not found: '%s' (omit or pass \"Empty\" for an empty level)"), *TemplateLevel);
		}
		else
		{
			Reason = FString::Printf(TEXT("LevelEditorSubsystem refused to create '%s' (path may be invalid, locked, or already open elsewhere)"), *LevelPath);
		}
		return MCPError(Reason);
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
TSharedPtr<FJsonValue> FEditorHandlers::CaptureScenePng(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	FString OutputPath;
	if (auto Err = RequireString(Params, TEXT("outputPath"), OutputPath)) return Err;

	// Resolution
	int32 Width = OptionalInt(Params, TEXT("width"), 1280);
	int32 Height = OptionalInt(Params, TEXT("height"), 720);
	Width = FMath::Clamp(Width, 16, 8192);
	Height = FMath::Clamp(Height, 16, 8192);

	const double Fov = OptionalNumber(Params, TEXT("fov"), 90.0);

	// Location / rotation
	FVector Location = FVector::ZeroVector;
	if (const TSharedPtr<FJsonObject>* LocObj = nullptr; Params->TryGetObjectField(TEXT("location"), LocObj) && LocObj && LocObj->IsValid())
	{
		(*LocObj)->TryGetNumberField(TEXT("x"), Location.X);
		(*LocObj)->TryGetNumberField(TEXT("y"), Location.Y);
		(*LocObj)->TryGetNumberField(TEXT("z"), Location.Z);
	}
	FRotator Rotation = FRotator::ZeroRotator;
	if (const TSharedPtr<FJsonObject>* RotObj = nullptr; Params->TryGetObjectField(TEXT("rotation"), RotObj) && RotObj && RotObj->IsValid())
	{
		(*RotObj)->TryGetNumberField(TEXT("pitch"), Rotation.Pitch);
		(*RotObj)->TryGetNumberField(TEXT("yaw"), Rotation.Yaw);
		(*RotObj)->TryGetNumberField(TEXT("roll"), Rotation.Roll);
	}

	// Find or spawn the reusable capture actor.
	static const FString CaptureLabel = TEXT("__ClaudeSceneCapture");
	ASceneCapture2D* CaptureActor = nullptr;
	for (TActorIterator<ASceneCapture2D> It(World); It; ++It)
	{
		if (It->GetActorLabel() == CaptureLabel)
		{
			CaptureActor = *It;
			break;
		}
	}
	if (!CaptureActor)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		CaptureActor = World->SpawnActor<ASceneCapture2D>(ASceneCapture2D::StaticClass(), Location, Rotation, SpawnParams);
		if (!CaptureActor) return MCPError(TEXT("Failed to spawn SceneCapture2D actor"));
		CaptureActor->SetActorLabel(CaptureLabel);
		CaptureActor->SetActorHiddenInGame(true);
	}
	CaptureActor->SetActorLocationAndRotation(Location, Rotation);

	USceneCaptureComponent2D* Comp = CaptureActor->GetCaptureComponent2D();
	if (!Comp) return MCPError(TEXT("SceneCapture2D has no capture component"));
	Comp->FOVAngle = (float)Fov;
	Comp->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Comp->bCaptureEveryFrame = false;
	Comp->bCaptureOnMovement = false;

	// Transient render target
	UTextureRenderTarget2D* RT = UKismetRenderingLibrary::CreateRenderTarget2D(
		World, Width, Height, ETextureRenderTargetFormat::RTF_RGBA8_SRGB, FLinearColor::Black, false);
	if (!RT) return MCPError(TEXT("Failed to create RenderTarget2D"));
	Comp->TextureTarget = RT;

	Comp->CaptureScene();

	// Split outputPath into directory + filename for ExportRenderTarget.
	FString AbsPath = OutputPath;
	if (FPaths::IsRelative(AbsPath))
	{
		AbsPath = FPaths::Combine(FPaths::ProjectDir(), AbsPath);
	}
	if (!AbsPath.EndsWith(TEXT(".png"))) AbsPath += TEXT(".png");
	FString OutDir = FPaths::GetPath(AbsPath);
	FString OutName = FPaths::GetCleanFilename(AbsPath);
	IFileManager::Get().MakeDirectory(*OutDir, /*Tree*/ true);

	UKismetRenderingLibrary::ExportRenderTarget(World, RT, OutDir, OutName);

	const int64 Size = IFileManager::Get().FileSize(*AbsPath);
	if (Size < 0)
	{
		return MCPError(FString::Printf(TEXT("Export did not produce a file at %s"), *AbsPath));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AbsPath);
	Result->SetNumberField(TEXT("width"), Width);
	Result->SetNumberField(TEXT("height"), Height);
	Result->SetNumberField(TEXT("sizeBytes"), (double)Size);
	Result->SetStringField(TEXT("actorLabel"), CaptureLabel);
	return MCPResult(Result);
}
