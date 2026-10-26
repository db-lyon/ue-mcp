#include "Handlers/PIE/PIEUatRunner.h"

#include "Handlers/PIE/PIELog.h"
#include "MCPPIEInputInjector.h"
#include "Handlers/PIE/PIERemotePlayers.h"
#include "Handlers/PIE/PIEStepActions.h"
#include "MCPPIERemoteControl.h"
#include "MCPPIEViewportCapture.h"
#include "SceneViewExtension.h"
#include "HAL/FileManager.h"
#include "Containers/Ticker.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "HandlerWorld.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformFileManager.h"
#include "InputAction.h"
#include "Internationalization/Regex.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Algo/AllOf.h"

#if UE_ENABLE_ICU
THIRD_PARTY_INCLUDES_START
#include <unicode/regex.h>
THIRD_PARTY_INCLUDES_END
#endif

namespace MCPPIE
{
	namespace
	{
		// ── Evidence ────────────────────────────────────────────────────────

		/** Every line the editor process logs while a run is active. */
		class FHostLogCapture : public FOutputDevice
		{
		public:
			virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
			{
				// Same shape as a log file line, so one pattern reads host and client logs alike.
				const ELogVerbosity::Type Level = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
				FString Line;
				if (Category != NAME_None) Line = Category.ToString() + TEXT(": ");
				if (Level != ELogVerbosity::Log) Line += FString(ToString(Level)) + TEXT(": ");
				Line += V;
				FScopeLock Lock(&Mutex);
				Lines.Add(MoveTemp(Line));
			}
			virtual bool CanBeUsedOnAnyThread() const override { return true; }
			virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

			int32 Num() const { FScopeLock Lock(&Mutex); return Lines.Num(); }
			void CopyFrom(int32 Start, TArray<FString>& Out) const
			{
				FScopeLock Lock(&Mutex);
				for (int32 i = FMath::Max(Start, 0); i < Lines.Num(); ++i) { Out.Add(Lines[i]); }
			}
			void Reset() { FScopeLock Lock(&Mutex); Lines.Reset(); }

		private:
			mutable FCriticalSection Mutex;
			TArray<FString> Lines;
		};

		/** A separate-process client's log file, read as it grows. */
		struct FClientLogTail
		{
			FString Path;
			int64 Offset = 0;
			TArray<uint8> Partial;
			TArray<FString> Lines;

			void Begin()
			{
				// A client process writes a fresh log file, so the whole file belongs to this run.
				Offset = 0;
				Partial.Reset();
			}

			void Poll()
			{
				IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
				const int64 Size = PF.FileSize(*Path);
				if (Size < 0)
				{
					return;
				}
				if (Size < Offset)
				{
					Offset = 0; // the client restarted its log
				}
				if (Size == Offset)
				{
					return;
				}
				TUniquePtr<IFileHandle> Handle(PF.OpenRead(*Path, /*bAllowWrite*/ true));
				if (!Handle || !Handle->Seek(Offset))
				{
					return;
				}
				const int64 Count = Size - Offset;
				TArray<uint8> Bytes;
				Bytes.SetNumUninitialized(Count);
				if (!Handle->Read(Bytes.GetData(), Count))
				{
					return;
				}
				Offset = Size;
				// Decode whole lines only: a read can end inside a multi-byte character.
				Partial.Append(Bytes);
				int32 Start = 0;
				for (int32 i = 0; i < Partial.Num(); ++i)
				{
					if (Partial[i] != '\n') continue;
					FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Partial.GetData() + Start), i - Start);
					FString Line(Converted.Length(), Converted.Get());
					Line.TrimEndInline();
					Lines.Add(MoveTemp(Line));
					Start = i + 1;
				}
				Partial.RemoveAt(0, Start, EAllowShrinking::No);
			}
		};

		// ── Scenario model ──────────────────────────────────────────────────

		struct FUatRule
		{
			FString Log;      // "host" or "client:N"
			int32 Client = 0; // 0 = the host log
			FString Pattern;
			int32 Min = 1;
			int32 Max = -1;
			bool bSinceRun = false; // judge every line since the run began, not only this step's
		};

		struct FUatStep
		{
			FString Name;
			TArray<FStepAction> Do;
			TArray<FUatRule> Expect;
			TArray<FUatRule> Forbid;
			float WindowMs = 4000.0f;
		};

		struct FUatStepResult
		{
			FString Name;
			FString Verdict; // PASS | FAIL | SKIPPED
			TArray<FString> Details;
			TArray<FString> Captures; // images written during the step, relative to the report directory
		};

		/** FRegexPattern swallows a bad pattern and then matches nothing, so compile it with ICU first. */
		bool ValidateRegex(const FString& Pattern, FString& Err)
		{
#if UE_ENABLE_ICU
			UErrorCode Status = U_ZERO_ERROR;
			UParseError Parse;
			const FTCHARToUTF8 Utf8(*Pattern);
			const icu::UnicodeString Source = icu::UnicodeString::fromUTF8(icu::StringPiece(Utf8.Get(), Utf8.Length()));
			TUniquePtr<icu::RegexPattern> Compiled(icu::RegexPattern::compile(Source, 0, Parse, Status));
			if (U_FAILURE(Status))
			{
				Err = FString::Printf(TEXT("pattern /%s/ is not a valid regex: %s at offset %d"), *Pattern, UTF8_TO_TCHAR(u_errorName(Status)), Parse.offset);
				return false;
			}
#endif
			return true;
		}

		bool ParseRule(const TSharedPtr<FJsonObject>& O, FUatRule& Out, FString& Err)
		{
			if (!O->TryGetStringField(TEXT("pattern"), Out.Pattern) || Out.Pattern.IsEmpty())
			{
				Err = TEXT("an expect/forbid entry has no pattern");
				return false;
			}
			Out.Log = O->HasField(TEXT("log")) ? O->GetStringField(TEXT("log")) : TEXT("host");
			const FString Index = Out.Log.StartsWith(TEXT("client:")) ? Out.Log.RightChop(7) : FString();
			const bool bDigits = !Index.IsEmpty() && Index.Len() < 4 && Algo::AllOf(Index, [](TCHAR C) { return FChar::IsDigit(C); });
			if (Out.Log != TEXT("host") && !bDigits)
			{
				Err = FString::Printf(TEXT("log must be \"host\" or \"client:N\", not \"%s\""), *Out.Log);
				return false;
			}
			Out.Client = bDigits ? FCString::Atoi(*Index) : 0;
			if (!ValidateRegex(Out.Pattern, Err))
			{
				return false;
			}
			double N = 0;
			if (O->TryGetNumberField(TEXT("min"), N)) Out.Min = static_cast<int32>(N);
			if (O->TryGetNumberField(TEXT("max"), N)) Out.Max = static_cast<int32>(N);
			FString Since;
			if (O->TryGetStringField(TEXT("since"), Since))
			{
				if (!Since.Equals(TEXT("run"), ESearchCase::CaseSensitive) && !Since.Equals(TEXT("step"), ESearchCase::CaseSensitive))
				{
					Err = FString::Printf(TEXT("since must be \"step\" or \"run\", not \"%s\""), *Since);
					return false;
				}
				Out.bSinceRun = Since == TEXT("run");
			}
			return true;
		}
	}

	// ── Runner ──────────────────────────────────────────────────────────────

	struct FPIEUatRunner::FImpl
	{
		enum class EState { Idle, Starting, Joining, Settling, Acting, Watching, Stopping, Finished };

		EState State = EState::Idle;
		FString Name;
		int32 Players = 2;
		bool bListen = true;
		bool bOneProcess = false;
		float JoinTimeoutS = 180.0f;
		float SettleMs = 2000.0f;
		TArray<FUatStep> Steps;
		TArray<FUatStepResult> Results;
		FString Error;
		FString ReportDir;
		bool bAborted = false;

		// Progress.
		int32 StepIndex = 0;
		double PhaseStart = 0.0;
		int32 HostMark = 0;
		TMap<int32, int32> ClientMarks;
		// Runs each step's actions; its relayed requests must be confirmed applied and its captures written.
		FStepActionRunner Actions;
		FStepActionSequence Sequence;

		// Separate-process clients render offscreen (no window) at a fixed frame rate.
		bool bWindowless = false;
		int32 ClientFps = 60;

		// Evidence.
		FHostLogCapture HostLog;
		TMap<int32, FClientLogTail> ClientLogs;

		// Restored after the run.
		EPlayNetMode SavedNetMode = PIE_Standalone;
		int32 SavedClients = 1;
		bool SavedOneProcess = true;
		FString SavedLaunchParameters;

		FTSTicker::FDelegateHandle TickHandle;

		static double Now() { return FPlatformTime::Seconds(); }

		void Fail(const FString& Why)
		{
			Error = Why;
			UE_LOG(LogMCPPIE, Warning, TEXT("[UAT] %s: %s"), *Name, *Why);
			BeginStop();
		}

		bool ConfigureAndStartPIE()
		{
			ULevelEditorPlaySettings* PS = GetMutableDefault<ULevelEditorPlaySettings>();
			PS->GetPlayNetMode(SavedNetMode);
			PS->GetPlayNumberOfClients(SavedClients);
			PS->GetRunUnderOneProcess(SavedOneProcess);
			SavedLaunchParameters = PS->AdditionalLaunchParameters;

			PS->SetPlayNetMode(bListen ? PIE_ListenServer : PIE_Client);
			PS->SetPlayNumberOfClients(Players);
			PS->SetRunUnderOneProcess(bOneProcess);
			if (bWindowless && !bOneProcess)
			{
				// No window, so nothing takes focus; the frame rate is pinned because no vsync paces an offscreen client.
				// The engine reads only the first -ExecCmds, so join an existing one rather than adding a second.
				const FString Cmds = FString::Printf(TEXT("t.MaxFPS %d,t.IdleWhenNotForeground 0"), ClientFps);
				FString LaunchParams = SavedLaunchParameters;
				const int32 At = LaunchParams.Find(TEXT("-ExecCmds="));
				if (At == INDEX_NONE)
				{
					LaunchParams += FString::Printf(TEXT(" -ExecCmds=\"%s\""), *Cmds);
				}
				else
				{
					const int32 ValueAt = At + FCString::Strlen(TEXT("-ExecCmds="));
					if (LaunchParams.IsValidIndex(ValueAt) && LaunchParams[ValueAt] == TEXT('"'))
					{
						LaunchParams.InsertAt(ValueAt + 1, Cmds + TEXT(","));
					}
					else
					{
						// Unquoted: the value ends at the next space; quote it, since ours contains spaces.
						int32 End = ValueAt;
						while (LaunchParams.IsValidIndex(End) && !FChar::IsWhitespace(LaunchParams[End])) ++End;
						const FString Existing = LaunchParams.Mid(ValueAt, End - ValueAt);
						LaunchParams = LaunchParams.Left(ValueAt) + TEXT("\"") + Cmds + (Existing.IsEmpty() ? TEXT("") : TEXT(",")) + Existing + TEXT("\"") + LaunchParams.Mid(End);
					}
				}
				PS->AdditionalLaunchParameters = (LaunchParams + TEXT(" -RenderOffscreen")).TrimStart();
			}

			FRequestPlaySessionParams Params;
			GEditor->RequestPlaySession(Params);
			return true;
		}

		void RestoreSettings()
		{
			ULevelEditorPlaySettings* PS = GetMutableDefault<ULevelEditorPlaySettings>();
			PS->SetPlayNetMode(SavedNetMode);
			PS->SetPlayNumberOfClients(SavedClients);
			PS->SetRunUnderOneProcess(SavedOneProcess);
			PS->AdditionalLaunchParameters = SavedLaunchParameters;
		}

		int32 RemoteCount() const { return bListen ? Players - 1 : Players; }

		bool AllJoined() const
		{
			const TArray<PIERemotePlayers::FRemotePlayer> Remote = PIERemotePlayers::List();
			int32 Ready = 0;
			for (const PIERemotePlayers::FRemotePlayer& P : Remote)
			{
				Ready += P.bReady ? 1 : 0;
			}
			if (Ready < RemoteCount())
			{
				return false;
			}
			if (bListen)
			{
				UWorld* Server = PIERemotePlayers::FindServerWorld();
				APlayerController* Host = Server ? Server->GetFirstPlayerController() : nullptr;
				return Host && Host->IsLocalController() && Host->GetPawn();
			}
			return true;
		}

		// Each client names its own log file when it joins; join order and launch order differ.
		bool BeginClientLogs(FString& OutError)
		{
			ClientLogs.Reset();
			if (bOneProcess)
			{
				return true;
			}
			for (const PIERemotePlayers::FRemotePlayer& P : PIERemotePlayers::List())
			{
				if (P.LogFile.IsEmpty())
				{
					OutError = FString::Printf(TEXT("client %d (%s) reported no log file"), P.Client, *P.PlayerName);
					return false;
				}
				FClientLogTail& Tail = ClientLogs.Add(P.Client);
				Tail.Path = P.LogFile;
				Tail.Begin();
			}
			return true;
		}

		void PollClientLogs()
		{
			for (TPair<int32, FClientLogTail>& Pair : ClientLogs)
			{
				Pair.Value.Poll();
			}
		}

		// Lines in a log since the current step began.
		void LinesSinceMark(const FUatRule& Rule, TArray<FString>& Out) const
		{
			if (Rule.Client == 0 || bOneProcess)
			{
				HostLog.CopyFrom(Rule.bSinceRun ? 0 : HostMark, Out);
				return;
			}
			const FClientLogTail* Tail = ClientLogs.Find(Rule.Client);
			const int32* Mark = ClientMarks.Find(Rule.Client);
			if (Tail && Mark)
			{
				for (int32 i = Rule.bSinceRun ? 0 : *Mark; i < Tail->Lines.Num(); ++i) { Out.Add(Tail->Lines[i]); }
			}
		}

		static int32 CountMatches(const TArray<FString>& Lines, const FString& Pattern, TArray<FString>* Samples)
		{
			const FRegexPattern Regex(Pattern);
			int32 Count = 0;
			for (const FString& Line : Lines)
			{
				FRegexMatcher Matcher(Regex, Line);
				if (Matcher.FindNext())
				{
					++Count;
					if (Samples && Samples->Num() < 3) { Samples->Add(Line); }
				}
			}
			return Count;
		}

		bool ExpectationsMet(const FUatStep& Step) const
		{
			for (const FUatRule& Rule : Step.Expect)
			{
				TArray<FString> Lines;
				LinesSinceMark(Rule, Lines);
				const int32 Count = CountMatches(Lines, Rule.Pattern, nullptr);
				if (Count < Rule.Min || (Rule.Max >= 0 && Count > Rule.Max))
				{
					return false;
				}
			}
			return true;
		}

		FUatStepResult Evaluate(const FUatStep& Step) const
		{
			FUatStepResult R;
			R.Name = Step.Name;
			bool bPass = true;
			for (const FUatRule& Rule : Step.Expect)
			{
				TArray<FString> Lines;
				LinesSinceMark(Rule, Lines);
				TArray<FString> Samples;
				const int32 Count = CountMatches(Lines, Rule.Pattern, &Samples);
				const bool bOk = Count >= Rule.Min && (Rule.Max < 0 || Count <= Rule.Max);
				bPass &= bOk;
				R.Details.Add(FString::Printf(TEXT("%s expect %s /%s/ %s: %d match(es)%s"),
					bOk ? TEXT("ok  ") : TEXT("MISS"), *Rule.Log, *Rule.Pattern,
					Rule.Max >= 0 ? *FString::Printf(TEXT("[%d..%d]"), Rule.Min, Rule.Max) : *FString::Printf(TEXT("[>=%d]"), Rule.Min),
					Count, Samples.Num() ? *FString::Printf(TEXT(" e.g. %s"), *Samples[0].Left(200)) : TEXT("")));
			}
			for (const FUatRule& Rule : Step.Forbid)
			{
				TArray<FString> Lines;
				LinesSinceMark(Rule, Lines);
				TArray<FString> Samples;
				const int32 Count = CountMatches(Lines, Rule.Pattern, &Samples);
				bPass &= Count == 0;
				R.Details.Add(FString::Printf(TEXT("%s forbid %s /%s/: %d match(es)%s"),
					Count == 0 ? TEXT("ok  ") : TEXT("HIT "), *Rule.Log, *Rule.Pattern, Count,
					Samples.Num() ? *FString::Printf(TEXT(" e.g. %s"), *Samples[0].Left(200)) : TEXT("")));
			}
			for (const FStepActionRunner::FRelayed& Req : Actions.Relayed)
			{
				const UMCPPIERemoteControl* Remote = PIERemotePlayers::FindById(Req.Id);
				const FMCPPIERemoteResult* Result = Remote ? Remote->FindResult(Req.Id) : nullptr;
				if (!Result)
				{
					bPass = false;
					R.Details.Add(FString::Printf(TEXT("MISS client %d never confirmed %s"), Req.Client, *Req.What));
				}
				else if (!Result->bOk)
				{
					bPass = false;
					R.Details.Add(FString::Printf(TEXT("FAIL client %d could not run %s: %s"), Req.Client, *Req.What, *Result->Error));
				}
			}
			for (const FStepActionRunner::FCapture& C : Actions.Captures)
			{
				if (C.Written.IsValid() && *C.Written != 1)
				{
					bPass = false;
					R.Details.Add(FString::Printf(TEXT("%s host capture %s"), *C.Written == 0 ? TEXT("MISS") : TEXT("FAIL"),
						*C.Written == 0 ? *FString::Printf(TEXT("%s was not written in the window"), *C.File) : *FString::Printf(TEXT("%s could not be written"), *C.File)));
					continue;
				}
				if (!C.Written.IsValid() && !IFileManager::Get().FileExists(*(ReportDir / C.File)))
				{
					continue; // a remote capture that did not land is already reported through its relayed result
				}
				R.Captures.Add(C.File);
			}
			R.Verdict = bPass ? TEXT("PASS") : TEXT("FAIL");
			return R;
		}

		// A step may end before its window only when nothing later in the window could change the verdict.
		bool CanFinishEarly(const FUatStep& Step) const
		{
			if (Step.Forbid.Num() > 0 || (Step.Expect.Num() == 0 && Actions.Captures.Num() == 0))
			{
				return false;
			}
			for (const FUatRule& Rule : Step.Expect)
			{
				if (Rule.Max >= 0) return false;
			}
			return ExpectationsMet(Step) && Actions.RelayedSettled() && Actions.HostCapturesWritten();
		}

		void MarkStepStart()
		{
			HostMark = HostLog.Num();
			Actions.CaptureRoot = ReportDir;
			Actions.StepNumber = StepIndex + 1;
			Actions.BeginStep();
			if (Steps.IsValidIndex(StepIndex)) Sequence.Begin(Steps[StepIndex].Do);
			ClientMarks.Reset();
			for (const TPair<int32, FClientLogTail>& Pair : ClientLogs)
			{
				ClientMarks.Add(Pair.Key, Pair.Value.Lines.Num());
			}
		}

		void BeginStop()
		{
			if (State == EState::Stopping || State == EState::Finished)
			{
				return;
			}
			State = EState::Stopping;
			PhaseStart = Now();
			if (GEditor && GEditor->IsPlaySessionRequestQueued())
			{
				GEditor->CancelRequestPlaySession();
			}
			if (GEditor && GEditor->PlayWorld)
			{
				GEditor->RequestEndPlayMap();
			}
		}

		void Finish()
		{
			State = EState::Finished;
			GLog->RemoveOutputDevice(&HostLog);
			RestoreSettings();
			Actions.Reset();
			for (int32 i = Results.Num(); i < Steps.Num(); ++i)
			{
				Results.Add({ Steps[i].Name, TEXT("SKIPPED"), {} });
			}
			WriteReport();
			UE_LOG(LogMCPPIE, Log, TEXT("[UAT] %s finished: %s (%s)"), *Name, *OverallVerdict(), *ReportDir);
		}

		FString OverallVerdict() const
		{
			if (!Error.IsEmpty() || bAborted)
			{
				return TEXT("ERROR");
			}
			for (const FUatStepResult& R : Results)
			{
				if (R.Verdict != TEXT("PASS")) return TEXT("FAIL");
			}
			return TEXT("PASS");
		}

		void WriteReport()
		{
			IFileManager::Get().MakeDirectory(*ReportDir, true);

			FString Md = FString::Printf(TEXT("# UAT: %s\n\nVerdict: **%s**\n\n"), *Name, *OverallVerdict());
			if (!Error.IsEmpty()) Md += FString::Printf(TEXT("Error: %s\n\n"), *Error);
			Md += TEXT("| # | Step | Verdict |\n|---|---|---|\n");
			for (int32 i = 0; i < Results.Num(); ++i)
			{
				Md += FString::Printf(TEXT("| %d | %s | %s |\n"), i + 1, *Results[i].Name, *Results[i].Verdict);
			}
			Md += TEXT("\n");
			for (int32 i = 0; i < Results.Num(); ++i)
			{
				if (Results[i].Verdict == TEXT("PASS") || Results[i].Details.Num() == 0) continue;
				Md += FString::Printf(TEXT("## %d. %s: %s\n\n"), i + 1, *Results[i].Name, *Results[i].Verdict);
				for (const FString& D : Results[i].Details) Md += FString::Printf(TEXT("- `%s`\n"), *D.Replace(TEXT("`"), TEXT("'")));
				Md += TEXT("\n");
			}
			bool bAnyCapture = false;
			for (int32 i = 0; i < Results.Num(); ++i)
			{
				for (const FString& C : Results[i].Captures)
				{
					if (!bAnyCapture) { Md += TEXT("## Captures\n\n"); bAnyCapture = true; }
					Md += FString::Printf(TEXT("%d. %s: ![%s](%s)\n\n"), i + 1, *Results[i].Name, *FPaths::GetBaseFilename(C), *C);
				}
			}
			FFileHelper::SaveStringToFile(Md, *(ReportDir / TEXT("report.md")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

			FString Json;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
			FJsonSerializer::Serialize(BuildStatus().ToSharedRef(), Writer);
			FFileHelper::SaveStringToFile(Json, *(ReportDir / TEXT("report.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}

		TSharedPtr<FJsonObject> BuildStatus() const
		{
			static const TCHAR* StateNames[] = { TEXT("idle"), TEXT("starting"), TEXT("joining"), TEXT("settling"), TEXT("acting"), TEXT("watching"), TEXT("stopping"), TEXT("finished") };
			TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetStringField(TEXT("state"), StateNames[static_cast<int32>(State)]);
			S->SetStringField(TEXT("name"), Name);
			S->SetNumberField(TEXT("steps_total"), Steps.Num());
			S->SetNumberField(TEXT("steps_done"), Results.Num());
			if (State == EState::Acting || State == EState::Watching)
			{
				S->SetStringField(TEXT("current_step"), Steps.IsValidIndex(StepIndex) ? Steps[StepIndex].Name : FString());
			}
			if (State == EState::Finished)
			{
				S->SetStringField(TEXT("verdict"), OverallVerdict());
				S->SetStringField(TEXT("report_dir"), ReportDir);
			}
			if (!Error.IsEmpty()) S->SetStringField(TEXT("error"), Error);

			TArray<TSharedPtr<FJsonValue>> Arr;
			for (int32 i = 0; i < Results.Num(); ++i)
			{
				TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
				R->SetNumberField(TEXT("step"), i + 1);
				R->SetStringField(TEXT("name"), Results[i].Name);
				R->SetStringField(TEXT("verdict"), Results[i].Verdict);
				TArray<TSharedPtr<FJsonValue>> Details;
				for (const FString& D : Results[i].Details) Details.Add(MakeShared<FJsonValueString>(D));
				R->SetArrayField(TEXT("details"), Details);
				if (Results[i].Captures.Num())
				{
					TArray<TSharedPtr<FJsonValue>> Captures;
					for (const FString& C : Results[i].Captures) Captures.Add(MakeShared<FJsonValueString>(ReportDir / C));
					R->SetArrayField(TEXT("captures"), Captures);
				}
				Arr.Add(MakeShared<FJsonValueObject>(R));
			}
			S->SetArrayField(TEXT("results"), Arr);
			return S;
		}

		bool Tick()
		{
			const double T = Now();
			PollClientLogs();

			Actions.TickStops(T);

			switch (State)
			{
			case EState::Starting:
				if (GEditor->PlayWorld)
				{
					State = EState::Joining;
					PhaseStart = T;
				}
				else if (T - PhaseStart > 60.0)
				{
					Fail(TEXT("PIE did not start within 60 s"));
				}
				break;

			case EState::Joining:
				if (!GEditor->PlayWorld)
				{
					Fail(TEXT("PIE ended while players were joining"));
				}
				else if (AllJoined())
				{
					FString Err;
					if (!BeginClientLogs(Err))
					{
						Fail(Err);
						break;
					}
					State = EState::Settling;
					PhaseStart = T;
				}
				else if (T - PhaseStart > JoinTimeoutS)
				{
					Fail(FString::Printf(TEXT("players did not finish joining within %.0f s"), JoinTimeoutS));
				}
				break;

			case EState::Settling:
				if (T - PhaseStart >= SettleMs / 1000.0)
				{
					StepIndex = 0;
					MarkStepStart();
					State = Steps.Num() ? EState::Acting : EState::Stopping;
					if (State == EState::Stopping) BeginStop();
				}
				break;

			case EState::Acting:
			{
				if (!GEditor->PlayWorld)
				{
					Fail(TEXT("PIE ended during the run"));
					break;
				}
				const FUatStep& Step = Steps[StepIndex];
				FString Err;
				const FStepActionSequence::EOutcome Outcome = Sequence.Tick(Actions, T, Err);
				if (Outcome == FStepActionSequence::EOutcome::Running)
				{
					return true;
				}
				if (Outcome == FStepActionSequence::EOutcome::Failed)
				{
					Results.Add({ Step.Name, TEXT("FAIL"), { FString::Printf(TEXT("action %d could not run: %s"), Sequence.FailedIndex + 1, *Err) } });
					AdvanceStep(T);
					return true;
				}
				State = EState::Watching;
				PhaseStart = T;
				break;
			}

			case EState::Watching:
			{
				if (!GEditor->PlayWorld)
				{
					Fail(TEXT("PIE ended during the run"));
					break;
				}
				const FUatStep& Step = Steps[StepIndex];
				const bool bWindowOver = T - PhaseStart >= Step.WindowMs / 1000.0;
				if (bWindowOver || CanFinishEarly(Step))
				{
					FUatStepResult R = Evaluate(Step);
					UE_LOG(LogMCPPIE, Log, TEXT("[UAT] %s step %d %s: %s"), *Name, StepIndex + 1, *Step.Name, *R.Verdict);
					Results.Add(MoveTemp(R));
					AdvanceStep(T);
				}
				break;
			}

			case EState::Stopping:
				if (!GEditor->PlayWorld)
				{
					Finish();
					return false;
				}
				if (T - PhaseStart > 60.0)
				{
					Finish();
					return false;
				}
				break;

			default:
				break;
			}
			return true;
		}

		void AdvanceStep(double T)
		{
			++StepIndex;
			if (bAborted || StepIndex >= Steps.Num())
			{
				BeginStop();
				return;
			}
			MarkStepStart();
			State = EState::Acting;
			PhaseStart = T;
		}
	};

	FPIEUatRunner& FPIEUatRunner::Get()
	{
		static FPIEUatRunner Instance;
		return Instance;
	}

	FPIEUatRunner::FPIEUatRunner() : Impl(MakeUnique<FImpl>()) {}
	FPIEUatRunner::~FPIEUatRunner() = default;

	void FPIEUatRunner::Init() {}

	void FPIEUatRunner::Shutdown()
	{
		if (IsRunning())
		{
			Impl->RestoreSettings();
		}
		Impl->Actions.Reset();
		if (Impl->TickHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(Impl->TickHandle);
			Impl->TickHandle.Reset();
		}
		if (GLog)
		{
			GLog->RemoveOutputDevice(&Impl->HostLog);
		}
	}

	bool FPIEUatRunner::IsRunning() const
	{
		return Impl->State != FImpl::EState::Idle && Impl->State != FImpl::EState::Finished;
	}

	bool FPIEUatRunner::Start(const TSharedPtr<FJsonObject>& Scenario, FString& OutError)
	{
		if (IsRunning())
		{
			OutError = FString::Printf(TEXT("UAT '%s' is already running"), *Impl->Name);
			return false;
		}
		if (!GEditor || GEditor->PlayWorld || GEditor->IsPlaySessionRequestQueued())
		{
			OutError = TEXT("Stop the current PIE session first; the run starts its own");
			return false;
		}

		FImpl Fresh;
		Fresh.Name = Scenario->HasField(TEXT("name")) ? Scenario->GetStringField(TEXT("name")) : TEXT("uat");
		const TSharedPtr<FJsonObject>* Pie = nullptr;
		if (Scenario->TryGetObjectField(TEXT("pie"), Pie))
		{
			double N = 0;
			if ((*Pie)->TryGetNumberField(TEXT("players"), N)) Fresh.Players = FMath::Clamp(static_cast<int32>(N), 1, 16);
			FString Mode;
			if ((*Pie)->TryGetStringField(TEXT("net_mode"), Mode))
			{
				if (!Mode.Equals(TEXT("listen"), ESearchCase::CaseSensitive) && !Mode.Equals(TEXT("dedicated"), ESearchCase::CaseSensitive))
				{
					OutError = FString::Printf(TEXT("pie.net_mode must be \"listen\" or \"dedicated\", not \"%s\""), *Mode);
					return false;
				}
				Fresh.bListen = Mode.Equals(TEXT("listen"), ESearchCase::CaseSensitive);
			}
			// A dedicated server only stays inside the editor, where the run can read it, in one-process PIE.
			bool b = !Fresh.bListen;
			const bool bOneProcessGiven = (*Pie)->TryGetBoolField(TEXT("one_process"), b);
			Fresh.bOneProcess = b;
			if (!Fresh.bListen && bOneProcessGiven && !b)
			{
				OutError = TEXT("net_mode \"dedicated\" needs one_process true: a separate-process dedicated server runs outside the editor, where the run cannot drive or read it");
				return false;
			}
			(*Pie)->TryGetBoolField(TEXT("windowless"), Fresh.bWindowless);
			if (Fresh.bWindowless && Fresh.bOneProcess)
			{
				OutError = TEXT("windowless applies to separate-process clients; set one_process false");
				return false;
			}
			double Fps = 0;
			if ((*Pie)->TryGetNumberField(TEXT("client_fps"), Fps))
			{
				if (Fps < 1 || Fps > 1000)
				{
					OutError = TEXT("pie.client_fps must be between 1 and 1000");
					return false;
				}
				Fresh.ClientFps = static_cast<int32>(Fps);
			}
		}
		double N = 0;
		if (Scenario->TryGetNumberField(TEXT("join_timeout_s"), N)) Fresh.JoinTimeoutS = static_cast<float>(N);
		if (Scenario->TryGetNumberField(TEXT("settle_ms"), N)) Fresh.SettleMs = static_cast<float>(N);

		const TArray<TSharedPtr<FJsonValue>>* StepsArr = nullptr;
		if (!Scenario->TryGetArrayField(TEXT("steps"), StepsArr) || StepsArr->Num() == 0)
		{
			OutError = TEXT("scenario has no steps");
			return false;
		}
		for (int32 i = 0; i < StepsArr->Num(); ++i)
		{
			const TSharedPtr<FJsonObject> SO = (*StepsArr)[i]->AsObject();
			if (!SO.IsValid())
			{
				OutError = FString::Printf(TEXT("step %d is not an object"), i + 1);
				return false;
			}
			FUatStep Step;
			Step.Name = SO->HasField(TEXT("name")) ? SO->GetStringField(TEXT("name")) : FString::Printf(TEXT("step %d"), i + 1);
			if (SO->TryGetNumberField(TEXT("window_ms"), N)) Step.WindowMs = static_cast<float>(N);
			FString Err;
			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			if (SO->TryGetArrayField(TEXT("do"), Arr))
			{
				for (const TSharedPtr<FJsonValue>& V : *Arr)
				{
					FStepAction A;
					if (!V->AsObject().IsValid() || !ParseStepAction(V->AsObject(), A, Err))
					{
						OutError = FString::Printf(TEXT("step %d (%s): %s"), i + 1, *Step.Name, *Err);
						return false;
					}
					if (A.Client < 0 || A.Client >= Fresh.Players + (Fresh.bListen ? 0 : 1) || (A.Client == 0 && !Fresh.bListen && A.Kind != FStepAction::EKind::Wait))
					{
						OutError = FString::Printf(TEXT("step %d (%s): client %d does not exist with %d players (%s)"), i + 1, *Step.Name, A.Client, Fresh.Players, Fresh.bListen ? TEXT("listen") : TEXT("dedicated"));
						return false;
					}
					Step.Do.Add(MoveTemp(A));
				}
			}
			for (const TCHAR* Key : { TEXT("expect"), TEXT("forbid") })
			{
				if (SO->TryGetArrayField(Key, Arr))
				{
					for (const TSharedPtr<FJsonValue>& V : *Arr)
					{
						FUatRule R;
						if (!V->AsObject().IsValid() || !ParseRule(V->AsObject(), R, Err))
						{
							OutError = FString::Printf(TEXT("step %d (%s): %s"), i + 1, *Step.Name, *Err);
							return false;
						}
						// client:0 is the listen server's own player, whose lines are in the host log.
						const bool bHostLog = R.Log == TEXT("host") || (R.Client == 0 && Fresh.bListen);
						if (!bHostLog && (R.Client < 1 || R.Client > Fresh.RemoteCount()))
						{
							OutError = FString::Printf(TEXT("step %d (%s): log %s does not exist with %d players (%s)"), i + 1, *Step.Name, *R.Log,
								Fresh.Players, Fresh.bListen ? TEXT("listen") : TEXT("dedicated"));
							return false;
						}
						(FCString::Strcmp(Key, TEXT("expect")) == 0 ? Step.Expect : Step.Forbid).Add(MoveTemp(R));
					}
				}
			}
			const bool bCaptures = Step.Do.ContainsByPredicate([](const FStepAction& A) { return A.Kind == FStepAction::EKind::Capture; });
			if (Step.Expect.Num() == 0 && Step.Forbid.Num() == 0 && !bCaptures)
			{
				OutError = FString::Printf(TEXT("step %d (%s) checks nothing; give it expect, forbid or a capture"), i + 1, *Step.Name);
				return false;
			}
			Fresh.Steps.Add(MoveTemp(Step));
		}

		FString Map;
		if (Scenario->TryGetStringField(TEXT("map"), Map) && !Map.IsEmpty() && !MCPLoadMapIfNeeded(Map, OutError))
		{
			return false;
		}

		// Commit.
		if (Impl->TickHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(Impl->TickHandle);
		}
		Impl = MakeUnique<FImpl>();
		Impl->Name = Fresh.Name;
		Impl->Players = Fresh.Players;
		Impl->bListen = Fresh.bListen;
		Impl->bOneProcess = Fresh.bOneProcess;
		Impl->JoinTimeoutS = Fresh.JoinTimeoutS;
		Impl->SettleMs = Fresh.SettleMs;
		Impl->bWindowless = Fresh.bWindowless;
		Impl->ClientFps = Fresh.ClientFps;
		Impl->Steps = MoveTemp(Fresh.Steps);
		Impl->ReportDir = FPaths::ConvertRelativePathToFull(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("PIEStudio") / TEXT("UAT") /
			FString::Printf(TEXT("%s_%s"), *FPaths::MakeValidFileName(Impl->Name), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))));
		IFileManager::Get().MakeDirectory(*Impl->ReportDir, true);

		GLog->AddOutputDevice(&Impl->HostLog);
		Impl->State = FImpl::EState::Starting;
		Impl->PhaseStart = FImpl::Now();
		Impl->ConfigureAndStartPIE();
		FImpl* Raw = Impl.Get();
		Impl->TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Raw](float) { return Raw->Tick(); }));
		UE_LOG(LogMCPPIE, Log, TEXT("[UAT] %s started: %d players, %s, %s, %d steps"), *Impl->Name, Impl->Players,
			Impl->bListen ? TEXT("listen server") : TEXT("dedicated server"), Impl->bOneProcess ? TEXT("one process") : TEXT("separate processes"), Impl->Steps.Num());
		return true;
	}

	void FPIEUatRunner::Abort(const FString& Reason)
	{
		if (!IsRunning())
		{
			return;
		}
		Impl->bAborted = true;
		Impl->Error = Reason.IsEmpty() ? TEXT("aborted") : Reason;
		Impl->BeginStop();
	}

	TSharedPtr<FJsonObject> FPIEUatRunner::Status() const
	{
		return Impl->BuildStatus();
	}
}
