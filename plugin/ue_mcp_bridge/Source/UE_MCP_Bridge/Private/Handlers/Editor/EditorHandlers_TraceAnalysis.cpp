// editor(analyze_trace): read a recorded .utrace back and summarise it.
//
// editor(start_trace) and editor(stop_trace) write a trace and report where it
// landed. This file is the other half: it opens the file with the engine's own
// analysis stack (TraceServices, the module Unreal Insights is built on) and
// answers the questions a profiling pass starts with, without leaving the
// editor:
//
//   * frames: count, average, p50/p95/p99, min and max frame time;
//   * hitches: every frame over a threshold, in time order;
//   * worst frames, each with the CPU timers that spent its time;
//   * top timers over the window, by inclusive or exclusive time;
//   * GPU busy time per frame, when the trace carries GPU timing;
//   * bookmarks with their times and the frame they fell in.
//
// How a session is opened and read mirrors Insights: ITraceServicesModule hands
// out the analysis service, StartAnalysis runs the analyzers on worker threads,
// and every provider read happens inside FAnalysisSessionReadScope once
// IsAnalysisComplete reports true. A long trace takes a while to analyse, so a
// bridge request defers its answer (HandlerDeferred.h): the handler starts the
// session on the game thread, returns the deferred marker, and a pool task
// waits for the analysis, reports progress, builds the summary and completes
// the call. A cancel stops the session.
//
// Engine versions. TraceServices exists on every supported engine, but its
// timing-profiler surface moved a lot before 5.7 (aggregation parameters, timer
// readers, GPU queues). The headers read for this file are 5.7, 5.8 and 5.9, so
// the analysis is compiled from 5.7 up and older engines get a clear error
// rather than a guess at an API nobody here has read. Within that range the
// calls used here (CreateAggregation, ReadTimeline, EnumerateGpuQueues and the
// frame, thread and bookmark providers) have the same signatures.
//
// Every helper is prefixed MCPTraceAnalysis: the module is a unity build, and
// two .cpp files that share a blob share their anonymous namespaces.

#include "EditorHandlers.h"

#include "HandlerDeferred.h"
#include "HandlerUtils.h"

#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "ProfilingDebugging/TraceAuxiliary.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#if UE_MCP_HAS_5_7_API
#include "TraceServices/AnalysisService.h"
#include "TraceServices/Containers/Tables.h"
#include "TraceServices/Containers/Timelines.h"
#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/Bookmarks.h"
#include "TraceServices/Model/Frames.h"
#include "TraceServices/Model/Threads.h"
#include "TraceServices/Model/TimingProfiler.h"
#endif

namespace
{
	/** What the caller asked for, copied off the params on the game thread so
	 *  the pool task never touches the request object. */
	struct FMCPTraceAnalysisRequest
	{
		FString File;
		double WindowStart = 0.0;
		/** Negative: to the end of the trace. */
		double WindowEnd = -1.0;
		double HitchThresholdMs = 50.0;
		int32 WorstFrames = 10;
		int32 TopTimers = 20;
		int32 TimersPerFrame = 5;
		int32 MaxHitches = 50;
		int32 MaxBookmarks = 100;
		bool bAllThreads = false;
		bool bSortExclusive = false;
		bool bRenderingFrames = false;
		bool bDumpToFile = false;
		FString OutputPath;
	};

	/** The newest .utrace under Saved/Profiling, which is where start_trace
	 *  writes by default. Empty when there is none. */
	FString MCPTraceAnalysisNewestTrace()
	{
		const FString Dir = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Profiling")));
		TArray<FString> Found;
		IFileManager::Get().FindFilesRecursive(Found, *Dir, TEXT("*.utrace"), /*Files=*/true, /*Directories=*/false);
		FString Newest;
		FDateTime NewestStamp = FDateTime::MinValue();
		for (const FString& Path : Found)
		{
			const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*Path);
			if (Newest.IsEmpty() || Stamp > NewestStamp)
			{
				Newest = Path;
				NewestStamp = Stamp;
			}
		}
		return Newest;
	}

	/** A relative path is tried under Saved/Profiling first, where traces
	 *  land, then under the project directory. */
	FString MCPTraceAnalysisResolveFile(const FString& Requested)
	{
		if (!FPaths::IsRelative(Requested))
		{
			return FPaths::ConvertRelativePathToFull(Requested);
		}
		const FString UnderProfiling = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Profiling"), Requested));
		if (FPaths::FileExists(UnderProfiling))
		{
			return UnderProfiling;
		}
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), Requested));
	}

#if UE_MCP_HAS_5_7_API
	double MCPTraceAnalysisPercentile(const TArray<double>& Sorted, double P)
	{
		if (Sorted.Num() == 0) return 0.0;
		// Nearest rank: the smallest value with at least P of the samples at
		// or below it. No interpolation, so every reported value is a frame
		// that really happened.
		const int32 Rank = FMath::Clamp(FMath::CeilToInt(P * Sorted.Num()) - 1, 0, Sorted.Num() - 1);
		return Sorted[Rank];
	}

	TSharedPtr<FJsonObject> MCPTraceAnalysisDistribution(TArray<double> Values)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		Values.Sort();
		double Total = 0.0;
		for (double V : Values) Total += V;
		Out->SetNumberField(TEXT("count"), Values.Num());
		Out->SetNumberField(TEXT("avgMs"), Values.Num() > 0 ? Total / Values.Num() : 0.0);
		Out->SetNumberField(TEXT("p50Ms"), MCPTraceAnalysisPercentile(Values, 0.50));
		Out->SetNumberField(TEXT("p95Ms"), MCPTraceAnalysisPercentile(Values, 0.95));
		Out->SetNumberField(TEXT("p99Ms"), MCPTraceAnalysisPercentile(Values, 0.99));
		Out->SetNumberField(TEXT("minMs"), Values.Num() > 0 ? Values[0] : 0.0);
		Out->SetNumberField(TEXT("maxMs"), Values.Num() > 0 ? Values.Last() : 0.0);
		return Out;
	}

	struct FMCPTraceAnalysisFrame
	{
		uint64 Index = 0;
		double Start = 0.0;
		double End = 0.0;
		double Ms = 0.0;
	};

	struct FMCPTraceAnalysisTimerRow
	{
		FString Name;
		const TCHAR* Type = TEXT("cpu");
		uint64 Count = 0;
		double InclusiveMs = 0.0;
		double ExclusiveMs = 0.0;
		double MaxInclusiveMs = 0.0;
		double AvgInclusiveMs = 0.0;
	};

	const TCHAR* MCPTraceAnalysisTimerTypeName(const TraceServices::FTimingProfilerTimer* Timer)
	{
		if (!Timer) return TEXT("unknown");
		switch (Timer->Type)
		{
		case TraceServices::ETimingProfilerTimerType::CpuScope:      return TEXT("cpu");
		case TraceServices::ETimingProfilerTimerType::CpuSampling:   return TEXT("cpuSampling");
		case TraceServices::ETimingProfilerTimerType::GpuScope:      return TEXT("gpu");
		case TraceServices::ETimingProfilerTimerType::VerseSampling: return TEXT("verse");
		default:                                                     return TEXT("unknown");
		}
	}

	/** One aggregation over [Start, End], CPU timelines only. ThreadId unset
	 *  means every CPU thread; set, only that one. Rows come back unsorted:
	 *  the caller sorts by the column it reports, because the engine's own
	 *  sort only knows inclusive time and the per-frame view wants exclusive. */
	TArray<FMCPTraceAnalysisTimerRow> MCPTraceAnalysisAggregate(
		const TraceServices::ITimingProfilerProvider& Timing, double Start, double End, TOptional<uint32> ThreadId)
	{
		TArray<FMCPTraceAnalysisTimerRow> Rows;

		TraceServices::FCreateAggregationParams Params;
		Params.IntervalStart = Start;
		Params.IntervalEnd = End;
		if (ThreadId.IsSet())
		{
			const uint32 Wanted = ThreadId.GetValue();
			Params.CpuThreadFilter = [Wanted](uint32 Id) { return Id == Wanted; };
		}
		else
		{
			Params.CpuThreadFilter = [](uint32) { return true; };
		}

		TUniquePtr<TraceServices::ITable<TraceServices::FTimingProfilerAggregatedStats>> Table(Timing.CreateAggregation(Params));
		if (!Table.IsValid()) return Rows;

		TUniquePtr<TraceServices::ITableReader<TraceServices::FTimingProfilerAggregatedStats>> Reader(Table->CreateReader());
		if (!Reader.IsValid()) return Rows;

		for (; Reader->IsValid(); Reader->NextRow())
		{
			const TraceServices::FTimingProfilerAggregatedStats* Stats = Reader->GetCurrentRow();
			if (!Stats || Stats->InstanceCount == 0) continue;
			FMCPTraceAnalysisTimerRow Row;
			Row.Name = (Stats->Timer && Stats->Timer->Name) ? FString(Stats->Timer->Name) : FString(TEXT("<unnamed>"));
			Row.Type = MCPTraceAnalysisTimerTypeName(Stats->Timer);
			Row.Count = Stats->InstanceCount;
			Row.InclusiveMs = Stats->TotalInclusiveTime * 1000.0;
			Row.ExclusiveMs = Stats->TotalExclusiveTime * 1000.0;
			Row.MaxInclusiveMs = Stats->MaxInclusiveTime > 0.0 ? Stats->MaxInclusiveTime * 1000.0 : 0.0;
			Row.AvgInclusiveMs = Stats->AverageInclusiveTime * 1000.0;
			Rows.Add(MoveTemp(Row));
		}
		return Rows;
	}

	void MCPTraceAnalysisSortRows(TArray<FMCPTraceAnalysisTimerRow>& Rows, bool bExclusive)
	{
		Rows.Sort([bExclusive](const FMCPTraceAnalysisTimerRow& A, const FMCPTraceAnalysisTimerRow& B)
		{
			const double KeyA = bExclusive ? A.ExclusiveMs : A.InclusiveMs;
			const double KeyB = bExclusive ? B.ExclusiveMs : B.InclusiveMs;
			if (KeyA != KeyB) return KeyA > KeyB;
			return A.Name < B.Name;
		});
	}

	TArray<TSharedPtr<FJsonValue>> MCPTraceAnalysisTimerRowsToJson(
		const TArray<FMCPTraceAnalysisTimerRow>& Rows, int32 Limit, int32 FrameCount)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (int32 i = 0; i < Rows.Num() && i < Limit; ++i)
		{
			const FMCPTraceAnalysisTimerRow& Row = Rows[i];
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("name"), Row.Name);
			Obj->SetStringField(TEXT("type"), Row.Type);
			Obj->SetNumberField(TEXT("count"), static_cast<double>(Row.Count));
			Obj->SetNumberField(TEXT("inclusiveMs"), Row.InclusiveMs);
			Obj->SetNumberField(TEXT("exclusiveMs"), Row.ExclusiveMs);
			Obj->SetNumberField(TEXT("maxInclusiveMs"), Row.MaxInclusiveMs);
			Obj->SetNumberField(TEXT("avgInclusiveMs"), Row.AvgInclusiveMs);
			if (FrameCount > 0)
			{
				Obj->SetNumberField(TEXT("inclusiveMsPerFrame"), Row.InclusiveMs / FrameCount);
				Obj->SetNumberField(TEXT("exclusiveMsPerFrame"), Row.ExclusiveMs / FrameCount);
			}
			Out.Add(MakeShared<FJsonValueObject>(Obj));
		}
		return Out;
	}

	/** GPU busy time per frame: for every GPU timeline the trace carries, the
	 *  depth-0 scopes are summed into the frames they overlap, and a frame's
	 *  figure is the busiest single timeline (queues run in parallel, so
	 *  adding them would count overlapping work twice). It approximates GPU
	 *  frame time from the scopes the RHI emitted; work outside any scope is
	 *  not counted, which the result says. */
	bool MCPTraceAnalysisGpuPerFrame(
		const TraceServices::ITimingProfilerProvider& Timing,
		const TArray<FMCPTraceAnalysisFrame>& Frames,
		TArray<double>& OutPerFrameMs,
		int32& OutTimelineCount)
	{
		OutPerFrameMs.Init(0.0, Frames.Num());
		OutTimelineCount = 0;
		if (Frames.Num() == 0) return false;

		TArray<uint32> TimelineIndices;
		uint32 Index = 0;
		if (Timing.GetGpuTimelineIndex(Index)) TimelineIndices.AddUnique(Index);
		if (Timing.GetGpu2TimelineIndex(Index)) TimelineIndices.AddUnique(Index);
		Timing.EnumerateGpuQueues([&TimelineIndices](const TraceServices::FGpuQueueInfo& Queue)
		{
			if (Queue.TimelineIndex != ~0u) TimelineIndices.AddUnique(Queue.TimelineIndex);
		});

		const double WindowStart = Frames[0].Start;
		const double WindowEnd = Frames.Last().End;
		bool bAnyEvent = false;

		for (const uint32 TimelineIndex : TimelineIndices)
		{
			TArray<double> Busy;
			Busy.Init(0.0, Frames.Num());
			bool bTimelineHasEvents = false;

			Timing.ReadTimeline(TimelineIndex, [&](const TraceServices::ITimingProfilerProvider::Timeline& Timeline)
			{
				Timeline.EnumerateEvents(WindowStart, WindowEnd,
					[&](double EventStart, double EventEnd, uint32 Depth, const TraceServices::FTimingProfilerEvent&) -> TraceServices::EEventEnumerate
					{
						if (Depth != 0 || EventEnd <= EventStart) return TraceServices::EEventEnumerate::Continue;
						bTimelineHasEvents = true;
						// First frame that ends after this event starts.
						int32 Lo = 0;
						int32 Hi = Frames.Num();
						while (Lo < Hi)
						{
							const int32 Mid = (Lo + Hi) / 2;
							if (Frames[Mid].End <= EventStart) Lo = Mid + 1; else Hi = Mid;
						}
						for (int32 F = Lo; F < Frames.Num() && Frames[F].Start < EventEnd; ++F)
						{
							const double Overlap = FMath::Min(EventEnd, Frames[F].End) - FMath::Max(EventStart, Frames[F].Start);
							if (Overlap > 0.0) Busy[F] += Overlap * 1000.0;
						}
						return TraceServices::EEventEnumerate::Continue;
					});
			});

			if (!bTimelineHasEvents) continue;
			bAnyEvent = true;
			++OutTimelineCount;
			for (int32 F = 0; F < Frames.Num(); ++F)
			{
				OutPerFrameMs[F] = FMath::Max(OutPerFrameMs[F], Busy[F]);
			}
		}
		return bAnyEvent;
	}

	/** The whole summary. Called inside FAnalysisSessionReadScope on a
	 *  completed session. Returns an error string on a request the trace
	 *  cannot answer, or empty with OutResult filled. */
	FString MCPTraceAnalysisSummarise(
		const TraceServices::IAnalysisSession& Session,
		const FMCPTraceAnalysisRequest& Request,
		TSharedPtr<FJsonObject>& OutResult)
	{
		TArray<FString> Warnings;
		const double Duration = Session.GetDurationSeconds();
		const double WindowStart = FMath::Max(0.0, Request.WindowStart);
		const double WindowEnd = Request.WindowEnd < 0.0 ? Duration : FMath::Min(Request.WindowEnd, Duration);
		if (WindowEnd <= WindowStart)
		{
			return FString::Printf(
				TEXT("The time window [%.3f, %.3f] s holds nothing: the trace lasts %.3f s. Pass startSeconds below endSeconds, inside the trace."),
				WindowStart, WindowEnd, Duration);
		}

		const ETraceFrameType FrameType = Request.bRenderingFrames ? TraceFrameType_Rendering : TraceFrameType_Game;
		const TraceServices::IFrameProvider& FrameProvider = TraceServices::ReadFrameProvider(Session);
		const uint64 FramesInTrace = FrameProvider.GetFrameCount(FrameType);

		// Whole frames inside the window only. A frame cut by the window edge
		// would report a duration it did not have, and the last frame of a
		// trace often never ended.
		TArray<FMCPTraceAnalysisFrame> Frames;
		FrameProvider.EnumerateFrames(FrameType, WindowStart, WindowEnd, [&](const TraceServices::FFrame& Frame)
		{
			if (Frame.StartTime < WindowStart || Frame.EndTime > WindowEnd) return;
			if (!(Frame.EndTime > Frame.StartTime) || Frame.EndTime >= Duration + 1.0) return;
			FMCPTraceAnalysisFrame Row;
			Row.Index = Frame.Index;
			Row.Start = Frame.StartTime;
			Row.End = Frame.EndTime;
			Row.Ms = (Frame.EndTime - Frame.StartTime) * 1000.0;
			Frames.Add(Row);
		});
		Frames.Sort([](const FMCPTraceAnalysisFrame& A, const FMCPTraceAnalysisFrame& B) { return A.Start < B.Start; });

		TSharedPtr<FJsonObject> Result = MCPSuccess();
		Result->SetNumberField(TEXT("sessionDurationSeconds"), Duration);
		TSharedPtr<FJsonObject> Window = MakeShared<FJsonObject>();
		Window->SetNumberField(TEXT("startSeconds"), WindowStart);
		Window->SetNumberField(TEXT("endSeconds"), WindowEnd);
		Window->SetBoolField(TEXT("wholeTrace"), Request.WindowStart <= 0.0 && Request.WindowEnd < 0.0);
		Result->SetObjectField(TEXT("window"), Window);

		// ── Frames ──────────────────────────────────────────────────────────
		TArray<double> FrameMs;
		FrameMs.Reserve(Frames.Num());
		double TotalMs = 0.0;
		for (const FMCPTraceAnalysisFrame& Frame : Frames)
		{
			FrameMs.Add(Frame.Ms);
			TotalMs += Frame.Ms;
		}
		TSharedPtr<FJsonObject> FramesObj = MCPTraceAnalysisDistribution(FrameMs);
		FramesObj->SetStringField(TEXT("frameType"), Request.bRenderingFrames ? TEXT("rendering") : TEXT("game"));
		FramesObj->SetNumberField(TEXT("framesInTrace"), static_cast<double>(FramesInTrace));
		FramesObj->SetNumberField(TEXT("avgFps"), TotalMs > 0.0 ? Frames.Num() * 1000.0 / TotalMs : 0.0);
		Result->SetObjectField(TEXT("frames"), FramesObj);
		if (FramesInTrace == 0)
		{
			Warnings.Add(TEXT("The trace has no frame events. Record with the 'frame' channel on (the 'default' preset includes it)."));
		}

		// ── Hitches ─────────────────────────────────────────────────────────
		{
			TArray<TSharedPtr<FJsonValue>> List;
			int32 Count = 0;
			double HitchTotalMs = 0.0;
			for (const FMCPTraceAnalysisFrame& Frame : Frames)
			{
				if (Frame.Ms < Request.HitchThresholdMs) continue;
				++Count;
				HitchTotalMs += Frame.Ms;
				if (List.Num() >= Request.MaxHitches) continue;
				TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetNumberField(TEXT("frameIndex"), static_cast<double>(Frame.Index));
				Row->SetNumberField(TEXT("startSeconds"), Frame.Start);
				Row->SetNumberField(TEXT("durationMs"), Frame.Ms);
				List.Add(MakeShared<FJsonValueObject>(Row));
			}
			TSharedPtr<FJsonObject> Hitches = MakeShared<FJsonObject>();
			Hitches->SetNumberField(TEXT("thresholdMs"), Request.HitchThresholdMs);
			Hitches->SetNumberField(TEXT("count"), Count);
			Hitches->SetNumberField(TEXT("totalMs"), HitchTotalMs);
			Hitches->SetArrayField(TEXT("list"), List);
			Hitches->SetBoolField(TEXT("truncated"), List.Num() < Count);
			Result->SetObjectField(TEXT("hitches"), Hitches);
		}

		// ── Threads ─────────────────────────────────────────────────────────
		TOptional<uint32> GameThreadId;
		{
			const TraceServices::IThreadProvider& Threads = TraceServices::ReadThreadProvider(Session);
			Threads.EnumerateThreads([&GameThreadId](const TraceServices::FThreadInfo& Info)
			{
				if (!GameThreadId.IsSet() && Info.Name && FCString::Stricmp(Info.Name, TEXT("GameThread")) == 0)
				{
					GameThreadId = Info.Id;
				}
			});
		}
		TOptional<uint32> TimerThread = Request.bAllThreads ? TOptional<uint32>() : GameThreadId;
		FString ThreadScope = Request.bAllThreads ? TEXT("all") : TEXT("game");
		if (!Request.bAllThreads && !GameThreadId.IsSet())
		{
			ThreadScope = TEXT("all");
			Warnings.Add(TEXT("No thread named GameThread is in the trace, so timers are aggregated over every CPU thread."));
		}

		// ── Timers: worst frames and the window ─────────────────────────────
		const TraceServices::ITimingProfilerProvider* Timing = TraceServices::ReadTimingProfilerProvider(Session);
		TArray<TSharedPtr<FJsonValue>> WorstJson;
		{
			TArray<FMCPTraceAnalysisFrame> Worst = Frames;
			Worst.Sort([](const FMCPTraceAnalysisFrame& A, const FMCPTraceAnalysisFrame& B) { return A.Ms > B.Ms; });
			for (int32 i = 0; i < Worst.Num() && i < Request.WorstFrames; ++i)
			{
				const FMCPTraceAnalysisFrame& Frame = Worst[i];
				TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetNumberField(TEXT("frameIndex"), static_cast<double>(Frame.Index));
				Row->SetNumberField(TEXT("startSeconds"), Frame.Start);
				Row->SetNumberField(TEXT("durationMs"), Frame.Ms);
				if (Timing && Request.TimersPerFrame > 0)
				{
					TArray<FMCPTraceAnalysisTimerRow> Rows = MCPTraceAnalysisAggregate(*Timing, Frame.Start, Frame.End, TimerThread);
					// Exclusive time names where the frame went; inclusive time
					// would list the call-stack spine (the engine tick and its
					// parents) first in every frame.
					MCPTraceAnalysisSortRows(Rows, /*bExclusive=*/true);
					Row->SetArrayField(TEXT("topTimers"), MCPTraceAnalysisTimerRowsToJson(Rows, Request.TimersPerFrame, 0));
				}
				WorstJson.Add(MakeShared<FJsonValueObject>(Row));
			}
		}
		Result->SetArrayField(TEXT("worstFrames"), WorstJson);

		TSharedPtr<FJsonObject> TopTimers = MakeShared<FJsonObject>();
		TopTimers->SetStringField(TEXT("threads"), ThreadScope);
		TopTimers->SetStringField(TEXT("sortedBy"), Request.bSortExclusive ? TEXT("exclusive") : TEXT("inclusive"));
		if (Timing)
		{
			TArray<FMCPTraceAnalysisTimerRow> Rows = MCPTraceAnalysisAggregate(*Timing, WindowStart, WindowEnd, TimerThread);
			MCPTraceAnalysisSortRows(Rows, Request.bSortExclusive);
			TopTimers->SetNumberField(TEXT("distinctTimers"), Rows.Num());
			TopTimers->SetArrayField(TEXT("rows"), MCPTraceAnalysisTimerRowsToJson(Rows, Request.TopTimers, Frames.Num()));
			TopTimers->SetBoolField(TEXT("truncated"), Rows.Num() > Request.TopTimers);
			if (Rows.Num() == 0)
			{
				Warnings.Add(TEXT("No CPU timer events in the window. Record with the 'cpu' channel on (the 'default' preset includes it)."));
			}
		}
		else
		{
			TopTimers->SetNumberField(TEXT("distinctTimers"), 0);
			TopTimers->SetArrayField(TEXT("rows"), TArray<TSharedPtr<FJsonValue>>());
			TopTimers->SetBoolField(TEXT("truncated"), false);
			Warnings.Add(TEXT("The trace has no timing-profiler data. Record with the 'cpu' channel on (the 'default' preset includes it)."));
		}
		Result->SetObjectField(TEXT("topTimers"), TopTimers);

		// ── GPU ─────────────────────────────────────────────────────────────
		{
			TSharedPtr<FJsonObject> Gpu = MakeShared<FJsonObject>();
			TArray<double> PerFrame;
			int32 TimelineCount = 0;
			const bool bPresent = Timing && MCPTraceAnalysisGpuPerFrame(*Timing, Frames, PerFrame, TimelineCount);
			Gpu->SetBoolField(TEXT("present"), bPresent);
			if (bPresent)
			{
				TArray<double> Measured;
				for (double Ms : PerFrame) if (Ms > 0.0) Measured.Add(Ms);
				TSharedPtr<FJsonObject> Dist = MCPTraceAnalysisDistribution(Measured);
				Gpu->SetObjectField(TEXT("busyMsPerFrame"), Dist);
				Gpu->SetNumberField(TEXT("framesWithGpuWork"), Measured.Num());
				Gpu->SetNumberField(TEXT("timelines"), TimelineCount);
				Gpu->SetStringField(TEXT("method"),
					TEXT("Per game frame, the time covered by top-level GPU scopes on the busiest GPU timeline. It approximates GPU frame time; GPU work outside any scope is not counted."));
			}
			else
			{
				Gpu->SetStringField(TEXT("reason"),
					TEXT("The trace carries no GPU timing events. Record with the 'gpu' channel on, from an editor with a real RHI (a -nullrhi editor emits none)."));
			}
			Result->SetObjectField(TEXT("gpu"), Gpu);
		}

		// ── Bookmarks ───────────────────────────────────────────────────────
		{
			const TraceServices::IBookmarkProvider& Bookmarks = TraceServices::ReadBookmarkProvider(Session);
			TArray<TSharedPtr<FJsonValue>> List;
			int32 Count = 0;
			Bookmarks.EnumerateBookmarks(WindowStart, WindowEnd, [&](const TraceServices::FBookmark& Bookmark)
			{
				++Count;
				if (List.Num() >= Request.MaxBookmarks) return;
				TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
				Row->SetNumberField(TEXT("timeSeconds"), Bookmark.Time);
				Row->SetStringField(TEXT("text"), Bookmark.Text ? FString(Bookmark.Text) : FString());
				Row->SetNumberField(TEXT("frameIndex"),
					static_cast<double>(FrameProvider.GetFrameNumberForTimestamp(FrameType, Bookmark.Time)));
				List.Add(MakeShared<FJsonValueObject>(Row));
			});
			TSharedPtr<FJsonObject> BookmarksObj = MakeShared<FJsonObject>();
			BookmarksObj->SetNumberField(TEXT("count"), Count);
			BookmarksObj->SetNumberField(TEXT("countInTrace"), static_cast<double>(Bookmarks.GetBookmarkCount()));
			BookmarksObj->SetArrayField(TEXT("list"), List);
			BookmarksObj->SetBoolField(TEXT("truncated"), List.Num() < Count);
			Result->SetObjectField(TEXT("bookmarks"), BookmarksObj);
		}

		Result->SetArrayField(TEXT("warnings"), MCPStringListToJson(Warnings));
		OutResult = Result;
		return FString();
	}

	/** Write the full result to disk and turn the response into a pointer to
	 *  it, keeping the headline numbers inline. Game thread. */
	TSharedPtr<FJsonValue> MCPTraceAnalysisFinish(TSharedPtr<FJsonObject> Result, const FMCPTraceAnalysisRequest& Request)
	{
		if (!Request.bDumpToFile)
		{
			return MCPResult(Result);
		}
		const FString DefaultPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UE_MCP"), TEXT("TraceAnalysis"),
			FPaths::GetBaseFilename(Request.File) + TEXT(".analysis.json"));
		const FString Target = MCPResolveDumpPath(Request.OutputPath.IsEmpty() ? DefaultPath : Request.OutputPath);

		FString JsonText;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
		if (!FJsonSerializer::Serialize(Result.ToSharedRef(), Writer))
		{
			return MCPError(TEXT("Failed to serialise the trace analysis to JSON."));
		}
		FString Error;
		if (!MCPWriteDumpFile(Target, JsonText, TEXT("trace analysis"), Error))
		{
			return MCPError(Error);
		}

		TSharedPtr<FJsonObject> Short = MCPSuccess();
		Short->SetBoolField(TEXT("dumpedToFile"), true);
		Short->SetStringField(TEXT("outputPath"), FPaths::ConvertRelativePathToFull(Target));
		for (const TCHAR* Key : { TEXT("traceFile"), TEXT("traceFileBytes"), TEXT("analysisSeconds"),
			TEXT("sessionDurationSeconds"), TEXT("window"), TEXT("frames"), TEXT("gpu"), TEXT("warnings") })
		{
			if (const TSharedPtr<FJsonValue> Value = Result->TryGetField(Key)) Short->SetField(Key, Value);
		}
		const TSharedPtr<FJsonObject>* Hitches = nullptr;
		if (Result->TryGetObjectField(TEXT("hitches"), Hitches) && Hitches)
		{
			Short->SetNumberField(TEXT("hitchCount"), (*Hitches)->GetNumberField(TEXT("count")));
		}
		const TSharedPtr<FJsonObject>* Bookmarks = nullptr;
		if (Result->TryGetObjectField(TEXT("bookmarks"), Bookmarks) && Bookmarks)
		{
			Short->SetNumberField(TEXT("bookmarkCount"), (*Bookmarks)->GetNumberField(TEXT("count")));
		}
		return MCPResult(Short);
	}

	void MCPTraceAnalysisStamp(TSharedPtr<FJsonObject> Result, const FMCPTraceAnalysisRequest& Request, double WallStart)
	{
		Result->SetStringField(TEXT("traceFile"), Request.File);
		Result->SetNumberField(TEXT("traceFileBytes"), static_cast<double>(IFileManager::Get().FileSize(*Request.File)));
		Result->SetNumberField(TEXT("analysisSeconds"), FPlatformTime::Seconds() - WallStart);
	}

	/** State the pool task and the cancel hook share. */
	struct FMCPTraceAnalysisJob
	{
		TSharedPtr<const TraceServices::IAnalysisSession> Session;
	};
#endif // UE_MCP_HAS_5_7_API
}

// ---------------------------------------------------------------------------
// analyze_insights_trace -- summarise a recorded .utrace.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::AnalyzeInsightsTrace(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FMCPTraceAnalysisRequest Request;
	FString Requested = OptionalString(Params, TEXT("file"));
	Requested.TrimStartAndEndInline();
	Request.WindowStart = OptionalNumber(Params, TEXT("startSeconds"), 0.0);
	Request.WindowEnd = OptionalNumber(Params, TEXT("endSeconds"), -1.0);
	Request.HitchThresholdMs = FMath::Max(0.1, OptionalNumber(Params, TEXT("hitchThresholdMs"), 50.0));
	Request.WorstFrames = FMath::Clamp(OptionalInt(Params, TEXT("worstFrames"), 10), 0, 50);
	Request.TopTimers = FMath::Clamp(OptionalInt(Params, TEXT("topTimers"), 20), 0, 200);
	Request.TimersPerFrame = FMath::Clamp(OptionalInt(Params, TEXT("timersPerFrame"), 5), 0, 20);
	Request.MaxHitches = FMath::Clamp(OptionalInt(Params, TEXT("maxHitches"), 50), 0, 500);
	Request.MaxBookmarks = FMath::Clamp(OptionalInt(Params, TEXT("maxBookmarks"), 100), 0, 1000);
	const FString Threads = OptionalString(Params, TEXT("threads"), TEXT("game")).ToLower();
	const FString TimerSort = OptionalString(Params, TEXT("timerSort"), TEXT("inclusive")).ToLower();
	const FString FrameType = OptionalString(Params, TEXT("frameType"), TEXT("game")).ToLower();
	Request.bDumpToFile = OptionalBool(Params, TEXT("dumpToFile"), false);
	Request.OutputPath = OptionalString(Params, TEXT("outputPath"));

	if (Threads != TEXT("game") && Threads != TEXT("all"))
	{
		return MCPError(FString::Printf(TEXT("Unknown threads '%s'. Use game (the game thread only, the default) or all (every CPU thread)."), *Threads));
	}
	if (TimerSort != TEXT("inclusive") && TimerSort != TEXT("exclusive"))
	{
		return MCPError(FString::Printf(TEXT("Unknown timerSort '%s'. Use inclusive (the default) or exclusive."), *TimerSort));
	}
	if (FrameType != TEXT("game") && FrameType != TEXT("rendering"))
	{
		return MCPError(FString::Printf(TEXT("Unknown frameType '%s'. Use game (the default) or rendering."), *FrameType));
	}
	if (!Request.OutputPath.IsEmpty() && !Request.bDumpToFile)
	{
		return MCPError(TEXT("outputPath needs dumpToFile=true."));
	}
	if (Request.WindowEnd >= 0.0 && Request.WindowEnd <= Request.WindowStart)
	{
		return MCPError(TEXT("endSeconds must be greater than startSeconds."));
	}
	Request.bAllThreads = Threads == TEXT("all");
	Request.bSortExclusive = TimerSort == TEXT("exclusive");
	Request.bRenderingFrames = FrameType == TEXT("rendering");

	Request.File = Requested.IsEmpty() ? MCPTraceAnalysisNewestTrace() : MCPTraceAnalysisResolveFile(Requested);
	if (Request.File.IsEmpty())
	{
		return MCPError(TEXT("No file was given and no .utrace is under <Project>/Saved/Profiling. Record one with editor(start_trace) and editor(stop_trace), or pass file."));
	}
	if (!FPaths::FileExists(Request.File))
	{
		return MCPError(FString::Printf(TEXT("Trace file not found: %s"), *Request.File));
	}
	if (!Request.File.EndsWith(TEXT(".utrace"), ESearchCase::IgnoreCase))
	{
		return MCPError(FString::Printf(TEXT("'%s' is not a .utrace file."), *Request.File));
	}
	// The file a running trace writes to is still growing and has no end; an
	// analysis of it would describe a moment and present it as the whole run.
	if (FTraceAuxiliary::IsConnected())
	{
		const FString Destination = FPaths::ConvertRelativePathToFull(FTraceAuxiliary::GetTraceDestinationString());
		if (FPaths::IsSamePath(Destination, Request.File))
		{
			return MCPError(TEXT("That trace is still being written. Stop it with editor(stop_trace) first; the file is only complete after the stop."));
		}
	}

#if UE_MCP_HAS_5_7_API
	ITraceServicesModule* Module = FModuleManager::LoadModulePtr<ITraceServicesModule>(TEXT("TraceServices"));
	if (!Module)
	{
		return MCPError(TEXT("The TraceServices module could not be loaded, so the trace cannot be analysed in this editor."));
	}
	const TSharedPtr<TraceServices::IAnalysisService> Service = Module->GetAnalysisService();
	if (!Service.IsValid())
	{
		return MCPError(TEXT("TraceServices returned no analysis service."));
	}

	const double WallStart = FPlatformTime::Seconds();
	const FMCPDeferredCallPtr Deferred = MCPDeferRequest();
	if (!Deferred)
	{
		// Called in-process rather than for a bridge request: nothing can wait
		// for a later answer, so analyse here and block.
		const TSharedPtr<const TraceServices::IAnalysisSession> Session = Service->Analyze(*Request.File);
		if (!Session.IsValid())
		{
			return MCPError(FString::Printf(TEXT("TraceServices could not open '%s' for analysis."), *Request.File));
		}
		TSharedPtr<FJsonObject> Result;
		FString Error;
		{
			TraceServices::FAnalysisSessionReadScope ReadScope(*Session);
			Error = MCPTraceAnalysisSummarise(*Session, Request, Result);
		}
		if (!Error.IsEmpty()) return MCPError(Error);
		MCPTraceAnalysisStamp(Result, Request, WallStart);
		return MCPTraceAnalysisFinish(Result, Request);
	}

	const TSharedPtr<FMCPTraceAnalysisJob, ESPMode::ThreadSafe> Job = MakeShared<FMCPTraceAnalysisJob, ESPMode::ThreadSafe>();
	Job->Session = Service->StartAnalysis(*Request.File);
	if (!Job->Session.IsValid())
	{
		return MCPError(FString::Printf(TEXT("TraceServices could not open '%s' for analysis."), *Request.File));
	}

	Deferred->SetCancelNote(TEXT("The analysis session was stopped. Nothing was written, so there is nothing to undo."));
	Deferred->OnCancel([Job](const FString&)
	{
		if (Job->Session.IsValid()) Job->Session->Stop(/*bAndWait=*/false);
	});

	FMCPDeferredCallPtr DeferredCopy = Deferred;
	Async(EAsyncExecution::ThreadPool, [DeferredCopy, Job, Request, WallStart]()
	{
		const TraceServices::IAnalysisSession& Session = *Job->Session;
		double LastReport = 0.0;
		while (!Session.IsAnalysisComplete())
		{
			if (DeferredCopy->IsCancelled())
			{
				Session.Stop(/*bAndWait=*/true);
				return;
			}
			FPlatformProcess::Sleep(0.05f);
			const double Now = FPlatformTime::Seconds();
			if (Now - LastReport >= 1.0)
			{
				LastReport = Now;
				double Analysed = 0.0;
				{
					TraceServices::FAnalysisSessionReadScope ReadScope(Session);
					Analysed = Session.GetDurationSeconds();
				}
				DeferredCopy->ReportProgress(Analysed, 0.0,
					FString::Printf(TEXT("Analysed %.1f s of trace"), Analysed));
			}
		}
		if (DeferredCopy->IsCancelled()) return;

		TSharedPtr<FJsonObject> Result;
		FString Error;
		{
			TraceServices::FAnalysisSessionReadScope ReadScope(Session);
			Error = MCPTraceAnalysisSummarise(Session, Request, Result);
		}

		// The dump write and the final stamp go back to the game thread, where
		// every other file write in the bridge happens.
		AsyncTask(ENamedThreads::GameThread, [DeferredCopy, Result, Error, Request, WallStart]()
		{
			if (DeferredCopy->IsCancelled()) return;
			if (!Error.IsEmpty())
			{
				DeferredCopy->Complete(MCPError(Error));
				return;
			}
			MCPTraceAnalysisStamp(Result, Request, WallStart);
			DeferredCopy->Complete(MCPTraceAnalysisFinish(Result, Request));
		});
	});
	return MCPDeferredMarker();
#else
	return MCPError(TEXT("editor(analyze_trace) needs UE 5.7 or newer: the TraceServices timing API it reads changed shape before 5.7. ")
		TEXT("Open the file in Unreal Insights with the openCommand editor(stop_trace) reported."));
#endif
}
