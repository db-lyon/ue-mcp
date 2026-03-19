#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FEditorHandlers
{
public:
	// Register all editor handlers
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	// Handler implementations
	static TSharedPtr<FJsonValue> ExecuteCommand(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ExecutePython(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetConfig(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadConfig(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetViewportInfo(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetEditorPerformanceStats(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetOutputLog(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SearchLog(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetMessageLog(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetBuildStatus(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieControl(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CaptureScreenshot(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetViewportCamera(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> Undo(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> Redo(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReloadHandlers(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SaveAsset(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SaveAll(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetCrashReports(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadEditorLog(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieGetRuntimeValue(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> BuildLighting(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> BuildAll(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ValidateAssets(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CookContent(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> FocusViewportOnActor(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> HotReload(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateNewLevel(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SaveCurrentLevel(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> OpenAsset(const TSharedPtr<FJsonObject>& Params);
};
