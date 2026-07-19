#include "EditorHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
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
#include "Slate/SceneViewport.h"
#include "HAL/PlatformMemory.h"
#include "Misc/App.h"
#include "Logging/MessageLog.h"
#include "HighResScreenshot.h"
#include "Framework/Application/SlateApplication.h"
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
#include "JsonSerializer.h"
#include "Engine/Blueprint.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

namespace
{
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
	Registry.RegisterHandler(TEXT("reload_handlers"), &ReloadHandlers);
	Registry.RegisterHandler(TEXT("save_asset"), &SaveAsset);
	Registry.RegisterHandler(TEXT("save_dirty"), &SaveDirty);
	Registry.RegisterHandler(TEXT("list_dirty_packages"), &ListDirtyPackages);
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
	Registry.RegisterHandler(TEXT("invoke_function_repeating"), &InvokeFunctionRepeating);
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
	if (bWasOpen)
	{
		Result->SetStringField(TEXT("closedSequence"), OpenPath);
	}
	return MCPResult(Result);
}

// #727: open a registered editor tab by ID (e.g. "ProjectSettings", "OutputLog")
// so an agent can screenshot editor UI as evidence.
TSharedPtr<FJsonValue> FEditorHandlers::OpenTab(const TSharedPtr<FJsonObject>& Params)
{
	FString TabId;
	if (auto Err = RequireString(Params, TEXT("tabId"), TabId)) return Err;

	TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->TryInvokeTab(FTabId(*TabId));

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("tabId"), TabId);
	Result->SetBoolField(TEXT("opened"), Tab.IsValid());
	if (!Tab.IsValid())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("No registered tab with id '%s' (try 'ProjectSettings', 'OutputLog', 'ContentBrowserTab1', ...)"), *TabId));
	}
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
	FGlobalTabmanager::Get()->TryInvokeTab(FTabId(Container == TEXT("Editor") ? TEXT("EditorSettings") : TEXT("ProjectSettings")));
	if (!Category.IsEmpty())
	{
		SettingsModule->ShowViewer(FName(*Container), FName(*Category), FName(*Section));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("container"), Container);
	Result->SetStringField(TEXT("category"), Category);
	Result->SetStringField(TEXT("section"), Section);
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
	for (FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
	{
		if (!ViewportClient) continue;
		ViewportClient->SetRealtime(bEnabled);
		ViewportsChanged++;
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("enabled"), bEnabled);
	Result->SetNumberField(TEXT("viewportsChanged"), ViewportsChanged);
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
			if (AActor* A = FindActorByLabel(World, Label)) Query.AddIgnoredActor(A);
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

	// target=window: synchronous FSlateApplication::TakeScreenshot of the whole
	// active (or last visible) Slate window. Unlike the FScreenshotRequest paths
	// this is pixel-true for ALL Slate content (painted UMG/Slate widgets that
	// showUI compositing can miss), returns only after the PNG is on disk, and
	// works even when the window is off-screen or unfocused - which makes it the
	// reliable way for an agent to visually QA game UI.
	if (Target == TEXT("window"))
	{
		if (!FSlateApplication::IsInitialized())
		{
			return MCPError(TEXT("Slate is not initialized"));
		}
		TSharedPtr<SWindow> CaptureWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
		if (!CaptureWindow.IsValid())
		{
			TArray<TSharedRef<SWindow>> VisibleWindows;
			FSlateApplication::Get().GetAllVisibleWindowsOrdered(VisibleWindows);
			if (VisibleWindows.Num() > 0)
			{
				CaptureWindow = VisibleWindows.Last();
			}
		}
		if (!CaptureWindow.IsValid())
		{
			return MCPError(TEXT("No visible Slate window to capture"));
		}

		TArray<FColor> Pixels;
		FIntVector CaptureSize = FIntVector::ZeroValue;
		if (!FSlateApplication::Get().TakeScreenshot(CaptureWindow.ToSharedRef(), Pixels, CaptureSize)
			|| Pixels.Num() == 0)
		{
			return MCPError(TEXT("Slate window screenshot failed"));
		}
		// Slate hands back per-widget alpha; force opaque so the PNG is viewable.
		for (FColor& Pixel : Pixels)
		{
			Pixel.A = 255;
		}

		FString WindowFullPath = Filename;
		if (FPaths::IsRelative(WindowFullPath))
		{
			WindowFullPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), Filename);
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
		const TArray64<uint8> PngData = PngWrapper->GetCompressed(100);
		if (!FFileHelper::SaveArrayToFile(PngData, *WindowFullPath))
		{
			return MCPError(FString::Printf(TEXT("Could not write %s"), *WindowFullPath));
		}

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("filename"), WindowFullPath);
		Result->SetStringField(TEXT("target"), TEXT("window"));
		Result->SetStringField(TEXT("window"), CaptureWindow->GetTitle().ToString());
		Result->SetNumberField(TEXT("width"), CaptureSize.X);
		Result->SetNumberField(TEXT("height"), CaptureSize.Y);
		Result->SetStringField(TEXT("note"), TEXT("Synchronous Slate window capture including all UI."));
		return MCPResult(Result);
	}

	UWorld* PieWorld = nullptr;
	if (FWorldContext* PieCtx = GEditor->GetPIEWorldContext())
	{
		PieWorld = PieCtx->World();
	}
	const bool bUsePie = (Target == TEXT("pie")) || (Target == TEXT("auto") && PieWorld);

	if (bUsePie && PieWorld)
	{
		// #724: HighResShot renders the PIE world offscreen and (a) strips the
		// debug canvas (AddOnScreenDebugMessage overlays) and (b) in
		// Play-in-New-Window did not reliably resolve to the PIE game window.
		// Capture the actual PIE game viewport with a normal screenshot request
		// and bShowUI=true, so we get exactly what the player sees - HUD and the
		// on-screen debug canvas included. The running game viewport consumes the
		// pending request on its next Draw, so this targets the PIE window even
		// in new-window mode. Fall back to HighResShot only if no game viewport.
		FString FullPath = Filename;
		if (FPaths::IsRelative(Filename))
		{
			FullPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), Filename);
		}

		UGameViewportClient* GameViewport = PieWorld->GetGameViewport();
		if (GameViewport && GameViewport->Viewport)
		{
			FScreenshotRequest::RequestScreenshot(FullPath, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
			GameViewport->Viewport->Invalidate();

			auto Result = MCPSuccess();
			Result->SetStringField(TEXT("filename"), FullPath);
			Result->SetStringField(TEXT("target"), TEXT("pie"));
			Result->SetBoolField(TEXT("includesDebugCanvas"), true);
			Result->SetStringField(TEXT("note"), TEXT("PIE game-viewport screenshot queued (UI + on-screen debug canvas included); written asynchronously."));
			return MCPResult(Result);
		}

		// Fallback: no resolvable game viewport (unusual) - dispatch HighResShot.
		int32 Width = OptionalInt(Params, TEXT("width"), 1920);
		int32 Height = OptionalInt(Params, TEXT("height"), 1080);
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
		Result->SetBoolField(TEXT("includesDebugCanvas"), false);
		Result->SetStringField(TEXT("note"), TEXT("No game viewport resolved; HighResShot dispatched (debug canvas not captured). Output in Saved/Screenshots/<map>/."));
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

	if (Params->HasField(TEXT("location")))
	{
		ViewportClient->SetViewLocation(OptionalVec3(Params, TEXT("location")));
	}
	if (Params->HasField(TEXT("rotation")))
	{
		ViewportClient->SetViewRotation(OptionalRotator(Params, TEXT("rotation")));
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

	AActor* TargetActor = FindActorByLabel(World, ActorLabel);
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
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("name"), Req.Key);
		Row->SetStringField(TEXT("oldValue"), OldValue);
		Row->SetStringField(TEXT("newValue"), CVar->GetString());
		Applied.Add(MakeShared<FJsonValueObject>(Row));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("applied"), Applied);
	Result->SetNumberField(TEXT("appliedCount"), Applied.Num());
	if (NotFound.Num() > 0) Result->SetArrayField(TEXT("notFound"), NotFound);
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
	// #599: allow capturing the PIE world (not just the editor world) so a
	// framed shot reflects the running game.
	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldScope(WorldScope);
	if (!World) return MCPError(FString::Printf(TEXT("World not available for scope '%s'"), *WorldScope));

	FString OutputPath;
	if (auto Err = RequireString(Params, TEXT("outputPath"), OutputPath)) return Err;

	// Resolution
	int32 Width = OptionalInt(Params, TEXT("width"), 1280);
	int32 Height = OptionalInt(Params, TEXT("height"), 720);
	Width = FMath::Clamp(Width, 16, 8192);
	Height = FMath::Clamp(Height, 16, 8192);

	const double Fov = OptionalNumber(Params, TEXT("fov"), 90.0);

	FVector Location = OptionalVec3(Params, TEXT("location"));
	FRotator Rotation = OptionalRotator(Params, TEXT("rotation"));

	// #599: focusActorLabel frames the capture on a specific actor by computing
	// a camera position from the actor's bounds and looking at its center.
	const FString FocusLabel = OptionalString(Params, TEXT("focusActorLabel"));
	if (!FocusLabel.IsEmpty())
	{
		AActor* Focus = FindActorByLabelNameOrPath(World, FocusLabel);
		if (!Focus) return MCPError(FString::Printf(TEXT("focusActorLabel not found: %s"), *FocusLabel));
		FVector Origin, Extent;
		Focus->GetActorBounds(false, Origin, Extent);
		const double Radius = FMath::Max(Extent.Size(), 50.0);
		// Distance so the bounds fill the frame at the given FOV, with margin.
		const double Distance = (Radius / FMath::Tan(FMath::DegreesToRadians(Fov * 0.5))) * OptionalNumber(Params, TEXT("focusMargin"), 1.5);
		// Default framing direction (front-ish, slightly above); overridable.
		FVector Dir = OptionalVec3(Params, TEXT("focusDirection"), FVector(-1.0, -1.0, 0.6));
		if (!Dir.Normalize()) Dir = FVector(-1, -1, 0.6).GetSafeNormal();
		Location = Origin + Dir * Distance;
		Rotation = (Origin - Location).Rotation();
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
// (that enqueue cross-frame commands) have their queued commands flushed on a
// best-effort tick loop with a timeout.
TSharedPtr<FJsonValue> FEditorHandlers::RunAutomationTests(const TSharedPtr<FJsonObject>& Params)
{
	const FString NameFilter = OptionalString(Params, TEXT("filter"));
	const int32 MaxTests = OptionalInt(Params, TEXT("maxTests"), 50);

	FAutomationTestFramework& Framework = FAutomationTestFramework::Get();
	Framework.SetRequestedTestFilter(EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter |
		EAutomationTestFlags::SmokeFilter | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::ClientContext);

	TArray<FAutomationTestInfo> AllTests;
	Framework.GetValidTestNames(AllTests);

	TArray<TSharedPtr<FJsonValue>> Results;
	int32 Ran = 0, Passed = 0, Failed = 0;
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

		// Flush latent commands (best effort, bounded) so latent tests finish.
		int32 Guard = 0;
		while (!Framework.ExecuteLatentCommands() && Guard++ < 1000)
		{
			FPlatformProcess::Sleep(0.0f);
		}

		FAutomationTestExecutionInfo ExecInfo;
		const bool bPassed = Framework.StopTest(ExecInfo);
		++Ran;
		if (bPassed) ++Passed; else ++Failed;

		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetStringField(TEXT("test"), DisplayName.IsEmpty() ? TestName : DisplayName);
		R->SetBoolField(TEXT("passed"), bPassed);
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
	Result->SetArrayField(TEXT("results"), Results);
	return MCPResult(Result);
}
