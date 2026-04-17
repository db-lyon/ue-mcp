#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "Misc/OutputDevice.h"
#include "HAL/CriticalSection.h"

// Ring-buffer output device that captures log lines for get_output_log / search_log (#82)
class FMCPLogCapture : public FOutputDevice
{
public:
	static FMCPLogCapture& Get()
	{
		static FMCPLogCapture Instance;
		return Instance;
	}

	void Install()
	{
		if (!bInstalled)
		{
			GLog->AddOutputDevice(this);
			bInstalled = true;
		}
	}

	void Uninstall()
	{
		if (bInstalled)
		{
			GLog->RemoveOutputDevice(this);
			bInstalled = false;
		}
	}

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override
	{
		FScopeLock Lock(&CritSection);
		FMCPLogLine Entry;
		Entry.Message = V;
		Entry.Category = Category.ToString();
		switch (Verbosity)
		{
		case ELogVerbosity::Fatal:   Entry.Verbosity = TEXT("Fatal"); break;
		case ELogVerbosity::Error:   Entry.Verbosity = TEXT("Error"); break;
		case ELogVerbosity::Warning: Entry.Verbosity = TEXT("Warning"); break;
		case ELogVerbosity::Display: Entry.Verbosity = TEXT("Display"); break;
		default:                     Entry.Verbosity = TEXT("Log"); break;
		}
		Lines[WriteIndex % MaxLines] = MoveTemp(Entry);
		WriteIndex++;
	}

	struct FMCPLogLine
	{
		FString Message;
		FString Category;
		FString Verbosity;
	};

	TArray<FMCPLogLine> GetRecentLines(int32 Count) const
	{
		FScopeLock Lock(&CritSection);
		TArray<FMCPLogLine> Result;
		int32 Available = FMath::Min((int32)WriteIndex, MaxLines);
		int32 Num = FMath::Min(Count, Available);
		int32 StartIdx = (int32)WriteIndex - Num;
		if (StartIdx < 0) StartIdx = 0;
		for (int32 i = StartIdx; i < (int32)WriteIndex; i++)
		{
			Result.Add(Lines[i % MaxLines]);
		}
		return Result;
	}

	TArray<FMCPLogLine> Search(const FString& Query, int32 MaxResults) const
	{
		FScopeLock Lock(&CritSection);
		TArray<FMCPLogLine> Result;
		int32 Available = FMath::Min((int32)WriteIndex, MaxLines);
		int32 StartIdx = (int32)WriteIndex - Available;
		for (int32 i = StartIdx; i < (int32)WriteIndex && Result.Num() < MaxResults; i++)
		{
			const FMCPLogLine& Line = Lines[i % MaxLines];
			if (Line.Message.Contains(Query, ESearchCase::IgnoreCase) ||
				Line.Category.Contains(Query, ESearchCase::IgnoreCase))
			{
				Result.Add(Line);
			}
		}
		return Result;
	}

private:
	FMCPLogCapture() : WriteIndex(0), bInstalled(false) { Lines.SetNum(MaxLines); }
	~FMCPLogCapture() { Uninstall(); }

	static constexpr int32 MaxLines = 4096;
	TArray<FMCPLogLine> Lines;
	TAtomic<int32> WriteIndex;
	mutable FCriticalSection CritSection;
	bool bInstalled;
};

class FEditorHandlers
{
public:
	// Register all editor handlers
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	// Handler implementations
	static TSharedPtr<FJsonValue> ExecuteCommand(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ExecutePython(const TSharedPtr<FJsonObject>& Params);
	// #142: run a Python file with __file__/__name__ context populated
	static TSharedPtr<FJsonValue> RunPythonFile(const TSharedPtr<FJsonObject>& Params);
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
	static TSharedPtr<FJsonValue> RunStatCommand(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetScalability(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> BuildGeometry(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> BuildHlod(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListCrashes(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetCrashInfo(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CheckForCrashes(const TSharedPtr<FJsonObject>& Params);
	// #14: Build project
	static TSharedPtr<FJsonValue> BuildProject(const TSharedPtr<FJsonObject>& Params);
	// #49: Generate project files
	static TSharedPtr<FJsonValue> GenerateProjectFiles(const TSharedPtr<FJsonObject>& Params);
	// #126: Fast-forward PIE game time
	static TSharedPtr<FJsonValue> SetPieTimeScale(const TSharedPtr<FJsonObject>& Params);
	// #148: Headless SceneCapture2D → PNG (works when editor is unfocused)
	static TSharedPtr<FJsonValue> CaptureScenePng(const TSharedPtr<FJsonObject>& Params);
};
