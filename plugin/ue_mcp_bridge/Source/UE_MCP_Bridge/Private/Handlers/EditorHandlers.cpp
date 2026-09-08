#include "EditorHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerPagination.h"

#include "MessageLogModule.h"
#include "IMessageLogListing.h"
#include "Logging/TokenizedMessage.h"
#include "Editor/EditorPerformanceSettings.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Scalability.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Editor/EditorEngine.h"
#include "Editor.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "IPythonScriptPlugin.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "ISettingsModule.h"
#include "Interfaces/IMainFrameModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ConfigContext.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "LevelEditorViewport.h"
#include "UnrealClient.h"
#include "Engine/GameViewportClient.h"
#include "ContentStreaming.h"
#include "RenderingThread.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Slate/SceneViewport.h"
#include "HAL/PlatformMemory.h"
#include "Misc/App.h"
#include "Logging/MessageLog.h"
#include "HighResScreenshot.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
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
#include "Containers/Ticker.h"
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
#include "HandlerPropertyText.h"
#include "JsonSerializer.h"
#include "Engine/Blueprint.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

namespace
{
	struct FDirtyEditorPackages
	{
		TArray<FString> Content;
		TArray<FString> Maps;
	};

	FDirtyEditorPackages CollectDirtyEditorPackages()
	{
		FDirtyEditorPackages Dirty;
		TArray<UPackage*> DirtyPackages;
		FEditorFileUtils::GetDirtyContentPackages(DirtyPackages);
		FEditorFileUtils::GetDirtyWorldPackages(DirtyPackages);
		for (UPackage* Package : DirtyPackages)
		{
			if (!Package || !Package->IsDirty())
			{
				continue;
			}

			const FString PackageName = Package->GetName();
			if (PackageName.StartsWith(TEXT("/Script/")))
			{
				continue;
			}

			(Package->ContainsMap() ? Dirty.Maps : Dirty.Content).AddUnique(PackageName);
		}

		Dirty.Content.Sort();
		Dirty.Maps.Sort();
		return Dirty;
	}

	TArray<TSharedPtr<FJsonValue>> PackageNamesToJson(const TArray<FString>& PackageNames)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		Values.Reserve(PackageNames.Num());
		for (const FString& PackageName : PackageNames)
		{
			Values.Add(MakeShared<FJsonValueString>(PackageName));
		}
		return Values;
	}

	bool ResolveEditorObjectFromPath(const FString& ObjectPath, UObject*& OutObject, FString& OutResolvedKind, FString& OutError)
	{
		UObject* Object = LoadObject<UObject>(nullptr, *ObjectPath);
		if (!Object)
		{
			Object = StaticFindObject(UObject::StaticClass(), nullptr, *ObjectPath);
		}
		if (!Object)
		{
			Object = UEditorAssetLibrary::LoadAsset(ObjectPath);
		}
		if (!Object)
		{
			OutError = FString::Printf(TEXT("Object not found: %s"), *ObjectPath);
			return false;
		}

		OutResolvedKind = TEXT("object");
		if (UClass* Cls = Cast<UClass>(Object))
		{
			Object = Cls->GetDefaultObject();
			OutResolvedKind = TEXT("classDefaultObject");
		}
		else if (UBlueprint* BP = Cast<UBlueprint>(Object))
		{
			if (!BP->GeneratedClass)
			{
				OutError = FString::Printf(TEXT("Blueprint has no generated class: %s"), *ObjectPath);
				return false;
			}
			Object = BP->GeneratedClass->GetDefaultObject();
			OutResolvedKind = TEXT("blueprintDefaultObject");
		}

		if (!Object)
		{
			OutError = FString::Printf(TEXT("Resolved object is null: %s"), *ObjectPath);
			return false;
		}

		OutObject = Object;
		return true;
	}

	TSharedPtr<FJsonObject> DescribeProperty(FProperty* Prop, const void* ValuePtr, UObject* Owner, bool bIncludeValue)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		if (!Prop)
		{
			return Obj;
		}

		Obj->SetStringField(TEXT("name"), Prop->GetName());
		Obj->SetStringField(TEXT("type"), Prop->GetCPPType());
		Obj->SetBoolField(TEXT("editable"), Prop->HasAnyPropertyFlags(CPF_Edit));
		Obj->SetBoolField(TEXT("blueprintVisible"), Prop->HasAnyPropertyFlags(CPF_BlueprintVisible));
		Obj->SetBoolField(TEXT("config"), Prop->HasAnyPropertyFlags(CPF_Config));
		Obj->SetBoolField(TEXT("transient"), Prop->HasAnyPropertyFlags(CPF_Transient));

		if (bIncludeValue && ValuePtr)
		{
			Obj->SetField(TEXT("value"), FMCPJsonSerializer::SerializeValue(ValuePtr, Prop));

			FString ValueText;
			Prop->ExportText_Direct(ValueText, ValuePtr, ValuePtr, Owner, PPF_None);
			Obj->SetStringField(TEXT("valueText"), ValueText);

			// #820: say outright whether this text can be copied back into a
			// setter. A TMap with a struct key exports pairs that do not read
			// back, and writing that text used to empty the map. `value` always
			// round-trips; `valueText` does not always.
			if (MCPPropertyText::ContainsMap(Prop))
			{
				Obj->SetBoolField(TEXT("valueTextRoundTrips"),
					MCPPropertyText::ExportedTextRoundTrips(Prop, ValuePtr, Owner));
			}
		}

		return Obj;
	}
}

void FEditorHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	// Install log capture ring buffer (#82)
	FMCPLogCapture::Get().Install();

	Registry.RegisterHandler(TEXT("execute_command"), &ExecuteCommand);
	Registry.RegisterHandler(TEXT("execute_python"), &ExecutePython);
	Registry.RegisterHandler(TEXT("run_python_file"), &RunPythonFile);
	Registry.RegisterHandler(TEXT("set_property"), &SetProperty);
	Registry.RegisterHandler(TEXT("get_property"), &GetProperty);
	Registry.RegisterHandler(TEXT("describe_object"), &DescribeObject);
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
	Registry.RegisterHandler(TEXT("get_viewport_state"), &GetViewportState);
	Registry.RegisterHandler(TEXT("set_view_mode"), &SetViewMode);
	Registry.RegisterHandler(TEXT("set_viewport_exposure"), &SetViewportExposure);
	Registry.RegisterHandler(TEXT("set_viewport_view"), &SetViewportView);
	Registry.RegisterHandler(TEXT("set_game_view"), &SetGameView);
	Registry.RegisterHandler(TEXT("redraw_viewport"), &RedrawViewport);
	Registry.RegisterHandler(TEXT("begin_editor_transaction"), &BeginEditorTransaction);
	Registry.RegisterHandler(TEXT("end_editor_transaction"), &EndEditorTransaction);
	Registry.RegisterHandler(TEXT("cancel_editor_transaction"), &CancelEditorTransaction);
	Registry.RegisterHandler(TEXT("get_undo_state"), &GetUndoState);
	Registry.RegisterHandler(TEXT("undo_redo_steps"), &UndoRedoSteps);
	Registry.RegisterHandler(TEXT("get_transaction_history"), &GetTransactionHistory);
	// Insights trace control, frame timing and standalone runs, in
	// EditorHandlers_Profiling.cpp.
	Registry.RegisterHandler(TEXT("start_insights_trace"), &StartInsightsTrace);
	Registry.RegisterHandler(TEXT("stop_insights_trace"), &StopInsightsTrace);
	Registry.RegisterHandler(TEXT("pause_insights_trace"), &PauseInsightsTrace);
	Registry.RegisterHandler(TEXT("get_insights_trace_status"), &GetInsightsTraceStatus);
	Registry.RegisterHandler(TEXT("list_trace_channels"), &ListTraceChannels);
	Registry.RegisterHandler(TEXT("set_trace_channels"), &SetTraceChannels);
	Registry.RegisterHandler(TEXT("begin_profile_region"), &BeginProfileRegion);
	Registry.RegisterHandler(TEXT("end_profile_region"), &EndProfileRegion);
	Registry.RegisterHandler(TEXT("add_trace_bookmark"), &AddTraceBookmark);
	Registry.RegisterHandler(TEXT("get_frame_timing"), &GetFrameTiming);
	// trigger_hitch blocks the game thread for up to 5 seconds by design, which
	// is longer than a default handler budget allows for.
	Registry.RegisterHandlerWithTimeout(TEXT("trigger_hitch"), &TriggerHitch, 30.0f);
	Registry.RegisterHandler(TEXT("launch_standalone_game"), &LaunchStandaloneGame);
	Registry.RegisterHandler(TEXT("get_standalone_status"), &GetStandaloneStatus);
	Registry.RegisterHandler(TEXT("stop_standalone_game"), &StopStandaloneGame);
	Registry.RegisterHandler(TEXT("reload_handlers"), &ReloadHandlers);
	// save_asset is owned by FAssetHandlers (#768: adds force, file size, mtime).
	// Registering it here too meant the winner was decided by registration
	// order in BridgeServer.cpp, which is not a contract.
	Registry.RegisterHandler(TEXT("save_dirty"), &SaveDirty);
	Registry.RegisterHandler(TEXT("list_dirty_packages"), &ListDirtyPackages);
	Registry.RegisterHandler(TEXT("get_world_state"), &GetWorldState);
	Registry.RegisterHandler(TEXT("request_editor_shutdown"), &RequestEditorShutdown);
	Registry.RegisterHandler(TEXT("list_pie_instances"), &ListPIEInstances);
	Registry.RegisterHandler(TEXT("invoke_object_function"), &InvokeObjectFunction);
	Registry.RegisterHandlerWithTimeout(TEXT("invoke_object_functions"), &InvokeObjectFunctions, 300.0f);
	Registry.RegisterHandler(TEXT("get_object_properties"), &GetObjectProperties);
	Registry.RegisterHandler(TEXT("read_bone_transforms"), &ReadBoneTransforms);
	Registry.RegisterHandler(TEXT("teleport_runtime_actor"), &TeleportRuntimeActor);
	Registry.RegisterHandler(TEXT("set_movement_mode"), &SetMovementMode);
	Registry.RegisterHandler(TEXT("set_runtime_visibility"), &SetRuntimeVisibility);
	Registry.RegisterHandler(TEXT("restore_runtime_visibility"), &RestoreRuntimeVisibility);
	// #802: resolve a live instance path, and write to a live instance.
	Registry.RegisterHandler(TEXT("find_object"), &FindLiveObjects);
	Registry.RegisterHandler(TEXT("set_object_property"), &SetObjectProperty);
	Registry.RegisterHandler(TEXT("build_lighting"), &BuildLighting);
	Registry.RegisterHandler(TEXT("build_all"), &BuildAll);
	Registry.RegisterHandler(TEXT("validate_assets"), &ValidateAssets);
	Registry.RegisterHandler(TEXT("cook_content"), &CookContent);
	Registry.RegisterHandler(TEXT("focus_viewport_on_actor"), &FocusViewportOnActor);
	Registry.RegisterHandler(TEXT("hot_reload"), &HotReload);
	Registry.RegisterHandler(TEXT("create_new_level"), &CreateNewLevel);
	Registry.RegisterHandler(TEXT("save_current_level"), &SaveCurrentLevel);
	Registry.RegisterHandler(TEXT("open_asset"), &OpenAsset);
	Registry.RegisterHandler(TEXT("get_runtime_value"), &PieGetRuntimeValue);
	// New handlers
	Registry.RegisterHandler(TEXT("run_stat_command"), &RunStatCommand);
	Registry.RegisterHandler(TEXT("set_scalability"), &SetScalability);
	Registry.RegisterHandler(TEXT("set_cvars"), &SetCVars);
	Registry.RegisterHandler(TEXT("build_geometry"), &BuildGeometry);
	Registry.RegisterHandler(TEXT("build_hlod"), &BuildHlod);
	Registry.RegisterHandler(TEXT("list_crashes"), &ListCrashes);
	Registry.RegisterHandler(TEXT("get_crash_info"), &GetCrashInfo);
	Registry.RegisterHandler(TEXT("check_for_crashes"), &CheckForCrashes);
	// #693: headless automation test runner.
	Registry.RegisterHandlerWithTimeout(TEXT("run_automation_tests"), &RunAutomationTests, 300.0f);
	// #14: Build project
	Registry.RegisterHandler(TEXT("build_project"), &BuildProject);
	// #49: Generate project files
	Registry.RegisterHandler(TEXT("generate_project_files"), &GenerateProjectFiles);
	// #126: fast-forward PIE game time
	Registry.RegisterHandler(TEXT("set_pie_time_scale"), &SetPieTimeScale);
	Registry.RegisterHandler(TEXT("capture_scene_png"), &CaptureScenePng);
	Registry.RegisterHandler(TEXT("set_realtime"), &SetRealtime);
	Registry.RegisterHandler(TEXT("get_pie_pawn"), &GetPiePawn);
	Registry.RegisterHandler(TEXT("invoke_function"), &InvokeFunction);
	Registry.RegisterHandler(TEXT("invoke_static_function"), &InvokeStaticFunction);
	Registry.RegisterHandler(TEXT("configure_pie"), &ConfigurePie);
	Registry.RegisterHandler(TEXT("get_pie_config"), &GetPieConfig);
	Registry.RegisterHandler(TEXT("pie_set_player_view"), &PieSetPlayerView);
	Registry.RegisterHandler(TEXT("stage_game_input"), &StageGameInput);
	// #455: discover UBlueprintFunctionLibrary classes (GeometryScript,
	// Kismet, anything user-defined). Pair with editor.invoke_function to
	// drive GeometryScript ops from MCP without hand-writing each handler.
	Registry.RegisterHandler(TEXT("list_function_libraries"), &ListFunctionLibraries);
	// #718: close the open Level Sequence editor before destructive actor ops.
	Registry.RegisterHandler(TEXT("close_sequence"), &CloseSequence);
	// #719: purge cached embedded-Python modules by prefix for tool-dev iteration.
	Registry.RegisterHandler(TEXT("purge_python_modules"), &PurgePythonModules);
	// #727: open a registered editor tab / Project Settings viewer for visual evidence.
	Registry.RegisterHandler(TEXT("open_tab"), &OpenTab);
	Registry.RegisterHandler(TEXT("open_settings"), &OpenSettings);
}

TSharedPtr<FJsonValue> FEditorHandlers::ExecuteCommand(const TSharedPtr<FJsonObject>& Params)
{
	FString Command;
	if (auto Err = RequireString(Params, TEXT("command"), Command)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	UKismetSystemLibrary::ExecuteConsoleCommand(World, Command, nullptr);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("command"), Command);
	// The command was dispatched. Whether it changed anything is the command's
	// business: the console reports nothing back that this handler could read.
	Result->SetBoolField(TEXT("changed"), true);
	// No rollback. An arbitrary console command has no captured before-state:
	// the string is opaque here, most commands are not writes to a value that
	// could be read first, and the ones that are (a cvar assignment, a toggle)
	// invert to their previous value rather than to running the same command
	// again. Use editor(set_cvars) when the write is a console variable - it
	// reads the old value first and emits the inverse.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("A console command's effect is opaque to the bridge and nothing was captured before it ran. Re-running the same command is not an inverse. For console variables use editor(set_cvars), which records the previous value and emits a rollback that restores it."));
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

	// #732: a first-class result channel. In ExecuteFile mode CommandResult is
	// normally empty and a top-level `return` is illegal, so scripts were forced
	// to use print() as transport - mixing application data with diagnostics and
	// duplicating it across log_output/output. When the caller names a
	// resultVariable, evaluate it in the Public (__main__) scope the script just
	// ran in and surface it as `result`, leaving print()/log as diagnostics only.
	FString ResultText = PythonCommand.CommandResult;
	const FString ResultVariable = OptionalString(Params, TEXT("resultVariable"));
	bool bResultVariableResolved = false;
	if (bSuccess && !ResultVariable.IsEmpty())
	{
		FPythonCommandEx EvalCommand;
		EvalCommand.Command = ResultVariable;
		EvalCommand.ExecutionMode = EPythonCommandExecutionMode::EvaluateStatement;
		EvalCommand.FileExecutionScope = EPythonFileExecutionScope::Public;
		if (PythonPlugin->ExecPythonCommandEx(EvalCommand))
		{
			ResultText = EvalCommand.CommandResult;
			bResultVariableResolved = true;
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bSuccess);
	Result->SetStringField(TEXT("result"), ResultText);
	if (!ResultVariable.IsEmpty())
	{
		Result->SetBoolField(TEXT("resultVariableResolved"), bResultVariableResolved);
	}

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

	// The script ran (or failed to). What it wrote is its own business: the
	// interpreter reports no diff this handler could read, and a script that is
	// a pure query is indistinguishable from one that rewrote the project.
	Result->SetBoolField(TEXT("changed"), bSuccess);
	// No rollback. Arbitrary Python can do anything, nothing was captured
	// before it ran, and there is no inverse script to name.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Arbitrary Python has no inverse and nothing was captured before it ran. Wrap the writes in editor(begin_editor_transaction) and roll back with editor(cancel_editor_transaction) when they are transactional, or write the undo script yourself."));

	return MCPResult(Result);
}

// #142 - Run a Python script file on disk with __file__/__name__ context populated.
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

	// #732: same first-class result channel as execute_python. The file runs in
	// the Public (__main__) scope, so a named resultVariable can be read back.
	FString ResultText = PythonCommand.CommandResult;
	const FString ResultVariable = OptionalString(Params, TEXT("resultVariable"));
	bool bResultVariableResolved = false;
	if (bSuccess && !ResultVariable.IsEmpty())
	{
		FPythonCommandEx EvalCommand;
		EvalCommand.Command = ResultVariable;
		EvalCommand.ExecutionMode = EPythonCommandExecutionMode::EvaluateStatement;
		EvalCommand.FileExecutionScope = EPythonFileExecutionScope::Public;
		if (PythonPlugin->ExecPythonCommandEx(EvalCommand))
		{
			ResultText = EvalCommand.CommandResult;
			bResultVariableResolved = true;
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bSuccess);
	Result->SetStringField(TEXT("path"), FilePath);
	Result->SetStringField(TEXT("result"), ResultText);
	if (!ResultVariable.IsEmpty())
	{
		Result->SetBoolField(TEXT("resultVariableResolved"), bResultVariableResolved);
	}

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

	// The file ran (or failed to). What it wrote is its own business: the
	// interpreter reports no diff this handler could read.
	Result->SetBoolField(TEXT("changed"), bSuccess);
	// No rollback. An arbitrary script file can do anything, nothing was
	// captured before it ran, and there is no inverse script to name.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("An arbitrary script file has no inverse and nothing was captured before it ran. Wrap the writes in editor(begin_editor_transaction) and roll back with editor(cancel_editor_transaction) when they are transactional, or ship an undo script alongside it."));

	return MCPResult(Result);
}

// #719: UE's embedded Python caches imported modules for the whole editor
// session, so after editing a pipeline tool on disk the editor keeps running
// stale code until the modules are purged from sys.modules. sys.modules is
// Python-runtime state with no C++ accessor, so the interpreter (a hard plugin
// dependency) is the correct owner to drive. We emit per-module markers rather
// than rely on eval repr/str ambiguity, then rebuild the list in C++.
TSharedPtr<FJsonValue> FEditorHandlers::PurgePythonModules(const TSharedPtr<FJsonObject>& Params)
{
	FString Prefix;
	if (auto Err = RequireString(Params, TEXT("prefix"), Prefix)) return Err;
	if (Prefix.TrimStartAndEnd().IsEmpty())
	{
		return MCPError(TEXT("'prefix' must be non-empty (an empty prefix would purge every module)"));
	}

	IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
	if (!PythonPlugin || !PythonPlugin->IsPythonAvailable())
	{
		return MCPError(TEXT("Python scripting is not available"));
	}

	// Escape the prefix into a Python single-quoted literal.
	FString Escaped = Prefix.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("'"), TEXT("\\'"));

	FString Code;
	Code += TEXT("import sys as __mcp_sys\n");
	Code += FString::Printf(TEXT("__mcp_names = [__m for __m in list(__mcp_sys.modules) if __m.startswith('%s')]\n"), *Escaped);
	Code += TEXT("for __m in __mcp_names:\n");
	Code += TEXT("    del __mcp_sys.modules[__m]\n");
	Code += TEXT("    print('MCP_PURGED_ITEM:' + __m)\n");

	FPythonCommandEx PythonCommand;
	PythonCommand.Command = Code;
	PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
	PythonCommand.FileExecutionScope = EPythonFileExecutionScope::Public;
	const bool bSuccess = PythonPlugin->ExecPythonCommandEx(PythonCommand);
	if (!bSuccess)
	{
		return MCPError(TEXT("Failed to purge Python modules (interpreter error)"));
	}

	TArray<TSharedPtr<FJsonValue>> Purged;
	const FString Marker = TEXT("MCP_PURGED_ITEM:");
	for (const FPythonLogOutputEntry& Entry : PythonCommand.LogOutput)
	{
		FString Line = Entry.Output;
		int32 Idx = Line.Find(Marker);
		if (Idx != INDEX_NONE)
		{
			FString Name = Line.RightChop(Idx + Marker.Len()).TrimStartAndEnd();
			if (!Name.IsEmpty())
			{
				Purged.Add(MakeShared<FJsonValueString>(Name));
			}
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("prefix"), Prefix);
	Result->SetArrayField(TEXT("purged"), Purged);
	Result->SetNumberField(TEXT("count"), Purged.Num());
	// A prefix that matched nothing left sys.modules exactly as it was, which
	// is also what a replay of this call finds.
	Result->SetBoolField(TEXT("changed"), Purged.Num() > 0);
	// No rollback. Re-importing is not the inverse of a purge: it reloads the
	// modules from what is on disk NOW, and the module objects the purge
	// dropped (with whatever state they held) are gone. Reloading the current
	// files is also the reason the purge was asked for in the first place.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("The purged module objects and the state they held cannot be put back. Importing the same names again builds new modules from the files on disk, which is a fresh load rather than a restore, and there is no bridge call that does it."));
	return MCPResult(Result);
}

// #718: close the currently open Level Sequence editor. Open sequences
// re-resolve possessables by name during actor destruction, which can mis-bind
// or destabilize the editor, so bulk actor ops want the sequencer closed first.
TSharedPtr<FJsonValue> FEditorHandlers::CloseSequence(const TSharedPtr<FJsonObject>& /*Params*/)
{
	ULevelSequence* Current = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	const bool bWasOpen = Current != nullptr;
	const FString OpenPath = bWasOpen ? Current->GetPathName() : FString();

	ULevelSequenceEditorBlueprintLibrary::CloseLevelSequence();

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("wasOpen"), bWasOpen);
	Result->SetBoolField(TEXT("changed"), bWasOpen);
	if (bWasOpen)
	{
		Result->SetStringField(TEXT("closedSequence"), OpenPath);
		// The inverse of closing the Sequencer on a sequence is opening that
		// sequence again, which is what open_asset does for a ULevelSequence.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), OpenPath);
		MCPSetRollback(Result, TEXT("open_asset"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Reopens the same sequence in Sequencer. Only the fact that it is open comes back: the playhead position, the selection, the track expansion and any locked-camera state the closed editor held are not captured and are not restored."));
	}
	else
	{
		// Nothing was open, so nothing was closed and there is nothing to
		// reopen. This is the idempotent replay of a flow that already closed.
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("No sequence was open, so nothing changed and there is nothing to restore."));
	}
	return MCPResult(Result);
}

// #727: open a registered editor tab by ID (e.g. "ProjectSettings", "OutputLog")
// so an agent can screenshot editor UI as evidence.
TSharedPtr<FJsonValue> FEditorHandlers::OpenTab(const TSharedPtr<FJsonObject>& Params)
{
	FString TabId;
	if (auto Err = RequireString(Params, TEXT("tabId"), TabId)) return Err;

	// Asked before invoking: TryInvokeTab draws attention to a tab that is
	// already up and returns it either way, so this is the only point at which
	// "was it already open" is answerable.
	const bool bAlreadyOpen = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(*TabId)).IsValid();
	TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->TryInvokeTab(FTabId(*TabId));

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("tabId"), TabId);
	Result->SetBoolField(TEXT("opened"), Tab.IsValid());
	Result->SetBoolField(TEXT("alreadyOpen"), bAlreadyOpen);
	Result->SetBoolField(TEXT("changed"), Tab.IsValid() && !bAlreadyOpen);
	if (!Tab.IsValid())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("No registered tab with id '%s' (try 'ProjectSettings', 'OutputLog', 'ContentBrowserTab1', ...)"), *TabId));
	}
	// No rollback: nothing in the bridge closes an editor tab. close_sequence
	// is the one exception and it is specific to Sequencer, not a tab id.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("No bridge call closes an editor tab by id, so an opened tab cannot be put away again. Nothing outside the editor's own window layout was written."));
	return MCPResult(Result);
}

// #727: open (and navigate) a settings viewer - Project Settings / Editor
// Preferences. section may be a bare section name (with category) or a dotted
// "Category.Section" pair for convenience.
TSharedPtr<FJsonValue> FEditorHandlers::OpenSettings(const TSharedPtr<FJsonObject>& Params)
{
	FString Container = OptionalString(Params, TEXT("container"), TEXT("Project"));
	FString Category = OptionalString(Params, TEXT("category"));
	FString Section = OptionalString(Params, TEXT("section"));

	// Accept a combined "Category.Section" in `section` when `category` is absent.
	if (Category.IsEmpty() && Section.Contains(TEXT(".")))
	{
		FString Left, Right;
		if (Section.Split(TEXT("."), &Left, &Right))
		{
			Category = Left;
			Section = Right;
		}
	}

	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (!SettingsModule)
	{
		return MCPError(TEXT("Settings module not available"));
	}

	// Make sure the viewer tab exists, then show the requested section.
	const FTabId ViewerTabId(Container == TEXT("Editor") ? TEXT("EditorSettings") : TEXT("ProjectSettings"));
	const bool bAlreadyOpen = FGlobalTabmanager::Get()->FindExistingLiveTab(ViewerTabId).IsValid();
	FGlobalTabmanager::Get()->TryInvokeTab(ViewerTabId);
	if (!Category.IsEmpty())
	{
		SettingsModule->ShowViewer(FName(*Container), FName(*Category), FName(*Section));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("container"), Container);
	Result->SetStringField(TEXT("category"), Category);
	Result->SetStringField(TEXT("section"), Section);
	Result->SetBoolField(TEXT("alreadyOpen"), bAlreadyOpen);
	// Navigating an already-open viewer to a different section is still a
	// change to what it shows, so only a repeat with no section is a true no-op.
	Result->SetBoolField(TEXT("changed"), !bAlreadyOpen || !Category.IsEmpty());
	// No rollback: nothing in the bridge closes a settings viewer or reads back
	// which section it was showing, so neither half can be restored.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("No bridge call closes the settings viewer, and the section it was showing before is not readable, so there is nothing to restore it to. This opens and navigates editor UI; it writes no project setting."));
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

	UObject* Asset = nullptr;
	FString ResolvedKind;
	FString ResolveObjectErr;
	if (!ResolveEditorObjectFromPath(AssetPath, Asset, ResolvedKind, ResolveObjectErr))
	{
		return MCPError(ResolveObjectErr);
	}

	TSharedPtr<FJsonValue> ValueJsonRef = Params->TryGetField(TEXT("value"));
	if (!ValueJsonRef.IsValid())
	{
		return MCPError(TEXT("Missing 'value' parameter"));
	}

	FProperty* Property = nullptr;
	void* PropertyValue = nullptr;
	UObject* LeafOwner = nullptr;
	FString ResolvePropertyErr;
	if (!MCPJsonProperty::ResolveDottedPath(Asset, PropertyName, Property, PropertyValue, LeafOwner, ResolvePropertyErr))
	{
		return MCPError(ResolvePropertyErr);
	}

	// Capture what is about to be overwritten. This is the only value an
	// inverse call could restore, and it is unreadable once the write lands.
	//
	// Structured, not export text. Exported text is refused or misread on
	// replay in three ways: a TMap with a struct key exports pairs that
	// ImportMapText rejects, a gameplay tag exports as TagName="A.B" rather
	// than the plain name its setter looks up, and a null object reference
	// exports as the literal None, which the setter then tries to load.
	// SerializeValue is the read half of the setter's own format.
	//
	// It does NOT make a float exact. MCPJsonProperty::SetJsonOnPropertyImpl
	// has no numeric branch (its only integer write is the enum one), so a
	// JSON number falls through to the export-text fallback, is stringified by
	// FString::SanitizeFloat, and SanitizeFloat is Printf("%f"): six
	// fractional digits with trailing zeros stripped. So 1e-9 still restores
	// as zero and 0.123456789 still restores as 0.123457, and an int64 past
	// 2^53 loses its low bits to the double it travelled in. The truncation
	// moved from the reader to the writer rather than going away. What did
	// change is that the probe below now MEASURES it, so those values are
	// reported rollbackLossy instead of silently restoring wrong. Making them
	// exact needs a numeric branch in the shared setter, which is not this
	// file.
	TSharedPtr<FJsonValue> PreviousStructured =
		FMCPJsonSerializer::SerializeValue(PropertyValue, Property);

	// A raw copy of the same value, which answers two questions no pair of
	// exported strings can: whether the write below actually changed anything,
	// and whether the rollback this call is about to advertise really restores
	// what was here. FText and instanced subobjects are new instances on every
	// import and are never Identical, so they are reported as uncompared
	// rather than as a false mismatch.
	const bool bComparable = MCPJsonProperty::PropertyIsComparableByIdentity(Property);
	FDefaultConstructedPropertyElement PreviousRaw(Property);
	Property->CopyCompleteValue(PreviousRaw.GetObjAddress(), PropertyValue);

	// Prove the round trip rather than asserting it: apply the exact value the
	// rollback would send to a scratch copy and compare. The two ways this can
	// go wrong are kept apart, because they call for opposite answers. If the
	// setter REFUSES the value the rollback cannot run at all and must not be
	// advertised - an FTransform is the common case, since it serializes as
	// translation/rotation/scale with a Rotator while its reflected fields are
	// Translation, Rotation as an FQuat and Scale3D, so the replay fails on
	// the field names. If the setter accepts it and the result differs, the
	// rollback runs and lands somewhere near, which is lossy but usable.
	// This costs one deserialize on a path that already runs
	// PostEditChangeProperty and, by default, writes the package to disk.
	bool bRollbackApplies = false;
	bool bRollbackRoundTrips = false;
	FString ProbeError;
	if (bComparable)
	{
		FDefaultConstructedPropertyElement Probe(Property);
		bRollbackApplies =
			MCPJsonProperty::SetJsonOnProperty(Property, Probe.GetObjAddress(), PreviousStructured, ProbeError);
		bRollbackRoundTrips = bRollbackApplies
			&& Property->Identical(Probe.GetObjAddress(), PreviousRaw.GetObjAddress(), PPF_None);
	}
	else
	{
		// Not comparable, so the probe would prove nothing either way. The
		// rollback is still offered: it is the same call that wrote the value
		// in the first place.
		bRollbackApplies = true;
	}

	Asset->Modify();
	if (LeafOwner && LeafOwner != Asset)
	{
		LeafOwner->Modify();
	}

	// #210/#221: route through the recursive setter so JSON objects, arrays,
	// asset-path strings (FObjectProperty), and nested structs all apply
	// without callers having to pre-format UE text.
	FString SetErr;
	if (!MCPJsonProperty::SetJsonOnProperty(Property, PropertyValue, ValueJsonRef, SetErr))
	{
		return MCPError(FString::Printf(TEXT("Failed to set '%s': %s"), *PropertyName, *SetErr));
	}

	FPropertyChangedEvent ChangeEvent(Property);
	(LeafOwner ? LeafOwner : Asset)->PostEditChangeProperty(ChangeEvent);
	Asset->MarkPackageDirty();
	// #674: opt out of the immediate disk save. Default true preserves the
	// prior behavior; pass save=false to leave the package dirty in-memory
	// (batch many writes, then save_dirty / save_asset once).
	const bool bSave = OptionalBool(Params, TEXT("save"), true);
	if (bSave)
	{
		UEditorAssetLibrary::SaveLoadedAsset(Asset, /*bOnlyIfIsDirty=*/true);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), AssetPath);
	Result->SetStringField(TEXT("resolvedPath"), Asset->GetPathName());
	Result->SetStringField(TEXT("resolvedKind"), ResolvedKind);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("type"), Property->GetCPPType());
	Result->SetBoolField(TEXT("saved"), bSave);
	// previousValue and value are BOTH structured, and set_object_property
	// reports the same pair in the same form. A caller that reads them off one
	// action must not have to re-parse them off the other.
	Result->SetField(TEXT("previousValue"), PreviousStructured);
	Result->SetField(TEXT("value"), FMCPJsonSerializer::SerializeValue(PropertyValue, Property));
	// The property's own comparison, not a string one. A value not comparable
	// by identity says so instead of claiming a no-op it cannot establish.
	const bool bChanged = !bComparable
		|| !Property->Identical(PropertyValue, PreviousRaw.GetObjAddress(), PPF_None);
	Result->SetBoolField(TEXT("changed"), bChanged);
	// changeDetected has ONE meaning everywhere it appears: true when `changed`
	// came from comparing the value before against the value after, false when
	// it came from anything else.
	Result->SetBoolField(TEXT("changeDetected"), bComparable);
	if (bChanged) MCPSetUpdated(Result);

	if (bRollbackApplies)
	{
		// Self-inverse: the same handler with the value this call replaced, in
		// the structured form the setter takes back. Addressed with the path the
		// caller used, which has just been proved to resolve to this object
		// under these same rules, and with the same save flag so the undo
		// persists exactly as far as the write did.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("objectPath"), AssetPath);
		Payload->SetStringField(TEXT("propertyName"), PropertyName);
		Payload->SetField(TEXT("value"), PreviousStructured);
		Payload->SetBoolField(TEXT("save"), bSave);
		MCPSetRollback(Result, TEXT("set_property"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), !bRollbackRoundTrips);
		if (bRollbackRoundTrips)
		{
			Result->SetStringField(TEXT("rollbackNote"),
				TEXT("The rollback value was applied to a scratch copy of this property and came back identical, so it restores the property's own value. That is what was measured: PostEditChangeProperty and the save are replayed but not verified, and side effects the first write had on other objects are not covered."));
		}
		else
		{
			Result->SetStringField(TEXT("rollbackNote"), bComparable
				? TEXT("The rollback value applied to a scratch copy but did not come back identical, so the restore is approximate. The usual cause is a number: the shared setter has no numeric branch, so a JSON number is written through FString::SanitizeFloat and keeps six fractional digits. Property kinds the serializer has no structured form for, and a null object reference, land here too.")
				: TEXT("The rollback value could not be verified: FText and instanced subobjects are fresh instances on every import and are never identical to the original, so no comparison can tell a good restore from a bad one here."));
		}
	}
	else
	{
		// The setter refused its own serialization of this value, so replaying
		// it would fail rather than restore. Advertising it would hand the flow
		// engine a rollback that errors.
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("No rollback is offered: the value this call replaced does not survive its own round trip. Applying the serialized form to a scratch copy of the property was refused with: %s"),
			*ProbeError));
	}
	// #820: report what actually landed for containers, so a caller can check
	// the count without a second read.
	if (FMapProperty* MapProp = CastField<FMapProperty>(Property))
	{
		Result->SetNumberField(TEXT("elementCount"), FScriptMapHelper(MapProp, PropertyValue).Num());
	}
	else if (FSetProperty* SetProp = CastField<FSetProperty>(Property))
	{
		Result->SetNumberField(TEXT("elementCount"), FScriptSetHelper(SetProp, PropertyValue).Num());
	}
	else if (FArrayProperty* ArrProp = CastField<FArrayProperty>(Property))
	{
		Result->SetNumberField(TEXT("elementCount"), FScriptArrayHelper(ArrProp, PropertyValue).Num());
	}
	else if (MCPPropertyText::ContainsMap(Property))
	{
		Result->SetNumberField(TEXT("mapPairCount"), MCPPropertyText::CountMapPairs(Property, PropertyValue));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::GetProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString ObjectPath;
	if (!Params->TryGetStringField(TEXT("objectPath"), ObjectPath))
	{
		if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), ObjectPath)) return Err;
	}

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	UObject* Object = nullptr;
	FString ResolvedKind;
	FString ResolveObjectErr;
	if (!ResolveEditorObjectFromPath(ObjectPath, Object, ResolvedKind, ResolveObjectErr))
	{
		return MCPError(ResolveObjectErr);
	}

	FProperty* Property = nullptr;
	void* ValuePtr = nullptr;
	UObject* LeafOwner = nullptr;
	FString ResolvePropertyErr;
	if (!MCPJsonProperty::ResolveDottedPath(Object, PropertyName, Property, ValuePtr, LeafOwner, ResolvePropertyErr))
	{
		return MCPError(ResolvePropertyErr);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), ObjectPath);
	Result->SetStringField(TEXT("resolvedPath"), Object->GetPathName());
	Result->SetStringField(TEXT("resolvedKind"), ResolvedKind);
	Result->SetStringField(TEXT("className"), Object->GetClass()->GetName());
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	Result->SetStringField(TEXT("leafPropertyName"), Property->GetName());
	Result->SetStringField(TEXT("type"), Property->GetCPPType());
	Result->SetField(TEXT("value"), FMCPJsonSerializer::SerializeValue(ValuePtr, Property));

	FString ValueText;
	Property->ExportText_Direct(ValueText, ValuePtr, ValuePtr, LeafOwner ? LeafOwner : Object, PPF_None);
	Result->SetStringField(TEXT("valueText"), ValueText);

	// #820: `value` is always safe to hand back to set_property. `valueText` is
	// only safe when the flag below says so - a struct-keyed TMap exports pairs
	// that do not read back.
	if (MCPPropertyText::ContainsMap(Property))
	{
		Result->SetBoolField(TEXT("valueTextRoundTrips"),
			MCPPropertyText::ExportedTextRoundTrips(Property, ValuePtr, LeafOwner ? LeafOwner : Object));
		Result->SetNumberField(TEXT("mapPairCount"), MCPPropertyText::CountMapPairs(Property, ValuePtr));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::DescribeObject(const TSharedPtr<FJsonObject>& Params)
{
	FString ObjectPath;
	if (!Params->TryGetStringField(TEXT("objectPath"), ObjectPath))
	{
		if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), ObjectPath)) return Err;
	}

	UObject* Object = nullptr;
	FString ResolvedKind;
	FString ResolveObjectErr;
	if (!ResolveEditorObjectFromPath(ObjectPath, Object, ResolvedKind, ResolveObjectErr))
	{
		return MCPError(ResolveObjectErr);
	}

	const bool bIncludeProperties = OptionalBool(Params, TEXT("includeProperties"), true);
	const bool bIncludeValues = OptionalBool(Params, TEXT("includeValues"), false);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), ObjectPath);
	Result->SetStringField(TEXT("resolvedPath"), Object->GetPathName());
	Result->SetStringField(TEXT("resolvedKind"), ResolvedKind);
	Result->SetStringField(TEXT("name"), Object->GetName());
	Result->SetStringField(TEXT("className"), Object->GetClass()->GetName());
	Result->SetStringField(TEXT("outerPath"), Object->GetOuter() ? Object->GetOuter()->GetPathName() : FString());

	if (!bIncludeProperties)
	{
		TArray<TSharedPtr<FJsonValue>> EmptyProperties;
		Result->SetNumberField(TEXT("propertyCount"), 0);
		Result->SetArrayField(TEXT("properties"), EmptyProperties);
		return MCPResult(Result);
	}

	TArray<TSharedPtr<FJsonValue>> Properties;
	const TArray<TSharedPtr<FJsonValue>>* PropertyNames = nullptr;
	if (Params->TryGetArrayField(TEXT("propertyNames"), PropertyNames) && PropertyNames)
	{
		for (const TSharedPtr<FJsonValue>& NameValue : *PropertyNames)
		{
			FString PropertyName;
			if (!NameValue.IsValid() || !NameValue->TryGetString(PropertyName) || PropertyName.IsEmpty())
			{
				continue;
			}

			FProperty* Property = nullptr;
			void* ValuePtr = nullptr;
			UObject* LeafOwner = nullptr;
			FString ResolvePropertyErr;
			if (!MCPJsonProperty::ResolveDottedPath(Object, PropertyName, Property, ValuePtr, LeafOwner, ResolvePropertyErr))
			{
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), PropertyName);
				Entry->SetStringField(TEXT("error"), ResolvePropertyErr);
				Properties.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}

			TSharedPtr<FJsonObject> Entry = DescribeProperty(Property, ValuePtr, LeafOwner ? LeafOwner : Object, bIncludeValues);
			Entry->SetStringField(TEXT("path"), PropertyName);
			Properties.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}
	else
	{
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			FProperty* Property = *It;
			const void* ValuePtr = bIncludeValues ? Property->ContainerPtrToValuePtr<void>(Object) : nullptr;
			Properties.Add(MakeShared<FJsonValueObject>(DescribeProperty(Property, ValuePtr, Object, bIncludeValues)));
		}
	}

	Result->SetNumberField(TEXT("propertyCount"), Properties.Num());
	Result->SetArrayField(TEXT("properties"), Properties);
	return MCPResult(Result);
}

// set_realtime -- toggle realtime update on the level editor viewports so the
// editor-world simulation (Niagara, animations) ticks. capture_scene_png
// renders without ticking the sim otherwise, producing empty/identical stills.
// (#537) Params: enabled (bool, default true).
TSharedPtr<FJsonValue> FEditorHandlers::SetRealtime(const TSharedPtr<FJsonObject>& Params)
{
	const bool bEnabled = OptionalBool(Params, TEXT("enabled"), true);

	if (!GEditor)
	{
		return MCPError(TEXT("GEditor not available"));
	}

	int32 ViewportsChanged = 0;
	// The realtime flag is per viewport and this call writes one value to all of
	// them, so the state being replaced is only restorable when every viewport
	// already agreed. Counted rather than assumed.
	int32 PreviouslyEnabled = 0;
	int32 PreviouslyDisabled = 0;
	for (FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
	{
		if (!ViewportClient) continue;
		if (ViewportClient->IsRealtime()) ++PreviouslyEnabled; else ++PreviouslyDisabled;
		ViewportClient->SetRealtime(bEnabled);
		ViewportsChanged++;
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("enabled"), bEnabled);
	Result->SetNumberField(TEXT("viewportsChanged"), ViewportsChanged);
	Result->SetNumberField(TEXT("previouslyEnabled"), PreviouslyEnabled);
	Result->SetNumberField(TEXT("previouslyDisabled"), PreviouslyDisabled);
	const bool bChanged = bEnabled ? PreviouslyDisabled > 0 : PreviouslyEnabled > 0;
	Result->SetBoolField(TEXT("changed"), bChanged);

	// Self-inverse: the same handler with the flag every viewport held before.
	const bool bWasMixed = PreviouslyEnabled > 0 && PreviouslyDisabled > 0;
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetBoolField(TEXT("enabled"), PreviouslyEnabled >= PreviouslyDisabled);
	MCPSetRollback(Result, TEXT("set_realtime"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), bWasMixed);
	if (bWasMixed)
	{
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("The viewports did not agree before this call (%d realtime, %d not) and this handler writes one flag to all of them, so the rollback restores the majority value to every viewport rather than the per-viewport split."),
			PreviouslyEnabled, PreviouslyDisabled));
	}
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
			// #983: an ignore list is the plural case, so a label naming
			// several actors ignores all of them.
			TArray<AActor*> Matches;
			MCPCollectActorsByToken(World, Label, EMCPActorMatch::LabelNameOrPath, Matches);
			for (AActor* A : Matches) Query.AddIgnoredActor(A);
		}
	}

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, RayOrigin, RayEnd, ECC_Visibility, Query);

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("hit"), bHit);
	// MISCLASSIFIED, and these markers are the interim answer rather than the
	// right one. This is a deprojection and a line trace: it writes nothing, so
	// under the house rule (markers belong on handlers classifyActionClass
	// calls a mutation, and only those) it should carry none. It is classified
	// a mutation today, so it has to satisfy the convention or be exempted, and
	// stating the truth is better than an exemption. The real fix is
	// reclassifying it to a read in src/action-class.ts, after which these
	// three lines should be deleted. Reported as misclassified, not fixed here,
	// because that file is owned centrally.
	Result->SetBoolField(TEXT("changed"), false);
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("A query, not a write: this deprojects a viewport pixel into a world ray and traces it. No actor, asset or viewport state is touched."));
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
	if (HitActor) Result->SetStringField(TEXT("actorPath"), HitActor->GetPathName());
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

	// T3: paged. `maxLines` still selects the recency WINDOW read out of the
	// ring buffer, so the default call returns what it always did. What it no
	// longer does is stop at that count and say nothing: every matching line in
	// the window is a row, and `limit` pages them.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("get_output_log|maxLines=%d|filter=%s|category=%s"),
				MaxLines, *Filter, *Category),
			/*DefaultLimit*/ 200, /*MaxLimit*/ 4096, Page))
	{
		return Err;
	}

	// Read from ring-buffer log capture (#82)
	TArray<FMCPLogCapture::FMCPLogLine> RecentLines = FMCPLogCapture::Get().GetRecentLines(MaxLines * 2); // over-fetch for filtering

	// Chronological, oldest first, exactly as the ring buffer holds them. No
	// sort: the log stream's own order is the contract here, and the sequence
	// number the rows are anchored on is monotonic in it.
	TArray<MCPPagination::FPageRow> Rows;
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
		LineObj->SetNumberField(TEXT("sequence"), Line.Sequence);
		Rows.Add({ FString::FromInt(Line.Sequence), MakeShared<FJsonValueObject>(LineObj) });
	}

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("maxLines"), MaxLines);
	MCPPagination::EmitPage(Page, Rows, TEXT("lines"), Result);
	// The old field name for the row count, kept so an existing caller reads
	// the same answer out of the same name.
	Result->SetNumberField(TEXT("lineCount"), Result->GetNumberField(TEXT("count")));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::SearchLog(const TSharedPtr<FJsonObject>& Params)
{
	FString Query;
	if (auto Err = RequireString(Params, TEXT("query"), Query)) return Err;

	// The buffer holds 4096 lines, so this default collects every match in it
	// rather than the first 100. It used to cap the SEARCH at 100 and report
	// nothing about the rest, which read as "that is all there is".
	const int32 MaxResults = FMath::Clamp(OptionalInt(Params, TEXT("maxResults"), 4096), 1, 4096);

	// T3: paged.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("search_log|query=%s|maxResults=%d"), *Query, MaxResults),
			/*DefaultLimit*/ 100, /*MaxLimit*/ 4096, Page))
	{
		return Err;
	}

	// Search ring-buffer log capture (#82)
	TArray<FMCPLogCapture::FMCPLogLine> Matches = FMCPLogCapture::Get().Search(Query, MaxResults);

	// Chronological, as the buffer holds them, anchored on each line's absolute
	// sequence number. No sort: the stream's own order is what a log read means.
	TArray<MCPPagination::FPageRow> Rows;
	Rows.Reserve(Matches.Num());
	for (const FMCPLogCapture::FMCPLogLine& Line : Matches)
	{
		TSharedPtr<FJsonObject> MatchObj = MakeShared<FJsonObject>();
		MatchObj->SetStringField(TEXT("message"), Line.Message);
		MatchObj->SetStringField(TEXT("category"), Line.Category);
		MatchObj->SetStringField(TEXT("verbosity"), Line.Verbosity);
		MatchObj->SetNumberField(TEXT("sequence"), Line.Sequence);
		Rows.Add({ FString::FromInt(Line.Sequence), MakeShared<FJsonValueObject>(MatchObj) });
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("query"), Query);
	// `maxResults` bounds the search itself, so say when it was the thing that
	// ended the collection rather than the buffer running out.
	Result->SetBoolField(TEXT("cappedAtMaxResults"), Rows.Num() >= MaxResults);
	MCPPagination::EmitPage(Page, Rows, TEXT("matches"), Result);
	Result->SetNumberField(TEXT("matchCount"), Result->GetNumberField(TEXT("count")));
	return MCPResult(Result);
}

// Read a Message Log listing (map check, asset check, PIE, load errors...).
//
// This returned an empty array unconditionally and ignored logName, so "no
// messages" was indistinguishable from "not implemented" - callers used it to
// confirm a clean compile and got a clean answer either way.
//
// Blueprint compile results are deliberately NOT the default. They do not go to
// a fixed listing: FCompilerResultsLog names one per Blueprint
// ("<Guid>_<Name>_CompilerResultsLog"), so defaulting to a single name would
// reintroduce the same false-clean answer in a subtler form. With no logName we
// report which listings actually exist instead of guessing.
TSharedPtr<FJsonValue> FEditorHandlers::GetMessageLog(const TSharedPtr<FJsonObject>& Params)
{
	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>(TEXT("MessageLog"));

	// The module exposes no enumeration, so probe the well-known listings.
	static const TCHAR* KnownListings[] = {
		TEXT("MapCheck"), TEXT("AssetCheck"), TEXT("PIE"), TEXT("LoadErrors"),
		TEXT("LightingResults"), TEXT("BlueprintLog"), TEXT("AssetTools"),
		TEXT("EditorErrors"), TEXT("AutomationTestingLog"), TEXT("Blueprint Log"),
		TEXT("PackagingResults"), TEXT("SourceControl"), TEXT("HLODResults"),
		TEXT("AnimBlueprintLog"), TEXT("WorldPartition"), TEXT("BlueprintCompiler"),
	};

	const FString LogName = OptionalString(Params, TEXT("logName"));
	if (LogName.IsEmpty())
	{
		TArray<TSharedPtr<FJsonValue>> Available;
		for (const TCHAR* Candidate : KnownListings)
		{
			if (!MessageLogModule.IsRegisteredLogListing(FName(Candidate))) continue;
			TSharedRef<IMessageLogListing> L = MessageLogModule.GetLogListing(FName(Candidate));
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("logName"), Candidate);
			Row->SetNumberField(TEXT("errors"), L->NumMessages(EMessageSeverity::Error));
			Row->SetNumberField(TEXT("warnings"), L->NumMessages(EMessageSeverity::Warning));
			Available.Add(MakeShared<FJsonValueObject>(Row));
		}
		auto Listing = MCPSuccess();
		Listing->SetArrayField(TEXT("listings"), Available);
		Listing->SetStringField(TEXT("note"),
			TEXT("Pass logName to read one of these. Blueprint COMPILE results are not here - the compiler creates a listing per Blueprint; use blueprint(compile) or blueprint(compile_all), which return the compiler log directly."));
		return MCPResult(Listing);
	}

	if (!MessageLogModule.IsRegisteredLogListing(FName(*LogName)))
	{
		// Naming a listing that does not exist would otherwise create an empty
		// one and report a clean log, which is the same false negative.
		return MCPError(FString::Printf(
			TEXT("No message log listing named '%s'. Call with no logName to list the registered ones."),
			*LogName));
	}

	TSharedRef<IMessageLogListing> Listing = MessageLogModule.GetLogListing(FName(*LogName));
	const int32 Limit = FMath::Max(1, OptionalInt(Params, TEXT("maxLines"), 200));
	// Named `severity`, not `filter`: the editor tool's `filter` is a substring
	// match on message text, and sharing the key made this look like one while
	// silently matching only severity names.
	const FString SeverityFilter = OptionalString(Params, TEXT("severity")).ToLower();

	auto SeverityName = [](EMessageSeverity::Type S) -> FString
	{
		switch (S)
		{
			case EMessageSeverity::Error:              return TEXT("Error");
			case EMessageSeverity::PerformanceWarning: return TEXT("PerformanceWarning");
			case EMessageSeverity::Warning:            return TEXT("Warning");
			case EMessageSeverity::Info:               return TEXT("Info");
			default:                                   return TEXT("Unknown");
		}
	};

	// Counts come from NumMessages, which reads the listing itself. The message
	// bodies can only be reached through GetFilteredMessages, which is scoped to
	// the current PAGE and honours the severity checkboxes in the Message Log
	// tab - so the two can legitimately disagree, and that difference is
	// reported rather than left to look like a clean log.
	const int32 TotalErrors   = Listing->NumMessages(EMessageSeverity::Error);
	const int32 TotalWarnings = Listing->NumMessages(EMessageSeverity::Warning);
	const int32 TotalInfo     = Listing->NumMessages(EMessageSeverity::Info);

	TArray<TSharedPtr<FJsonValue>> MessagesArray;
	int32 Visible = 0;
	bool bTruncated = false;
	for (const TSharedRef<FTokenizedMessage>& Message : Listing->GetFilteredMessages())
	{
		++Visible;
		const FString Severity = SeverityName(Message->GetSeverity());
		if (!SeverityFilter.IsEmpty() && !Severity.ToLower().Contains(SeverityFilter)) continue;
		if (MessagesArray.Num() >= Limit) { bTruncated = true; continue; }

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("severity"), Severity);
		Obj->SetStringField(TEXT("text"), Message->ToText().ToString());
		MessagesArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("logName"), LogName);
	Result->SetArrayField(TEXT("messages"), MessagesArray);
	Result->SetNumberField(TEXT("errors"), TotalErrors);
	Result->SetNumberField(TEXT("warnings"), TotalWarnings);
	Result->SetNumberField(TEXT("total"), TotalErrors + TotalWarnings + TotalInfo);
	Result->SetBoolField(TEXT("truncated"), bTruncated);
	const int32 Reachable = Visible;
	if (Reachable < TotalErrors + TotalWarnings + TotalInfo)
	{
		Result->SetStringField(TEXT("note"), FString::Printf(
			TEXT("The listing holds %d messages but only %d %s readable: the message bodies come from the current page and honour the Message Log tab's severity checkboxes. The counts above are the real totals."),
			TotalErrors + TotalWarnings + TotalInfo, Reachable,
			Reachable == 1 ? TEXT("is") : TEXT("are")));
	}
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
	// #966: capture_scene_png names this parameter `outputPath` and this action
	// named it `filename`, for the same thing, so passing one to the other
	// failed on a call that was otherwise correct. Both are accepted by both.
	FString Filename = OptionalString(Params, TEXT("filename"));
	if (Filename.IsEmpty())
	{
		Filename = OptionalString(Params, TEXT("outputPath"));
	}
	if (Filename.IsEmpty())
	{
		return MCPError(TEXT("Missing 'filename' (also accepted as 'outputPath'): where to write the image"));
	}

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

	// #226/#724: target=pie (or auto while PIE is running) resolves the
	// actual client game viewport instead of the first PIE world context.
	const FString Target = OptionalString(Params, TEXT("target"), TEXT("auto")).ToLower();

	double RequestedPIEInstanceValue = INDEX_NONE;
	const bool bHasRequestedPIEInstance =
		Params->TryGetNumberField(TEXT("pieInstance"), RequestedPIEInstanceValue);
	const int32 RequestedPIEInstance = bHasRequestedPIEInstance
		? FMath::RoundToInt(RequestedPIEInstanceValue)
		: INDEX_NONE;
	if (bHasRequestedPIEInstance
		&& !FMath::IsNearlyEqual(RequestedPIEInstanceValue, RequestedPIEInstance))
	{
		return MCPError(TEXT("'pieInstance' must be an integer"));
	}

	FString RequestedWorldPath;
	const bool bHasRequestedWorldPath =
		Params->TryGetStringField(TEXT("worldPath"), RequestedWorldPath)
		&& !RequestedWorldPath.IsEmpty();

	const FWorldContext* SelectedPIEContext = nullptr;
	const FWorldContext* FirstMatchingPIEContext = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* CandidateWorld = Context.World();
		if (Context.WorldType != EWorldType::PIE || !CandidateWorld)
		{
			continue;
		}
		if (bHasRequestedPIEInstance && Context.PIEInstance != RequestedPIEInstance)
		{
			continue;
		}
		if (bHasRequestedWorldPath
			&& CandidateWorld->GetPathName() != RequestedWorldPath
			&& CandidateWorld->GetName() != RequestedWorldPath)
		{
			continue;
		}

		if (!FirstMatchingPIEContext)
		{
			FirstMatchingPIEContext = &Context;
		}

		UGameViewportClient* CandidateViewport = CandidateWorld->GetGameViewport();
		if (CandidateViewport
			&& CandidateViewport->Viewport
			&& CandidateViewport->GetGameViewportWidget().IsValid())
		{
			SelectedPIEContext = &Context;
			break;
		}
	}
	if (!SelectedPIEContext)
	{
		SelectedPIEContext = FirstMatchingPIEContext;
	}
	if ((bHasRequestedPIEInstance || bHasRequestedWorldPath) && !SelectedPIEContext)
	{
		return MCPError(FString::Printf(
			TEXT("No PIE world matched pieInstance=%s worldPath='%s'"),
			bHasRequestedPIEInstance ? *FString::FromInt(RequestedPIEInstance) : TEXT("<any>"),
			bHasRequestedWorldPath ? *RequestedWorldPath : TEXT("<any>")));
	}

	UWorld* PieWorld = SelectedPIEContext ? SelectedPIEContext->World() : nullptr;
	UGameViewportClient* PieGameViewport = PieWorld ? PieWorld->GetGameViewport() : nullptr;
	const bool bUsePie = (Target == TEXT("pie")) || (Target == TEXT("auto") && PieWorld);

	// ReportContext is the PIE world the capture actually targeted, or null when
	// the capture is of something else (the editor window). Reporting an
	// instance the call did not capture would be worse than reporting none.
	auto CaptureSlateWidget = [&Filename](
		const TSharedRef<SWidget>& CaptureWidget,
		const FString& ResultTarget,
		const FWorldContext* ReportContext,
		const FString& CaptureDescription) -> TSharedPtr<FJsonValue>
	{
		TArray<FColor> Pixels;
		FIntVector CaptureSize = FIntVector::ZeroValue;
		if (!FSlateApplication::Get().TakeScreenshot(CaptureWidget, Pixels, CaptureSize)
			|| Pixels.Num() == 0)
		{
			return MCPError(TEXT("Slate screenshot failed"));
		}
		for (FColor& Pixel : Pixels)
		{
			Pixel.A = 255;
		}

		FString FullPath = Filename;
		if (FPaths::IsRelative(FullPath))
		{
			FullPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), Filename);
		}

		IImageWrapperModule& ImageWrapperModule =
			FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		TSharedPtr<IImageWrapper> PngWrapper =
			ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
		if (!PngWrapper.IsValid()
			|| !PngWrapper->SetRaw(
				Pixels.GetData(),
				Pixels.Num() * sizeof(FColor),
				CaptureSize.X,
				CaptureSize.Y,
				ERGBFormat::BGRA,
				8))
		{
			return MCPError(TEXT("PNG encode failed"));
		}
		// Asked before the write, because it is the difference between creating
		// a file and destroying whatever was at that path.
		const bool bOverwrote = IFileManager::Get().FileExists(*FullPath);
		const TArray64<uint8> PngData = PngWrapper->GetCompressed(100);
		if (!FFileHelper::SaveArrayToFile(PngData, *FullPath))
		{
			return MCPError(FString::Printf(TEXT("Could not write %s"), *FullPath));
		}

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("filename"), FullPath);
		Result->SetStringField(TEXT("target"), ResultTarget);
		Result->SetNumberField(TEXT("width"), CaptureSize.X);
		Result->SetNumberField(TEXT("height"), CaptureSize.Y);
		Result->SetStringField(TEXT("note"), CaptureDescription);
		Result->SetBoolField(TEXT("overwrote"), bOverwrote);
		// A capture always writes a fresh frame, so it never finds work already
		// done and skips it.
		Result->SetBoolField(TEXT("changed"), true);
		// No rollback: nothing in the bridge deletes a file from disk, so a
		// capture cannot be taken back, and when it overwrote an existing image
		// that image is gone as well.
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"), bOverwrote
			? TEXT("No bridge call deletes a file from disk, and this capture replaced a file that was already at that path. Neither the new file nor the one it overwrote can be restored. Capture to a fresh path when a flow may need to roll back.")
			: TEXT("No bridge call deletes a file from disk, so the captured image cannot be removed again. Nothing else was written."));
		if (ReportContext && ReportContext->World())
		{
			Result->SetNumberField(TEXT("pieInstance"), ReportContext->PIEInstance);
			Result->SetStringField(TEXT("worldPath"), ReportContext->World()->GetPathName());
		}
		return MCPResult(Result);
	};

	// target=window: synchronous FSlateApplication::TakeScreenshot of a whole
	// Slate window, pixel-true for ALL Slate content.
	//
	// Which window is addressed, not guessed: an explicit pieInstance/worldPath
	// asks for that PIE client's window, and everything else keeps the original
	// meaning of "the active window". Preferring the PIE window whenever PIE
	// happened to be running took away the ability to capture editor UI during a
	// PIE session, which is how editor utility widgets get QA'd.
	if (Target == TEXT("window"))
	{
		if (!FSlateApplication::IsInitialized())
		{
			return MCPError(TEXT("Slate is not initialized"));
		}
		const bool bWantsPieWindow = bHasRequestedPIEInstance || bHasRequestedWorldPath;
		TSharedPtr<SWindow> CaptureWindow;
		if (bWantsPieWindow)
		{
			CaptureWindow = PieGameViewport ? PieGameViewport->GetWindow() : nullptr;
			if (!CaptureWindow.IsValid())
			{
				return MCPError(FString::Printf(
					TEXT("PIE world '%s' (instance %d) has no Slate window; omit pieInstance/worldPath to capture the active window"),
					PieWorld ? *PieWorld->GetPathName() : TEXT("<none>"),
					SelectedPIEContext ? SelectedPIEContext->PIEInstance : INDEX_NONE));
			}
		}
		else
		{
			CaptureWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
			if (!CaptureWindow.IsValid())
			{
				TArray<TSharedRef<SWindow>> VisibleWindows;
				FSlateApplication::Get().GetAllVisibleWindowsOrdered(VisibleWindows);
				if (VisibleWindows.Num() > 0)
				{
					CaptureWindow = VisibleWindows.Last();
				}
			}
		}
		if (!CaptureWindow.IsValid())
		{
			return MCPError(TEXT("No visible Slate window to capture"));
		}

		TSharedPtr<FJsonValue> CaptureResult = CaptureSlateWidget(
			CaptureWindow.ToSharedRef(),
			TEXT("window"),
			bWantsPieWindow ? SelectedPIEContext : nullptr,
			bWantsPieWindow
				? TEXT("Synchronous PIE client window capture including all UI.")
				: TEXT("Synchronous active window capture including all UI."));
		if (CaptureResult.IsValid() && CaptureResult->Type == EJson::Object)
		{
			CaptureResult->AsObject()->SetStringField(
				TEXT("window"),
				CaptureWindow->GetTitle().ToString());
		}
		return CaptureResult;
	}

	if (Target == TEXT("pie") && !PieWorld)
	{
		return MCPError(TEXT("No PIE world is running"));
	}

	if (bUsePie && PieWorld)
	{
		// Resolve the client viewport explicitly in multi-instance PIE. Capturing
		// its SViewport is synchronous and includes the painted UMG/Slate layer.
		if (PieGameViewport && PieGameViewport->GetGameViewportWidget().IsValid())
		{
			TSharedPtr<FJsonValue> CaptureResult = CaptureSlateWidget(
				PieGameViewport->GetGameViewportWidget().ToSharedRef(),
				TEXT("pie"),
				SelectedPIEContext,
				TEXT("Synchronous selected PIE game-viewport capture including UMG/Slate UI."));
			if (CaptureResult.IsValid() && CaptureResult->Type == EJson::Object)
			{
				// The capture is the game viewport's composited output, which is
				// where AddOnScreenDebugMessage draws. That overlay exists only
				// while the engine is drawing on-screen messages at all, so read
				// the flags instead of advertising the canvas unconditionally.
				const bool bDebugCanvas = GEngine->bEnableOnScreenDebugMessages
					&& GEngine->bEnableOnScreenDebugMessagesDisplay;
				CaptureResult->AsObject()->SetBoolField(TEXT("includesDebugCanvas"), bDebugCanvas);
				if (!bDebugCanvas)
				{
					CaptureResult->AsObject()->SetStringField(
						TEXT("debugCanvasNote"),
						TEXT("On-screen debug messages are disabled on GEngine, so AddOnScreenDebugMessage overlays are not drawn into this capture."));
				}
			}
			return CaptureResult;
		}

		return MCPError(FString::Printf(
			TEXT("PIE world '%s' (instance %d) has no capturable game viewport"),
			*PieWorld->GetPathName(),
			SelectedPIEContext ? SelectedPIEContext->PIEInstance : INDEX_NONE));
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

	// Asked before the request is queued, because it is the difference between
	// creating a file and destroying whatever was at that path.
	const bool bOverwrote = IFileManager::Get().FileExists(*FullPath);

	// Request the screenshot
	FScreenshotRequest::RequestScreenshot(FullPath, false, false);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("filename"), FullPath);
	Result->SetStringField(TEXT("target"), TEXT("editor"));
	Result->SetStringField(TEXT("note"), TEXT("Screenshot queued. The file will be written asynchronously by the renderer."));
	Result->SetBoolField(TEXT("overwrote"), bOverwrote);
	// The request was queued; a capture never finds work already done and
	// skips it. The write itself happens after this response is sent.
	Result->SetBoolField(TEXT("changed"), true);
	// No rollback: nothing in the bridge deletes a file from disk, and the
	// write is asynchronous, so a rollback could run before the file even
	// exists and would have nothing to act on either way.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"), bOverwrote
		? TEXT("No bridge call deletes a file from disk, and a file was already at that path when the request was queued. Capture to a fresh path when a flow may need to roll back.")
		: TEXT("No bridge call deletes a file from disk, so the captured image cannot be removed again. The renderer also writes it after this response is sent, so there is nothing to act on at rollback time."));
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

	// Read the camera that is about to be replaced. This is the only state an
	// inverse call could restore, and it is unreadable once the write lands.
	const FVector PreviousLocation = ViewportClient->GetViewLocation();
	const FRotator PreviousRotation = ViewportClient->GetViewRotation();

	if (Params->HasField(TEXT("location")))
	{
		ViewportClient->SetViewLocation(OptionalVec3(Params, TEXT("location")));
	}
	if (Params->HasField(TEXT("rotation")))
	{
		ViewportClient->SetViewRotation(OptionalRotator(Params, TEXT("rotation")));
	}

	auto Result = MCPSuccess();
	const FVector CurrentLocation = ViewportClient->GetViewLocation();
	const FRotator CurrentRotation = ViewportClient->GetViewRotation();
	TSharedPtr<FJsonObject> PrevLoc = MakeShared<FJsonObject>();
	PrevLoc->SetNumberField(TEXT("x"), PreviousLocation.X);
	PrevLoc->SetNumberField(TEXT("y"), PreviousLocation.Y);
	PrevLoc->SetNumberField(TEXT("z"), PreviousLocation.Z);
	TSharedPtr<FJsonObject> PrevRot = MakeShared<FJsonObject>();
	PrevRot->SetNumberField(TEXT("pitch"), PreviousRotation.Pitch);
	PrevRot->SetNumberField(TEXT("yaw"), PreviousRotation.Yaw);
	PrevRot->SetNumberField(TEXT("roll"), PreviousRotation.Roll);
	Result->SetObjectField(TEXT("previousLocation"), PrevLoc);
	Result->SetObjectField(TEXT("previousRotation"), PrevRot);
	Result->SetBoolField(TEXT("changed"),
		!CurrentLocation.Equals(PreviousLocation) || !CurrentRotation.Equals(PreviousRotation));

	// Self-inverse: the same handler with the camera this call replaced. Both
	// halves are sent, so a location-only write still restores the whole pose.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetObjectField(TEXT("location"), PrevLoc);
	Payload->SetObjectField(TEXT("rotation"), PrevRot);
	MCPSetRollback(Result, TEXT("set_viewport_camera"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Restores the view location and rotation. It targets the active level viewport the same way this call did, so a different viewport becoming active in between moves that one instead."));
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
	// False means the undo buffer had nothing left to reverse, which is a
	// no-op rather than a failure to report a change for.
	Result->SetBoolField(TEXT("changed"), bSuccess);
	if (bSuccess)
	{
		// Undo and redo really are each other's inverse: undoing pushes the
		// step onto the redo stack, and editor(redo) pops exactly that step.
		MCPSetRollback(Result, TEXT("redo"), MakeShared<FJsonObject>());
		Result->SetBoolField(TEXT("rollbackLossy"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("editor(redo) re-applies the step this undid. The redo stack is cleared by any new transaction, so a write made between the undo and the rollback discards the step and the redo then does nothing."));
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Nothing was undone, so there is nothing to redo."));
	}
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
	// False means the redo stack was empty, which is a no-op rather than a
	// failure to report a change for.
	Result->SetBoolField(TEXT("changed"), bSuccess);
	if (bSuccess)
	{
		// Redo and undo really are each other's inverse: redoing pushes the
		// step back onto the undo stack, and editor(undo) reverses exactly it.
		MCPSetRollback(Result, TEXT("undo"), MakeShared<FJsonObject>());
		Result->SetBoolField(TEXT("rollbackLossy"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("editor(undo) reverses the step this re-applied, as long as no later transaction was recorded on top of it. When one was, the undo reverses that newer step instead."));
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Nothing was redone, so there is nothing to undo."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::ReloadHandlers(const TSharedPtr<FJsonObject>& Params)
{
	// No-op in C++ bridge - this was used in the Python bridge to reload Python handler modules.
	auto Result = MCPSuccess();
	// Nothing to reload: handlers are registered once at module startup, so
	// this writes no state at all and every call is the same no-op.
	Result->SetBoolField(TEXT("changed"), false);
	Result->SetBoolField(TEXT("unchanged"), true);
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Nothing happened, so there is nothing to undo. Handler registration is fixed for the life of the loaded module; editor(hot_reload) is what replaces the code behind it."));
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
	// Nothing dirty means nothing was written, which is what a replay of this
	// call after a successful one finds.
	Result->SetBoolField(TEXT("changed"), Saved.Num() > 0);
	// No rollback: the previous contents of each .uasset were overwritten on
	// disk and are not held anywhere the bridge can reach.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("SavePackage overwrites each package on disk and the bytes it replaced are gone. Nothing un-saves a package, and marking one dirty again would not restore its previous contents. Recover from source control if the write was wrong."));
	return MCPResult(Result);
}

// #340: list every dirty package (content + map) so callers can audit
// before flushing. Mirrors EditorLoadingAndSavingUtils.get_dirty_*_packages
// without the Python escape.
TSharedPtr<FJsonValue> FEditorHandlers::ListDirtyPackages(const TSharedPtr<FJsonObject>& Params)
{
	const FDirtyEditorPackages Dirty = CollectDirtyEditorPackages();
	TArray<TSharedPtr<FJsonValue>> Content;
	TArray<TSharedPtr<FJsonValue>> Maps;
	Content.Reserve(Dirty.Content.Num());
	Maps.Reserve(Dirty.Maps.Num());
	for (const FString& PackageName : Dirty.Content)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("package"), PackageName);
		Content.Add(MakeShared<FJsonValueObject>(Entry));
	}
	for (const FString& PackageName : Dirty.Maps)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("package"), PackageName);
		Maps.Add(MakeShared<FJsonValueObject>(Entry));
	}

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("contentCount"), Content.Num());
	Result->SetNumberField(TEXT("mapCount"), Maps.Num());
	Result->SetArrayField(TEXT("content"), Content);
	Result->SetArrayField(TEXT("maps"), Maps);
	return MCPResult(Result);
}

// #920 / #921: one read that answers "which world is open, and what is unsaved"
// together. level(get_current) and list_dirty_packages are two calls, so the
// editor can change between them and neither answer proves which world the
// other described. Both reports were filed independently by the same team,
// which is a fair signal that two calls is genuinely not good enough for a QA
// gate. This runs in one game-thread dispatch, so the pair is consistent.
TSharedPtr<FJsonValue> FEditorHandlers::GetWorldState(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor)
	{
		return MCPError(TEXT("Editor not available"));
	}

	UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
	if (!EditorWorld)
	{
		// Fail closed. An empty answer here would read as "nothing is open and
		// nothing is dirty", which is the reassuring shape of a wrong answer.
		return MCPError(TEXT("No editor world is currently resolvable, so world state cannot be reported"));
	}

	auto Result = MCPSuccess();

	UPackage* WorldPackage = EditorWorld->GetOutermost();
	Result->SetStringField(TEXT("editorWorldName"), EditorWorld->GetName());
	Result->SetStringField(TEXT("editorWorldPackage"), WorldPackage ? WorldPackage->GetName() : FString());
	Result->SetBoolField(TEXT("worldPackageDirty"), WorldPackage != nullptr && WorldPackage->IsDirty());

	if (ULevel* Persistent = EditorWorld->PersistentLevel)
	{
		if (UPackage* LevelPackage = Persistent->GetOutermost())
		{
			Result->SetStringField(TEXT("persistentLevelPackage"), LevelPackage->GetName());
		}
	}

	// PIE and SIE are distinct states and a caller acting on the wrong one gets
	// a confidently wrong result, so report both rather than one "inPIE" flag.
	const bool bSimulating = GEditor->bIsSimulatingInEditor;
	const bool bPlayWorld = GEditor->PlayWorld != nullptr;
	Result->SetBoolField(TEXT("playInEditor"), bPlayWorld && !bSimulating);
	Result->SetBoolField(TEXT("simulateInEditor"), bSimulating);
	Result->SetStringField(TEXT("mode"), bSimulating ? TEXT("simulate") : (bPlayWorld ? TEXT("play") : TEXT("editor")));

	const FDirtyEditorPackages Dirty = CollectDirtyEditorPackages();
	TArray<FString> All;
	All.Reserve(Dirty.Content.Num() + Dirty.Maps.Num());
	All.Append(Dirty.Content);
	All.Append(Dirty.Maps);
	All.Sort();

	TArray<TSharedPtr<FJsonValue>> DirtyJson;
	DirtyJson.Reserve(All.Num());
	for (const FString& PackageName : All)
	{
		DirtyJson.Add(MakeShared<FJsonValueString>(PackageName));
	}
	Result->SetArrayField(TEXT("dirtyPackages"), DirtyJson);
	Result->SetNumberField(TEXT("dirtyPackageCount"), All.Num());
	Result->SetNumberField(TEXT("dirtyContentCount"), Dirty.Content.Num());
	Result->SetNumberField(TEXT("dirtyMapCount"), Dirty.Maps.Num());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::RequestEditorShutdown(const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor)
	{
		return MCPError(TEXT("Editor not available"));
	}

	const bool bRequireClean = OptionalBool(Params, TEXT("requireClean"), true);
	const bool bEndPIE = OptionalBool(Params, TEXT("endPIE"), true);
	const bool bPIEWasActive = GEditor->PlayWorld != nullptr || GEditor->bIsSimulatingInEditor;
	const FDirtyEditorPackages Dirty = CollectDirtyEditorPackages();

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("scheduled"), false);
	Result->SetBoolField(TEXT("requireClean"), bRequireClean);
	Result->SetBoolField(TEXT("endPIE"), bEndPIE);
	Result->SetBoolField(TEXT("pieWasActive"), bPIEWasActive);
	Result->SetBoolField(TEXT("pieEndRequested"), false);
	Result->SetNumberField(TEXT("dirtyContentCount"), Dirty.Content.Num());
	Result->SetNumberField(TEXT("dirtyMapCount"), Dirty.Maps.Num());
	Result->SetArrayField(TEXT("dirtyContentPackages"), PackageNamesToJson(Dirty.Content));
	Result->SetArrayField(TEXT("dirtyMapPackages"), PackageNamesToJson(Dirty.Maps));

	if (bPIEWasActive && !bEndPIE)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("PIE is active and endPIE is false; editor shutdown was not scheduled"));
		return MCPResult(Result);
	}

	if (bRequireClean && (Dirty.Content.Num() > 0 || Dirty.Maps.Num() > 0))
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Editor has dirty content or map packages; save or discard them before requesting shutdown"));
		return MCPResult(Result);
	}

	if (bPIEWasActive)
	{
		GEditor->RequestEndPlayMap();
		Result->SetBoolField(TEXT("pieEndRequested"), true);
	}

	// Use a positive delay because a zero-delay ticker added while the core
	// ticker is pumping can execute in the same pass. If PIE/SIE was active,
	// keep polling until the editor has actually finished ending play.
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([](float) -> bool
		{
			if (!GEditor)
			{
				return false;
			}
			if (GEditor->PlayWorld != nullptr || GEditor->bIsSimulatingInEditor)
			{
				return true;
			}
			IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
			MainFrameModule.RequestCloseEditor();
			return false;
		}),
		1.0f);

	Result->SetBoolField(TEXT("scheduled"), true);
	Result->SetStringField(TEXT("message"), TEXT("Graceful editor shutdown scheduled; active PIE/SIE will end before close"));
	// The early returns above leave scheduled=false and success=false, so
	// reaching here always armed a shutdown that was not already armed.
	Result->SetBoolField(TEXT("changed"), true);
	// No rollback. The ticker cannot be cancelled from here, and once it fires
	// the editor is closing: a rollback would either arrive too late to matter
	// or arrive after the process that would run it is gone.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("The scheduled close cannot be called off, and nothing restarts the editor from inside it. editor(start_editor) launches a NEW editor process rather than restoring this session."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::FocusViewportOnActor(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	TSharedPtr<FJsonValue> ActorErr;
	AActor* TargetActor = MCPResolveActor(World, Params, ActorErr);
	if (!TargetActor) return ActorErr;
	ActorLabel = TargetActor->GetActorLabel();

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

	// Read the camera that is about to be moved. This is the only state an
	// inverse call could restore, and it is unreadable once the focus lands.
	const FVector PreviousLocation = ViewportClient->GetViewLocation();
	const FRotator PreviousRotation = ViewportClient->GetViewRotation();
	const bool bViewportIsOrtho = ViewportClient->IsOrtho();

	// Focus on the actor's bounding box
	FBox ActorBounds = TargetActor->GetComponentsBoundingBox(true);
	// Which branch runs decides whether the camera move is observable here.
	// FocusViewportOnBox defaults bInstant to false and hands the move to
	// TransitionToLocation, which, whenever the client has a real viewport
	// widget, only sets DesiredLocation and starts a curve: ViewLocation is
	// advanced by UpdateTransition on later ticks, so reading it back in this
	// call still returns the location the camera had before the focus.
	const bool bFocusedOnBounds = ActorBounds.IsValid != 0;
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
	Result->SetStringField(TEXT("actorPath"), TargetActor->GetPathName());

	TSharedPtr<FJsonObject> PrevLoc = MakeShared<FJsonObject>();
	PrevLoc->SetNumberField(TEXT("x"), PreviousLocation.X);
	PrevLoc->SetNumberField(TEXT("y"), PreviousLocation.Y);
	PrevLoc->SetNumberField(TEXT("z"), PreviousLocation.Z);
	TSharedPtr<FJsonObject> PrevRot = MakeShared<FJsonObject>();
	PrevRot->SetNumberField(TEXT("pitch"), PreviousRotation.Pitch);
	PrevRot->SetNumberField(TEXT("yaw"), PreviousRotation.Yaw);
	PrevRot->SetNumberField(TEXT("roll"), PreviousRotation.Roll);
	Result->SetObjectField(TEXT("previousLocation"), PrevLoc);
	Result->SetObjectField(TEXT("previousRotation"), PrevRot);
	// The bounds branch animates, so a read-back here would compare the camera
	// against itself and report changed=false for a call that visibly moves it.
	// Only the fallback branch writes the transform outright and is comparable.
	Result->SetBoolField(TEXT("changed"), bFocusedOnBounds
		|| !FinalLocation.Equals(PreviousLocation)
		|| !ViewportClient->GetViewRotation().Equals(PreviousRotation));
	Result->SetBoolField(TEXT("changeDetected"), !bFocusedOnBounds);
	if (bFocusedOnBounds)
	{
		Result->SetStringField(TEXT("viewLocationNote"),
			TEXT("The camera moves to the framed position over several ticks, so viewLocation is where it started rather than where it will end up. Read it back with editor(get_viewport_state) after the transition to see the destination."));
	}

	// The inverse of framing an actor is putting the camera back where it was,
	// which is what set_viewport_camera writes. Both halves are sent so the
	// whole pose comes back, and SetViewLocation assigns DesiredLocation as
	// well as ViewLocation, so a rollback that runs mid-transition ends the
	// animation instead of being overwritten by the next tick.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetObjectField(TEXT("location"), PrevLoc);
	Payload->SetObjectField(TEXT("rotation"), PrevRot);
	MCPSetRollback(Result, TEXT("set_viewport_camera"), Payload);
	// Lossy either way when the bounds branch ran: it writes more viewport
	// state than set_viewport_camera can write back.
	Result->SetBoolField(TEXT("rollbackLossy"), bFocusedOnBounds);
	if (bFocusedOnBounds)
	{
		// FocusViewportOnBox calls ToggleOrbitCamera(false) and
		// CameraController->ResetVelocity() before it branches on projection,
		// so both of those apply either way; SetLookAt is the one the
		// perspective branch adds. set_viewport_camera writes none of them.
		Result->SetStringField(TEXT("rollbackNote"), bViewportIsOrtho
			? TEXT("Restores the view location and rotation only. Framing a box in an orthographic viewport also rewrites its ortho zoom, turns orbit mode off and resets the camera controller's velocity, and set_viewport_camera has no parameter for any of those, so they stay as this call left them.")
			: TEXT("Restores the view location and rotation only. Framing a box also moves the orbit look-at point onto the framed actor, turns orbit mode off and resets the camera controller's velocity, and set_viewport_camera writes none of those, so the pivot stays on the actor after the rollback."));
	}
	else
	{
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The actor had no valid bounds, so this wrote the camera transform directly and restoring it is exact. It targets the active level viewport the same way this call did, so a different viewport becoming active in between moves that one instead."));
	}
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

	// #833: validate the destination BEFORE handing it to the subsystem.
	// NewLevel("") logs "NewLevel. Failed to create the level." at Error and
	// leaves the editor on an untitled map, so a caller that simply omitted the
	// parameter produced an engine error in the log AND unloaded whatever level
	// was open. Every one of these is knowable up front.
	if (LevelPath.IsEmpty())
	{
		return MCPError(TEXT("levelPath is required (e.g. \"/Game/Maps/MyLevel\"). Nothing was created and the "
			"currently loaded level was left alone."));
	}
	{
		FText PackageNameError;
		if (!FPackageName::IsValidLongPackageName(LevelPath, /*bIncludeReadOnlyRoots*/ false, &PackageNameError))
		{
			return MCPError(FString::Printf(
				TEXT("levelPath '%s' is not a valid long package name: %s. Use a mounted root, e.g. ")
				TEXT("\"/Game/Maps/MyLevel\". Nothing was created."),
				*LevelPath, *PackageNameError.ToString()));
		}
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
		// The empty and malformed paths are rejected above, before the engine
		// is asked, so what reaches here is a real refusal.
		FString Reason;
		if (bHasTemplate && !UEditorAssetLibrary::DoesAssetExist(TemplateLevel))
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

	// #833: an untitled map has no file to write, and SaveCurrentLevel answers
	// that with "Can't save the level because it doesn't have a filename" in
	// the editor log and a bare false here. The world knows its own package, so
	// say which call gives it a name instead of asking for an impossible save.
	const FString PackageName = World->GetOutermost()->GetName();
	if (!FPackageName::DoesPackageExist(PackageName))
	{
		return MCPError(FString::Printf(
			TEXT("The current level '%s' has never been saved, so it has no file to save to and this action cannot ")
			TEXT("give it one. Write it to a path first with level(create, levelPath=\"/Game/Maps/<Name>\"), which ")
			TEXT("creates and saves the map, or level(save) to save the packages that do have files."),
			*PackageName));
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
	// SaveCurrentLevel reports only whether the save ran; it does not say
	// whether the map was dirty, so this is the honest granularity available.
	Result->SetBoolField(TEXT("changed"), bSuccess);
	// No rollback: the previous .umap on disk was overwritten and is not held
	// anywhere the bridge can reach.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Saving overwrites the map file and the bytes it replaced are gone. Nothing un-saves a level. Recover from source control if the write was wrong."));

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

	// Asked before opening: OpenEditorForAsset focuses an editor that is already
	// up and returns true either way, so this is the only point at which
	// "was it already open" is answerable.
	const bool bAlreadyOpen = AssetEditorSubsystem->FindEditorForAsset(Asset, /*bFocusIfOpen=*/false) != nullptr;
	bool bOpened = AssetEditorSubsystem->OpenEditorForAsset(Asset);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("assetClass"), Asset->GetClass()->GetName());
	Result->SetBoolField(TEXT("success"), bOpened);
	Result->SetBoolField(TEXT("alreadyOpen"), bAlreadyOpen);
	Result->SetBoolField(TEXT("changed"), bOpened && !bAlreadyOpen);
	if (!bOpened)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to open editor for '%s' (%s)"), *AssetPath, *Asset->GetClass()->GetName()));
	}
	// No rollback: nothing in the bridge closes an asset editor. close_sequence
	// is the one exception and it only handles Level Sequences, so naming it
	// here would fail for every other asset class.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("No bridge call closes an asset editor for an arbitrary asset, so an opened editor cannot be put away again. Opening one writes no asset data; editor(close_sequence) is available when the asset is a Level Sequence."));

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::RunStatCommand(const TSharedPtr<FJsonObject>& Params)
{
	// #722: callers naturally pass the stat name (e.g. name="unit"); the old
	// handler only read "command" and silently defaulted to "stat fps" when the
	// stat was passed as "name", so "unit" ran the FPS counter. Accept either:
	// an explicit full "command", or a bare stat "name" that we prefix.
	FString Command = OptionalString(Params, TEXT("command"));
	if (Command.IsEmpty())
	{
		const FString StatName = OptionalString(Params, TEXT("name"));
		if (!StatName.IsEmpty())
		{
			Command = StatName.StartsWith(TEXT("stat "), ESearchCase::IgnoreCase)
				? StatName
				: FString::Printf(TEXT("stat %s"), *StatName);
		}
		else
		{
			Command = TEXT("stat fps");
		}
	}

	REQUIRE_EDITOR_WORLD(World);

	GEditor->Exec(World, *Command);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("command"), Command);
	// The command was dispatched. Most stat commands toggle rather than set, so
	// the overlay's state after this depends on what it was before, and Exec
	// reports nothing back that this handler could compare.
	Result->SetBoolField(TEXT("changed"), true);
	// No rollback. A stat command toggles, so its inverse is the state the
	// overlay was in beforehand, and that was not captured: nothing here reads
	// which stats were enabled before the Exec. Re-running the same command is
	// an inverse only when it happens to be a plain toggle and the stat was off
	// to begin with, which is a guess rather than a rollback, and "stat none"
	// has no inverse at all.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Stat commands toggle rather than set, and the overlay state before this call was not captured, so there is no value to restore. Re-running the same command would be a guess, not an inverse, and 'stat none' has no inverse at all."));
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
	else return MCPError(FString::Printf(TEXT("Unknown level '%s' (expected Low/Medium/High/Epic/Cinematic)"), *Level));

	// #591: setting the sg.* cvars via Exec does not reliably apply the quality
	// group in-editor. Drive the Scalability system directly so the cached
	// quality levels actually take effect, then persist them.
	Scalability::FQualityLevels QL = Scalability::GetQualityLevels();
	// Read the group level being replaced before writing. GetSingleQualityLevel
	// answers -1 when the groups do not all agree, which is exactly the case
	// this handler cannot write back: it only takes one name for all of them.
	const int32 PreviousSingleLevel = QL.GetSingleQualityLevel();
	QL.SetFromSingleQualityLevel(Idx);
	Scalability::SetQualityLevels(QL, /*bForce=*/true);
	Scalability::SaveState(GGameUserSettingsIni);

	const Scalability::FQualityLevels Applied = Scalability::GetQualityLevels();
	TSharedPtr<FJsonObject> Levels = MakeShared<FJsonObject>();
	Levels->SetNumberField(TEXT("viewDistance"), Applied.ViewDistanceQuality);
	Levels->SetNumberField(TEXT("antiAliasing"), Applied.AntiAliasingQuality);
	Levels->SetNumberField(TEXT("shadow"), Applied.ShadowQuality);
	Levels->SetNumberField(TEXT("globalIllumination"), Applied.GlobalIlluminationQuality);
	Levels->SetNumberField(TEXT("reflection"), Applied.ReflectionQuality);
	Levels->SetNumberField(TEXT("postProcess"), Applied.PostProcessQuality);
	Levels->SetNumberField(TEXT("texture"), Applied.TextureQuality);
	Levels->SetNumberField(TEXT("effects"), Applied.EffectsQuality);
	Levels->SetNumberField(TEXT("foliage"), Applied.FoliageQuality);
	Levels->SetNumberField(TEXT("shading"), Applied.ShadingQuality);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("level"), Level);
	Result->SetNumberField(TEXT("qualityLevel"), Idx);
	Result->SetObjectField(TEXT("appliedLevels"), Levels);
	Result->SetNumberField(TEXT("previousQualityLevel"), PreviousSingleLevel);
	Result->SetBoolField(TEXT("changed"), PreviousSingleLevel != Idx);

	// Self-inverse: the same handler with the group level this call replaced.
	// Only when every group agreed beforehand, because that is the only state
	// a single `level` name can put back.
	static const TCHAR* const LevelNames[] = { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic"), TEXT("Cinematic") };
	if (PreviousSingleLevel >= 0 && PreviousSingleLevel < static_cast<int32>(UE_ARRAY_COUNT(LevelNames)))
	{
		Result->SetStringField(TEXT("previousLevel"), LevelNames[PreviousSingleLevel]);
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("level"), LevelNames[PreviousSingleLevel]);
		MCPSetRollback(Result, TEXT("set_scalability"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Every quality group was at the same level before this call, so replaying that level restores all of them, and SaveState persists the restore the same way the write was persisted."));
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The quality groups did not all sit at one level before this call (GetSingleQualityLevel reported custom), and this handler takes a single level name that writes every group. There is no value it could be given that restores the mixed state, so no rollback is offered."));
	}
	return MCPResult(Result);
}

// #591 bulk console-variable setter. Accepts a {name: value} object (or array
// of {name, value}) and applies each via the console manager with SetByConsole
// priority. Returns per-cvar old/new values so callers can confirm the write.
TSharedPtr<FJsonValue> FEditorHandlers::SetCVars(const TSharedPtr<FJsonObject>& Params)
{
	// Collect requested (name, value) pairs from either shape.
	TArray<TPair<FString, FString>> Requests;
	const TSharedPtr<FJsonObject>* CVarObj = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* CVarArr = nullptr;
	if (Params->TryGetObjectField(TEXT("cvars"), CVarObj) && CVarObj && CVarObj->IsValid())
	{
		for (const auto& Pair : (*CVarObj)->Values)
		{
			const FString CVarName(*Pair.Key);
			FString ValStr;
			if (Pair.Value.IsValid() && Pair.Value->TryGetString(ValStr))
			{
				Requests.Add({ CVarName, ValStr });
			}
			else if (Pair.Value.IsValid())
			{
				// numbers/bools arrive as non-string JSON; stringify them.
				double Num = 0.0; bool bBool = false;
				if (Pair.Value->TryGetNumber(Num)) Requests.Add({ CVarName, FString::SanitizeFloat(Num) });
				else if (Pair.Value->TryGetBool(bBool)) Requests.Add({ CVarName, bBool ? TEXT("1") : TEXT("0") });
			}
		}
	}
	else if (Params->TryGetArrayField(TEXT("cvars"), CVarArr) && CVarArr)
	{
		for (const TSharedPtr<FJsonValue>& Entry : *CVarArr)
		{
			const TSharedPtr<FJsonObject>* EObj = nullptr;
			if (Entry.IsValid() && Entry->TryGetObject(EObj) && EObj && EObj->IsValid())
			{
				FString Name = (*EObj)->GetStringField(TEXT("name"));
				FString ValStr;
				if (!Name.IsEmpty() && (*EObj)->TryGetStringField(TEXT("value"), ValStr))
				{
					Requests.Add({ Name, ValStr });
				}
			}
		}
	}
	if (Requests.Num() == 0) return MCPError(TEXT("Supply 'cvars' as a {name: value} object or an array of {name, value}"));

	IConsoleManager& CM = IConsoleManager::Get();
	TArray<TSharedPtr<FJsonValue>> Applied;
	TArray<TSharedPtr<FJsonValue>> NotFound;
	// The values being replaced, in the shape this handler takes back: the
	// inverse of a cvar write is its previous value, read before the write.
	TArray<TSharedPtr<FJsonValue>> InversePairs;
	int32 ValuesActuallyChanged = 0;
	for (const TPair<FString, FString>& Req : Requests)
	{
		IConsoleVariable* CVar = CM.FindConsoleVariable(*Req.Key);
		if (!CVar)
		{
			NotFound.Add(MakeShared<FJsonValueString>(Req.Key));
			continue;
		}
		const FString OldValue = CVar->GetString();
		CVar->Set(*Req.Value, ECVF_SetByConsole);
		const FString NewValue = CVar->GetString();
		if (NewValue != OldValue) ++ValuesActuallyChanged;
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("name"), Req.Key);
		Row->SetStringField(TEXT("oldValue"), OldValue);
		Row->SetStringField(TEXT("newValue"), NewValue);
		Applied.Add(MakeShared<FJsonValueObject>(Row));

		TSharedPtr<FJsonObject> Inverse = MakeShared<FJsonObject>();
		Inverse->SetStringField(TEXT("name"), Req.Key);
		Inverse->SetStringField(TEXT("value"), OldValue);
		InversePairs.Add(MakeShared<FJsonValueObject>(Inverse));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("applied"), Applied);
	Result->SetNumberField(TEXT("appliedCount"), Applied.Num());
	if (NotFound.Num() > 0) Result->SetArrayField(TEXT("notFound"), NotFound);
	// A write of the value a cvar already held is a no-op, and a request that
	// found no cvar wrote nothing at all.
	Result->SetNumberField(TEXT("valuesChanged"), ValuesActuallyChanged);
	Result->SetBoolField(TEXT("changed"), ValuesActuallyChanged > 0);

	if (InversePairs.Num() > 0)
	{
		// Self-inverse: the same handler with each cvar's previous value, in the
		// array form it accepts. Only the cvars that were actually found are
		// listed; the ones in notFound were never written.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("cvars"), InversePairs);
		MCPSetRollback(Result, TEXT("set_cvars"), Payload);
		// The value comes back; the priority it came back at does not. Both the
		// write and the rollback go in at ECVF_SetByConsole, so a cvar that was
		// last set by a lower-priority source (a config file, a scalability
		// group, a device profile) keeps the console priority afterwards and
		// stops accepting writes from that source.
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Restores each cvar's previous string value, but at SetByConsole priority rather than the priority it originally held. A variable that had been set by a config file, a scalability group or a device profile keeps the console priority afterwards and will ignore later writes from those sources until the editor restarts."));
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("No requested console variable exists in this build, so nothing was written and there is nothing to restore."));
	}
	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FEditorHandlers::ListCrashes(const TSharedPtr<FJsonObject>& Params)
{
	FString CrashesDir = FPaths::ProjectSavedDir() / TEXT("Crashes");

	// T3: paged. A project that has been crashing accumulates hundreds of
	// folders, each carrying its own file listing.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params, TEXT("list_crashes"), /*DefaultLimit*/ 50, /*MaxLimit*/ 500, Page))
	{
		return Err;
	}

	TArray<MCPPagination::FPageRow> Rows;
	IFileManager& FileManager = IFileManager::Get();

	TArray<FString> CrashFolders;
	FileManager.FindFiles(CrashFolders, *(CrashesDir / TEXT("*")), false, true);
	// FindFiles returns whatever order the filesystem enumerates in, which is
	// not a contract, so the folders are sorted before paging. Crash folder
	// names carry their timestamp, so this is also chronological.
	CrashFolders.Sort();

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
		// The folder name is the page anchor: unique within Saved/Crashes, and
		// unchanged while the folder is on disk.
		Rows.Add({ Folder, MakeShared<FJsonValueObject>(CrashObj) });
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("crashesDir"), CrashesDir);
	MCPPagination::EmitPage(Page, Rows, TEXT("crashes"), Result);
	Result->SetNumberField(TEXT("crashCount"), Result->GetNumberField(TEXT("count")));
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
	// #599: allow capturing the PIE world (not just the editor world) so a
	// framed shot reflects the running game.
	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World) return MCPError(FString::Printf(TEXT("World not available for scope '%s'"), *WorldScope));

	// #966: capture_screenshot names this parameter `filename` and this action
	// named it `outputPath`, for the same thing, so passing one to the other
	// failed on a call that was otherwise correct. Both are accepted by both.
	FString OutputPath = OptionalString(Params, TEXT("outputPath"));
	if (OutputPath.IsEmpty())
	{
		OutputPath = OptionalString(Params, TEXT("filename"));
	}
	if (OutputPath.IsEmpty())
	{
		return MCPError(TEXT("Missing 'outputPath' (also accepted as 'filename'): where to write the PNG"));
	}

	// Resolution
	int32 Width = OptionalInt(Params, TEXT("width"), 1280);
	int32 Height = OptionalInt(Params, TEXT("height"), 720);
	Width = FMath::Clamp(Width, 16, 8192);
	Height = FMath::Clamp(Height, 16, 8192);

	const double Fov = OptionalNumber(Params, TEXT("fov"), 90.0);
	const float CaptureFov = static_cast<float>(Fov);
	if (!FMath::IsFinite(CaptureFov) || CaptureFov <= 0.0f || CaptureFov >= 180.0f)
	{
		return MCPError(TEXT("fov must be finite and between 0 and 180 degrees (exclusive)"));
	}

	FVector Location = OptionalVec3(Params, TEXT("location"));
	FRotator Rotation = OptionalRotator(Params, TEXT("rotation"));
	const double FocusMargin = OptionalNumber(Params, TEXT("focusMargin"), 1.5);
	if (!FMath::IsFinite(FocusMargin) || FocusMargin <= 0.0)
	{
		return MCPError(TEXT("focusMargin must be finite and greater than zero"));
	}

	// #599: focusActorLabel frames the capture on a specific actor by computing
	// a camera position from the actor's bounds and looking at its center.
	const FString FocusLabel = OptionalString(Params, TEXT("focusActorLabel"));
	const FString FocusPath = OptionalString(Params, TEXT("focusActorPath"));
	TSharedPtr<FJsonObject> FocusMetadata;
	if (!FocusLabel.IsEmpty() || !FocusPath.IsEmpty())
	{
		// #983: framing on the wrong copy of a duplicated label is a capture
		// that quietly shows somewhere else entirely.
		FMCPActorSelector FocusSel;
		FocusSel.LabelKey = TEXT("focusActorLabel");
		FocusSel.PathKey = TEXT("focusActorPath");
		FocusSel.Match = EMCPActorMatch::LabelNameOrPath;
		TSharedPtr<FJsonValue> FocusErr;
		AActor* Focus = MCPResolveActor(World, Params, FocusErr, FocusSel);
		if (!Focus) return FocusErr;
		FVector Origin, Extent;
		Focus->GetActorBounds(false, Origin, Extent);
		const double Radius = FMath::Max(Extent.Size(), 50.0);
		// Distance so the bounds fill the frame at the given FOV, with margin.
		const double Distance = (Radius / FMath::Tan(FMath::DegreesToRadians(Fov * 0.5))) * FocusMargin;
		// Default framing direction (front-ish, slightly above); overridable.
		FVector Dir = OptionalVec3(Params, TEXT("focusDirection"), FVector(-1.0, -1.0, 0.6));
		if (Dir.ContainsNaN() || !FMath::IsFinite(Distance)) return MCPError(TEXT("Focus framing must produce finite coordinates"));
		if (!Dir.Normalize()) Dir = FVector(-1, -1, 0.6).GetSafeNormal();
		Location = Origin + Dir * Distance;
		Rotation = (Origin - Location).Rotation();
		FocusMetadata = MakeShared<FJsonObject>();
		FocusMetadata->SetStringField(TEXT("actorPath"), Focus->GetPathName());
		FocusMetadata->SetStringField(TEXT("actorLabel"), Focus->GetActorLabel());
		FocusMetadata->SetObjectField(TEXT("boundsOrigin"), MCPVec3ToJsonObject(Origin));
		FocusMetadata->SetObjectField(TEXT("boundsExtent"), MCPVec3ToJsonObject(Extent));
		FocusMetadata->SetObjectField(TEXT("directionWorld"), MCPVec3ToJsonObject(Dir));
	}
	if (Location.ContainsNaN() || Rotation.ContainsNaN()) return MCPError(TEXT("Capture camera location and rotation must be finite"));

	// #966: the capture actor used to be spawned once and LEFT IN THE LEVEL.
	// It is a level actor, so it was saved into the map and committed to source
	// control as though somebody had authored it, and list_dirty_packages
	// reported nothing while it sat there, so the usual "did I change
	// anything" check missed it entirely. A capture must leave the level
	// exactly as it found it.
	//
	// Two halves. First, sweep any debris earlier builds left behind, so a
	// project that already has one is cleaned by the next capture rather than
	// by hand.
	static const FString CaptureLabel = TEXT("__ClaudeSceneCapture");
	int32 RemovedStrayCaptures = 0;
	{
		TArray<ASceneCapture2D*> Strays;
		for (TActorIterator<ASceneCapture2D> It(World); It; ++It)
		{
			if (It->GetActorLabel() == CaptureLabel)
			{
				Strays.Add(*It);
			}
		}
		for (ASceneCapture2D* Stray : Strays)
		{
			// These ones ARE in the map, so their removal is a real edit and
			// goes through Modify(): the level has to become dirty, or the
			// debris comes straight back on the next load.
			if (IsValid(Stray) && World->DestroyActor(Stray))
			{
				++RemovedStrayCaptures;
			}
		}
	}

	// Second, this capture's own actor: transient, outside the scene outliner,
	// in no actor package of its own, and destroyed on every exit from here.
	// The transient flag alone is not enough, because the debris that prompted
	// this was visible in the outliner and in the map's actor list either way.
	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
#if WITH_EDITOR
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.bHideFromSceneOutliner = true;
	SpawnParams.bCreateActorPackage = false;
#endif
	ASceneCapture2D* CaptureActor = World->SpawnActor<ASceneCapture2D>(ASceneCapture2D::StaticClass(), Location, Rotation, SpawnParams);
	if (!CaptureActor) return MCPError(TEXT("Failed to spawn SceneCapture2D actor"));
	CaptureActor->SetActorHiddenInGame(true);

	ON_SCOPE_EXIT
	{
		if (IsValid(CaptureActor))
		{
			// Drop the render target first: the component holds the only
			// reference keeping it alive past this call.
			if (USceneCaptureComponent2D* Dying = CaptureActor->GetCaptureComponent2D())
			{
				Dying->TextureTarget = nullptr;
			}
			// bShouldModifyLevel=false: this actor was never part of the map,
			// so removing it is not an edit and must not dirty the package.
			World->DestroyActor(CaptureActor, /*bNetForce*/ false, /*bShouldModifyLevel*/ false);
		}
	};

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

	// #662: force-stream all textures to full resolution and flush the render
	// thread so the capture is a complete, resident frame rather than the
	// unloaded-texture checker or a stale cached image. Double-capture ensures
	// streamed mips that arrive after the first pass are present in the second.
	const bool bFullyLoadTextures = OptionalBool(Params, TEXT("fullyLoadTextures"), true);
	if (bFullyLoadTextures)
	{
		IStreamingManager::Get().StreamAllResources(0.0f);
		FlushRenderingCommands();
		Comp->CaptureScene();
		FlushRenderingCommands();
	}
	Comp->CaptureScene();
	FlushRenderingCommands();

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

	// Asked before the export, because it is the difference between creating a
	// file and destroying whatever was at that path.
	const bool bOverwrote = IFileManager::Get().FileExists(*AbsPath);

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
	// Pair the image with the camera that rendered it. The editor viewport is
	// a different camera and cannot be used to deproject this image's pixels.
	auto CaptureMetadata = MakeShared<FJsonObject>();
	CaptureMetadata->SetStringField(TEXT("worldPath"), World->GetPathName());
	CaptureMetadata->SetStringField(TEXT("world"), World->IsGameWorld() ? TEXT("pie") : TEXT("editor"));
	if (const FWorldContext* Context = GEngine->GetWorldContextFromWorld(World))
	{
		if (Context->WorldType == EWorldType::PIE)
			CaptureMetadata->SetNumberField(TEXT("pieInstance"), Context->PIEInstance);
	}
	CaptureMetadata->SetStringField(TEXT("projection"), TEXT("perspective"));
	CaptureMetadata->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Comp->GetComponentLocation()));
	CaptureMetadata->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Comp->GetComponentRotation()));
	CaptureMetadata->SetObjectField(TEXT("forward"), MCPVec3ToJsonObject(Comp->GetForwardVector()));
	CaptureMetadata->SetObjectField(TEXT("right"), MCPVec3ToJsonObject(Comp->GetRightVector()));
	CaptureMetadata->SetObjectField(TEXT("up"), MCPVec3ToJsonObject(Comp->GetUpVector()));
	CaptureMetadata->SetNumberField(TEXT("fovDegrees"), Comp->FOVAngle);
	CaptureMetadata->SetNumberField(TEXT("width"), RT->SizeX);
	CaptureMetadata->SetNumberField(TEXT("height"), RT->SizeY);
	CaptureMetadata->SetNumberField(TEXT("worldTimeSeconds"), World->GetTimeSeconds());
	CaptureMetadata->SetBoolField(TEXT("fullyLoadTextures"), bFullyLoadTextures);
	if (FocusMetadata) CaptureMetadata->SetObjectField(TEXT("focus"), FocusMetadata);
	Result->SetObjectField(TEXT("captureMetadata"), CaptureMetadata);
	// The capture actor is gone by the time this reaches the caller, so say so
	// rather than naming an actor they could go and look for.
	Result->SetBoolField(TEXT("captureActorRemoved"), true);
	if (RemovedStrayCaptures > 0)
	{
		Result->SetNumberField(TEXT("strayCaptureActorsRemoved"), RemovedStrayCaptures);
	}
	Result->SetBoolField(TEXT("overwrote"), bOverwrote);
	// A capture always renders and writes a fresh frame, so it never finds work
	// already done and skips it.
	Result->SetBoolField(TEXT("changed"), true);
	// No rollback: nothing in the bridge deletes a file from disk, so a capture
	// cannot be taken back, and when it overwrote an existing image that image
	// is gone as well. The transient capture actor and render target it used
	// were already torn down, so nothing else is left to undo.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"), bOverwrote
		? TEXT("No bridge call deletes a file from disk, and this capture replaced a file that was already at that path. Neither the new file nor the one it overwrote can be restored. Capture to a fresh path when a flow may need to roll back.")
		: TEXT("No bridge call deletes a file from disk, so the captured image cannot be removed again. The capture actor and render target were transient and are already gone."));
	return MCPResult(Result);
}

// #455: enumerate UBlueprintFunctionLibrary subclasses. Filters by pattern
// (case-insensitive substring) so callers can find UGeometryScriptLibrary_*,
// UKismetMathLibrary, UAnimationLibrary, etc. Each entry includes function
// names so editor.invoke_function can target an op directly. Pair with
// invoke_function to drive GeometryScript / any function library from MCP
// without authoring per-op C++ wrappers.
//
// Params: pattern? (substring filter), includeFunctions? (default true)
TSharedPtr<FJsonValue> FEditorHandlers::ListFunctionLibraries(const TSharedPtr<FJsonObject>& Params)
{
	const FString Pattern = OptionalString(Params, TEXT("pattern"), TEXT("")).ToLower();
	const bool bIncludeFunctions = OptionalBool(Params, TEXT("includeFunctions"), true);

	TArray<TSharedPtr<FJsonValue>> Libraries;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* C = *It;
		if (!C || !C->IsChildOf(UBlueprintFunctionLibrary::StaticClass())) continue;
		if (C == UBlueprintFunctionLibrary::StaticClass()) continue;
		if (C->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) continue;

		const FString ClassName = C->GetName();
		if (!Pattern.IsEmpty() && !ClassName.ToLower().Contains(Pattern)) continue;

		TSharedPtr<FJsonObject> LibObj = MakeShared<FJsonObject>();
		LibObj->SetStringField(TEXT("name"), ClassName);
		LibObj->SetStringField(TEXT("path"), C->GetPathName());
		if (UPackage* Pkg = C->GetOuterUPackage())
		{
			LibObj->SetStringField(TEXT("module"), Pkg->GetName());
		}

		if (bIncludeFunctions)
		{
			TArray<TSharedPtr<FJsonValue>> Funcs;
			for (TFieldIterator<UFunction> FIt(C, EFieldIteratorFlags::ExcludeSuper); FIt; ++FIt)
			{
				UFunction* Func = *FIt;
				if (!Func) continue;
				if (!Func->HasAllFunctionFlags(FUNC_Static | FUNC_BlueprintCallable)) continue;
				TSharedPtr<FJsonObject> FObj = MakeShared<FJsonObject>();
				FObj->SetStringField(TEXT("name"), Func->GetName());
				const FString Tooltip = Func->GetToolTipText().ToString();
				if (!Tooltip.IsEmpty()) FObj->SetStringField(TEXT("tooltip"), Tooltip.Left(240));
				Funcs.Add(MakeShared<FJsonValueObject>(FObj));
			}
			LibObj->SetArrayField(TEXT("functions"), Funcs);
			LibObj->SetNumberField(TEXT("functionCount"), Funcs.Num());
		}

		Libraries.Add(MakeShared<FJsonValueObject>(LibObj));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("libraries"), Libraries);
	Result->SetNumberField(TEXT("count"), Libraries.Num());
	if (!Pattern.IsEmpty()) Result->SetStringField(TEXT("pattern"), Pattern);
	return MCPResult(Result);
}

// ─── #693 run_automation_tests ──────────────────────────────────────
// Headlessly run registered Automation tests whose name matches a filter and
// report pass/fail + errors. Uses FAutomationTestFramework's synchronous
// StartTestByName/StopTest, which fully runs non-latent tests. Latent tests
// (that enqueue cross-frame commands) get a bounded drain, and one that has not
// finished when the budget runs out is abandoned and reported, never stopped
// mid-queue.
//
// #993: StopTest asserts on LatentCommands.IsEmpty() inside InternalStopTest,
// and this handler used to call it unconditionally after a drain loop that gave
// up after a fixed iteration count. A CQTest test that starts multi-client PIE
// still had commands queued at that point, so the assert fired and took the
// editor down: the caller saw a WebSocket close 1006, not a test result. A
// handler must never take the editor down.
TSharedPtr<FJsonValue> FEditorHandlers::RunAutomationTests(const TSharedPtr<FJsonObject>& Params)
{
	const FString NameFilter = OptionalString(Params, TEXT("filter"));
	const int32 MaxTests = OptionalInt(Params, TEXT("maxTests"), 50);
	// How long one test's latent queue may take. The handler occupies the game
	// thread for its whole run, so this is deliberately small by default: a
	// latent command that needs real frames (PIE, anything waiting on a world
	// tick) cannot make progress from in here however long it is given, and
	// waiting longer only holds the editor still for longer.
	const double LatentBudgetSeconds = FMath::Clamp(
		OptionalNumber(Params, TEXT("latentTimeoutSeconds"), 5.0), 0.0, 120.0);

	// #765: this handler runs tests SYNCHRONOUSLY via StartTestByName inside a
	// single tick, so it never depended on the interactive-frame-rate gate that
	// blocks the console "Automation RunTests" queue - that is the actual fix
	// for the reported hang. The throttle is still suspended here because the
	// latent-command flush below can span frames if the editor does tick, and
	// an agent-driven editor is unfocused by definition. It is a belt-and-braces
	// measure, not the mechanism.
	UEditorPerformanceSettings* PerfSettings = GetMutableDefault<UEditorPerformanceSettings>();
	const bool bPrevThrottle = PerfSettings && PerfSettings->bThrottleCPUWhenNotForeground;
	if (PerfSettings && bPrevThrottle)
	{
		PerfSettings->bThrottleCPUWhenNotForeground = false;
		PerfSettings->PostEditChange();
	}
	ON_SCOPE_EXIT
	{
		if (PerfSettings && bPrevThrottle)
		{
			PerfSettings->bThrottleCPUWhenNotForeground = true;
			PerfSettings->PostEditChange();
		}
	};

	FAutomationTestFramework& Framework = FAutomationTestFramework::Get();
	Framework.SetRequestedTestFilter(EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter |
		EAutomationTestFlags::SmokeFilter | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::ClientContext);

	TArray<FAutomationTestInfo> AllTests;
	Framework.GetValidTestNames(AllTests);

	TArray<TSharedPtr<FJsonValue>> Results;
	int32 Ran = 0, Passed = 0, Failed = 0, Abandoned = 0;
	for (const FAutomationTestInfo& Info : AllTests)
	{
		const FString TestName = Info.GetTestName();
		const FString DisplayName = Info.GetDisplayName();
		if (!NameFilter.IsEmpty() &&
			!TestName.Contains(NameFilter) && !DisplayName.Contains(NameFilter))
		{
			continue;
		}
		if (Ran >= MaxTests) break;

		Framework.StartTestByName(TestName, /*RoleIndex*/ 0);

		// Drain the latent queue, bounded by wall clock rather than by a fixed
		// iteration count. The old bound was 1000 iterations of a zero-length
		// sleep, which elapses in no time at all and gave a command that polls
		// for a condition no real chance to reach one. Network commands are
		// pumped alongside, because the framework's own per-frame path does and
		// a latent queue behind an undelivered network command never empties.
		const double DrainStartedAt = FPlatformTime::Seconds();
		while (!Framework.ExecuteLatentCommands()
			&& (FPlatformTime::Seconds() - DrainStartedAt) < LatentBudgetSeconds)
		{
			Framework.ExecuteNetworkCommands();
			FPlatformProcess::Sleep(0.001f);
		}

		// #993: StopTest asserts that the latent queue is empty. Anything still
		// queued here needs frames this handler cannot give it, because the
		// handler owns the game thread for its whole run and the engine loop
		// only advances once it returns. Clear the queue and record the test as
		// abandoned; calling StopTest with commands outstanding is what took
		// the editor down.
		const bool bAbandoned = !Framework.IsLatentCommandQueueEmpty();
		if (bAbandoned)
		{
			Framework.DequeueAllCommands();
		}

		FAutomationTestExecutionInfo ExecInfo;
		const bool bStopReportedPass = Framework.StopTest(ExecInfo);
		const bool bPassed = bStopReportedPass && !bAbandoned;
		++Ran;
		if (bAbandoned) ++Abandoned;
		else if (bPassed) ++Passed;
		else ++Failed;

		// An abandoned test may have left PIE running, and leaving it up would
		// silently change what every later call in this session sees.
		if (bAbandoned && GEditor && (GEditor->PlayWorld != nullptr || GEditor->bIsSimulatingInEditor))
		{
			GEditor->RequestEndPlayMap();
		}

		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetStringField(TEXT("test"), DisplayName.IsEmpty() ? TestName : DisplayName);
		R->SetBoolField(TEXT("passed"), bPassed);
		R->SetBoolField(TEXT("abandoned"), bAbandoned);
		if (bAbandoned)
		{
			R->SetStringField(TEXT("abandonedReason"), FString::Printf(
				TEXT("Latent commands were still queued after %.1fs. They need engine frames, which cannot happen while this handler holds the game thread, so the test was abandoned rather than stopped mid-queue (stopping mid-queue asserts and terminates the editor). Run this one from the editor's Automation window, or with -ExecCmds=\"Automation RunTests %s\" at launch."),
				LatentBudgetSeconds, *TestName));
		}
		R->SetNumberField(TEXT("errors"), ExecInfo.GetErrorTotal());
		R->SetNumberField(TEXT("warnings"), ExecInfo.GetWarningTotal());
		if (ExecInfo.GetErrorTotal() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> ErrMsgs;
			for (const FAutomationExecutionEntry& Entry : ExecInfo.GetEntries())
			{
				if (Entry.Event.Type == EAutomationEventType::Error)
				{
					ErrMsgs.Add(MakeShared<FJsonValueString>(Entry.Event.Message));
				}
			}
			R->SetArrayField(TEXT("errorMessages"), ErrMsgs);
		}
		Results.Add(MakeShared<FJsonValueObject>(R));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("filter"), NameFilter);
	Result->SetNumberField(TEXT("discovered"), AllTests.Num());
	Result->SetNumberField(TEXT("ran"), Ran);
	Result->SetNumberField(TEXT("passed"), Passed);
	Result->SetNumberField(TEXT("failed"), Failed);
	Result->SetNumberField(TEXT("abandoned"), Abandoned);
	Result->SetNumberField(TEXT("latentTimeoutSeconds"), LatentBudgetSeconds);
	if (Abandoned > 0)
	{
		Result->SetStringField(TEXT("warning"), FString::Printf(
			TEXT("%d test(s) were abandoned with latent commands still queued. They need engine frames, and this handler holds the game thread for its whole run, so it cannot supply any. Raising latentTimeoutSeconds helps a test that only polls; a test that starts PIE needs the editor's Automation window or -ExecCmds=\"Automation RunTests <name>\" at launch. Nothing was stopped mid-queue, which is what used to terminate the editor (#993)."),
			Abandoned));
	}
	Result->SetArrayField(TEXT("results"), Results);
	// A filter that matched nothing ran no test code at all. Past that, what a
	// test wrote is the test's business: this handler observes pass/fail, not
	// a diff of the project.
	Result->SetBoolField(TEXT("changed"), Ran > 0);
	// No rollback. A test can spawn actors, write assets, start PIE and leave
	// state behind (an abandoned one demonstrably does), and none of that was
	// captured before the run. Re-running is not an inverse.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Automation tests run arbitrary engine and project code, nothing was captured before the run, and re-running them is not an inverse. A test that leaves state behind has to clean up after itself; abandoned tests are reported so that case is visible."));
	return MCPResult(Result);
}
