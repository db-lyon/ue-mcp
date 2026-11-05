#pragma once

#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"
#include "HAL/CriticalSection.h"
#include "Dom/JsonObject.h"
#include "UObject/Script.h"

namespace MCPPIE
{
	// Captures the Output Log for the lifetime of a PIE session and distils it
	// into two artifacts an agent can read: session_log.jsonl (every retained
	// line) and session_errors.json (deduped errors/warnings with counts and a
	// context window). This is the highest-leverage debugging signal and works
	// for every bug, not just input-deterministic ones.
	//
	// Registered on GLog as an FOutputDevice, so it sees log lines from any
	// thread. It also hooks Blueprint script exceptions (null access, etc.),
	// which do not always surface as Error-verbosity log lines.
	//
	// Module-owned singleton. Attaches automatically on BeginPIE and finalises on
	// EndPIE; other subsystems can co-locate output by calling SetNextSessionDir
	// before PIE starts.
	class FPIESessionLog : public FOutputDevice
	{
	public:
		static FPIESessionLog& Get();

		void Init();       // hook editor PIE delegates + register on GLog
		void Shutdown();   // unregister, finalise any open session

		// Redirect the NEXT BeginPIE-triggered session to this directory instead
		// of the default Saved/MCPSessions/<timestamp>/. Cleared once consumed.
		void SetNextSessionDir(const FString& Dir);

		// Begin/end are normally driven by PIE delegates but are public so tests
		// (and manual callers) can drive them directly.
		void BeginSession(const FString& OutputDir);
		FString EndSession();   // returns the finalised directory ("" if none)

		bool IsActive() const;
		FString GetActiveDir() const;
		FString GetLastDir() const;

		struct FLogLine
		{
			double Time = 0.0;
			uint64 Frame = 0;
			ELogVerbosity::Type Verbosity = ELogVerbosity::Log;
			FName Category;
			FString Message;
		};

		// Deduped error/warning summary for a session. If Dir is empty, uses the
		// active-or-last in-memory session; otherwise reads the on-disk
		// session_errors.json from Dir. MinVerbosity: Error=1, Warning=2 gate.
		TSharedPtr<FJsonObject> BuildErrorSummary(ELogVerbosity::Type MinVerbosity) const;

		// Filtered raw lines from the active/last in-memory session.
		TArray<FLogLine> QueryLines(
			ELogVerbosity::Type MinVerbosity,
			const FName& CategoryFilter,
			const FString& Contains,
			int32 Offset,
			int32 Limit) const;

		// FOutputDevice
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override;
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

	private:
		void OnBeginPIE(bool bIsSimulating);
		void OnEndPIE(bool bIsSimulating);
		void OnScriptException(const UObject* Object, const FFrame& Stack, const FBlueprintExceptionInfo& Info);
		void RecordLine(double Time, ELogVerbosity::Type Verbosity, const FName& Category, const FString& Message);
		TSharedRef<FJsonObject> SummaryFromLines(const TArray<FLogLine>& Lines) const;
		void WriteArtifacts(const FString& Dir) const;

		// Cap on retained all-verbosity lines; oldest dropped past this. Errors and
		// warnings are ALSO retained separately so the summary stays complete even
		// if the ring wraps.
		static constexpr int32 kMaxRingLines = 200000;

		mutable FCriticalSection Lock;
		bool bSessionActive = false;
		FString ActiveDir;
		FString LastDir;
		FString PendingDir;             // set by SetNextSessionDir
		double SessionStartSeconds = 0.0;

		TArray<FLogLine> RingLines;     // bounded, all verbosities
		TArray<FLogLine> ErrorLines;    // unbounded, Error+Warning only

		bool bInitialised = false;
		bool bRegisteredOnLog = false;
		FDelegateHandle BeginPIEHandle;
		FDelegateHandle EndPIEHandle;
		FDelegateHandle ScriptExceptionHandle;
	};
}
