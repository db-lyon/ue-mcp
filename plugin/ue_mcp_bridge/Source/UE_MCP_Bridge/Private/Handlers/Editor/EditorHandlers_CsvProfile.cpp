// editor(start_csv_profile) / editor(stop_csv_profile): the CSV profiler, the
// per-frame stat recorder behind the `csvprofile start` / `csvprofile stop`
// console commands, driven with a named capture and an answer that says where
// the file landed.
//
// The console command returns nothing and the profiler acts on the next frame
// boundary, so a caller using it had to guess the file name and poll the
// folder. These actions call FCsvProfiler directly and wait (through the
// deferred-answer pattern, HandlerDeferred.h) for the two moments that matter:
// the capture really beginning, which is when the output path is fixed, and the
// file being completely written after the stop, which is when the CSV profiler
// fulfils the future EndCapture returns. editor(summarize_csv_profile) reads the
// file back on the server side.
//
// Engine versions. BeginCapture, EndCapture (returning TSharedFuture<FString>),
// GetOutputFilename and GetCaptureFrameNumber have the same shape on every
// supported engine. IsCapturing is a const member up to 5.7 and static from
// 5.8; it is called through the instance pointer, which compiles either way.
// FCsvProfiler::GetDefaultDirectory is 5.8+, so the default folder is built
// here from FPaths::ProfilingDir(), which is what that function returns.
//
// Helpers are prefixed MCPCsvProfile for the unity build.

#include "EditorHandlers.h"

#include "HandlerDeferred.h"
#include "HandlerUtils.h"

#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CsvProfiler.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	/** Where captures go when the caller names no folder: the folder the CSV
	 *  profiler itself defaults to. */
	FString MCPCsvProfileDefaultFolder()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProfilingDir(), TEXT("CSV")));
	}

	/** File stem for a capture: the caller's name made file-safe, or a
	 *  timestamped default, without a .csv extension (the profiler adds it). */
	FString MCPCsvProfileStem(FString Name)
	{
		Name.TrimStartAndEndInline();
		if (Name.EndsWith(TEXT(".csv.gz"), ESearchCase::IgnoreCase)) Name.LeftChopInline(7);
		else if (Name.EndsWith(TEXT(".csv"), ESearchCase::IgnoreCase)) Name.LeftChopInline(4);
		Name = FPaths::MakeValidFileName(Name);
		if (Name.IsEmpty())
		{
			const FString Project = FApp::GetProjectName();
			Name = FString::Printf(TEXT("%s_%s"), Project.IsEmpty() ? TEXT("Csv") : *Project,
				*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
		}
		return Name;
	}

	void MCPCsvProfileDescribeFile(TSharedPtr<FJsonObject> Result, const FString& Path)
	{
		const FString Absolute = FPaths::ConvertRelativePathToFull(Path);
		Result->SetStringField(TEXT("csvPath"), Absolute);
		const bool bExists = FPaths::FileExists(Absolute);
		Result->SetBoolField(TEXT("csvExists"), bExists);
		if (bExists)
		{
			Result->SetNumberField(TEXT("csvBytes"), static_cast<double>(IFileManager::Get().FileSize(*Absolute)));
		}
		Result->SetBoolField(TEXT("compressed"), Absolute.EndsWith(TEXT(".gz"), ESearchCase::IgnoreCase));
	}
}

// ---------------------------------------------------------------------------
// start_csv_profile -- begin a named CSV capture.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::StartCsvProfile(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	const FString CaptureName = OptionalString(Params, TEXT("captureName"));
	const int32 MaxFrames = static_cast<int32>(OptionalNumber(Params, TEXT("maxFrames"), -1.0));
	FString Folder = OptionalString(Params, TEXT("folder"));
	Folder.TrimStartAndEndInline();

#if CSV_PROFILER
	FCsvProfiler* Csv = FCsvProfiler::Get();
	if (!Csv)
	{
		return MCPError(TEXT("The CSV profiler is not available in this process."));
	}

	// One capture at a time per process. A second start would be queued by
	// the engine behind the first and silently ignored, so it is reported
	// instead of being sent.
	if (Csv->IsCapturing())
	{
		TSharedPtr<FJsonObject> Existing = MCPSuccess();
		MCPSetExisted(Existing);
		Existing->SetBoolField(TEXT("started"), false);
		Existing->SetBoolField(TEXT("alreadyCapturing"), true);
		Existing->SetNumberField(TEXT("capturedFrames"), Csv->GetCaptureFrameNumber());
		MCPCsvProfileDescribeFile(Existing, Csv->GetOutputFilename());
		Existing->SetStringField(TEXT("note"),
			TEXT("A CSV capture was already running, so nothing was started. Stop it with editor(stop_csv_profile) first."));
		return MCPResult(Existing);
	}

	if (Folder.IsEmpty())
	{
		Folder = MCPCsvProfileDefaultFolder();
	}
	else
	{
		FString Resolved;
		if (auto Err = MCPRequireWritablePath(Folder, Resolved)) return Err;
		Folder = Resolved;
	}
	while (Folder.EndsWith(TEXT("/")) || Folder.EndsWith(TEXT("\\"))) Folder.LeftChopInline(1);
	IFileManager::Get().MakeDirectory(*Folder, /*Tree=*/true);

	const FString Stem = MCPCsvProfileStem(CaptureName);
	const FString Predicted = FPaths::Combine(Folder, Stem + TEXT(".csv"));
	const int32 FramesToCapture = MaxFrames > 0 ? MaxFrames : -1;

	Csv->BeginCapture(FramesToCapture, Folder, Stem, ECsvProfilerFlags::None);

	auto BuildStarted = [Stem, FramesToCapture](const FString& Path, bool bConfirmed) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Result = MCPSuccess();
		MCPSetCreated(Result);
		Result->SetBoolField(TEXT("started"), true);
		Result->SetBoolField(TEXT("alreadyCapturing"), false);
		Result->SetBoolField(TEXT("captureConfirmed"), bConfirmed);
		Result->SetStringField(TEXT("captureName"), Stem);
		Result->SetNumberField(TEXT("maxFrames"), FramesToCapture);
		MCPCsvProfileDescribeFile(Result, Path);
		Result->SetStringField(TEXT("note"), FramesToCapture > 0
			? TEXT("The capture ends by itself after maxFrames frames; editor(stop_csv_profile) ends it sooner and reports the finished file.")
			: TEXT("The CSV file is written as frames are captured and is only complete after editor(stop_csv_profile)."));
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		MCPSetRollback(Result, TEXT("stop_csv_profile"), Payload);
		return Result;
	};

	// The capture begins at the next frame boundary, which is when the path is
	// fixed (and when a forced-compression setting would turn .csv into
	// .csv.gz). A bridge request waits for that; an in-process call reports
	// the path the profiler was asked for.
	const FMCPDeferredCallPtr Deferred = MCPDeferRequest();
	if (!Deferred)
	{
		return MCPResult(BuildStarted(Predicted, false));
	}
	Deferred->SetCancelNote(TEXT("Only the wait was stopped. The capture request had already been queued; stop it with editor(stop_csv_profile)."));
	const double Deadline = FPlatformTime::Seconds() + 15.0;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[Deferred, Deadline, Predicted, BuildStarted](float) -> bool
		{
			if (Deferred->IsCancelled()) return false;
			FCsvProfiler* Profiler = FCsvProfiler::Get();
			if (Profiler && Profiler->IsCapturing())
			{
				Deferred->Complete(MCPResult(BuildStarted(Profiler->GetOutputFilename(), true)));
				return false;
			}
			if (FPlatformTime::Seconds() > Deadline)
			{
				Deferred->Complete(MCPError(FString::Printf(
					TEXT("The CSV capture was requested but had not begun after 15 s. The profiler refuses to start when it cannot create the file (%s); check the output log for LogCsvProfiler."),
					*Predicted)));
				return false;
			}
			return true;
		}));
	return MCPDeferredMarker();
#else
	return MCPError(TEXT("This build compiles the CSV profiler out (CSV_PROFILER is 0), so there is nothing to start."));
#endif
}

// ---------------------------------------------------------------------------
// stop_csv_profile -- end the capture and report the finished file.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::StopCsvProfile(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

#if CSV_PROFILER
	FCsvProfiler* Csv = FCsvProfiler::Get();
	if (!Csv)
	{
		return MCPError(TEXT("The CSV profiler is not available in this process."));
	}

	if (!Csv->IsCapturing())
	{
		TSharedPtr<FJsonObject> NoOp = MCPSuccess();
		NoOp->SetBoolField(TEXT("wasCapturing"), false);
		NoOp->SetBoolField(TEXT("stopped"), false);
		NoOp->SetBoolField(TEXT("unchanged"), true);
		NoOp->SetBoolField(TEXT("writing"), Csv->IsWritingFile());
		const FString Last = Csv->GetOutputFilename();
		if (!Last.IsEmpty()) MCPCsvProfileDescribeFile(NoOp, Last);
		NoOp->SetStringField(TEXT("note"),
			TEXT("No CSV capture was running, so nothing was stopped. csvPath, when present, is the last capture's file."));
		MCPSetNoRollback(NoOp, TEXT("Nothing was stopped, so there is nothing to undo."));
		return MCPResult(NoOp);
	}

	const FString Path = Csv->GetOutputFilename();
	const int32 Frames = Csv->GetCaptureFrameNumber();
	const TSharedFuture<FString> Written = Csv->EndCapture();

	auto BuildStopped = [Path, Frames](const FString& FinalPath, bool bWritten) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Result = MCPSuccess();
		MCPSetUpdated(Result);
		Result->SetBoolField(TEXT("wasCapturing"), true);
		Result->SetBoolField(TEXT("stopped"), true);
		Result->SetBoolField(TEXT("writing"), !bWritten);
		Result->SetNumberField(TEXT("capturedFrames"), Frames);
		MCPCsvProfileDescribeFile(Result, FinalPath.IsEmpty() ? Path : FinalPath);
		if (!bWritten)
		{
			Result->SetStringField(TEXT("note"),
				TEXT("The stop was queued but the file was still being written when this answered; its size will grow until the profiler finishes."));
		}
		// Restarting is the inverse in the same lossy sense as stop_trace's:
		// a new capture writes a NEW file and the frames in between are absent.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("captureName"), FPaths::GetBaseFilename(FinalPath.IsEmpty() ? Path : FinalPath) + TEXT("_resumed"));
		MCPSetRollback(Result, TEXT("start_csv_profile"), Payload);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("Rolling back starts a NEW capture under a new name. A finished CSV cannot be reopened, so the frames in the gap are lost."));
		return Result;
	};

	const FMCPDeferredCallPtr Deferred = MCPDeferRequest();
	if (!Deferred || !Written.IsValid())
	{
		return MCPResult(BuildStopped(Path, Written.IsValid() && Written.IsReady()));
	}
	Deferred->SetCancelNote(TEXT("Only the wait was stopped. The capture had already been ended and the profiler finishes writing the file on its own."));
	const double Deadline = FPlatformTime::Seconds() + 60.0;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[Deferred, Deadline, Written, BuildStopped](float) -> bool
		{
			if (Deferred->IsCancelled()) return false;
			if (Written.IsReady())
			{
				Deferred->Complete(MCPResult(BuildStopped(Written.Get(), true)));
				return false;
			}
			if (FPlatformTime::Seconds() > Deadline)
			{
				Deferred->Complete(MCPResult(BuildStopped(FString(), false)));
				return false;
			}
			return true;
		}));
	return MCPDeferredMarker();
#else
	return MCPError(TEXT("This build compiles the CSV profiler out (CSV_PROFILER is 0), so there is nothing to stop."));
#endif
}
