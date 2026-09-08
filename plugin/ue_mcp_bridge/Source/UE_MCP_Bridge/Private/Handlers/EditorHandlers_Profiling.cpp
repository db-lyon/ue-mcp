// Insights trace control, frame timing, profiling regions and standalone runs.
//
// What existed before this file: editor(get_perf_stats), which divides 1 by
// FApp::GetDeltaTime and reads FPlatformMemory::GetStats
// (EditorHandlers.cpp:1246), and editor(run_stat), which Execs a "stat ..."
// console command and returns the string it sent (EditorHandlers.cpp:2274).
// Neither reads a number back. An agent could turn an overlay on and then had
// no way to learn what the overlay said.
//
// What is deliberately NOT here, said plainly rather than faked:
//
// * READING A .utrace FILE. The engine ships TraceServices and TraceAnalysis
//   (Engine/Source/Developer/TraceServices, TraceAnalysis) and Unreal Insights
//   is built on them, so it is not impossible in principle. It is out of reach
//   for this plugin: both are Developer modules the bridge does not link, and
//   consuming one means standing up an analysis session with per-provider
//   readers, which is an application rather than a handler. Every action below
//   that produces a trace therefore reports the absolute file path, its size
//   on disk, and the exact UnrealInsights command line that opens it. That is
//   the handoff, and it is stated instead of implied.
//
// * A BACKGROUND-THROTTLE SETTER. UEditorPerformanceSettings::
//   bThrottleCPUWhenNotForeground and bAllowThrottling are ordinary
//   UPROPERTYs, so editor(set_property) already writes them (and calls
//   PostEditChangeProperty, which is what makes the write take effect).
//   Writing a typed setter for them would duplicate a path that already
//   works. What is missing is that nobody knew to look: get_frame_timing
//   reports the throttle state in `warnings[]` together with the objectPath to
//   aim set_property at, because an unfocused throttled editor is exactly the
//   condition under which the numbers this action returns are meaningless.
//
// * SAMPLING OVER TIME. A handler runs on the game thread inside one tick, so
//   it cannot advance frames to gather a window. get_frame_timing drains the
//   RHI's own 16-entry GPU history and reports min/avg/max over whatever that
//   held, and names its window in `sampleWindow` rather than presenting a
//   single frame as an average.
//
// Engine version notes. The floor is UE 5.4. FTraceAuxiliary's start/stop/
// pause/channel surface predates that and is used unguarded. Two things are
// gated at 5.7, which is the oldest engine on this machine whose headers were
// read rather than remembered: UE::Trace::FChannelInfo enumeration (below it,
// the older name+state callback is used and descriptions/ids are reported as
// unavailable) and FMiscTrace's timing regions, which fall back to wall-clock
// timing with `traced:false` and the reason.

#include "EditorHandlers.h"

#include "HandlerUtils.h"

#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Editor/EditorPerformanceSettings.h"
#include "Engine/Engine.h"
#include "GPUProfiler.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Misc/StringBuilder.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "RenderTimer.h"
#include "Trace/Trace.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// Timing regions reached UE 5.7 in a form these headers confirm. Below that
// the bracket still measures wall clock; it just does not appear in a trace.
#define UE_MCP_HAS_TRACE_REGIONS (UE_MCP_HAS_5_7_API && MISCTRACE_ENABLED)

// Every helper here is prefixed MCPProfiling. The module is a unity build, so
// two .cpp files sharing a blob merge their anonymous namespaces and a helper
// named the same as one in another handler file is a redefinition (C2084).
// Nothing below is copied from another handler file for the same reason.
namespace
{
	// ── Shared state ────────────────────────────────────────────────────────
	// Both of these outlive a single call by design: a region is a bracket and
	// a standalone run is a process. Neither is serialised anywhere, so an
	// editor restart drops them, which is correct - a region cannot span a
	// restart and a child process is reported as gone once its handle is lost.

	struct FMCPProfilingRegion
	{
		/** Region id handed back by the trace system, 0 when untraced. */
		uint64 TraceId = 0;
		/** Wall clock at begin, so the bracket yields a duration with or
		 *  without a trace running. */
		double StartSeconds = 0.0;
		/** Frame counter at begin, so the bracket can say how many frames it
		 *  spanned - a region that spans zero frames measured nothing that a
		 *  frame-time verdict could explain. */
		uint64 StartFrame = 0;
		FString Category;
		bool bTraced = false;
	};

	TMap<FString, FMCPProfilingRegion>& MCPProfilingOpenRegions()
	{
		static TMap<FString, FMCPProfilingRegion> Regions;
		return Regions;
	}

	struct FMCPProfilingStandalone
	{
		FProcHandle Handle;
		uint32 ProcessId = 0;
		FString CommandLine;
		FString ExecutablePath;
		FString TraceFile;
		FString Channels;
		FString MapName;
		FDateTime LaunchedAt = FDateTime::MinValue();
		bool bLaunched = false;
	};

	FMCPProfilingStandalone& MCPProfilingStandalone()
	{
		static FMCPProfilingStandalone State;
		return State;
	}

	/** True when the recorded child process is still alive. Read-only on
	 *  purpose: status and stop both call it, and clearing the record here
	 *  would mean whichever ran first silently erased the pid and exit code
	 *  the other still has to report. stop_standalone_game does the clearing. */
	bool MCPProfilingStandaloneIsRunning()
	{
		FMCPProfilingStandalone& State = MCPProfilingStandalone();
		if (!State.bLaunched || !State.Handle.IsValid())
		{
			return false;
		}
		if (FPlatformProcess::IsProcRunning(State.Handle))
		{
			return true;
		}
		return false;
	}

	// ── Small conversions ───────────────────────────────────────────────────

	double MCPProfilingCyclesToMs(uint32 Cycles)
	{
		return FPlatformTime::GetSecondsPerCycle64() * static_cast<double>(Cycles) * 1000.0;
	}

	double MCPProfilingCycles64ToMs(uint64 Cycles)
	{
		return FPlatformTime::GetSecondsPerCycle64() * static_cast<double>(Cycles) * 1000.0;
	}

	/** Read a comma-separated string, or a JSON array of strings, into one
	 *  comma-separated list. Trace channel lists are a comma-separated string
	 *  everywhere in the engine; accepting an array too means a caller does not
	 *  have to know that. */
	FString MCPProfilingReadChannelList(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Params->TryGetArrayField(Key, Arr) && Arr)
		{
			TArray<FString> Names;
			for (const TSharedPtr<FJsonValue>& Value : *Arr)
			{
				FString Name;
				if (Value.IsValid() && Value->TryGetString(Name))
				{
					Name.TrimStartAndEndInline();
					if (!Name.IsEmpty()) Names.Add(Name);
				}
			}
			return FString::Join(Names, TEXT(","));
		}
		FString AsString = OptionalString(Params, Key);
		AsString.TrimStartAndEndInline();
		return AsString;
	}

	/** Read a list parameter as individual names (for enable/disable, where the
	 *  per-name result matters). */
	TArray<FString> MCPProfilingReadNameList(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key)
	{
		TArray<FString> Names;
		const FString Joined = MCPProfilingReadChannelList(Params, Key);
		if (Joined.IsEmpty()) return Names;
		Joined.ParseIntoArray(Names, TEXT(","), /*InCullEmpty=*/true);
		for (FString& Name : Names) Name.TrimStartAndEndInline();
		Names.RemoveAll([](const FString& Name) { return Name.IsEmpty(); });
		return Names;
	}

	FString MCPProfilingActiveChannels()
	{
		TStringBuilder<512> Builder;
		FTraceAuxiliary::GetActiveChannelsString(Builder);
		return FString(Builder.ToString());
	}

	bool MCPProfilingChannelIsActive(const TCHAR* ChannelName)
	{
		TArray<FString> Active;
		MCPProfilingActiveChannels().ParseIntoArray(Active, TEXT(","), true);
		for (FString& Name : Active)
		{
			Name.TrimStartAndEndInline();
			if (Name.Equals(ChannelName, ESearchCase::IgnoreCase)) return true;
		}
		return false;
	}

	const TCHAR* MCPProfilingConnectionTypeName(FTraceAuxiliary::EConnectionType Type)
	{
		switch (Type)
		{
		case FTraceAuxiliary::EConnectionType::Network:       return TEXT("network");
		case FTraceAuxiliary::EConnectionType::File:          return TEXT("file");
		case FTraceAuxiliary::EConnectionType::Relay:         return TEXT("relay");
#if UE_MCP_HAS_5_8_API
		// Secure tracing is a 5.8 addition; the enumerator does not exist before it.
		case FTraceAuxiliary::EConnectionType::SecureNetwork: return TEXT("secureNetwork");
#endif
		case FTraceAuxiliary::EConnectionType::None:          return TEXT("none");
		default:                                              return TEXT("unknown");
		}
	}

	const TCHAR* MCPProfilingSystemStatusName(FTraceAuxiliary::ETraceSystemStatus Status)
	{
		switch (Status)
		{
		case FTraceAuxiliary::ETraceSystemStatus::NotAvailable:           return TEXT("notAvailable");
		case FTraceAuxiliary::ETraceSystemStatus::Available:              return TEXT("available");
		case FTraceAuxiliary::ETraceSystemStatus::TracingToServer:        return TEXT("tracingToServer");
		case FTraceAuxiliary::ETraceSystemStatus::TracingToFile:          return TEXT("tracingToFile");
#if UE_MCP_HAS_5_8_API
		// Same 5.8 addition, mirrored in the system-status enum.
		case FTraceAuxiliary::ETraceSystemStatus::TracingToSecureNetwork: return TEXT("tracingToSecureNetwork");
#endif
		case FTraceAuxiliary::ETraceSystemStatus::TracingToCustomRelay:   return TEXT("tracingToCustomRelay");
		default:                                                          return TEXT("unknown");
		}
	}

	/** The UnrealInsights binary that ships with this engine, or an empty
	 *  string when it is not on disk (a launcher install without the tool, or
	 *  a platform that does not build it). */
	FString MCPProfilingInsightsExecutable()
	{
		const FString BinariesDir = FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries"));
#if PLATFORM_WINDOWS
		const FString Candidate = FPaths::Combine(BinariesDir, TEXT("Win64"), TEXT("UnrealInsights.exe"));
#elif PLATFORM_MAC
		const FString Candidate = FPaths::Combine(BinariesDir, TEXT("Mac"), TEXT("UnrealInsights"));
#else
		const FString Candidate = FPaths::Combine(BinariesDir, TEXT("Linux"), TEXT("UnrealInsights"));
#endif
		return FPaths::FileExists(Candidate) ? FPaths::ConvertRelativePathToFull(Candidate) : FString();
	}

	/** Describe a .utrace on disk: whether it exists, how big it is, and the
	 *  command that opens it. This is the whole of the "read the trace"
	 *  answer, and it is deliberate rather than a placeholder. */
	void MCPProfilingDescribeTraceFile(TSharedPtr<FJsonObject> Result, const FString& FilePath)
	{
		if (FilePath.IsEmpty()) return;
		const FString Absolute = FPaths::ConvertRelativePathToFull(FilePath);
		Result->SetStringField(TEXT("traceFile"), Absolute);

		const bool bExists = FPaths::FileExists(Absolute);
		Result->SetBoolField(TEXT("traceFileExists"), bExists);
		if (bExists)
		{
			Result->SetNumberField(TEXT("traceFileBytes"),
				static_cast<double>(IFileManager::Get().FileSize(*Absolute)));
		}

		const FString Insights = MCPProfilingInsightsExecutable();
		if (!Insights.IsEmpty())
		{
			Result->SetStringField(TEXT("insightsExecutable"), Insights);
			Result->SetStringField(TEXT("openCommand"),
				FString::Printf(TEXT("\"%s\" -OpenTraceFile=\"%s\""), *Insights, *Absolute));
		}
		Result->SetStringField(TEXT("analysisNote"),
			TEXT("The bridge writes .utrace files and reports where they landed; it does not read them back. ")
			TEXT("Trace analysis lives in the engine's TraceServices/TraceAnalysis Developer modules, which this ")
			TEXT("plugin does not link, so open the file with the reported openCommand. For numbers inside the ")
			TEXT("editor without leaving it, use editor(get_frame_timing) and editor(begin_profile_region) / ")
			TEXT("editor(end_profile_region)."));
	}

	/** Default destination for a file trace: an absolute, deterministic path
	 *  under the project's Saved/Profiling folder. FTraceAuxiliary will invent
	 *  one when handed a null target, but then nothing can report where it
	 *  went, which is the whole point of the action. */
	FString MCPProfilingDefaultTraceFile()
	{
		const FString Dir = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Profiling")));
		IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);
		const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
		const FString Project = FApp::GetProjectName();
		return FPaths::Combine(Dir, FString::Printf(TEXT("%s_%s.utrace"),
			Project.IsEmpty() ? TEXT("Trace") : *Project, *Stamp));
	}

	/** Common status block, shared by start/stop/status/pause so a caller
	 *  never has to make a second call to learn what state it left the trace
	 *  system in. */
	void MCPProfilingWriteTraceStatus(TSharedPtr<FJsonObject> Result)
	{
		const bool bConnected = FTraceAuxiliary::IsConnected();
		Result->SetBoolField(TEXT("tracing"), bConnected);
		Result->SetBoolField(TEXT("paused"), FTraceAuxiliary::IsPaused());
		Result->SetStringField(TEXT("systemStatus"),
			MCPProfilingSystemStatusName(FTraceAuxiliary::GetTraceSystemStatus()));
		Result->SetStringField(TEXT("connectionType"),
			MCPProfilingConnectionTypeName(FTraceAuxiliary::GetConnectionType()));

		const FString Destination = FTraceAuxiliary::GetTraceDestinationString();
		Result->SetStringField(TEXT("destination"), Destination);
		Result->SetStringField(TEXT("activeChannels"), MCPProfilingActiveChannels());

		UE::Trace::FStatistics Stats;
		UE::Trace::GetStatistics(Stats);
		TSharedPtr<FJsonObject> StatsObj = MakeShared<FJsonObject>();
		StatsObj->SetNumberField(TEXT("bytesSent"), static_cast<double>(Stats.BytesSent));
		StatsObj->SetNumberField(TEXT("bytesTraced"), static_cast<double>(Stats.BytesTraced));
		StatsObj->SetNumberField(TEXT("bytesEmitted"), static_cast<double>(Stats.BytesEmitted));
		StatsObj->SetNumberField(TEXT("memoryUsed"), static_cast<double>(Stats.MemoryUsed));
		StatsObj->SetNumberField(TEXT("cacheAllocated"), static_cast<double>(Stats.CacheAllocated));
		StatsObj->SetNumberField(TEXT("cacheUsed"), static_cast<double>(Stats.CacheUsed));
		StatsObj->SetNumberField(TEXT("cacheWaste"), static_cast<double>(Stats.CacheWaste));
		Result->SetObjectField(TEXT("statistics"), StatsObj);

		// A file destination is the case where "where did it land" has an
		// answer, so describe the file. A network destination is a host:port.
		if (Destination.EndsWith(TEXT(".utrace")))
		{
			MCPProfilingDescribeTraceFile(Result, Destination);
		}
	}

	/** Open profiling regions, reported by every trace-status read so a
	 *  bracket that was never closed is visible rather than silently open. */
	void MCPProfilingWriteOpenRegions(TSharedPtr<FJsonObject> Result)
	{
		const double Now = FPlatformTime::Seconds();
		TArray<TSharedPtr<FJsonValue>> Rows;
		for (const TPair<FString, FMCPProfilingRegion>& Pair : MCPProfilingOpenRegions())
		{
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("regionName"), Pair.Key);
			if (!Pair.Value.Category.IsEmpty()) Row->SetStringField(TEXT("regionCategory"), Pair.Value.Category);
			Row->SetNumberField(TEXT("openForMs"), (Now - Pair.Value.StartSeconds) * 1000.0);
			Row->SetNumberField(TEXT("openForFrames"),
				static_cast<double>(GFrameCounter - Pair.Value.StartFrame));
			Row->SetBoolField(TEXT("traced"), Pair.Value.bTraced);
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
		Result->SetArrayField(TEXT("openRegions"), Rows);
		Result->SetNumberField(TEXT("openRegionCount"), Rows.Num());
	}

	// ── Channel enumeration ─────────────────────────────────────────────────

	struct FMCPProfilingChannelRow
	{
		FString Name;
		FString Description;
		uint32 Id = 0;
		bool bEnabled = false;
		bool bReadOnly = false;
		bool bHasDetail = false;
	};

	struct FMCPProfilingChannelSinkState
	{
		TArray<FMCPProfilingChannelRow> Rows;
	};

#if UE_MCP_HAS_5_7_API
	bool MCPProfilingChannelSink(const UE::Trace::FChannelInfo& Info, void* User)
	{
		FMCPProfilingChannelSinkState* State = static_cast<FMCPProfilingChannelSinkState*>(User);
		if (!State) return false;
		FMCPProfilingChannelRow Row;
		Row.Name = Info.Name ? FString(ANSI_TO_TCHAR(Info.Name)) : FString();
		Row.Description = Info.Desc ? FString(ANSI_TO_TCHAR(Info.Desc)) : FString();
		Row.Id = Info.Id;
		Row.bEnabled = Info.bIsEnabled;
		Row.bReadOnly = Info.bIsReadOnly;
		Row.bHasDetail = true;
		State->Rows.Add(MoveTemp(Row));
		return true;
	}
#else
	void MCPProfilingChannelSinkLegacy(const ANSICHAR* Name, bool bState, void* User)
	{
		FMCPProfilingChannelSinkState* State = static_cast<FMCPProfilingChannelSinkState*>(User);
		if (!State) return;
		FMCPProfilingChannelRow Row;
		Row.Name = Name ? FString(ANSI_TO_TCHAR(Name)) : FString();
		Row.bEnabled = bState;
		Row.bHasDetail = false;
		State->Rows.Add(MoveTemp(Row));
	}
#endif

	TArray<FMCPProfilingChannelRow> MCPProfilingEnumerateChannels()
	{
		FMCPProfilingChannelSinkState State;
#if UE_MCP_HAS_5_7_API
		UE::Trace::EnumerateChannels(
			static_cast<UE::Trace::ChannelIterCallback*>(&MCPProfilingChannelSink), &State);
#else
		UE::Trace::EnumerateChannels(
			static_cast<UE::Trace::ChannelIterFunc*>(&MCPProfilingChannelSinkLegacy), &State);
#endif
		State.Rows.Sort([](const FMCPProfilingChannelRow& A, const FMCPProfilingChannelRow& B)
		{
			return A.Name < B.Name;
		});
		return MoveTemp(State.Rows);
	}

	/** Names closest to what the caller asked for, used to make an unknown
	 *  channel name say what the valid ones are rather than just failing. */
	FString MCPProfilingChannelSuggestions(const FString& Wanted, const TArray<FMCPProfilingChannelRow>& Rows)
	{
		TArray<FString> Close;
		for (const FMCPProfilingChannelRow& Row : Rows)
		{
			if (Row.Name.Contains(Wanted, ESearchCase::IgnoreCase) ||
				Wanted.Contains(Row.Name, ESearchCase::IgnoreCase))
			{
				Close.Add(Row.Name);
			}
			if (Close.Num() >= 8) break;
		}
		if (Close.Num() == 0)
		{
			for (const FMCPProfilingChannelRow& Row : Rows)
			{
				Close.Add(Row.Name);
				if (Close.Num() >= 12) break;
			}
		}
		return FString::Join(Close, TEXT(", "));
	}

	// ── Frame timing ────────────────────────────────────────────────────────

	struct FMCPProfilingGpuSamples
	{
		int32 Count = 0;
		double MinMs = 0.0;
		double MaxMs = 0.0;
		double AvgMs = 0.0;
		bool bDisjoint = false;
	};

	/** Drain the RHI's own GPU frame-time ring. The FState is deliberately
	 *  static: it is a cursor into a 16-entry history, so consecutive calls
	 *  each report the frames that elapsed since the previous call rather than
	 *  replaying the same numbers. */
	FMCPProfilingGpuSamples MCPProfilingDrainGpuHistory()
	{
		FMCPProfilingGpuSamples Out;
		static FRHIGPUFrameTimeHistory::FState CursorState;

		double Total = 0.0;
		uint64 Cycles = 0;
		for (;;)
		{
			const FRHIGPUFrameTimeHistory::EResult Result = CursorState.PopFrameCycles(Cycles);
			if (Result == FRHIGPUFrameTimeHistory::EResult::Empty) break;
			if (Result == FRHIGPUFrameTimeHistory::EResult::Disjoint) Out.bDisjoint = true;

			const double Ms = MCPProfilingCycles64ToMs(Cycles);
			if (Out.Count == 0 || Ms < Out.MinMs) Out.MinMs = Ms;
			if (Out.Count == 0 || Ms > Out.MaxMs) Out.MaxMs = Ms;
			Total += Ms;
			Out.Count++;
			// The history holds 16 entries; this bound only guards against a
			// producer that refills faster than the loop drains.
			if (Out.Count >= 64) break;
		}
		if (Out.Count > 0) Out.AvgMs = Total / static_cast<double>(Out.Count);
		return Out;
	}
}

// ---------------------------------------------------------------------------
// start_insights_trace -- begin a trace session.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::StartInsightsTrace(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FString Target = OptionalString(Params, TEXT("traceTarget"), TEXT("file"));
	Target.ToLowerInline();
	if (Target != TEXT("file") && Target != TEXT("network") && Target != TEXT("none"))
	{
		return MCPError(FString::Printf(
			TEXT("Unknown traceTarget '%s'. Use file (writes a .utrace), network (streams to a trace server) or none (records into memory only)."),
			*Target));
	}

	// Idempotency, and the specific case the ticket calls out: a trace that is
	// already running must SAY so rather than opening a second one. The engine
	// itself silently no-ops a second Start, which would otherwise read as a
	// fresh session that never existed.
	if (FTraceAuxiliary::IsConnected())
	{
		TSharedPtr<FJsonObject> Existing = MCPSuccess();
		MCPSetExisted(Existing);
		Existing->SetBoolField(TEXT("started"), false);
		Existing->SetBoolField(TEXT("alreadyTracing"), true);
		Existing->SetStringField(TEXT("note"),
			TEXT("A trace was already running, so nothing was started. Only one trace connection exists per process. ")
			TEXT("Stop it with editor(stop_trace) before starting a different one, or add channels to the running ")
			TEXT("session with editor(set_trace_channels)."));
		MCPProfilingWriteTraceStatus(Existing);
		MCPProfilingWriteOpenRegions(Existing);
		return MCPResult(Existing);
	}

	FString Channels = MCPProfilingReadChannelList(Params, TEXT("channels"));
	if (Channels.IsEmpty()) Channels = TEXT("default");

	// Validate every requested name before starting, so a typo does not produce
	// a running trace that is missing the data it was started for.
	//
	// A name that is not a registered channel is not automatically wrong: a
	// preset ("default", "cpu", "memory") EXPANDS to channels rather than being
	// one, and there is no public "is this a preset" query on every supported
	// engine. So an unresolved name is refused only when NOTHING in the request
	// resolved, and otherwise travels back on the result as
	// unrecognisedChannels for the caller to check against list_trace_channels.
	TArray<FString> UnrecognisedChannels;
	{
		const TArray<FMCPProfilingChannelRow> Rows = MCPProfilingEnumerateChannels();
		TArray<FString> Wanted;
		Channels.ParseIntoArray(Wanted, TEXT(","), true);
		for (FString& Name : Wanted)
		{
			Name.TrimStartAndEndInline();
			if (Name.IsEmpty()) continue;
			if (UE::Trace::IsChannel(*Name)) continue;
			UnrecognisedChannels.Add(Name);
		}
		const bool bNothingResolved =
			Wanted.Num() > 0 && UnrecognisedChannels.Num() == Wanted.Num();
		// "default" always resolves through the preset path even though it is
		// not a channel, so it is never the reason for a refusal.
		if (bNothingResolved && !Channels.Equals(TEXT("default"), ESearchCase::IgnoreCase))
		{
			return MCPError(FString::Printf(
				TEXT("None of the requested channels exist and none is the 'default' preset: [%s]. Channels close to those ")
				TEXT("names: %s. Call editor(list_trace_channels) for the full list, or pass a preset such as 'default', ")
				TEXT("'cpu' or 'memory'."),
				*FString::Join(UnrecognisedChannels, TEXT(", ")),
				*MCPProfilingChannelSuggestions(UnrecognisedChannels[0], Rows)));
		}
	}

	FString FilePath;
	FString Host;
	FTraceAuxiliary::EConnectionType Type = FTraceAuxiliary::EConnectionType::File;
	if (Target == TEXT("file"))
	{
		FilePath = OptionalString(Params, TEXT("file"));
		FilePath.TrimStartAndEndInline();
		if (FilePath.IsEmpty())
		{
			FilePath = MCPProfilingDefaultTraceFile();
		}
		else
		{
			FilePath = FPaths::ConvertRelativePathToFull(FilePath);
			if (!FilePath.EndsWith(TEXT(".utrace"))) FilePath += TEXT(".utrace");
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), /*Tree=*/true);
		}
	}
	else if (Target == TEXT("network"))
	{
		Host = OptionalString(Params, TEXT("host"), TEXT("127.0.0.1"));
		Host.TrimStartAndEndInline();
		if (Host.IsEmpty())
		{
			return MCPError(TEXT("traceTarget='network' needs a host (IP or hostname of the running trace server); pass host, or use traceTarget='file'."));
		}
		Type = FTraceAuxiliary::EConnectionType::Network;
	}
	else
	{
		Type = FTraceAuxiliary::EConnectionType::None;
	}

	// FOptions carries a member deprecated in 5.7 that the implicitly-defined
	// constructor still touches, so the declaration is wrapped rather than the
	// whole file being compiled with deprecation warnings suppressed.
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FTraceAuxiliary::FOptions Options;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	Options.bTruncateFile = OptionalBool(Params, TEXT("truncate"), true);
	Options.bExcludeTail = OptionalBool(Params, TEXT("excludeTail"), false);

	const TCHAR* TargetPtr = nullptr;
	if (Type == FTraceAuxiliary::EConnectionType::File)          TargetPtr = *FilePath;
	else if (Type == FTraceAuxiliary::EConnectionType::Network)  TargetPtr = *Host;

	const bool bStarted = FTraceAuxiliary::Start(Type, TargetPtr, *Channels, &Options);
	if (!bStarted)
	{
		return MCPError(FString::Printf(
			TEXT("Failed to start the trace (traceTarget='%s', destination='%s'). A file target fails when the path is not writable; ")
			TEXT("a network target fails when no trace server is listening on the host. Current system status: %s."),
			*Target,
			Type == FTraceAuxiliary::EConnectionType::File ? *FilePath : *Host,
			MCPProfilingSystemStatusName(FTraceAuxiliary::GetTraceSystemStatus())));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetBoolField(TEXT("started"), true);
	Result->SetBoolField(TEXT("alreadyTracing"), false);
	Result->SetStringField(TEXT("traceTarget"), Target);
	Result->SetStringField(TEXT("requestedChannels"), Channels);
	Result->SetArrayField(TEXT("unrecognisedChannels"), MCPStringListToJson(UnrecognisedChannels));
	if (UnrecognisedChannels.Num() > 0)
	{
		Result->SetStringField(TEXT("unrecognisedChannelsNote"),
			TEXT("These names are not registered channels. They were passed through because a preset expands to channels ")
			TEXT("rather than being one, so a preset spelling looks the same as a typo here. Compare activeChannels below ")
			TEXT("against what you asked for: if a name is missing from it, it was a typo. ")
			TEXT("editor(list_trace_channels) lists the real ones."));
	}
	if (Type == FTraceAuxiliary::EConnectionType::File)
	{
		MCPProfilingDescribeTraceFile(Result, FilePath);
		Result->SetStringField(TEXT("note"),
			TEXT("The .utrace is written continuously and is only complete after editor(stop_trace). Its size will read as 0 until then."));
	}
	MCPProfilingWriteTraceStatus(Result);
	MCPProfilingWriteOpenRegions(Result);

	MCPSetRollback(Result, TEXT("stop_insights_trace"), MakeShared<FJsonObject>());
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// stop_insights_trace -- end the session and report the file it produced.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::StopInsightsTrace(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	// The destination is only readable while connected, so capture it before
	// stopping. Otherwise the one thing the caller needs - where the file went
	// - is gone by the time there is an answer to return.
	const FString Destination = FTraceAuxiliary::GetTraceDestinationString();
	const FString ChannelsAtStop = MCPProfilingActiveChannels();
	const bool bWasTracing = FTraceAuxiliary::IsConnected();

	if (!bWasTracing)
	{
		TSharedPtr<FJsonObject> NoOp = MCPSuccess();
		NoOp->SetBoolField(TEXT("wasTracing"), false);
		NoOp->SetBoolField(TEXT("stopped"), false);
		NoOp->SetBoolField(TEXT("unchanged"), true);
		NoOp->SetStringField(TEXT("note"),
			TEXT("No trace was running, so nothing was stopped. Replaying a stop is safe and reports this rather than erroring."));
		MCPProfilingWriteTraceStatus(NoOp);
		MCPProfilingWriteOpenRegions(NoOp);
		return MCPResult(NoOp);
	}

	const bool bStopped = FTraceAuxiliary::Stop();

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetBoolField(TEXT("wasTracing"), true);
	Result->SetBoolField(TEXT("stopped"), bStopped);
	Result->SetStringField(TEXT("channelsAtStop"), ChannelsAtStop);
	if (Destination.EndsWith(TEXT(".utrace")))
	{
		MCPProfilingDescribeTraceFile(Result, Destination);
	}
	else if (!Destination.IsEmpty())
	{
		Result->SetStringField(TEXT("destination"), Destination);
	}
	MCPProfilingWriteTraceStatus(Result);
	MCPProfilingWriteOpenRegions(Result);

	if (MCPProfilingOpenRegions().Num() > 0)
	{
		Result->SetStringField(TEXT("openRegionWarning"),
			TEXT("Profiling regions were still open when the trace stopped. They have no end event in the file. ")
			TEXT("Close them with editor(end_profile_region) before stopping."));
	}

	// The inverse of stopping is starting, and it is LOSSY: a new start writes
	// a NEW file rather than reopening this one, and the events between the
	// stop and the restart are simply absent. Said here rather than implied by
	// the presence of a rollback record.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("channels"), ChannelsAtStop.IsEmpty() ? TEXT("default") : *ChannelsAtStop);
	MCPSetRollback(Result, TEXT("start_insights_trace"), Payload);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Rolling back restarts tracing into a NEW .utrace with the same channels. A stopped trace file cannot be reopened or appended to, so the events in the gap are lost."));
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// pause_insights_trace -- mute/unmute every active channel without closing.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::PauseInsightsTrace(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	const bool bWantPaused = OptionalBool(Params, TEXT("paused"), true);
	const bool bWasPaused = FTraceAuxiliary::IsPaused();

	if (bWantPaused == bWasPaused)
	{
		TSharedPtr<FJsonObject> NoOp = MCPSuccess();
		NoOp->SetBoolField(TEXT("paused"), bWasPaused);
		NoOp->SetBoolField(TEXT("changed"), false);
		NoOp->SetBoolField(TEXT("unchanged"), true);
		MCPProfilingWriteTraceStatus(NoOp);
		return MCPResult(NoOp);
	}

	const bool bApplied = bWantPaused ? FTraceAuxiliary::Pause() : FTraceAuxiliary::Resume();
	if (!bApplied)
	{
		return MCPError(bWantPaused
			? TEXT("Pause failed. Pausing disables the active channels, so there has to be at least one active channel; start a trace first with editor(start_trace).")
			: TEXT("Resume failed. Resume re-enables the channel set a previous pause recorded, so there is nothing to resume unless editor(pause_trace) paused this session."));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetBoolField(TEXT("paused"), FTraceAuxiliary::IsPaused());
	Result->SetBoolField(TEXT("changed"), true);
	Result->SetBoolField(TEXT("previouslyPaused"), bWasPaused);
	MCPProfilingWriteTraceStatus(Result);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetBoolField(TEXT("paused"), bWasPaused);
	MCPSetRollback(Result, TEXT("pause_insights_trace"), Payload);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// get_insights_trace_status -- everything about the trace system, read-only.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::GetInsightsTraceStatus(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPProfilingWriteTraceStatus(Result);
	MCPProfilingWriteOpenRegions(Result);

	// Channel presets are what a caller passes to start_trace as `channels`,
	// so listing them here is what makes that parameter discoverable.
#if UE_MCP_HAS_5_7_API
	TArray<TSharedPtr<FJsonValue>> Presets;
	auto CollectPreset = [&Presets](const FTraceAuxiliary::FChannelPreset& Preset)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("name"), Preset.Name ? Preset.Name : TEXT(""));
		Row->SetStringField(TEXT("channels"), Preset.ChannelList ? Preset.ChannelList : TEXT(""));
		Row->SetBoolField(TEXT("readOnly"), Preset.bIsReadOnly);
		Presets.Add(MakeShared<FJsonValueObject>(Row));
		return FTraceAuxiliary::EEnumerateResult::Continue;
	};
	FTraceAuxiliary::EnumerateFixedChannelPresets(CollectPreset);
	FTraceAuxiliary::EnumerateChannelPresetsFromSettings(CollectPreset);
	Result->SetArrayField(TEXT("channelPresets"), Presets);
#else
	Result->SetArrayField(TEXT("channelPresets"), TArray<TSharedPtr<FJsonValue>>());
	Result->SetStringField(TEXT("channelPresetsNote"),
		TEXT("Channel-preset enumeration needs UE 5.7 or newer. 'default' is always accepted by start_trace on every supported engine."));
#endif

	const FString Insights = MCPProfilingInsightsExecutable();
	Result->SetBoolField(TEXT("insightsAvailable"), !Insights.IsEmpty());
	if (!Insights.IsEmpty()) Result->SetStringField(TEXT("insightsExecutable"), Insights);

	TSharedPtr<FJsonObject> Standalone = MakeShared<FJsonObject>();
	Standalone->SetBoolField(TEXT("running"), MCPProfilingStandaloneIsRunning());
	if (MCPProfilingStandalone().bLaunched)
	{
		Standalone->SetNumberField(TEXT("processId"), static_cast<double>(MCPProfilingStandalone().ProcessId));
	}
	Result->SetObjectField(TEXT("standalone"), Standalone);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// list_trace_channels -- every registered channel and whether it is on.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::ListTraceChannels(const TSharedPtr<FJsonObject>& Params)
{
	const FString Filter = OptionalString(Params, TEXT("filter"));
	const bool bEnabledOnly = OptionalBool(Params, TEXT("enabledOnly"), false);

	const TArray<FMCPProfilingChannelRow> Rows = MCPProfilingEnumerateChannels();

	TArray<TSharedPtr<FJsonValue>> Out;
	int32 EnabledCount = 0;
	for (const FMCPProfilingChannelRow& Row : Rows)
	{
		if (Row.bEnabled) EnabledCount++;
		if (bEnabledOnly && !Row.bEnabled) continue;
		if (!Filter.IsEmpty() &&
			!Row.Name.Contains(Filter, ESearchCase::IgnoreCase) &&
			!Row.Description.Contains(Filter, ESearchCase::IgnoreCase))
		{
			continue;
		}
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Row.Name);
		Obj->SetBoolField(TEXT("enabled"), Row.bEnabled);
		if (Row.bHasDetail)
		{
			Obj->SetStringField(TEXT("description"), Row.Description);
			Obj->SetNumberField(TEXT("id"), static_cast<double>(Row.Id));
			Obj->SetBoolField(TEXT("readOnly"), Row.bReadOnly);
		}
		Out.Add(MakeShared<FJsonValueObject>(Obj));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetArrayField(TEXT("channels"), Out);
	Result->SetNumberField(TEXT("channelCount"), Out.Num());
	Result->SetNumberField(TEXT("registeredCount"), Rows.Num());
	Result->SetNumberField(TEXT("enabledCount"), EnabledCount);
	Result->SetStringField(TEXT("activeChannels"), MCPProfilingActiveChannels());
	Result->SetBoolField(TEXT("tracing"), FTraceAuxiliary::IsConnected());
#if !UE_MCP_HAS_5_7_API
	Result->SetStringField(TEXT("detailNote"),
		TEXT("Channel descriptions, ids and the read-only flag need UE 5.7 or newer; only name and enabled state are available on this engine."));
#endif
	Result->SetStringField(TEXT("note"),
		TEXT("A read-only channel can only be enabled from the command line at process start, so enabling one here will report changed=false. ")
		TEXT("Enabling a channel mid-trace is legal: events start appearing from that point on."));
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// set_trace_channels -- enable and disable named channels.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::SetTraceChannels(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	const TArray<FString> Enable = MCPProfilingReadNameList(Params, TEXT("enable"));
	const TArray<FString> Disable = MCPProfilingReadNameList(Params, TEXT("disable"));
	if (Enable.Num() == 0 && Disable.Num() == 0)
	{
		return MCPError(TEXT("Pass enable and/or disable: a comma-separated string or an array of channel names. editor(list_trace_channels) lists what this build registers."));
	}

	const TArray<FMCPProfilingChannelRow> Rows = MCPProfilingEnumerateChannels();
	auto FindRow = [&Rows](const FString& Name) -> const FMCPProfilingChannelRow*
	{
		for (const FMCPProfilingChannelRow& Row : Rows)
		{
			if (Row.Name.Equals(Name, ESearchCase::IgnoreCase)) return &Row;
		}
		return nullptr;
	};

	// Validate the whole request before applying any of it. A half-applied
	// channel set is a trace that recorded a different thing than was asked
	// for, which is worse than a refusal.
	TArray<FString> Unknown;
	for (const FString& Name : Enable)  { if (!FindRow(Name)) Unknown.Add(Name); }
	for (const FString& Name : Disable) { if (!FindRow(Name)) Unknown.Add(Name); }
	if (Unknown.Num() > 0)
	{
		return MCPError(FString::Printf(
			TEXT("Unknown trace channel(s): [%s]. Channels close to '%s': %s. Call editor(list_trace_channels) for the full list. ")
			TEXT("Presets such as 'default' are not channels and belong in editor(start_trace)'s channels parameter."),
			*FString::Join(Unknown, TEXT(", ")), *Unknown[0],
			*MCPProfilingChannelSuggestions(Unknown[0], Rows)));
	}

	// A channel in both lists has no defined answer, and applying enable then
	// disable would quietly leave it off while the response claimed both.
	TArray<FString> Both;
	for (const FString& Name : Enable)
	{
		if (Disable.ContainsByPredicate([&Name](const FString& Other) { return Other.Equals(Name, ESearchCase::IgnoreCase); }))
		{
			Both.Add(Name);
		}
	}
	if (Both.Num() > 0)
	{
		return MCPError(FString::Printf(
			TEXT("These channels appear in both enable and disable, so the request contradicts itself: [%s]. Name each channel once."),
			*FString::Join(Both, TEXT(", "))));
	}

	TArray<TSharedPtr<FJsonValue>> Applied;
	TArray<FString> RollbackEnable;
	TArray<FString> RollbackDisable;
	int32 ChangedCount = 0;

	auto Apply = [&](const TArray<FString>& Names, bool bWantEnabled)
	{
		for (const FString& Name : Names)
		{
			const FMCPProfilingChannelRow* Row = FindRow(Name);
			const bool bBefore = Row ? Row->bEnabled : false;
			if (bWantEnabled) FTraceAuxiliary::EnableChannels(*Name);
			else              FTraceAuxiliary::DisableChannels(*Name);

			// ToggleChannel applies the same state again and returns the state
			// the channel actually ended up in, which is the only way to learn
			// whether the change landed: EnableChannels/DisableChannels return
			// void, and a read-only channel refuses at runtime. Reporting the
			// request as if it took is exactly the silent failure this action
			// exists to avoid. The second apply is a no-op by construction.
			const bool bAfter = UE::Trace::ToggleChannel(*Name, bWantEnabled);
			const bool bChanged = bAfter != bBefore;
			if (bChanged)
			{
				ChangedCount++;
				if (bBefore) RollbackEnable.Add(Name); else RollbackDisable.Add(Name);
			}

			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("name"), Name);
			Obj->SetBoolField(TEXT("requestedEnabled"), bWantEnabled);
			Obj->SetBoolField(TEXT("wasEnabled"), bBefore);
			Obj->SetBoolField(TEXT("enabled"), bAfter);
			Obj->SetBoolField(TEXT("changed"), bChanged);
			if (bAfter != bWantEnabled)
			{
				Obj->SetStringField(TEXT("refusedReason"),
					(Row && Row->bReadOnly)
						? TEXT("read-only channel: it can only be enabled on the command line at process start")
						: TEXT("the trace system did not accept the change"));
			}
			Applied.Add(MakeShared<FJsonValueObject>(Obj));
		}
	};
	Apply(Enable, true);
	Apply(Disable, false);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	if (ChangedCount > 0) MCPSetUpdated(Result); else Result->SetBoolField(TEXT("unchanged"), true);
	Result->SetArrayField(TEXT("channels"), Applied);
	Result->SetNumberField(TEXT("changedCount"), ChangedCount);
	Result->SetStringField(TEXT("activeChannels"), MCPProfilingActiveChannels());
	Result->SetBoolField(TEXT("tracing"), FTraceAuxiliary::IsConnected());

	// The inverse is the exact set that actually moved, so replaying it puts
	// every channel back where it was and leaves the ones that refused alone.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("enable"), FString::Join(RollbackEnable, TEXT(",")));
	Payload->SetStringField(TEXT("disable"), FString::Join(RollbackDisable, TEXT(",")));
	MCPSetRollback(Result, TEXT("set_trace_channels"), Payload);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// begin_profile_region -- open a named bracket around an operation.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::BeginProfileRegion(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FString RegionName;
	if (auto Err = RequireString(Params, TEXT("regionName"), RegionName)) return Err;
	RegionName.TrimStartAndEndInline();
	if (RegionName.IsEmpty())
	{
		return MCPError(TEXT("regionName must be a non-empty name; it is the key end_profile_region closes the bracket by."));
	}
	const FString Category = OptionalString(Params, TEXT("regionCategory"));

	TMap<FString, FMCPProfilingRegion>& Regions = MCPProfilingOpenRegions();
	if (FMCPProfilingRegion* Open = Regions.Find(RegionName))
	{
		// Opening the same name twice would produce two begin events and one
		// end, so the second is reported rather than nested. The name is the
		// key, and a key that is already taken is an existing region.
		TSharedPtr<FJsonObject> Existing = MCPSuccess();
		MCPSetExisted(Existing);
		Existing->SetStringField(TEXT("regionName"), RegionName);
		Existing->SetBoolField(TEXT("opened"), false);
		Existing->SetNumberField(TEXT("openForMs"), (FPlatformTime::Seconds() - Open->StartSeconds) * 1000.0);
		Existing->SetStringField(TEXT("note"),
			TEXT("A region with this name is already open, so nothing was started. Close it with editor(end_profile_region) ")
			TEXT("or use a different regionName; overlapping regions cannot be told apart by name."));
		MCPProfilingWriteOpenRegions(Existing);
		return MCPResult(Existing);
	}

	FMCPProfilingRegion Region;
	Region.StartSeconds = FPlatformTime::Seconds();
	Region.StartFrame = GFrameCounter;
	Region.Category = Category;

#if UE_MCP_HAS_TRACE_REGIONS
	Region.TraceId = Category.IsEmpty()
		? FMiscTrace::OutputBeginRegionWithId(*RegionName)
		: FMiscTrace::OutputBeginRegionWithId(*RegionName, *Category);
	Region.bTraced = true;
#endif

	Regions.Add(RegionName, Region);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("regionName"), RegionName);
	if (!Category.IsEmpty()) Result->SetStringField(TEXT("regionCategory"), Category);
	Result->SetBoolField(TEXT("opened"), true);
	Result->SetBoolField(TEXT("traced"), Region.bTraced);
	Result->SetBoolField(TEXT("tracing"), FTraceAuxiliary::IsConnected());
#if !UE_MCP_HAS_TRACE_REGIONS
	Result->SetStringField(TEXT("tracedReason"),
		TEXT("Timing regions need UE 5.7 or newer with trace compiled in. The bracket is still timed by wall clock here, ")
		TEXT("so end_profile_region returns a duration; it just will not appear as a region in Unreal Insights."));
#else
	if (!FTraceAuxiliary::IsConnected())
	{
		Result->SetStringField(TEXT("tracedReason"),
			TEXT("No trace is running, so the region event has nowhere to land. The wall-clock duration is still measured. ")
			TEXT("Call editor(start_trace) first if the region should show up in Unreal Insights."));
	}
#endif
	MCPProfilingWriteOpenRegions(Result);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("regionName"), RegionName);
	MCPSetRollback(Result, TEXT("end_profile_region"), Payload);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// end_profile_region -- close the bracket and report what it measured.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::EndProfileRegion(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FString RegionName;
	if (auto Err = RequireString(Params, TEXT("regionName"), RegionName)) return Err;
	RegionName.TrimStartAndEndInline();

	TMap<FString, FMCPProfilingRegion>& Regions = MCPProfilingOpenRegions();
	FMCPProfilingRegion Region;
	if (!Regions.RemoveAndCopyValue(RegionName, Region))
	{
		TArray<FString> OpenNames;
		Regions.GetKeys(OpenNames);
		TSharedPtr<FJsonObject> NoOp = MCPSuccess();
		NoOp->SetStringField(TEXT("regionName"), RegionName);
		NoOp->SetBoolField(TEXT("wasOpen"), false);
		NoOp->SetBoolField(TEXT("closed"), false);
		NoOp->SetBoolField(TEXT("unchanged"), true);
		NoOp->SetStringField(TEXT("note"), OpenNames.Num() > 0
			? FString::Printf(TEXT("No region named '%s' is open. Open regions: [%s]."), *RegionName, *FString::Join(OpenNames, TEXT(", ")))
			: FString::Printf(TEXT("No region named '%s' is open, and no region is open at all. Replaying an end is safe and reports this rather than erroring."), *RegionName));
		MCPProfilingWriteOpenRegions(NoOp);
		return MCPResult(NoOp);
	}

	const double DurationMs = (FPlatformTime::Seconds() - Region.StartSeconds) * 1000.0;
	const uint64 Frames = GFrameCounter - Region.StartFrame;

#if UE_MCP_HAS_TRACE_REGIONS
	if (Region.bTraced) FMiscTrace::OutputEndRegionWithId(Region.TraceId);
#endif

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("regionName"), RegionName);
	if (!Region.Category.IsEmpty()) Result->SetStringField(TEXT("regionCategory"), Region.Category);
	Result->SetBoolField(TEXT("wasOpen"), true);
	Result->SetBoolField(TEXT("closed"), true);
	Result->SetBoolField(TEXT("traced"), Region.bTraced);
	Result->SetNumberField(TEXT("durationMs"), DurationMs);
	Result->SetNumberField(TEXT("frames"), static_cast<double>(Frames));
	if (Frames == 0)
	{
		Result->SetStringField(TEXT("framesNote"),
			TEXT("The region spanned zero rendered frames, so a CPU-versus-GPU verdict cannot describe it: it measured work ")
			TEXT("that ran inside one tick. get_frame_timing answers about frames, not about this bracket."));
	}
	MCPProfilingWriteOpenRegions(Result);

	// Reopening a closed region is not the inverse of closing it: the trace
	// would carry a second begin with the same name and a different span.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Closing a region emits an end event into the trace stream, which cannot be withdrawn. There is no inverse action. ")
		TEXT("Reopening the same regionName starts a NEW region rather than restoring this one."));
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// add_trace_bookmark -- drop a named marker into the trace timeline.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::AddTraceBookmark(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FString BookmarkName;
	if (auto Err = RequireString(Params, TEXT("bookmarkName"), BookmarkName)) return Err;
	BookmarkName.TrimStartAndEndInline();
	if (BookmarkName.IsEmpty())
	{
		return MCPError(TEXT("bookmarkName must be a non-empty label; it is the text Unreal Insights shows on the timeline marker."));
	}

	const bool bTracing = FTraceAuxiliary::IsConnected();
	const bool bChannelOn = MCPProfilingChannelIsActive(TEXT("Bookmark"));

	// Emitted unconditionally: the macro is a no-op when the channel is off, so
	// this is cheap, and guarding it would mean a bookmark could be dropped by
	// a stale reading of the channel state.
	TRACE_BOOKMARK(TEXT("%s"), *BookmarkName);

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetStringField(TEXT("bookmarkName"), BookmarkName);
	Result->SetBoolField(TEXT("tracing"), bTracing);
	Result->SetBoolField(TEXT("bookmarkChannelActive"), bChannelOn);
	Result->SetBoolField(TEXT("recorded"), bTracing && bChannelOn);
	// The did-anything-happen answer this convention asks every mutation for.
	// For a bookmark it is exactly "did a marker reach the trace": with no
	// trace running or the Bookmark channel off, the call is a no-op and says
	// so instead of reporting a success for an event that was dropped.
	Result->SetBoolField(TEXT("changed"), bTracing && bChannelOn);
	Result->SetNumberField(TEXT("frame"), static_cast<double>(GFrameCounter));
	if (!bTracing)
	{
		Result->SetStringField(TEXT("note"),
			TEXT("No trace is running, so the bookmark had nowhere to land and was dropped. Call editor(start_trace) first."));
	}
	else if (!bChannelOn)
	{
		Result->SetStringField(TEXT("note"),
			TEXT("The Bookmark channel is not active, so the bookmark was dropped. Turn it on with ")
			TEXT("editor(set_trace_channels, enable=\"Bookmark\"), or start the trace with a channel set that includes it."));
	}

	// A bookmark is an event, not a state. Two calls produce two markers, and
	// that is correct behaviour rather than a broken idempotency contract.
	Result->SetBoolField(TEXT("idempotent"), false);
	Result->SetStringField(TEXT("idempotencyNote"),
		TEXT("A bookmark is a point event: calling twice writes two markers, which is the intended behaviour. There is no ")
		TEXT("'already bookmarked' state to report and no inverse action that removes one from a trace stream."));
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// get_frame_timing -- the numbers, and a CPU-versus-GPU verdict over them.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::GetFrameTiming(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	// How far ahead one side has to be before the frame is called bound by it.
	// Exposed because the right number depends on what is being measured: a
	// 5% lead is noise on an editor viewport and decisive on a locked 60Hz
	// standalone run.
	const double MarginPercent = FMath::Clamp(
		OptionalNumber(Params, TEXT("cpuGpuMarginPercent"), 10.0), 0.0, 100.0);

	const double GameThreadMs   = MCPProfilingCyclesToMs(GGameThreadTime);
	const double RenderThreadMs = MCPProfilingCyclesToMs(GRenderThreadTime);
	const double RhiThreadMs    = MCPProfilingCyclesToMs(GRHIThreadTime);
	const double SwapBufferMs   = MCPProfilingCyclesToMs(GSwapBufferTime);
	const double GameWaitMs     = MCPProfilingCyclesToMs(GGameThreadWaitTime);
	const double RenderWaitMs   = MCPProfilingCyclesToMs(GRenderThreadWaitTime);
	const double DeltaMs        = FApp::GetDeltaTime() * 1000.0;

	const FMCPProfilingGpuSamples Gpu = MCPProfilingDrainGpuHistory();
	const bool bHaveGpu = Gpu.Count > 0;
	const double GpuMs = bHaveGpu ? Gpu.AvgMs : 0.0;

	TSharedPtr<FJsonObject> Result = MCPSuccess();

	TSharedPtr<FJsonObject> Timings = MakeShared<FJsonObject>();
	Timings->SetNumberField(TEXT("frameMs"), DeltaMs);
	Timings->SetNumberField(TEXT("fps"), DeltaMs > 0.0 ? 1000.0 / DeltaMs : 0.0);
	Timings->SetNumberField(TEXT("gameThreadMs"), GameThreadMs);
	Timings->SetNumberField(TEXT("gameThreadWaitMs"), GameWaitMs);
	Timings->SetNumberField(TEXT("renderThreadMs"), RenderThreadMs);
	Timings->SetNumberField(TEXT("renderThreadWaitMs"), RenderWaitMs);
	Timings->SetNumberField(TEXT("rhiThreadMs"), RhiThreadMs);
	Timings->SetNumberField(TEXT("swapBufferMs"), SwapBufferMs);
	if (bHaveGpu)
	{
		Timings->SetNumberField(TEXT("gpuMs"), Gpu.AvgMs);
		Timings->SetNumberField(TEXT("gpuMinMs"), Gpu.MinMs);
		Timings->SetNumberField(TEXT("gpuMaxMs"), Gpu.MaxMs);
		Timings->SetNumberField(TEXT("gpuSamples"), Gpu.Count);
	}
	Result->SetObjectField(TEXT("timings"), Timings);
	Result->SetBoolField(TEXT("gpuTimingAvailable"), bHaveGpu);

	// The verdict. Stated as a rule the caller can check, not as an oracle:
	// the busiest CPU thread against the GPU, with a margin, and the losing
	// side named so "cpu" is never ambiguous about WHICH cpu thread.
	FString CpuSide = TEXT("game");
	double CpuMs = GameThreadMs;
	if (RenderThreadMs > CpuMs) { CpuMs = RenderThreadMs; CpuSide = TEXT("render"); }
	if (RhiThreadMs > CpuMs)    { CpuMs = RhiThreadMs;    CpuSide = TEXT("rhi"); }

	FString Bound;
	FString Reason;
	if (!bHaveGpu)
	{
		Bound = TEXT("unknown");
		Reason = TEXT("The RHI reported no GPU frame timings, so there is nothing to compare the CPU against. This is normal ")
			TEXT("when the editor has not rendered since the last read, when the viewport is not realtime, or on an RHI that ")
			TEXT("does not publish GPU timings. Turn on realtime with editor(set_realtime) and force a repaint with ")
			TEXT("editor(redraw_viewport), then call again.");
	}
	else
	{
		const double Larger = FMath::Max(CpuMs, GpuMs);
		const double Diff = FMath::Abs(CpuMs - GpuMs);
		const double MarginMs = Larger * (MarginPercent / 100.0);
		if (Diff <= MarginMs)
		{
			Bound = TEXT("balanced");
			Reason = FString::Printf(
				TEXT("The busiest CPU thread (%s, %.2f ms) and the GPU (%.2f ms) are within %.0f%% of each other, so neither ")
				TEXT("is the bottleneck on its own. Lower cpuGpuMarginPercent to force a verdict."),
				*CpuSide, CpuMs, GpuMs, MarginPercent);
		}
		else if (GpuMs > CpuMs)
		{
			Bound = TEXT("gpu");
			Reason = FString::Printf(
				TEXT("GPU %.2f ms against the busiest CPU thread (%s) at %.2f ms, a %.0f%% lead. The frame waits on rendering work."),
				GpuMs, *CpuSide, CpuMs, Larger > 0.0 ? (Diff / Larger) * 100.0 : 0.0);
		}
		else
		{
			Bound = FString::Printf(TEXT("cpu-%s"), *CpuSide);
			Reason = FString::Printf(
				TEXT("The %s thread at %.2f ms against the GPU at %.2f ms, a %.0f%% lead. The frame waits on that thread."),
				*CpuSide, CpuMs, GpuMs, Larger > 0.0 ? (Diff / Larger) * 100.0 : 0.0);
		}
	}
	Result->SetStringField(TEXT("bound"), Bound);
	Result->SetStringField(TEXT("verdict"), Reason);
	Result->SetStringField(TEXT("busiestCpuThread"), CpuSide);
	Result->SetNumberField(TEXT("busiestCpuMs"), CpuMs);
	Result->SetNumberField(TEXT("cpuGpuMarginPercent"), MarginPercent);
	Result->SetStringField(TEXT("sampleWindow"), bHaveGpu
		? FString::Printf(TEXT("CPU: the last completed frame. GPU: %d frame(s) drained from the RHI's history since the previous call."), Gpu.Count)
		: TEXT("The last completed frame. A handler runs inside one tick and cannot advance frames to build a window."));
	if (Gpu.bDisjoint)
	{
		Result->SetStringField(TEXT("gpuSampleNote"),
			TEXT("The GPU history overflowed between calls, so some frames were dropped before they could be read. The average ")
			TEXT("covers what survived."));
	}

	// Conditions that make the numbers above untrustworthy, each with the
	// exact call that fixes it. This is where the background-throttle half of
	// the ticket lands: it is reported, and pointed at set_property, rather
	// than given a typed setter that would duplicate a working path.
	TArray<TSharedPtr<FJsonValue>> Warnings;
	auto AddWarning = [&Warnings](const FString& Text)
	{
		Warnings.Add(MakeShared<FJsonValueString>(Text));
	};

	const UEditorPerformanceSettings* PerfSettings = GetDefault<UEditorPerformanceSettings>();
	const bool bThrottleWhenBackground = PerfSettings && PerfSettings->bThrottleCPUWhenNotForeground != 0;
	// Read through the cvar rather than the settings object: the UPROPERTY
	// behind "Allow Editor (Slate) Throttling" is named bAllowSlateThrottling
	// and only exists on UE 5.8, while the cvar it drives has been
	// Slate.bAllowThrottling on every engine in the supported range.
	const IConsoleVariable* SlateThrottleVar =
		IConsoleManager::Get().FindConsoleVariable(TEXT("Slate.bAllowThrottling"));
	const bool bSlateThrottle = SlateThrottleVar && SlateThrottleVar->GetInt() != 0;
	Result->SetBoolField(TEXT("throttleCPUWhenNotForeground"), bThrottleWhenBackground);
	Result->SetBoolField(TEXT("allowSlateThrottling"), bSlateThrottle);
	Result->SetBoolField(TEXT("slateThrottleCVarFound"), SlateThrottleVar != nullptr);
	Result->SetStringField(TEXT("editorPerformanceSettingsPath"),
		TEXT("/Script/UnrealEd.Default__EditorPerformanceSettings"));

	if (bThrottleWhenBackground)
	{
		AddWarning(TEXT("bThrottleCPUWhenNotForeground is ON. An agent-driven editor is unfocused by definition, so the editor ")
			TEXT("is running at a few frames per second and every number above is a measurement of the throttle rather than of ")
			TEXT("the scene. Turn it off with editor(set_property, objectPath=\"/Script/UnrealEd.Default__EditorPerformanceSettings\", ")
			TEXT("propertyName=\"bThrottleCPUWhenNotForeground\", value=false) - it is a plain UPROPERTY, which is why there is no ")
			TEXT("typed setter for it."));
	}
	if (bSlateThrottle)
	{
		AddWarning(TEXT("Slate throttling is ON, so the editor stops redrawing during UI interaction and the frame time reflects ")
			TEXT("idle rather than work. Turn it off with editor(set_cvars, cvars={\"Slate.bAllowThrottling\": 0}), which is the ")
			TEXT("same switch the Editor Preferences checkbox drives."));
	}
	if (!bHaveGpu)
	{
		AddWarning(TEXT("No GPU timing was available this call, so the verdict is 'unknown' rather than a guess."));
	}
	if (GEditor && GEditor->PlayWorld == nullptr && !bHaveGpu)
	{
		AddWarning(TEXT("Nothing is playing. Editor viewport timings measure the editor, not the game. For game numbers, run PIE ")
			TEXT("with editor(play_in_editor) or a separate process with editor(launch_standalone)."));
	}
	Result->SetArrayField(TEXT("warnings"), Warnings);
	Result->SetNumberField(TEXT("warningCount"), Warnings.Num());

	// GEngine's rolling averages are only fed while the stat unit overlay is
	// drawing, so they are reported with that condition attached rather than
	// as if they were always live.
	if (GEngine)
	{
		TArray<float> AverageTimes;
		GEngine->GetAverageUnitTimes(AverageTimes);
		if (AverageTimes.Num() >= 4)
		{
			TSharedPtr<FJsonObject> Averages = MakeShared<FJsonObject>();
			Averages->SetNumberField(TEXT("frameMs"), AverageTimes[0]);
			Averages->SetNumberField(TEXT("renderThreadMs"), AverageTimes[1]);
			Averages->SetNumberField(TEXT("gameThreadMs"), AverageTimes[2]);
			Averages->SetNumberField(TEXT("gpuMs"), AverageTimes[3]);
			if (AverageTimes.Num() >= 5) Averages->SetNumberField(TEXT("rhiThreadMs"), AverageTimes[4]);
			Result->SetObjectField(TEXT("statUnitAverages"), Averages);
			Result->SetStringField(TEXT("statUnitAveragesNote"),
				TEXT("These are the engine's own rolling averages, and they are only updated while the 'stat unit' overlay is ")
				TEXT("drawing. They read as zero otherwise. Turn the overlay on with editor(run_stat, name=\"unit\")."));
		}
	}

	Result->SetNumberField(TEXT("frame"), static_cast<double>(GFrameCounter));
	MCPProfilingWriteOpenRegions(Result);
	Result->SetBoolField(TEXT("tracing"), FTraceAuxiliary::IsConnected());
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// trigger_hitch -- stall the game thread on purpose.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::TriggerHitch(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	// Bounded hard. This blocks the game thread, which is the same thread the
	// bridge answers on, so an unbounded value would make the editor look hung
	// to every other client and time the call out.
	constexpr double MaxHitchMs = 5000.0;
	const double RequestedMs = OptionalNumber(Params, TEXT("hitchMilliseconds"), 250.0);
	if (!FMath::IsFinite(RequestedMs) || RequestedMs <= 0.0)
	{
		return MCPError(TEXT("hitchMilliseconds must be a finite number greater than 0."));
	}
	if (RequestedMs > MaxHitchMs)
	{
		return MCPError(FString::Printf(
			TEXT("hitchMilliseconds must be at most %.0f. This call blocks the game thread, which is the thread the bridge ")
			TEXT("answers on, so a longer stall would time the call out and make the editor look hung."), MaxHitchMs));
	}

	const bool bBookmark = OptionalBool(Params, TEXT("bookmark"), true);
	const FString Label = FString::Printf(TEXT("MCP injected hitch %.0f ms"), RequestedMs);

	if (bBookmark)
	{
		TRACE_BOOKMARK(TEXT("%s"), *Label);
	}

	const double Before = FPlatformTime::Seconds();
#if UE_MCP_HAS_TRACE_REGIONS
	const uint64 RegionId = FMiscTrace::OutputBeginRegionWithId(TEXT("MCP_InjectedHitch"), TEXT("UE_MCP"));
#endif
	FPlatformProcess::Sleep(static_cast<float>(RequestedMs / 1000.0));
#if UE_MCP_HAS_TRACE_REGIONS
	FMiscTrace::OutputEndRegionWithId(RegionId);
#endif
	const double ActualMs = (FPlatformTime::Seconds() - Before) * 1000.0;

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetNumberField(TEXT("requestedMs"), RequestedMs);
	Result->SetNumberField(TEXT("actualMs"), ActualMs);
	// Always true, and stated rather than omitted: this call always stalls the
	// thread, so unlike most mutations there is no state it can find already in
	// place and skip. Two calls produce two hitches, which is the point.
	Result->SetBoolField(TEXT("changed"), true);
	Result->SetBoolField(TEXT("bookmark"), bBookmark);
	Result->SetBoolField(TEXT("tracing"), FTraceAuxiliary::IsConnected());
	Result->SetStringField(TEXT("bookmarkName"), Label);
	Result->SetStringField(TEXT("purpose"),
		TEXT("A known-size stall on the game thread, so hitch-detection logic can be checked against a hitch whose duration is ")
		TEXT("known in advance rather than against one that has to be provoked. Sleeping does not consume CPU, so this shows up as ")
		TEXT("a long frame rather than as game-thread work."));
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Time that has passed cannot be given back. There is no inverse action, and the frame this stalled is already recorded."));
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// launch_standalone_game -- run the project as a separate -game process.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::LaunchStandaloneGame(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FMCPProfilingStandalone& State = MCPProfilingStandalone();

	if (MCPProfilingStandaloneIsRunning())
	{
		TSharedPtr<FJsonObject> Existing = MCPSuccess();
		MCPSetExisted(Existing);
		Existing->SetBoolField(TEXT("launched"), false);
		Existing->SetBoolField(TEXT("alreadyRunning"), true);
		Existing->SetNumberField(TEXT("processId"), static_cast<double>(State.ProcessId));
		Existing->SetStringField(TEXT("commandLine"), State.CommandLine);
		if (!State.TraceFile.IsEmpty()) MCPProfilingDescribeTraceFile(Existing, State.TraceFile);
		Existing->SetStringField(TEXT("note"),
			TEXT("A standalone run launched by this bridge is already going, so a second one was not started. ")
			TEXT("Stop it with editor(stop_standalone) first, or leave it running and read editor(get_standalone_status)."));
		return MCPResult(Existing);
	}

	const FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	if (ProjectPath.IsEmpty() || !FPaths::FileExists(ProjectPath))
	{
		return MCPError(TEXT("No .uproject file path is available, so there is nothing to launch. This action needs a project on disk."));
	}

	const FString ExePath = FString(FPlatformProcess::ExecutablePath());
	if (ExePath.IsEmpty() || !FPaths::FileExists(ExePath))
	{
		return MCPError(TEXT("Could not resolve the running editor executable, so there is nothing to launch a standalone run with."));
	}

	FString MapName = OptionalString(Params, TEXT("mapName"));
	MapName.TrimStartAndEndInline();

	FString Args = FString::Printf(TEXT("\"%s\""), *ProjectPath);
	if (!MapName.IsEmpty()) Args += FString::Printf(TEXT(" %s"), *MapName);
	Args += TEXT(" -game");

	if (OptionalBool(Params, TEXT("windowed"), true))
	{
		Args += TEXT(" -windowed");
		const int32 ResX = OptionalInt(Params, TEXT("resX"), 1280);
		const int32 ResY = OptionalInt(Params, TEXT("resY"), 720);
		if (ResX <= 0 || ResY <= 0)
		{
			return MCPError(TEXT("resX and resY must both be greater than 0."));
		}
		Args += FString::Printf(TEXT(" -ResX=%d -ResY=%d"), ResX, ResY);
	}

	// Tracing a standalone run is the point of launching one: an editor
	// process profiles the editor, and the frame times a game actually gets
	// are only visible in a -game process.
	FString Channels = MCPProfilingReadChannelList(Params, TEXT("channels"));
	FString TraceFile = OptionalString(Params, TEXT("traceFile"));
	TraceFile.TrimStartAndEndInline();
	if (!Channels.IsEmpty() || !TraceFile.IsEmpty())
	{
		if (Channels.IsEmpty()) Channels = TEXT("default");
		if (TraceFile.IsEmpty())
		{
			TraceFile = MCPProfilingDefaultTraceFile();
		}
		else
		{
			TraceFile = FPaths::ConvertRelativePathToFull(TraceFile);
			if (!TraceFile.EndsWith(TEXT(".utrace"))) TraceFile += TEXT(".utrace");
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(TraceFile), /*Tree=*/true);
		}
		Args += FString::Printf(TEXT(" -trace=%s -tracefile=\"%s\""), *Channels, *TraceFile);
	}

	const FString ExtraArgs = OptionalString(Params, TEXT("extraArgs"));
	if (!ExtraArgs.IsEmpty()) Args += TEXT(" ") + ExtraArgs;

	uint32 ProcessId = 0;
	FProcHandle Handle = FPlatformProcess::CreateProc(
		*ExePath, *Args,
		/*bLaunchDetached=*/true, /*bLaunchHidden=*/false, /*bLaunchReallyHidden=*/false,
		&ProcessId, /*PriorityModifier=*/0, /*OptionalWorkingDirectory=*/nullptr,
		/*PipeWriteChild=*/nullptr);

	if (!Handle.IsValid())
	{
		return MCPError(FString::Printf(
			TEXT("Failed to launch the standalone process. Executable: '%s'. Arguments: %s"), *ExePath, *Args));
	}

	State.Handle = Handle;
	State.ProcessId = ProcessId;
	State.CommandLine = FString::Printf(TEXT("\"%s\" %s"), *ExePath, *Args);
	State.ExecutablePath = ExePath;
	State.TraceFile = TraceFile;
	State.Channels = Channels;
	State.MapName = MapName;
	State.LaunchedAt = FDateTime::Now();
	State.bLaunched = true;

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetBoolField(TEXT("launched"), true);
	Result->SetBoolField(TEXT("alreadyRunning"), false);
	Result->SetNumberField(TEXT("processId"), static_cast<double>(ProcessId));
	Result->SetStringField(TEXT("executablePath"), ExePath);
	Result->SetStringField(TEXT("commandLine"), State.CommandLine);
	Result->SetStringField(TEXT("projectPath"), ProjectPath);
	if (!MapName.IsEmpty()) Result->SetStringField(TEXT("mapName"), MapName);
	if (!Channels.IsEmpty()) Result->SetStringField(TEXT("channels"), Channels);
	if (!TraceFile.IsEmpty())
	{
		MCPProfilingDescribeTraceFile(Result, TraceFile);
		Result->SetStringField(TEXT("traceNote"),
			TEXT("The child process writes this file and only closes it on exit, so it stays incomplete until ")
			TEXT("editor(stop_standalone) or the process quits on its own."));
	}
	else
	{
		Result->SetStringField(TEXT("traceNote"),
			TEXT("No trace was requested. Pass channels (and optionally traceFile) to have the standalone process record one; ")
			TEXT("that is the only way to get frame times from a real -game process rather than from the editor."));
	}
	Result->SetStringField(TEXT("note"),
		TEXT("The process runs detached and the bridge does not read its output. Poll editor(get_standalone_status) for liveness ")
		TEXT("and exit code. Only one bridge-launched standalone run is tracked at a time."));

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	MCPSetRollback(Result, TEXT("stop_standalone_game"), Payload);
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// get_standalone_status -- is the launched process still alive.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::GetStandaloneStatus(const TSharedPtr<FJsonObject>& Params)
{
	FMCPProfilingStandalone& State = MCPProfilingStandalone();

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetBoolField(TEXT("launched"), State.bLaunched);

	if (!State.bLaunched)
	{
		Result->SetBoolField(TEXT("running"), false);
		Result->SetStringField(TEXT("note"),
			TEXT("This bridge has not launched a standalone run in this editor session. A run started outside the bridge is not ")
			TEXT("tracked here. Start one with editor(launch_standalone)."));
		return MCPResult(Result);
	}

	const bool bRunning = MCPProfilingStandaloneIsRunning();
	Result->SetBoolField(TEXT("running"), bRunning);
	Result->SetNumberField(TEXT("processId"), static_cast<double>(State.ProcessId));
	Result->SetStringField(TEXT("commandLine"), State.CommandLine);
	Result->SetStringField(TEXT("launchedAt"), State.LaunchedAt.ToIso8601());
	Result->SetNumberField(TEXT("uptimeSeconds"),
		(FDateTime::Now() - State.LaunchedAt).GetTotalSeconds());
	if (!State.MapName.IsEmpty()) Result->SetStringField(TEXT("mapName"), State.MapName);
	if (!State.Channels.IsEmpty()) Result->SetStringField(TEXT("channels"), State.Channels);
	if (!State.TraceFile.IsEmpty()) MCPProfilingDescribeTraceFile(Result, State.TraceFile);

	if (!bRunning)
	{
		int32 ReturnCode = 0;
		if (FPlatformProcess::GetProcReturnCode(State.Handle, &ReturnCode))
		{
			Result->SetNumberField(TEXT("exitCode"), ReturnCode);
		}
		Result->SetStringField(TEXT("note"),
			TEXT("The process has exited. Its .utrace, if it wrote one, is complete and can be opened with the reported openCommand."));
	}
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// stop_standalone_game -- terminate the launched process.
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FEditorHandlers::StopStandaloneGame(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	FMCPProfilingStandalone& State = MCPProfilingStandalone();

	if (!MCPProfilingStandaloneIsRunning())
	{
		TSharedPtr<FJsonObject> NoOp = MCPSuccess();
		NoOp->SetBoolField(TEXT("wasRunning"), false);
		NoOp->SetBoolField(TEXT("stopped"), false);
		NoOp->SetBoolField(TEXT("unchanged"), true);
		if (State.bLaunched)
		{
			NoOp->SetNumberField(TEXT("processId"), static_cast<double>(State.ProcessId));
			if (!State.TraceFile.IsEmpty()) MCPProfilingDescribeTraceFile(NoOp, State.TraceFile);
			NoOp->SetStringField(TEXT("note"),
				TEXT("The standalone process had already exited, so nothing was stopped. Replaying a stop is safe."));
		}
		else
		{
			NoOp->SetStringField(TEXT("note"),
				TEXT("This bridge has not launched a standalone run, so there is nothing to stop. It only stops processes it started; ")
				TEXT("a run launched another way has to be closed the way it was opened."));
		}
		if (State.bLaunched && State.Handle.IsValid())
		{
			FPlatformProcess::CloseProc(State.Handle);
			State.Handle = FProcHandle();
			State.bLaunched = false;
		}
		return MCPResult(NoOp);
	}

	const uint32 ProcessId = State.ProcessId;
	const FString TraceFile = State.TraceFile;
	const FString CommandLine = State.CommandLine;

	// KillTree=true, because a -game launch can spawn helper processes and
	// leaving one holding the .utrace open means the file never finishes.
	FPlatformProcess::TerminateProc(State.Handle, /*KillTree=*/true);
	FPlatformProcess::CloseProc(State.Handle);
	State.Handle = FProcHandle();
	State.bLaunched = false;

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetBoolField(TEXT("wasRunning"), true);
	Result->SetBoolField(TEXT("stopped"), true);
	Result->SetNumberField(TEXT("processId"), static_cast<double>(ProcessId));
	Result->SetStringField(TEXT("commandLine"), CommandLine);
	if (!TraceFile.IsEmpty()) MCPProfilingDescribeTraceFile(Result, TraceFile);
	Result->SetStringField(TEXT("note"),
		TEXT("The process was terminated rather than asked to quit, so anything it had not flushed is gone. A trace it was writing ")
		TEXT("is usually still readable up to the last flushed block."));

	// Relaunching is not an inverse: a new process is a new run with a new
	// trace, and the state the terminated one had reached is not recoverable.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Terminating a process cannot be undone. editor(launch_standalone) starts a NEW run rather than restoring this one."));
	return MCPResult(Result);
}

// Undefined at the end of the translation unit: the module is a unity build,
// so a file-scope macro would otherwise leak into every .cpp compiled after
// this one in the same blob.
#undef UE_MCP_HAS_TRACE_REGIONS
