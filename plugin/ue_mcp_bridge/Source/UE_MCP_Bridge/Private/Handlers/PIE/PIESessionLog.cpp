#include "Handlers/PIE/PIESessionLog.h"
#include "Handlers/PIE/PIELog.h"
#include "Editor.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "CoreGlobals.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UObjectGlobals.h"
#include "Blueprint/BlueprintExceptionInfo.h" // full FBlueprintExceptionInfo + EBlueprintExceptionType (Script.h only forward-declares)

namespace MCPPIE
{
	namespace
	{
		ELogVerbosity::Type Masked(ELogVerbosity::Type V)
		{
			return static_cast<ELogVerbosity::Type>(V & ELogVerbosity::VerbosityMask);
		}

		const TCHAR* VerbosityToString(ELogVerbosity::Type V)
		{
			switch (Masked(V))
			{
			case ELogVerbosity::Fatal:   return TEXT("Fatal");
			case ELogVerbosity::Error:   return TEXT("Error");
			case ELogVerbosity::Warning: return TEXT("Warning");
			case ELogVerbosity::Display: return TEXT("Display");
			case ELogVerbosity::Log:     return TEXT("Log");
			case ELogVerbosity::Verbose: return TEXT("Verbose");
			default:                     return TEXT("VeryVerbose");
			}
		}

		bool IsErrorOrWarning(ELogVerbosity::Type V)
		{
			const ELogVerbosity::Type M = Masked(V);
			return M >= ELogVerbosity::Fatal && M <= ELogVerbosity::Warning;
		}
	}

	FPIESessionLog& FPIESessionLog::Get()
	{
		static FPIESessionLog Instance;
		return Instance;
	}

	void FPIESessionLog::Init()
	{
		if (bInitialised) return;
		bInitialised = true;

		if (GLog && !bRegisteredOnLog)
		{
			GLog->AddOutputDevice(this);
			bRegisteredOnLog = true;
		}

		BeginPIEHandle = FEditorDelegates::BeginPIE.AddRaw(this, &FPIESessionLog::OnBeginPIE);
		EndPIEHandle = FEditorDelegates::EndPIE.AddRaw(this, &FPIESessionLog::OnEndPIE);
		ScriptExceptionHandle = FBlueprintCoreDelegates::OnScriptException.AddRaw(this, &FPIESessionLog::OnScriptException);
	}

	void FPIESessionLog::Shutdown()
	{
		if (!bInitialised) return;
		bInitialised = false;

		if (bSessionActive)
		{
			EndSession();
		}

		FEditorDelegates::BeginPIE.Remove(BeginPIEHandle);
		FEditorDelegates::EndPIE.Remove(EndPIEHandle);
		FBlueprintCoreDelegates::OnScriptException.Remove(ScriptExceptionHandle);

		if (GLog && bRegisteredOnLog)
		{
			GLog->RemoveOutputDevice(this);
			bRegisteredOnLog = false;
		}
	}

	void FPIESessionLog::SetNextSessionDir(const FString& Dir)
	{
		FScopeLock SL(&Lock);
		PendingDir = Dir;
	}

	void FPIESessionLog::OnBeginPIE(bool /*bIsSimulating*/)
	{
		FString Dir;
		{
			FScopeLock SL(&Lock);
			Dir = PendingDir;
			PendingDir.Reset();
		}
		if (Dir.IsEmpty())
		{
			Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("MCPSessions") /
				FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
		}
		BeginSession(Dir);
	}

	void FPIESessionLog::OnEndPIE(bool /*bIsSimulating*/)
	{
		EndSession();
	}

	void FPIESessionLog::BeginSession(const FString& OutputDir)
	{
		FScopeLock SL(&Lock);
		// If a session is already open, finalise it in place first (write to disk)
		// without recursing; keep the buffers for the new session fresh.
		if (bSessionActive)
		{
			WriteArtifacts(ActiveDir);
			LastDir = ActiveDir;
		}
		IFileManager::Get().MakeDirectory(*OutputDir, true);
		ActiveDir = OutputDir;
		bSessionActive = true;
		SessionStartSeconds = FPlatformTime::Seconds();
		RingLines.Reset();
		ErrorLines.Reset();
	}

	FString FPIESessionLog::EndSession()
	{
		FScopeLock SL(&Lock);
		if (!bSessionActive) return FString();
		WriteArtifacts(ActiveDir);
		LastDir = ActiveDir;
		bSessionActive = false;
		ActiveDir.Reset();
		return LastDir;
	}

	bool FPIESessionLog::IsActive() const
	{
		FScopeLock SL(&Lock);
		return bSessionActive;
	}

	FString FPIESessionLog::GetActiveDir() const
	{
		FScopeLock SL(&Lock);
		return ActiveDir;
	}

	FString FPIESessionLog::GetLastDir() const
	{
		FScopeLock SL(&Lock);
		return bSessionActive ? ActiveDir : LastDir;
	}

	void FPIESessionLog::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category)
	{
		// Only capture while a PIE session is open. Cheap early-out with no lock:
		// bSessionActive is a plain bool but a stale read only drops/keeps a line
		// at the exact session boundary, which is harmless.
		if (!bSessionActive) return;
		const ELogVerbosity::Type M = Masked(Verbosity);
		if (M == ELogVerbosity::NoLogging) return;

		FScopeLock SL(&Lock);
		if (!bSessionActive) return;
		RecordLine(FPlatformTime::Seconds() - SessionStartSeconds, Verbosity, Category, FString(V));
	}

	void FPIESessionLog::RecordLine(double Time, ELogVerbosity::Type Verbosity, const FName& Category, const FString& Message)
	{
		// Caller holds Lock.
		FLogLine Line;
		Line.Time = Time;
		Line.Frame = GFrameCounter;
		Line.Verbosity = Masked(Verbosity);
		Line.Category = Category;
		Line.Message = Message;

		if (RingLines.Num() >= kMaxRingLines)
		{
			// Drop the oldest 10% in one shot to avoid per-line shifts.
			RingLines.RemoveAt(0, kMaxRingLines / 10, EAllowShrinking::No);
		}
		RingLines.Add(Line);

		if (IsErrorOrWarning(Verbosity))
		{
			ErrorLines.Add(MoveTemp(Line));
		}
	}

	void FPIESessionLog::OnScriptException(const UObject* Object, const FFrame& Stack, const FBlueprintExceptionInfo& Info)
	{
		if (!bSessionActive) return;

		const EBlueprintExceptionType::Type Type = Info.GetType();
		// Only the genuinely bad ones; breakpoints/tracepoints are not bugs.
		if (Type != EBlueprintExceptionType::AccessViolation &&
			Type != EBlueprintExceptionType::InfiniteLoop &&
			Type != EBlueprintExceptionType::FatalError &&
			Type != EBlueprintExceptionType::NonFatalError &&
			Type != EBlueprintExceptionType::AbortExecution)
		{
			return;
		}

		FString NodeName;
		if (Stack.Node)
		{
			NodeName = Stack.Node->GetName();
		}
		const FString ObjPath = Object ? Object->GetPathName() : TEXT("<null>");
		const FString Msg = FString::Printf(
			TEXT("[BlueprintException type=%d] %s | object=%s node=%s"),
			static_cast<int32>(Type),
			*Info.GetDescription().ToString(),
			*ObjPath,
			*NodeName);

		FScopeLock SL(&Lock);
		if (!bSessionActive) return;
		RecordLine(FPlatformTime::Seconds() - SessionStartSeconds, ELogVerbosity::Error, TEXT("ScriptException"), Msg);
	}

	TSharedRef<FJsonObject> FPIESessionLog::SummaryFromLines(const TArray<FLogLine>& Lines) const
	{
		// Dedupe by (category, verbosity, message). Preserve first-seen order for
		// stable output; carry count + first/last frame.
		struct FAgg
		{
			FLogLine Sample;
			int32 Count = 0;
			uint64 FirstFrame = 0;
			uint64 LastFrame = 0;
			double FirstTime = 0.0;
		};
		TMap<FString, FAgg> Aggs;
		TArray<FString> Order;
		int32 ErrorCount = 0, WarningCount = 0;

		for (const FLogLine& L : Lines)
		{
			if (Masked(L.Verbosity) == ELogVerbosity::Warning) WarningCount++;
			else ErrorCount++;

			const FString Key = FString::Printf(TEXT("%s|%s|%s"),
				VerbosityToString(L.Verbosity), *L.Category.ToString(), *L.Message);
			FAgg* Existing = Aggs.Find(Key);
			if (!Existing)
			{
				FAgg A;
				A.Sample = L;
				A.Count = 1;
				A.FirstFrame = L.Frame;
				A.LastFrame = L.Frame;
				A.FirstTime = L.Time;
				Aggs.Add(Key, A);
				Order.Add(Key);
			}
			else
			{
				Existing->Count++;
				Existing->LastFrame = L.Frame;
			}
		}

		// Sort issues: errors before warnings, then by count desc.
		Order.Sort([&Aggs](const FString& A, const FString& B)
		{
			const FAgg& FA = Aggs[A];
			const FAgg& FB = Aggs[B];
			const int32 VA = Masked(FA.Sample.Verbosity);
			const int32 VB = Masked(FB.Sample.Verbosity);
			if (VA != VB) return VA < VB;          // Fatal/Error (lower) first
			return FA.Count > FB.Count;
		});

		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("error_count"), ErrorCount);
		Root->SetNumberField(TEXT("warning_count"), WarningCount);
		Root->SetNumberField(TEXT("unique_issues"), Order.Num());

		TArray<TSharedPtr<FJsonValue>> Issues;
		for (const FString& Key : Order)
		{
			const FAgg& A = Aggs[Key];
			TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
			J->SetStringField(TEXT("verbosity"), VerbosityToString(A.Sample.Verbosity));
			J->SetStringField(TEXT("category"), A.Sample.Category.ToString());
			J->SetStringField(TEXT("message"), A.Sample.Message);
			J->SetNumberField(TEXT("count"), A.Count);
			J->SetNumberField(TEXT("first_frame"), static_cast<double>(A.FirstFrame));
			J->SetNumberField(TEXT("last_frame"), static_cast<double>(A.LastFrame));
			J->SetNumberField(TEXT("first_time"), A.FirstTime);
			Issues.Add(MakeShared<FJsonValueObject>(J));
		}
		Root->SetArrayField(TEXT("issues"), Issues);
		return Root;
	}

	TSharedPtr<FJsonObject> FPIESessionLog::BuildErrorSummary(ELogVerbosity::Type MinVerbosity) const
	{
		FScopeLock SL(&Lock);
		const ELogVerbosity::Type Floor = Masked(MinVerbosity);
		TArray<FLogLine> Filtered;
		Filtered.Reserve(ErrorLines.Num());
		for (const FLogLine& L : ErrorLines)
		{
			if (Masked(L.Verbosity) <= Floor) Filtered.Add(L);
		}
		return SummaryFromLines(Filtered);
	}

	TArray<FPIESessionLog::FLogLine> FPIESessionLog::QueryLines(
		ELogVerbosity::Type MinVerbosity,
		const FName& CategoryFilter,
		const FString& Contains,
		int32 Offset,
		int32 Limit) const
	{
		FScopeLock SL(&Lock);
		const ELogVerbosity::Type Floor = Masked(MinVerbosity);
		TArray<FLogLine> Out;
		int32 Skipped = 0;
		for (const FLogLine& L : RingLines)
		{
			if (Masked(L.Verbosity) > Floor) continue;
			if (!CategoryFilter.IsNone() && L.Category != CategoryFilter) continue;
			if (!Contains.IsEmpty() && !L.Message.Contains(Contains)) continue;
			if (Skipped < Offset) { Skipped++; continue; }
			Out.Add(L);
			if (Out.Num() >= Limit) break;
		}
		return Out;
	}

	void FPIESessionLog::WriteArtifacts(const FString& Dir) const
	{
		// Caller holds Lock.
		if (Dir.IsEmpty()) return;

		// session_log.jsonl: one JSON object per retained line.
		FString JsonL;
		JsonL.Reserve(RingLines.Num() * 96);
		for (const FLogLine& L : RingLines)
		{
			FString LineStr;
			TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&LineStr);
			W->WriteObjectStart();
			W->WriteValue(TEXT("time"), L.Time);
			W->WriteValue(TEXT("frame"), static_cast<double>(L.Frame));
			W->WriteValue(TEXT("verbosity"), VerbosityToString(L.Verbosity));
			W->WriteValue(TEXT("category"), L.Category.ToString());
			W->WriteValue(TEXT("message"), L.Message);
			W->WriteObjectEnd();
			W->Close();
			JsonL += LineStr;
			JsonL += TEXT("\n");
		}
		FFileHelper::SaveStringToFile(JsonL, *(Dir / TEXT("session_log.jsonl")));

		// session_errors.json: deduped errors + warnings.
		const TSharedRef<FJsonObject> Summary = SummaryFromLines(ErrorLines);
		FString SummaryStr;
		TSharedRef<TJsonWriter<>> SW = TJsonWriterFactory<>::Create(&SummaryStr);
		FJsonSerializer::Serialize(Summary, SW);
		FFileHelper::SaveStringToFile(SummaryStr, *(Dir / TEXT("session_errors.json")));
	}
}
