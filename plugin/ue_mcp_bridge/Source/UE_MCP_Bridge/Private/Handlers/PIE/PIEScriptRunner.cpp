#include "Handlers/PIE/PIEScriptRunner.h"
#include "Handlers/PIE/MCPPIETestScript.h"
#include "Handlers/PIE/MCPPIEObservationProfile.h"
#include "Handlers/PIE/PIEObserver.h"
#include "Handlers/PIE/PIEInputRecorder.h"
#include "Handlers/PIE/PIEInputReplayer.h"
#include "Handlers/PIE/PIESequenceFormat.h"
#include "Handlers/PIE/PIEScenario.h"
#include "Handlers/PIE/PIEStepActions.h"
#include "Handlers/PIE/PIERemotePlayers.h"
#include "Handlers/PIE/PIELog.h"
#include "MCPPIERemoteControl.h"
#include "HandlerWorld.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "HAL/FileManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

namespace MCPPIE
{
	namespace
	{
		constexpr int32 ScriptResultsVersion = 1;

		FString ScriptNowIso()
		{
			return FDateTime::UtcNow().ToIso8601();
		}

		const TCHAR* ScriptOpNames[] = {
			TEXT("decreasing"), TEXT("increasing"), TEXT("eq"), TEXT("gt"), TEXT("lt"), TEXT("visible"), TEXT("exists")
		};

		const TCHAR* ScriptStatusNames[] = {
			TEXT("pending"), TEXT("running"), TEXT("passed"), TEXT("failed"), TEXT("awaiting_verdict"), TEXT("skipped")
		};

		const TCHAR* ScriptRunStateNames[] = {
			TEXT("idle"), TEXT("armed"), TEXT("running"), TEXT("awaiting_verdict"), TEXT("finished"), TEXT("stopped")
		};

		const TCHAR* ScriptActStateNames[] = {
			TEXT("none"), TEXT("running"), TEXT("done"), TEXT("failed"), TEXT("skipped")
		};

		TArray<TSharedPtr<FJsonValue>> ScriptStringsToJson(const TArray<FString>& Values)
		{
			TArray<TSharedPtr<FJsonValue>> Out;
			for (const FString& V : Values) Out.Add(MakeShared<FJsonValueString>(V));
			return Out;
		}

		// The verdict's note and captures, onto an evidence object.
		void ScriptVerdictEvidenceToJson(const FScriptEvidence& E, const TSharedRef<FJsonObject>& O)
		{
			if (!E.Note.IsEmpty()) O->SetStringField(TEXT("note"), E.Note);
			if (E.Captures.Num() > 0) O->SetArrayField(TEXT("captures"), ScriptStringsToJson(E.Captures));
		}

		bool ScriptHasVerdictEvidence(const FScriptEvidence& E)
		{
			return !E.Note.IsEmpty() || E.Captures.Num() > 0;
		}

		TSharedRef<FJsonObject> ScriptEvidenceToJson(const FScriptEvidence& E)
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("value"), E.Value);
			if (E.bHasAtStart) O->SetNumberField(TEXT("at_start"), E.AtStart);
			else O->SetField(TEXT("at_start"), MakeShared<FJsonValueNull>());
			if (E.bHasAtEnd) O->SetNumberField(TEXT("at_end"), E.AtEnd);
			else O->SetField(TEXT("at_end"), MakeShared<FJsonValueNull>());
			O->SetNumberField(TEXT("samples"), E.Samples);
			O->SetBoolField(TEXT("observed"), E.bObserved);
			if (E.VerdictFrame >= 0) O->SetNumberField(TEXT("verdict_frame"), static_cast<double>(E.VerdictFrame));
			if (E.VerdictAtMs >= 0.0) O->SetNumberField(TEXT("verdict_at_ms"), E.VerdictAtMs);
			if (!E.Reason.IsEmpty()) O->SetStringField(TEXT("reason"), E.Reason);
			ScriptVerdictEvidenceToJson(E, O);
			return O;
		}

		void ScriptEvidenceFromJson(const TSharedPtr<FJsonObject>& O, FScriptEvidence& E)
		{
			if (!O.IsValid()) return;
			O->TryGetStringField(TEXT("value"), E.Value);
			E.bHasAtStart = O->TryGetNumberField(TEXT("at_start"), E.AtStart);
			E.bHasAtEnd = O->TryGetNumberField(TEXT("at_end"), E.AtEnd);
			O->TryGetNumberField(TEXT("samples"), E.Samples);
			O->TryGetBoolField(TEXT("observed"), E.bObserved);
			double Frame = -1.0;
			if (O->TryGetNumberField(TEXT("verdict_frame"), Frame)) E.VerdictFrame = static_cast<int64>(Frame);
			O->TryGetNumberField(TEXT("verdict_at_ms"), E.VerdictAtMs);
			O->TryGetStringField(TEXT("reason"), E.Reason);
			O->TryGetStringField(TEXT("note"), E.Note);
			const TArray<TSharedPtr<FJsonValue>>* Captures = nullptr;
			if (O->TryGetArrayField(TEXT("captures"), Captures) && Captures)
			{
				for (const TSharedPtr<FJsonValue>& C : *Captures)
				{
					FString Path;
					if (C.IsValid() && C->TryGetString(Path) && !Path.IsEmpty()) E.Captures.Add(Path);
				}
			}
		}

		int32 ScriptEffectiveWithinMs(const FScriptStepDef& Def, int32 DefaultWithinMs)
		{
			if (Def.bHasExpect && Def.Expect.WithinMs > 0) return Def.Expect.WithinMs;
			return DefaultWithinMs > 0 ? DefaultWithinMs : 5000;
		}

		TSharedRef<FJsonObject> ScriptStatusData(const FScriptStepRun& St)
		{
			TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
			D->SetStringField(TEXT("status"), ScriptStepStatusToString(St.Status));
			if (St.Def.Judge == EScriptJudge::Auto && St.Def.bHasExpect)
			{
				D->SetObjectField(TEXT("evidence"), ScriptEvidenceToJson(St.Evidence));
			}
			else if (!St.Evidence.Reason.IsEmpty())
			{
				D->SetStringField(TEXT("reason"), St.Evidence.Reason);
			}
			return D;
		}

		void ScriptSettleStep(FScriptRun& Run, int32 Index, EScriptStepStatus Status, const FString& Reason)
		{
			FScriptStepRun& St = Run.Steps[Index];
			St.Status = Status;
			St.EndedAtMs = Run.NowMs;
			St.EndedAt = ScriptNowIso();
			St.VerdictSource = TEXT("auto");
			St.Evidence.VerdictFrame = Run.Frame;
			St.Evidence.VerdictAtMs = St.StartedAtMs >= 0.0 ? Run.NowMs - St.ExpectFromMs() : 0.0;
			St.Evidence.Reason = Reason;
			if (Status == EScriptStepStatus::Failed && St.bHasSourceStatus
				&& St.SourceStatus == EScriptStepStatus::Passed)
			{
				St.bRegression = true;
			}
			TSharedRef<FJsonObject> D = ScriptStatusData(St);
			if (St.bRegression) D->SetBoolField(TEXT("regression"), true);
			AddScriptEvent(Run, Status == EScriptStepStatus::Passed ? TEXT("step_passed") : TEXT("step_failed"), Index, D);
		}

		// Whether this run runs the step's arrange and act. A manual run has no PIE, and a replay replays the recorded input.
		bool ScriptStepActs(const FScriptRun& Run, int32 Index)
		{
			return Run.Steps[Index].Def.HasArrangeOrAct() && !Run.bManualMode && !Run.bReplay;
		}

		void ScriptStartStep(FScriptRun& Run, int32 Index)
		{
			FScriptStepRun& St = Run.Steps[Index];
			const bool bActs = ScriptStepActs(Run, Index);
			St.StartedAtMs = Run.NowMs;
			St.StartedAt = ScriptNowIso();
			St.WithinMs = ScriptEffectiveWithinMs(St.Def, Run.DefaultWithinMs);
			// A manual step that acts asks for its verdict once the act has set the scene.
			St.Status = St.Def.Judge == EScriptJudge::Manual && !bActs ? EScriptStepStatus::AwaitingVerdict : EScriptStepStatus::Running;
			if (St.Def.bHasExpect) St.Evidence.Value = St.Def.Expect.Value;

			TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
			D->SetStringField(TEXT("status"), ScriptStepStatusToString(St.Status));
			D->SetStringField(TEXT("instruction"), St.Def.Instruction);
			D->SetStringField(TEXT("judge"), ScriptJudgeToString(St.Def.Judge));
			if (St.Def.Judge == EScriptJudge::Manual) D->SetBoolField(TEXT("agent_judgeable"), St.Def.bAgentJudgeable);
			if (St.Def.bHasExpect)
			{
				D->SetObjectField(TEXT("expect"), ScriptExpectToJson(St.Def.Expect));
				D->SetNumberField(TEXT("within_ms"), St.WithinMs);
			}
			if (St.Def.Arrange.Num() > 0) D->SetArrayField(TEXT("arrange"), St.Def.Arrange);
			if (St.Def.Act.Num() > 0) D->SetArrayField(TEXT("act"), St.Def.Act);
			if (St.WindowStartMs >= 0.0)
			{
				D->SetNumberField(TEXT("window_start_ms"), St.WindowStartMs);
				D->SetNumberField(TEXT("window_end_ms"), St.WindowEndMs);
			}
			AddScriptEvent(Run, TEXT("step_started"), Index, D);

			if (St.Def.Judge == EScriptJudge::Auto && !St.Def.bHasExpect && !bActs)
			{
				ScriptSettleStep(Run, Index, EScriptStepStatus::Passed, TEXT("no expectation"));
			}
		}

		void ScriptEndAct(FScriptRun& Run, int32 Index, EScriptActState State)
		{
			FScriptStepRun& St = Run.Steps[Index];
			St.Act.State = State;
			St.Act.EndedAtMs = Run.NowMs;
			TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
			D->SetStringField(TEXT("status"), ScriptStepStatusToString(State == EScriptActState::Failed ? EScriptStepStatus::Failed : St.Status));
			D->SetObjectField(TEXT("act_result"), ScriptActReportToJson(St.Act));
			AddScriptEvent(Run, TEXT("step_acted"), Index, D);
			if (State == EScriptActState::Failed)
			{
				ScriptSettleStep(Run, Index, EScriptStepStatus::Failed, St.Act.Reason);
			}
		}

		// Runs a started step's arrange and act. True while the act still runs, which holds the run.
		// When it ends, a manual step asks for its verdict and an auto step without expect passes.
		bool ScriptDriveAct(FScriptRun& Run, int32 Index, IScriptStepDriver* Driver)
		{
			if (!ScriptStepActs(Run, Index)) return false;
			FScriptStepRun& St = Run.Steps[Index];
			if (St.Act.State == EScriptActState::None)
			{
				St.Act.StartedAtMs = Run.NowMs;
				if (!Driver)
				{
					St.Act.State = EScriptActState::Skipped;
					St.Act.Reason = TEXT("nothing ran arrange and act");
				}
				else
				{
					St.Act.State = EScriptActState::Running;
					if (!Driver->BeginAct(Index, St.Def, Run.NowMs, St.Act))
					{
						ScriptEndAct(Run, Index, EScriptActState::Failed);
						return false;
					}
				}
			}
			if (St.Act.State == EScriptActState::Running)
			{
				const EScriptActOutcome Outcome = Driver ? Driver->TickAct(Run.NowMs, St.Act) : EScriptActOutcome::Failed;
				if (Outcome == EScriptActOutcome::Running) return true;
				ScriptEndAct(Run, Index, Outcome == EScriptActOutcome::Done ? EScriptActState::Done : EScriptActState::Failed);
			}
			if (St.Status == EScriptStepStatus::Running)
			{
				if (St.Def.Judge == EScriptJudge::Manual)
				{
					St.Status = EScriptStepStatus::AwaitingVerdict;
				}
				else if (!St.Def.bHasExpect)
				{
					ScriptSettleStep(Run, Index, EScriptStepStatus::Passed, TEXT("no expectation"));
				}
			}
			return false;
		}

		TArray<TSharedPtr<FJsonValue>> ScriptJsonArrayFromString(const FString& Text)
		{
			TArray<TSharedPtr<FJsonValue>> Out;
			if (Text.TrimStartAndEnd().IsEmpty()) return Out;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
			FJsonSerializer::Deserialize(Reader, Out);
			return Out;
		}

		FString ScriptJsonArrayToString(const TArray<TSharedPtr<FJsonValue>>& Values)
		{
			if (Values.Num() == 0) return FString();
			FString Out;
			TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
				TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
			FJsonSerializer::Serialize(Values, W);
			return Out;
		}

		// The array field Name of a step: absent or null is empty, anything but an array is an error.
		bool ScriptStepArrayField(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Name, TArray<TSharedPtr<FJsonValue>>& Out, bool& bOutWrongType)
		{
			bOutWrongType = false;
			const TSharedPtr<FJsonValue> V = Obj->TryGetField(Name);
			if (!V.IsValid() || V->IsNull()) return false;
			if (V->Type != EJson::Array)
			{
				bOutWrongType = true;
				return false;
			}
			Out = V->AsArray();
			return Out.Num() > 0;
		}

		void ScriptEvaluateRunningStep(FScriptRun& Run, int32 Index, FScriptValueReader Reader)
		{
			FScriptStepRun& St = Run.Steps[Index];
			const FScriptExpect& E = St.Def.Expect;
			double Current = 0.0;
			const bool bObserved = Reader(E, Current);
			if (bObserved)
			{
				St.Evidence.Samples++;
				St.Evidence.bObserved = true;
				St.Evidence.bHasAtEnd = true;
				St.Evidence.AtEnd = Current;
				if (!St.Evidence.bHasAtStart)
				{
					St.Evidence.bHasAtStart = true;
					St.Evidence.AtStart = Current;
				}
			}
			const double Elapsed = Run.NowMs - St.ExpectFromMs();
			const EScriptExpectOutcome Outcome = EvaluateScriptExpectFrame(E, bObserved,
				St.Evidence.AtStart, Current, Elapsed, St.WithinMs);
			if (Outcome == EScriptExpectOutcome::Passed)
			{
				ScriptSettleStep(Run, Index, EScriptStepStatus::Passed, FString());
			}
			else if (Outcome == EScriptExpectOutcome::Failed)
			{
				const FString Reason = St.Evidence.bObserved
					? FString::Printf(TEXT("%s %s did not hold within %d ms"), *E.Value, *ScriptExpectOpToString(E.Op), St.WithinMs)
					: FString::Printf(TEXT("'%s' was never readable within %d ms (no such tracked value, actor or widget sample)"), *E.Value, St.WithinMs);
				ScriptSettleStep(Run, Index, EScriptStepStatus::Failed, Reason);
			}
		}

		TArray<TSharedPtr<FJsonValue>> ScriptIndicesToJson(const TArray<int32>& Indices)
		{
			TArray<TSharedPtr<FJsonValue>> Out;
			for (int32 I : Indices) Out.Add(MakeShared<FJsonValueNumber>(I));
			return Out;
		}

		void ScriptAnnounceAwaiting(FScriptRun& Run, const TArray<int32>& Indices, bool bBlocking)
		{
			TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
			D->SetArrayField(TEXT("awaiting"), ScriptIndicesToJson(Indices));
			D->SetBoolField(TEXT("blocking"), bBlocking);
			TArray<TSharedPtr<FJsonValue>> Steps;
			for (int32 I : Indices)
			{
				TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
				S->SetNumberField(TEXT("index"), I);
				S->SetStringField(TEXT("instruction"), Run.Steps[I].Def.Instruction);
				S->SetBoolField(TEXT("agent_judgeable"), Run.Steps[I].Def.bAgentJudgeable);
				Steps.Add(MakeShared<FJsonValueObject>(S));
			}
			D->SetArrayField(TEXT("steps"), Steps);
			AddScriptEvent(Run, TEXT("awaiting_verdict"), Indices.Num() == 1 ? Indices[0] : -1, D);
		}

		FString ScriptSanitizeName(const FString& Name)
		{
			FString Out;
			for (TCHAR C : Name)
			{
				Out.AppendChar(FChar::IsAlnum(C) || C == TEXT('-') || C == TEXT('_') ? C : TEXT('_'));
			}
			return Out.IsEmpty() ? FString(TEXT("inline")) : Out.Left(64);
		}
	}

	// ── Enums ───────────────────────────────────────────────────────────

	FString ScriptJudgeToString(EScriptJudge Judge)
	{
		return Judge == EScriptJudge::Manual ? TEXT("manual") : TEXT("auto");
	}

	bool ParseScriptJudge(const FString& Str, EScriptJudge& Out)
	{
		if (Str.Equals(TEXT("auto"), ESearchCase::IgnoreCase)) { Out = EScriptJudge::Auto; return true; }
		if (Str.Equals(TEXT("manual"), ESearchCase::IgnoreCase)) { Out = EScriptJudge::Manual; return true; }
		return false;
	}

	FString ScriptExpectOpToString(EScriptExpectOp Op)
	{
		return ScriptOpNames[static_cast<int32>(Op)];
	}

	bool ParseScriptExpectOp(const FString& Str, EScriptExpectOp& Out)
	{
		for (int32 I = 0; I < UE_ARRAY_COUNT(ScriptOpNames); ++I)
		{
			if (Str.Equals(ScriptOpNames[I], ESearchCase::IgnoreCase))
			{
				Out = static_cast<EScriptExpectOp>(I);
				return true;
			}
		}
		return false;
	}

	FString ScriptStepStatusToString(EScriptStepStatus Status)
	{
		return ScriptStatusNames[static_cast<int32>(Status)];
	}

	bool ParseScriptStepStatus(const FString& Str, EScriptStepStatus& Out)
	{
		for (int32 I = 0; I < UE_ARRAY_COUNT(ScriptStatusNames); ++I)
		{
			if (Str.Equals(ScriptStatusNames[I], ESearchCase::IgnoreCase))
			{
				Out = static_cast<EScriptStepStatus>(I);
				return true;
			}
		}
		return false;
	}

	FString ScriptActStateToString(EScriptActState State)
	{
		return ScriptActStateNames[static_cast<int32>(State)];
	}

	bool ParseScriptActState(const FString& Str, EScriptActState& Out)
	{
		for (int32 I = 0; I < UE_ARRAY_COUNT(ScriptActStateNames); ++I)
		{
			if (Str.Equals(ScriptActStateNames[I], ESearchCase::IgnoreCase))
			{
				Out = static_cast<EScriptActState>(I);
				return true;
			}
		}
		return false;
	}

	FString ScriptRunStateToString(EScriptRunState State)
	{
		return ScriptRunStateNames[static_cast<int32>(State)];
	}

	bool ParseScriptRunState(const FString& Str, EScriptRunState& Out)
	{
		for (int32 I = 0; I < UE_ARRAY_COUNT(ScriptRunStateNames); ++I)
		{
			if (Str.Equals(ScriptRunStateNames[I], ESearchCase::IgnoreCase))
			{
				Out = static_cast<EScriptRunState>(I);
				return true;
			}
		}
		return false;
	}

	bool IsScriptBooleanOp(EScriptExpectOp Op)
	{
		return Op == EScriptExpectOp::Visible || Op == EScriptExpectOp::Exists;
	}

	bool IsScriptRelativeOp(EScriptExpectOp Op)
	{
		return Op == EScriptExpectOp::Decreasing || Op == EScriptExpectOp::Increasing;
	}

	bool ScriptOpNeedsTarget(EScriptExpectOp Op)
	{
		return Op == EScriptExpectOp::Eq || Op == EScriptExpectOp::Gt || Op == EScriptExpectOp::Lt;
	}

	bool IsScriptStepSettled(EScriptStepStatus Status)
	{
		return Status == EScriptStepStatus::Passed
			|| Status == EScriptStepStatus::Failed
			|| Status == EScriptStepStatus::Skipped;
	}

	// ── Step definitions ────────────────────────────────────────────────

	bool ParseScriptStepJson(const TSharedPtr<FJsonValue>& Value, int32 Index, FScriptStepDef& Out, FString& OutError, FString* OutField)
	{
		FString Ignored;
		FString& Field = OutField ? *OutField : Ignored;
		Field.Reset();
		const FString Where = FString::Printf(TEXT("steps[%d]"), Index);
		const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(ObjPtr) || !ObjPtr || !ObjPtr->IsValid())
		{
			OutError = FString::Printf(TEXT("%s must be an object {instruction, judge, expect}"), *Where);
			return false;
		}
		const TSharedPtr<FJsonObject>& Obj = *ObjPtr;
		Out = FScriptStepDef();

		if (!Obj->TryGetStringField(TEXT("instruction"), Out.Instruction) || Out.Instruction.TrimStartAndEnd().IsEmpty())
		{
			Field = TEXT("instruction");
			OutError = FString::Printf(TEXT("%s.instruction is required"), *Where);
			return false;
		}

		FString Judge;
		if (Obj->TryGetStringField(TEXT("judge"), Judge) && !ParseScriptJudge(Judge, Out.Judge))
		{
			Field = TEXT("judge");
			OutError = FString::Printf(TEXT("%s.judge must be auto or manual, got '%s'"), *Where, *Judge);
			return false;
		}

		const TSharedPtr<FJsonValue> AgentValue = Obj->TryGetField(TEXT("agent_judgeable"));
		if (AgentValue.IsValid() && !AgentValue->IsNull())
		{
			if (AgentValue->Type != EJson::Boolean)
			{
				Field = TEXT("agent_judgeable");
				OutError = FString::Printf(TEXT("%s.agent_judgeable must be a boolean"), *Where);
				return false;
			}
			Out.bAgentJudgeable = AgentValue->AsBool();
			if (!Out.bAgentJudgeable && Out.Judge != EScriptJudge::Manual)
			{
				Field = TEXT("agent_judgeable");
				OutError = FString::Printf(TEXT("%s: agent_judgeable applies to manual steps; an auto step is judged from the observed values"), *Where);
				return false;
			}
		}

		const TSharedPtr<FJsonValue> ResolveValue = Obj->TryGetField(TEXT("resolve_in_pie"));
		if (ResolveValue.IsValid() && !ResolveValue->IsNull())
		{
			if (ResolveValue->Type != EJson::Boolean)
			{
				Field = TEXT("resolve_in_pie");
				OutError = FString::Printf(TEXT("%s.resolve_in_pie must be a boolean"), *Where);
				return false;
			}
			Out.bResolveInPIE = ResolveValue->AsBool();
		}

		// arrange: a scenario's arrange entries, checked by the scenario's own rules.
		bool bWrongType = false;
		if (ScriptStepArrayField(Obj, TEXT("arrange"), Out.Arrange, bWrongType))
		{
			TArray<FString> ArrangeErrors;
			FPIEScenario::ValidateArrange(Out.Arrange, Where + TEXT(".arrange"), ArrangeErrors);
			if (ArrangeErrors.Num() > 0)
			{
				Field = TEXT("arrange");
				OutError = ArrangeErrors[0];
				return false;
			}
		}
		else if (bWrongType)
		{
			Field = TEXT("arrange");
			OutError = FString::Printf(TEXT("%s.arrange must be an array of {kind: spawn|set|call, ...}"), *Where);
			return false;
		}

		// act: uat_run's step actions, parsed by the executor that runs them.
		if (ScriptStepArrayField(Obj, TEXT("act"), Out.Act, bWrongType))
		{
			for (int32 J = 0; J < Out.Act.Num(); ++J)
			{
				const TSharedPtr<FJsonObject>* ActObj = nullptr;
				FStepAction Action;
				FString ActErr;
				if (!Out.Act[J].IsValid() || !Out.Act[J]->TryGetObject(ActObj) || !ActObj || !ParseStepAction(*ActObj, Action, ActErr))
				{
					Field = TEXT("act");
					OutError = FString::Printf(TEXT("%s.act[%d]: %s"), *Where, J, ActErr.IsEmpty() ? TEXT("an action must be an object") : *ActErr);
					return false;
				}
				if (Action.Client < 0)
				{
					Field = TEXT("act");
					OutError = FString::Printf(TEXT("%s.act[%d]: client cannot be negative"), *Where, J);
					return false;
				}
			}
		}
		else if (bWrongType)
		{
			Field = TEXT("act");
			OutError = FString::Printf(TEXT("%s.act must be an array of step actions (tape, press, hold, inject, console, place, capture, wait_ms)"), *Where);
			return false;
		}

		const TSharedPtr<FJsonValue> ExpectValue = Obj->TryGetField(TEXT("expect"));
		if (!ExpectValue.IsValid() || ExpectValue->IsNull())
		{
			if (Out.bResolveInPIE && !Out.HasArrangeOrAct())
			{
				Field = TEXT("resolve_in_pie");
				OutError = FString::Printf(TEXT("%s.resolve_in_pie applies to a step with expect, arrange or act; this step has none"), *Where);
				return false;
			}
			return true;
		}
		const TSharedPtr<FJsonObject>* ExpPtr = nullptr;
		if (!ExpectValue->TryGetObject(ExpPtr) || !ExpPtr || !ExpPtr->IsValid())
		{
			Field = TEXT("expect");
			OutError = FString::Printf(TEXT("%s.expect must be an object {value, op, target, within_ms}"), *Where);
			return false;
		}
		if (Out.Judge == EScriptJudge::Manual)
		{
			Field = TEXT("expect");
			OutError = FString::Printf(TEXT("%s: a manual step takes no expect; use judge=auto to evaluate one"), *Where);
			return false;
		}
		const TSharedPtr<FJsonObject>& Exp = *ExpPtr;

		FString Op;
		if (!Exp->TryGetStringField(TEXT("op"), Op) || Op.IsEmpty())
		{
			Field = TEXT("expect.op");
			OutError = FString::Printf(TEXT("%s.expect.op is required (decreasing, increasing, eq, gt, lt, visible, exists)"), *Where);
			return false;
		}
		if (!ParseScriptExpectOp(Op, Out.Expect.Op))
		{
			Field = TEXT("expect.op");
			OutError = FString::Printf(TEXT("%s.expect.op '%s' is unknown (decreasing, increasing, eq, gt, lt, visible, exists)"), *Where, *Op);
			return false;
		}
		if (!Exp->TryGetStringField(TEXT("value"), Out.Expect.Value) || Out.Expect.Value.TrimStartAndEnd().IsEmpty())
		{
			Field = TEXT("expect.value");
			OutError = FString::Printf(TEXT("%s.expect.value is required for op '%s' (%s)"), *Where, *Op,
				Out.Expect.Op == EScriptExpectOp::Visible ? TEXT("a widget class or name")
				: Out.Expect.Op == EScriptExpectOp::Exists ? TEXT("an actor id")
				: TEXT("a tracked value path"));
			return false;
		}
		Out.Expect.Value.TrimStartAndEndInline();

		const TSharedPtr<FJsonValue> TargetValue = Exp->TryGetField(TEXT("target"));
		if (TargetValue.IsValid() && !TargetValue->IsNull())
		{
			if (IsScriptRelativeOp(Out.Expect.Op))
			{
				Field = TEXT("expect.target");
				OutError = FString::Printf(TEXT("%s.expect.target does not apply to op '%s', which compares against the value at step start"), *Where, *Op);
				return false;
			}
			if (TargetValue->Type == EJson::Boolean)
			{
				Out.Expect.Target = TargetValue->AsBool() ? 1.0 : 0.0;
				Out.Expect.bTargetIsBool = true;
			}
			else if (TargetValue->Type == EJson::Number)
			{
				Out.Expect.Target = TargetValue->AsNumber();
			}
			else
			{
				Field = TEXT("expect.target");
				OutError = FString::Printf(TEXT("%s.expect.target must be a number or a boolean"), *Where);
				return false;
			}
			Out.Expect.bHasTarget = true;
		}
		else if (ScriptOpNeedsTarget(Out.Expect.Op))
		{
			Field = TEXT("expect.target");
			OutError = FString::Printf(TEXT("%s.expect.target is required for op '%s'"), *Where, *Op);
			return false;
		}

		const TSharedPtr<FJsonValue> WithinValue = Exp->TryGetField(TEXT("within_ms"));
		if (WithinValue.IsValid() && !WithinValue->IsNull())
		{
			if (WithinValue->Type != EJson::Number || WithinValue->AsNumber() < 0.0)
			{
				Field = TEXT("expect.within_ms");
				OutError = FString::Printf(TEXT("%s.expect.within_ms must be a non-negative number"), *Where);
				return false;
			}
			Out.Expect.WithinMs = static_cast<int32>(FMath::Min(WithinValue->AsNumber(), static_cast<double>(MAX_int32)));
		}
		Out.bHasExpect = true;
		return true;
	}

	bool ParseScriptStepsJson(const TArray<TSharedPtr<FJsonValue>>& Values, TArray<FScriptStepDef>& Out, FString& OutError)
	{
		Out.Reset();
		if (Values.Num() == 0)
		{
			OutError = TEXT("steps must contain at least one step");
			return false;
		}
		for (int32 I = 0; I < Values.Num(); ++I)
		{
			FScriptStepDef Def;
			if (!ParseScriptStepJson(Values[I], I, Def, OutError)) return false;
			Out.Add(MoveTemp(Def));
		}
		return true;
	}

	TSharedRef<FJsonObject> ScriptExpectToJson(const FScriptExpect& Expect)
	{
		TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("value"), Expect.Value);
		E->SetStringField(TEXT("op"), ScriptExpectOpToString(Expect.Op));
		if (Expect.bHasTarget)
		{
			if (IsScriptBooleanOp(Expect.Op)) E->SetBoolField(TEXT("target"), Expect.Target != 0.0);
			else E->SetNumberField(TEXT("target"), Expect.Target);
		}
		if (Expect.WithinMs > 0) E->SetNumberField(TEXT("within_ms"), Expect.WithinMs);
		return E;
	}

	TSharedRef<FJsonObject> ScriptStepDefToJson(const FScriptStepDef& Step)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("instruction"), Step.Instruction);
		O->SetStringField(TEXT("judge"), ScriptJudgeToString(Step.Judge));
		if (Step.Judge == EScriptJudge::Manual) O->SetBoolField(TEXT("agent_judgeable"), Step.bAgentJudgeable);
		if (Step.bResolveInPIE) O->SetBoolField(TEXT("resolve_in_pie"), true);
		if (Step.Arrange.Num() > 0) O->SetArrayField(TEXT("arrange"), Step.Arrange);
		if (Step.Act.Num() > 0) O->SetArrayField(TEXT("act"), Step.Act);
		if (Step.bHasExpect) O->SetObjectField(TEXT("expect"), ScriptExpectToJson(Step.Expect));
		return O;
	}

	TArray<TSharedPtr<FJsonValue>> ScriptStepDefsToJson(const TArray<FScriptStepDef>& Steps)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FScriptStepDef& S : Steps) Out.Add(MakeShared<FJsonValueObject>(ScriptStepDefToJson(S)));
		return Out;
	}

	void ScriptStepsFromAsset(const UMCPPIETestScript* Asset, TArray<FScriptStepDef>& Out)
	{
		Out.Reset();
		if (!Asset) return;
		for (const FMCPPIETestStep& S : Asset->Steps)
		{
			FScriptStepDef D;
			D.Instruction = S.Instruction;
			D.Judge = S.Judge == EMCPPIEStepJudge::Manual ? EScriptJudge::Manual : EScriptJudge::Auto;
			D.bHasExpect = S.bHasExpect && D.Judge == EScriptJudge::Auto;
			D.Expect.Value = S.Expect.Value;
			D.Expect.Op = static_cast<EScriptExpectOp>(static_cast<uint8>(S.Expect.Op));
			D.Expect.bHasTarget = S.Expect.bHasTarget;
			D.Expect.Target = S.Expect.Target;
			D.Expect.WithinMs = FMath::Max(0, S.Expect.WithinMs);
			D.bAgentJudgeable = D.Judge != EScriptJudge::Manual || S.bAgentJudgeable;
			D.Arrange = ScriptJsonArrayFromString(S.ArrangeJson);
			D.Act = ScriptJsonArrayFromString(S.ActJson);
			D.bResolveInPIE = (D.bHasExpect || D.HasArrangeOrAct()) && S.bResolveInPIE;
			Out.Add(MoveTemp(D));
		}
	}

	void ScriptStepsToAsset(const TArray<FScriptStepDef>& Steps, UMCPPIETestScript* Asset)
	{
		if (!Asset) return;
		Asset->Steps.Reset();
		for (const FScriptStepDef& D : Steps)
		{
			FMCPPIETestStep S;
			S.Instruction = D.Instruction;
			S.Judge = D.Judge == EScriptJudge::Manual ? EMCPPIEStepJudge::Manual : EMCPPIEStepJudge::Auto;
			S.bHasExpect = D.bHasExpect;
			S.Expect.Value = D.Expect.Value;
			S.Expect.Op = static_cast<EMCPPIEExpectOp>(static_cast<uint8>(D.Expect.Op));
			S.Expect.bHasTarget = D.Expect.bHasTarget;
			S.Expect.Target = D.Expect.Target;
			S.Expect.WithinMs = D.Expect.WithinMs;
			S.bAgentJudgeable = D.bAgentJudgeable;
			S.bResolveInPIE = D.bResolveInPIE;
			S.ArrangeJson = ScriptJsonArrayToString(D.Arrange);
			S.ActJson = ScriptJsonArrayToString(D.Act);
			Asset->Steps.Add(MoveTemp(S));
		}
	}

	FScriptDef ScriptDefFromAsset(const UMCPPIETestScript* Asset, const FString& AssetPath)
	{
		FScriptDef Def;
		if (!Asset) return Def;
		Def.Name = Asset->GetName();
		Def.AssetPath = AssetPath;
		Def.Description = Asset->Description;
		Def.Profile = Asset->Profile;
		Def.Map = Asset->Map;
		Def.DefaultWithinMs = Asset->DefaultWithinMs > 0 ? Asset->DefaultWithinMs : 5000;
		ScriptStepsFromAsset(Asset, Def.Steps);
		return Def;
	}

	void CollectScriptObservedNames(const TArray<FScriptStepDef>& Steps,
		const TArray<FString>& ProfileValuePaths,
		TArray<FString>& OutValues, TArray<FString>& OutActors, TArray<FString>& OutWidgets)
	{
		for (const FScriptStepDef& S : Steps)
		{
			if (!S.bHasExpect || S.Expect.Value.IsEmpty()) continue;
			switch (S.Expect.Op)
			{
			case EScriptExpectOp::Visible:
				OutWidgets.AddUnique(S.Expect.Value);
				break;
			case EScriptExpectOp::Exists:
				OutActors.AddUnique(S.Expect.Value);
				break;
			default:
			{
				const bool bCovered = ProfileValuePaths.ContainsByPredicate([&S](const FString& P)
				{
					return TrackedValuePathMatches(P, S.Expect.Value);
				});
				if (!bCovered) OutValues.AddUnique(S.Expect.Value);
				break;
			}
			}
		}
	}

	// ── Evaluation ──────────────────────────────────────────────────────

	bool ScriptExpectTargetBool(const FScriptExpect& Expect)
	{
		return !Expect.bHasTarget || Expect.Target != 0.0;
	}

	bool EvaluateScriptCondition(const FScriptExpect& Expect, double AtStart, double Current)
	{
		switch (Expect.Op)
		{
		case EScriptExpectOp::Decreasing: return Current < AtStart;
		case EScriptExpectOp::Increasing: return Current > AtStart;
		case EScriptExpectOp::Eq:
		{
			const double Tolerance = FMath::Max(1e-4, 1e-4 * FMath::Max(FMath::Abs(Current), FMath::Abs(Expect.Target)));
			return FMath::Abs(Current - Expect.Target) <= Tolerance;
		}
		case EScriptExpectOp::Gt: return Current > Expect.Target;
		case EScriptExpectOp::Lt: return Current < Expect.Target;
		case EScriptExpectOp::Visible:
		case EScriptExpectOp::Exists:
			return (Current != 0.0) == ScriptExpectTargetBool(Expect);
		}
		return false;
	}

	EScriptExpectOutcome EvaluateScriptExpectFrame(const FScriptExpect& Expect, bool bObserved,
		double AtStart, double Current, double ElapsedMs, int32 WithinMs)
	{
		if (bObserved && ElapsedMs <= WithinMs && EvaluateScriptCondition(Expect, AtStart, Current))
		{
			return EScriptExpectOutcome::Passed;
		}
		if (ElapsedMs >= WithinMs)
		{
			return EScriptExpectOutcome::Failed;
		}
		return EScriptExpectOutcome::Pending;
	}

	// ── Run model ───────────────────────────────────────────────────────

	void InitScriptRun(FScriptRun& Run, const TArray<FScriptStepDef>& Steps)
	{
		Run.Steps.Reset();
		for (const FScriptStepDef& D : Steps)
		{
			FScriptStepRun S;
			S.Def = D;
			S.WithinMs = ScriptEffectiveWithinMs(D, Run.DefaultWithinMs);
			if (D.bHasExpect) S.Evidence.Value = D.Expect.Value;
			Run.Steps.Add(MoveTemp(S));
		}
		Run.Events.Reset();
		Run.NextSeq = 1;
		Run.CurrentStep = 0;
		Run.NowMs = 0.0;
		Run.Frame = -1;
		Run.bClockStarted = false;
		Run.AnnouncedBlockStart = -1;
	}

	void AddScriptEvent(FScriptRun& Run, const FString& Type, int32 Step, const TSharedPtr<FJsonObject>& Data)
	{
		FScriptEvent E;
		E.Seq = Run.NextSeq++;
		E.Type = Type;
		E.Step = Step;
		E.AtMs = Run.NowMs;
		E.WallTime = ScriptNowIso();
		E.Data = Data.IsValid() ? Data : MakeShared<FJsonObject>();
		E.Data->SetStringField(TEXT("mode"), ScriptRunModeToString(Run));
		Run.Events.Add(MoveTemp(E));
	}

	FString ScriptRunModeToString(const FScriptRun& Run)
	{
		return Run.bManualMode ? TEXT("manual") : TEXT("full");
	}

	void StartScriptManualSteps(FScriptRun& Run)
	{
		for (FScriptStepRun& St : Run.Steps)
		{
			if (!St.Def.HasArrangeOrAct()) continue;
			St.Act.State = EScriptActState::Skipped;
			St.Act.Reason = TEXT("a manual run has no PIE, so arrange and act did not run");
		}
		TArray<int32> Manual;
		for (int32 I = 0; I < Run.Steps.Num(); ++I)
		{
			if (Run.Steps[I].Def.Judge != EScriptJudge::Manual || Run.Steps[I].Status != EScriptStepStatus::Pending) continue;
			ScriptStartStep(Run, I);
			Manual.Add(I);
		}
		if (Manual.Num() > 0) ScriptAnnounceAwaiting(Run, Manual, false);
		Run.CurrentStep = Manual.Num() > 0 ? Manual[0] : Run.Steps.Num();
		Run.State = Manual.Num() > 0 ? EScriptRunState::AwaitingVerdict : EScriptRunState::Running;
	}

	bool AreScriptManualStepsSettled(const FScriptRun& Run)
	{
		for (const FScriptStepRun& S : Run.Steps)
		{
			if (S.Def.Judge == EScriptJudge::Manual && !IsScriptStepSettled(S.Status)) return false;
		}
		return true;
	}

	void AdvanceScriptRun(FScriptRun& Run, double NowMs, int64 Frame, FScriptValueReader Reader, IScriptStepDriver* Driver)
	{
		if (Run.State != EScriptRunState::Running && Run.State != EScriptRunState::AwaitingVerdict) return;
		Run.NowMs = NowMs;
		Run.Frame = Frame;
		const int32 N = Run.Steps.Num();
		while (Run.CurrentStep < N)
		{
			const int32 I = Run.CurrentStep;
			if (Run.Steps[I].Def.Judge == EScriptJudge::Manual)
			{
				// A manual step that acts runs its act on its own first, so a block never shares one act.
				if (ScriptStepActs(Run, I))
				{
					if (Run.Steps[I].Status == EScriptStepStatus::Pending) ScriptStartStep(Run, I);
					if (ScriptDriveAct(Run, I, Driver))
					{
						Run.State = EScriptRunState::Running;
						return;
					}
				}
				int32 BlockEnd = I + 1;
				if (Run.bBlockOnManual)
				{
					while (BlockEnd < N && Run.Steps[BlockEnd].Def.Judge == EScriptJudge::Manual && !ScriptStepActs(Run, BlockEnd)) ++BlockEnd;
				}
				for (int32 J = I; J < BlockEnd; ++J)
				{
					if (Run.Steps[J].Status == EScriptStepStatus::Pending) ScriptStartStep(Run, J);
				}
				if (!Run.bBlockOnManual)
				{
					if (Run.Steps[I].Status == EScriptStepStatus::AwaitingVerdict)
					{
						ScriptAnnounceAwaiting(Run, { I }, false);
					}
					Run.CurrentStep = BlockEnd;
					continue;
				}
				TArray<int32> Waiting;
				for (int32 J = I; J < BlockEnd; ++J)
				{
					if (Run.Steps[J].Status == EScriptStepStatus::AwaitingVerdict) Waiting.Add(J);
				}
				if (Waiting.Num() > 0)
				{
					if (Run.AnnouncedBlockStart != I)
					{
						ScriptAnnounceAwaiting(Run, Waiting, true);
						Run.AnnouncedBlockStart = I;
					}
					Run.State = EScriptRunState::AwaitingVerdict;
					return;
				}
				Run.State = EScriptRunState::Running;
				Run.CurrentStep = BlockEnd;
				continue;
			}

			if (Run.Steps[I].Status == EScriptStepStatus::Pending) ScriptStartStep(Run, I);
			if (ScriptDriveAct(Run, I, Driver))
			{
				Run.State = EScriptRunState::Running;
				return;
			}
			if (Run.Steps[I].Status == EScriptStepStatus::Running) ScriptEvaluateRunningStep(Run, I, Reader);
			if (Run.Steps[I].Status == EScriptStepStatus::Running)
			{
				Run.State = EScriptRunState::Running;
				return;
			}
			Run.CurrentStep++;
		}
		Run.State = ScriptAwaitingSteps(Run).Num() > 0 ? EScriptRunState::AwaitingVerdict : EScriptRunState::Running;
	}

	void AdvanceScriptReplay(FScriptRun& Run, double NowMs, int64 Frame, FScriptValueReader Reader)
	{
		if (Run.State != EScriptRunState::Running) return;
		Run.NowMs = NowMs;
		Run.Frame = Frame;
		int32 FirstUnsettled = Run.Steps.Num();
		for (int32 I = 0; I < Run.Steps.Num(); ++I)
		{
			FScriptStepRun& St = Run.Steps[I];
			if (St.Def.Judge == EScriptJudge::Manual || IsScriptStepSettled(St.Status)) continue;
			if (St.Status == EScriptStepStatus::Pending)
			{
				if (St.WindowStartMs < 0.0 || NowMs < St.WindowStartMs)
				{
					FirstUnsettled = FMath::Min(FirstUnsettled, I);
					continue;
				}
				ScriptStartStep(Run, I);
			}
			if (Run.Steps[I].Status == EScriptStepStatus::Running)
			{
				ScriptEvaluateRunningStep(Run, I, Reader);
			}
			if (!IsScriptStepSettled(Run.Steps[I].Status)) FirstUnsettled = FMath::Min(FirstUnsettled, I);
		}
		Run.CurrentStep = FirstUnsettled;
	}

	FString NormalizeScriptVerdictSource(const FString& Source)
	{
		const FString S = Source.TrimStartAndEnd().ToLower();
		if (S.IsEmpty() || S == TEXT("human") || S == TEXT("action")) return TEXT("action");
		if (S == TEXT("elicitation") || S == TEXT("agent")) return S;
		return FString();
	}

	bool ApplyScriptVerdict(FScriptRun& Run, int32 Step, const FString& Verdict, const FString& Note,
		const FString& Source, const FScriptVerdictEvidence& Evidence, bool& bOutUnchanged, FString& OutError)
	{
		bOutUnchanged = false;
		EScriptStepStatus NewStatus;
		if (Verdict.Equals(TEXT("pass"), ESearchCase::IgnoreCase)) NewStatus = EScriptStepStatus::Passed;
		else if (Verdict.Equals(TEXT("fail"), ESearchCase::IgnoreCase)) NewStatus = EScriptStepStatus::Failed;
		else if (Verdict.Equals(TEXT("skip"), ESearchCase::IgnoreCase)) NewStatus = EScriptStepStatus::Skipped;
		else
		{
			OutError = FString::Printf(TEXT("verdict must be pass, fail or skip, got '%s'"), *Verdict);
			return false;
		}
		const FString VerdictSource = NormalizeScriptVerdictSource(Source);
		if (VerdictSource.IsEmpty())
		{
			OutError = FString::Printf(TEXT("source must be human or agent, got '%s'"), *Source);
			return false;
		}
		const bool bAgent = VerdictSource == TEXT("agent");
		const FString EvidenceNote = Evidence.Note.TrimStartAndEnd();
		if (bAgent && EvidenceNote.IsEmpty())
		{
			OutError = TEXT("an agent verdict needs evidence: {note, captures?}, a note saying what was observed");
			return false;
		}
		if (!Run.Steps.IsValidIndex(Step))
		{
			OutError = FString::Printf(TEXT("step %d is out of range (the run has %d steps, 0-based)"), Step, Run.Steps.Num());
			return false;
		}
		FScriptStepRun& St = Run.Steps[Step];
		if (St.Def.Judge != EScriptJudge::Manual)
		{
			OutError = FString::Printf(TEXT("step %d is an auto step; its verdict comes from the observed values"), Step);
			return false;
		}
		if (bAgent && !St.Def.bAgentJudgeable)
		{
			OutError = FString::Printf(TEXT("step %d is agent_judgeable false; only a person can give its verdict"), Step);
			return false;
		}
		if (St.Status == EScriptStepStatus::Pending)
		{
			OutError = FString::Printf(TEXT("step %d has not started yet"), Step);
			return false;
		}
		if (St.Status == EScriptStepStatus::Running)
		{
			OutError = FString::Printf(TEXT("step %d is still running its arrange and act"), Step);
			return false;
		}
		if (St.Status == NewStatus && St.Note == Note && St.VerdictSource == VerdictSource
			&& St.Evidence.Note == EvidenceNote && St.Evidence.Captures == Evidence.Captures)
		{
			bOutUnchanged = true;
			return true;
		}
		St.Status = NewStatus;
		St.Note = Note;
		St.VerdictSource = VerdictSource;
		St.Evidence.Note = EvidenceNote;
		St.Evidence.Captures = Evidence.Captures;
		St.EndedAtMs = Run.NowMs;
		St.EndedAt = ScriptNowIso();

		TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
		D->SetStringField(TEXT("status"), ScriptStepStatusToString(NewStatus));
		D->SetStringField(TEXT("verdict"), Verdict.ToLower());
		if (!Note.IsEmpty()) D->SetStringField(TEXT("note"), Note);
		D->SetStringField(TEXT("verdict_source"), VerdictSource);
		if (ScriptHasVerdictEvidence(St.Evidence))
		{
			TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
			ScriptVerdictEvidenceToJson(St.Evidence, E);
			D->SetObjectField(TEXT("evidence"), E);
		}
		AddScriptEvent(Run, TEXT("verdict"), Step, D);
		return true;
	}

	bool IsScriptRunSettled(const FScriptRun& Run)
	{
		for (const FScriptStepRun& S : Run.Steps)
		{
			if (!IsScriptStepSettled(S.Status)) return false;
		}
		return true;
	}

	void SettleUnfinishedScriptSteps(FScriptRun& Run, const FString& Reason)
	{
		for (int32 I = 0; I < Run.Steps.Num(); ++I)
		{
			FScriptStepRun& St = Run.Steps[I];
			if (St.Act.State == EScriptActState::Running)
			{
				St.Act.State = EScriptActState::Failed;
				St.Act.EndedAtMs = Run.NowMs;
				St.Act.Reason = Reason;
			}
			if (St.Status == EScriptStepStatus::Running)
			{
				ScriptSettleStep(Run, I, EScriptStepStatus::Failed, St.Evidence.bObserved || !St.Def.bHasExpect
					? Reason
					: FString::Printf(TEXT("%s; '%s' was never readable"), *Reason, *St.Def.Expect.Value));
			}
		}
	}

	FScriptSummary SummarizeScriptRun(const FScriptRun& Run)
	{
		FScriptSummary S;
		S.Total = Run.Steps.Num();
		for (const FScriptStepRun& St : Run.Steps)
		{
			switch (St.Status)
			{
			case EScriptStepStatus::Passed: S.Passed++; break;
			case EScriptStepStatus::Failed: S.Failed++; break;
			case EScriptStepStatus::Skipped: S.Skipped++; break;
			case EScriptStepStatus::AwaitingVerdict: S.Awaiting++; S.Pending++; break;
			default:
				if (Run.bManualMode && St.Def.Judge == EScriptJudge::Auto) S.Unchecked++;
				else S.Pending++;
				break;
			}
			if (St.bRegression) S.Regressions++;
			if (St.Def.Judge == EScriptJudge::Manual && IsScriptStepSettled(St.Status))
			{
				if (St.VerdictSource == TEXT("agent")) S.AgentVerdicts++;
				else if (St.VerdictSource == TEXT("action") || St.VerdictSource == TEXT("elicitation")) S.HumanVerdicts++;
			}
		}
		return S;
	}

	TArray<int32> ScriptAwaitingSteps(const FScriptRun& Run)
	{
		TArray<int32> Out;
		for (int32 I = 0; I < Run.Steps.Num(); ++I)
		{
			if (Run.Steps[I].Status == EScriptStepStatus::AwaitingVerdict) Out.Add(I);
		}
		return Out;
	}

	TSharedRef<FJsonObject> ScriptSummaryToJson(const FScriptSummary& Summary, bool bReplay)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("total"), Summary.Total);
		O->SetNumberField(TEXT("passed"), Summary.Passed);
		O->SetNumberField(TEXT("failed"), Summary.Failed);
		O->SetNumberField(TEXT("skipped"), Summary.Skipped);
		O->SetNumberField(TEXT("pending"), Summary.Pending);
		O->SetNumberField(TEXT("awaiting"), Summary.Awaiting);
		O->SetNumberField(TEXT("human_verdicts"), Summary.HumanVerdicts);
		O->SetNumberField(TEXT("agent_verdicts"), Summary.AgentVerdicts);
		O->SetNumberField(TEXT("unchecked"), Summary.Unchecked);
		if (bReplay) O->SetNumberField(TEXT("regressions"), Summary.Regressions);
		return O;
	}

	TSharedRef<FJsonObject> ScriptStepRunToJson(const FScriptStepRun& St, int32 Index)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("index"), Index);
		O->SetStringField(TEXT("instruction"), St.Def.Instruction);
		O->SetStringField(TEXT("judge"), ScriptJudgeToString(St.Def.Judge));
		if (St.Def.Judge == EScriptJudge::Manual) O->SetBoolField(TEXT("agent_judgeable"), St.Def.bAgentJudgeable);
		if (St.Def.bResolveInPIE) O->SetBoolField(TEXT("resolve_in_pie"), true);
		if (St.Def.Arrange.Num() > 0) O->SetArrayField(TEXT("arrange"), St.Def.Arrange);
		if (St.Def.Act.Num() > 0) O->SetArrayField(TEXT("act"), St.Def.Act);
		if (St.Def.bHasExpect) O->SetObjectField(TEXT("expect"), ScriptExpectToJson(St.Def.Expect));
		O->SetStringField(TEXT("status"), ScriptStepStatusToString(St.Status));
		O->SetNumberField(TEXT("within_ms"), St.WithinMs);
		if (St.StartedAtMs >= 0.0) O->SetNumberField(TEXT("started_at_ms"), St.StartedAtMs);
		if (St.EndedAtMs >= 0.0) O->SetNumberField(TEXT("ended_at_ms"), St.EndedAtMs);
		if (!St.StartedAt.IsEmpty()) O->SetStringField(TEXT("started_at"), St.StartedAt);
		if (!St.EndedAt.IsEmpty()) O->SetStringField(TEXT("ended_at"), St.EndedAt);
		if (St.Def.Judge == EScriptJudge::Auto && St.Def.bHasExpect)
		{
			O->SetObjectField(TEXT("evidence"), ScriptEvidenceToJson(St.Evidence));
		}
		else if (!St.Evidence.Reason.IsEmpty() || ScriptHasVerdictEvidence(St.Evidence))
		{
			TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
			if (!St.Evidence.Reason.IsEmpty()) E->SetStringField(TEXT("reason"), St.Evidence.Reason);
			ScriptVerdictEvidenceToJson(St.Evidence, E);
			O->SetObjectField(TEXT("evidence"), E);
		}
		if (!St.Note.IsEmpty()) O->SetStringField(TEXT("note"), St.Note);
		if (!St.VerdictSource.IsEmpty()) O->SetStringField(TEXT("verdict_source"), St.VerdictSource);
		if (St.bHasSourceStatus)
		{
			O->SetStringField(TEXT("source_status"), ScriptStepStatusToString(St.SourceStatus));
			O->SetBoolField(TEXT("regression"), St.bRegression);
		}
		if (St.WindowStartMs >= 0.0)
		{
			O->SetNumberField(TEXT("window_start_ms"), St.WindowStartMs);
			O->SetNumberField(TEXT("window_end_ms"), St.WindowEndMs);
		}
		if (St.Act.State != EScriptActState::None) O->SetObjectField(TEXT("act_result"), ScriptActReportToJson(St.Act));
		return O;
	}

	TSharedRef<FJsonObject> ScriptActReportToJson(const FScriptActReport& R)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("status"), ScriptActStateToString(R.State));
		if (!R.Reason.IsEmpty()) O->SetStringField(TEXT("reason"), R.Reason);
		if (R.State == EScriptActState::Skipped) return O;
		O->SetNumberField(TEXT("arranged"), R.Arranged);
		O->SetArrayField(TEXT("actions"), R.Actions);
		if (R.StartedAtMs >= 0.0) O->SetNumberField(TEXT("started_at_ms"), R.StartedAtMs);
		if (R.EndedAtMs >= 0.0)
		{
			O->SetNumberField(TEXT("ended_at_ms"), R.EndedAtMs);
			O->SetNumberField(TEXT("duration_ms"), FMath::Max(0.0, R.EndedAtMs - R.StartedAtMs));
		}
		O->SetNumberField(TEXT("wall_ms"), R.WallMs);
		if (R.Captures.Num() > 0) O->SetArrayField(TEXT("captures"), ScriptStringsToJson(R.Captures));
		return O;
	}

	TSharedRef<FJsonObject> ScriptEventToJson(const FScriptEvent& Event, const FString& RunId)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("seq"), Event.Seq);
		O->SetStringField(TEXT("type"), Event.Type);
		O->SetStringField(TEXT("run_id"), RunId);
		O->SetNumberField(TEXT("step"), Event.Step);
		O->SetNumberField(TEXT("at_ms"), Event.AtMs);
		O->SetStringField(TEXT("wall_time"), Event.WallTime);
		O->SetObjectField(TEXT("data"), Event.Data.IsValid() ? Event.Data.ToSharedRef() : MakeShared<FJsonObject>());
		return O;
	}

	TSharedRef<FJsonObject> ScriptRunToResultsJson(const FScriptRun& Run)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("version"), ScriptResultsVersion);
		O->SetStringField(TEXT("run_id"), Run.RunId);
		O->SetStringField(TEXT("script"), Run.ScriptPath);
		O->SetStringField(TEXT("script_name"), Run.ScriptName);
		if (!Run.Description.IsEmpty()) O->SetStringField(TEXT("description"), Run.Description);
		O->SetStringField(TEXT("profile"), Run.Profile);
		if (!Run.Map.IsEmpty()) O->SetStringField(TEXT("map"), Run.Map);
		O->SetStringField(TEXT("started_at"), Run.StartedAt);
		O->SetStringField(TEXT("ended_at"), Run.EndedAt);
		O->SetStringField(TEXT("state"), ScriptRunStateToString(Run.State));
		O->SetStringField(TEXT("mode"), ScriptRunModeToString(Run));
		if (!Run.StopReason.IsEmpty()) O->SetStringField(TEXT("stop_reason"), Run.StopReason);
		O->SetBoolField(TEXT("block_on_manual"), Run.bBlockOnManual);
		O->SetNumberField(TEXT("default_within_ms"), Run.DefaultWithinMs);
		O->SetStringField(TEXT("observation_run_id"), Run.ObservationRunId);
		O->SetStringField(TEXT("output_dir"), Run.OutputDir);
		O->SetStringField(TEXT("results_path"), Run.ResultsPath);
		if (!Run.RecordingId.IsEmpty()) O->SetStringField(TEXT("recording_id"), Run.RecordingId);
		if (!Run.RecordingDir.IsEmpty()) O->SetStringField(TEXT("recording_dir"), Run.RecordingDir);
		if (Run.bReplay)
		{
			O->SetStringField(TEXT("replay_of"), Run.ReplayOf);
			O->SetNumberField(TEXT("time_scale"), Run.TimeScale);
			O->SetNumberField(TEXT("replay_offset_ms"), Run.ReplayOffsetMs);
		}
		O->SetObjectField(TEXT("summary"), ScriptSummaryToJson(SummarizeScriptRun(Run), Run.bReplay));
		TArray<TSharedPtr<FJsonValue>> Steps;
		for (int32 I = 0; I < Run.Steps.Num(); ++I)
		{
			Steps.Add(MakeShared<FJsonValueObject>(ScriptStepRunToJson(Run.Steps[I], I)));
		}
		O->SetArrayField(TEXT("steps"), Steps);
		return O;
	}

	bool ScriptRunFromResultsJson(const TSharedPtr<FJsonObject>& Obj, FScriptRun& Out, FString& OutError)
	{
		if (!Obj.IsValid())
		{
			OutError = TEXT("results.json is not a JSON object");
			return false;
		}
		int32 Version = 0;
		if (!Obj->TryGetNumberField(TEXT("version"), Version) || Version < 1 || Version > ScriptResultsVersion)
		{
			OutError = FString::Printf(TEXT("results.json version %d is not supported (expected 1 to %d)"), Version, ScriptResultsVersion);
			return false;
		}
		Out = FScriptRun();
		if (!Obj->TryGetStringField(TEXT("run_id"), Out.RunId) || Out.RunId.IsEmpty())
		{
			OutError = TEXT("results.json has no run_id");
			return false;
		}
		Obj->TryGetStringField(TEXT("script"), Out.ScriptPath);
		Obj->TryGetStringField(TEXT("script_name"), Out.ScriptName);
		Obj->TryGetStringField(TEXT("description"), Out.Description);
		Obj->TryGetStringField(TEXT("profile"), Out.Profile);
		Obj->TryGetStringField(TEXT("map"), Out.Map);
		Obj->TryGetStringField(TEXT("started_at"), Out.StartedAt);
		Obj->TryGetStringField(TEXT("ended_at"), Out.EndedAt);
		FString State;
		if (Obj->TryGetStringField(TEXT("state"), State)) ParseScriptRunState(State, Out.State);
		Obj->TryGetStringField(TEXT("stop_reason"), Out.StopReason);
		Obj->TryGetBoolField(TEXT("block_on_manual"), Out.bBlockOnManual);
		FString Mode;
		Out.bManualMode = Obj->TryGetStringField(TEXT("mode"), Mode) && Mode == TEXT("manual");
		Obj->TryGetNumberField(TEXT("default_within_ms"), Out.DefaultWithinMs);
		Obj->TryGetStringField(TEXT("observation_run_id"), Out.ObservationRunId);
		Obj->TryGetStringField(TEXT("output_dir"), Out.OutputDir);
		Obj->TryGetStringField(TEXT("results_path"), Out.ResultsPath);
		Obj->TryGetStringField(TEXT("recording_id"), Out.RecordingId);
		Obj->TryGetStringField(TEXT("recording_dir"), Out.RecordingDir);
		Out.bReplay = Obj->TryGetStringField(TEXT("replay_of"), Out.ReplayOf);
		Obj->TryGetNumberField(TEXT("time_scale"), Out.TimeScale);
		Obj->TryGetNumberField(TEXT("replay_offset_ms"), Out.ReplayOffsetMs);

		const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
		if (!Obj->TryGetArrayField(TEXT("steps"), Steps) || !Steps)
		{
			OutError = TEXT("results.json has no steps array");
			return false;
		}
		for (int32 I = 0; I < Steps->Num(); ++I)
		{
			FScriptStepRun St;
			if (!ParseScriptStepJson((*Steps)[I], I, St.Def, OutError))
			{
				OutError = FString::Printf(TEXT("results.json: %s"), *OutError);
				return false;
			}
			const TSharedPtr<FJsonObject> S = (*Steps)[I]->AsObject();
			FString Status;
			if (S->TryGetStringField(TEXT("status"), Status)) ParseScriptStepStatus(Status, St.Status);
			St.WithinMs = ScriptEffectiveWithinMs(St.Def, Out.DefaultWithinMs);
			S->TryGetNumberField(TEXT("within_ms"), St.WithinMs);
			S->TryGetNumberField(TEXT("started_at_ms"), St.StartedAtMs);
			S->TryGetNumberField(TEXT("ended_at_ms"), St.EndedAtMs);
			S->TryGetStringField(TEXT("started_at"), St.StartedAt);
			S->TryGetStringField(TEXT("ended_at"), St.EndedAt);
			const TSharedPtr<FJsonObject>* Ev = nullptr;
			if (S->TryGetObjectField(TEXT("evidence"), Ev) && Ev) ScriptEvidenceFromJson(*Ev, St.Evidence);
			S->TryGetStringField(TEXT("note"), St.Note);
			S->TryGetStringField(TEXT("verdict_source"), St.VerdictSource);
			FString SourceStatus;
			if (S->TryGetStringField(TEXT("source_status"), SourceStatus))
			{
				St.bHasSourceStatus = ParseScriptStepStatus(SourceStatus, St.SourceStatus);
			}
			S->TryGetBoolField(TEXT("regression"), St.bRegression);
			S->TryGetNumberField(TEXT("window_start_ms"), St.WindowStartMs);
			S->TryGetNumberField(TEXT("window_end_ms"), St.WindowEndMs);
			const TSharedPtr<FJsonObject>* ActResult = nullptr;
			if (S->TryGetObjectField(TEXT("act_result"), ActResult) && ActResult)
			{
				FString ActState;
				if ((*ActResult)->TryGetStringField(TEXT("status"), ActState)) ParseScriptActState(ActState, St.Act.State);
				(*ActResult)->TryGetStringField(TEXT("reason"), St.Act.Reason);
				(*ActResult)->TryGetNumberField(TEXT("arranged"), St.Act.Arranged);
				(*ActResult)->TryGetNumberField(TEXT("started_at_ms"), St.Act.StartedAtMs);
				(*ActResult)->TryGetNumberField(TEXT("ended_at_ms"), St.Act.EndedAtMs);
				(*ActResult)->TryGetNumberField(TEXT("wall_ms"), St.Act.WallMs);
				const TArray<TSharedPtr<FJsonValue>>* Actions = nullptr;
				if ((*ActResult)->TryGetArrayField(TEXT("actions"), Actions) && Actions) St.Act.Actions = *Actions;
				(*ActResult)->TryGetStringArrayField(TEXT("captures"), St.Act.Captures);
			}
			Out.Steps.Add(MoveTemp(St));
		}
		return true;
	}

	void PrepareScriptReplayRun(const FScriptRun& Source, double OffsetMs, FScriptRun& OutReplay)
	{
		OutReplay.bReplay = true;
		OutReplay.ReplayOf = Source.RunId;
		OutReplay.ReplayOffsetMs = OffsetMs;
		OutReplay.ScriptPath = Source.ScriptPath;
		OutReplay.ScriptName = Source.ScriptName;
		OutReplay.Description = Source.Description;
		OutReplay.Profile = Source.Profile;
		OutReplay.Map = Source.Map;
		OutReplay.DefaultWithinMs = Source.DefaultWithinMs;
		OutReplay.bBlockOnManual = false;

		TArray<FScriptStepDef> Defs;
		for (const FScriptStepRun& S : Source.Steps) Defs.Add(S.Def);
		InitScriptRun(OutReplay, Defs);

		for (int32 I = 0; I < Source.Steps.Num(); ++I)
		{
			const FScriptStepRun& Src = Source.Steps[I];
			FScriptStepRun& St = OutReplay.Steps[I];
			St.bHasSourceStatus = true;
			St.SourceStatus = Src.Status;
			St.WithinMs = Src.WithinMs > 0 ? Src.WithinMs : St.WithinMs;
			if (St.Def.HasArrangeOrAct())
			{
				St.Act.State = EScriptActState::Skipped;
				St.Act.Reason = TEXT("a replay replays the recorded input, so arrange and act did not run again");
			}
			if (St.Def.Judge == EScriptJudge::Manual)
			{
				if (IsScriptStepSettled(Src.Status))
				{
					St.Status = Src.Status;
					St.Note = Src.Note;
					St.Evidence.Note = Src.Evidence.Note;
					St.Evidence.Captures = Src.Evidence.Captures;
				}
				else
				{
					St.Status = EScriptStepStatus::Skipped;
					St.Evidence.Reason = TEXT("no verdict in the source run");
				}
				St.VerdictSource = TEXT("source_run");
				continue;
			}
			if (Src.StartedAtMs < 0.0)
			{
				St.Status = EScriptStepStatus::Skipped;
				St.Evidence.Reason = TEXT("not reached in the source run");
				continue;
			}
			// The window opened when the source step's act ended.
			St.WindowStartMs = Src.ExpectFromMs() + OffsetMs;
			St.WindowEndMs = St.WindowStartMs + St.WithinMs;
		}
	}

	FString DefaultScriptObservationsRoot()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("MCPObservations");
	}

	FString MakeScriptRunId(const FString& Prefix, const FString& Name)
	{
		const FString Base = FString::Printf(TEXT("%s_%s_%s"), *Prefix, *ScriptSanitizeName(Name),
			*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
		FString Id = Base;
		for (int32 Suffix = 2; Suffix < 1000; ++Suffix)
		{
			if (!IFileManager::Get().DirectoryExists(*(DefaultScriptObservationsRoot() / Id))
				&& !IFileManager::Get().DirectoryExists(*(DefaultRecordingsRoot() / Id)))
			{
				break;
			}
			Id = FString::Printf(TEXT("%s_%d"), *Base, Suffix);
		}
		return Id;
	}

	// ── Arrange and act in the editor ───────────────────────────────────

	namespace
	{
		/**
		 * Runs a step's arrange through FPIEScenario::ApplyArrange and its act through the step action
		 * executor pie(uat_run) drives, so a script step and a scenario step mean the same thing.
		 */
		class FEditorScriptStepDriver : public IScriptStepDriver
		{
		public:
			explicit FEditorScriptStepDriver(const FString& InCaptureRoot) : CaptureRoot(InCaptureRoot) {}

			virtual bool BeginAct(int32 Index, const FScriptStepDef& Def, double NowMs, FScriptActReport& Report) override
			{
				Runner.Reset();
				UWorld* World = StepActionWorld();
				if (!World)
				{
					Report.Reason = TEXT("no PIE world to arrange and act in");
					return false;
				}
				if (Def.Arrange.Num() > 0)
				{
					FScenario Scenario;
					Scenario.Arrange = Def.Arrange;
					FString Err;
					if (!FPIEScenario::ApplyArrange(World, Scenario, Report.Arranged, Err))
					{
						Report.Reason = Err;
						return false;
					}
				}
				TArray<FStepAction> Actions;
				for (int32 I = 0; I < Def.Act.Num(); ++I)
				{
					const TSharedPtr<FJsonObject>* Obj = nullptr;
					FStepAction A;
					FString Err;
					if (!Def.Act[I].IsValid() || !Def.Act[I]->TryGetObject(Obj) || !Obj || !ParseStepAction(*Obj, A, Err))
					{
						Report.Reason = FString::Printf(TEXT("act[%d]: %s"), I, *Err);
						return false;
					}
					Actions.Add(MoveTemp(A));
				}
				Runner.CaptureRoot = CaptureRoot;
				Runner.StepNumber = Index + 1;
				Runner.BeginStep();
				Sequence.Begin(Actions);
				Reported = 0;
				WallStart = FPlatformTime::Seconds();
				SettleDeadline = 0.0;
				return true;
			}

			virtual EScriptActOutcome TickAct(double NowMs, FScriptActReport& Report) override
			{
				const double Now = FPlatformTime::Seconds();
				Runner.TickStops(Now);
				FString Err;
				const FStepActionSequence::EOutcome Outcome = Sequence.Tick(Runner, Now, Err);
				Record(NowMs, Report);
				if (Outcome == FStepActionSequence::EOutcome::Failed)
				{
					Report.Reason = FString::Printf(TEXT("act[%d] could not run: %s"), Sequence.FailedIndex, *Err);
					return End(Report, Now, EScriptActOutcome::Failed);
				}
				if (Outcome == FStepActionSequence::EOutcome::Running || !Runner.InputIdle())
				{
					return EScriptActOutcome::Running;
				}
				// Every entry ran and its input played out. Remote players confirm what they ran, and host captures land.
				if (!Runner.RelayedSettled() || !Runner.HostCapturesWritten())
				{
					if (SettleDeadline == 0.0) SettleDeadline = Now + 10.0;
					if (Now < SettleDeadline) return EScriptActOutcome::Running;
				}
				for (const FStepActionRunner::FRelayed& Req : Runner.Relayed)
				{
					const UMCPPIERemoteControl* Remote = PIERemotePlayers::FindById(Req.Id);
					const FMCPPIERemoteResult* Result = Remote ? Remote->FindResult(Req.Id) : nullptr;
					if (!Result || !Result->bOk)
					{
						Report.Reason = Result
							? FString::Printf(TEXT("client %d could not run %s: %s"), Req.Client, *Req.What, *Result->Error)
							: FString::Printf(TEXT("client %d never confirmed %s"), Req.Client, *Req.What);
						return End(Report, Now, EScriptActOutcome::Failed);
					}
				}
				for (const FStepActionRunner::FCapture& C : Runner.Captures)
				{
					if (C.Written.IsValid() && *C.Written != 1)
					{
						Report.Reason = FString::Printf(TEXT("capture %s was not written"), *C.File);
						return End(Report, Now, EScriptActOutcome::Failed);
					}
					Report.Captures.Add(CaptureRoot / C.File);
				}
				return End(Report, Now, EScriptActOutcome::Done);
			}

			virtual void Cancel() override
			{
				Runner.Reset();
			}

		private:
			void Record(double NowMs, FScriptActReport& Report)
			{
				for (; Reported < Sequence.Records.Num(); ++Reported)
				{
					const FStepActionSequence::FRecord& R = Sequence.Records[Reported];
					TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
					A->SetNumberField(TEXT("index"), R.Index);
					A->SetStringField(TEXT("action"), R.Verb);
					A->SetBoolField(TEXT("ok"), R.bOk);
					if (!R.Error.IsEmpty()) A->SetStringField(TEXT("error"), R.Error);
					A->SetNumberField(TEXT("at_ms"), NowMs);
					Report.Actions.Add(MakeShared<FJsonValueObject>(A));
				}
			}

			EScriptActOutcome End(FScriptActReport& Report, double Now, EScriptActOutcome Outcome)
			{
				Report.WallMs = (Now - WallStart) * 1000.0;
				return Outcome;
			}

			FString CaptureRoot;
			FStepActionRunner Runner;
			FStepActionSequence Sequence;
			int32 Reported = 0;
			double WallStart = 0.0;
			double SettleDeadline = 0.0;
		};
	}

	// ── Runner ──────────────────────────────────────────────────────────

	FPIEScriptRunner& FPIEScriptRunner::Get()
	{
		static FPIEScriptRunner Instance;
		return Instance;
	}

	void FPIEScriptRunner::Init()
	{
		if (EndPIEHandle.IsValid()) return;
		EndPIEHandle = FEditorDelegates::EndPIE.AddLambda([this](bool bSim)
		{
			this->OnEndPIE(bSim);
		});
	}

	void FPIEScriptRunner::Shutdown()
	{
		if (EndPIEHandle.IsValid()) FEditorDelegates::EndPIE.Remove(EndPIEHandle);
		EndPIEHandle.Reset();
		UnbindEndFrame();
		Run = FScriptRun();
		bHasRun = false;
	}

	bool FPIEScriptRunner::IsActive() const
	{
		return bHasRun && (Run.State == EScriptRunState::Armed
			|| Run.State == EScriptRunState::Running
			|| Run.State == EScriptRunState::AwaitingVerdict);
	}

	void FPIEScriptRunner::BindEndFrame()
	{
		if (bEndFrameBound) return;
		EndFrameHandle = FCoreDelegates::OnEndFrame.AddLambda([this]()
		{
			this->OnEndFrame();
		});
		bEndFrameBound = true;
	}

	void FPIEScriptRunner::UnbindEndFrame()
	{
		if (!bEndFrameBound) return;
		FCoreDelegates::OnEndFrame.Remove(EndFrameHandle);
		EndFrameHandle.Reset();
		bEndFrameBound = false;
	}

	bool FPIEScriptRunner::ReadLive(const FScriptExpect& Expect, double& OutValue) const
	{
		const FPIEObserver& Obs = FPIEObserver::Get();
		if (Expect.Op == EScriptExpectOp::Visible)
		{
			bool bVisible = false;
			if (!Obs.ReadWidgetVisible(Run.ObservationRunId, Expect.Value, bVisible)) return false;
			OutValue = bVisible ? 1.0 : 0.0;
			return true;
		}
		if (Expect.Op == EScriptExpectOp::Exists)
		{
			bool bResolved = false;
			if (!Obs.ReadActorResolved(Run.ObservationRunId, Expect.Value, bResolved)) return false;
			OutValue = bResolved ? 1.0 : 0.0;
			return true;
		}
		return Obs.ReadTrackedValue(Run.ObservationRunId, Expect.Value, OutValue);
	}

	void FPIEScriptRunner::AdvanceNow()
	{
		auto Reader = [this](const FScriptExpect& E, double& V) { return ReadLive(E, V); };
		if (Run.bReplay) AdvanceScriptReplay(Run, Run.NowMs, Run.Frame, Reader);
		else AdvanceScriptRun(Run, Run.NowMs, Run.Frame, Reader, StepDriver.Get());
		if (IsScriptRunSettled(Run))
		{
			Finish(EScriptRunState::Finished, TEXT("all steps settled"));
		}
	}

	bool FPIEScriptRunner::Start(const FScriptRunConfig& Cfg, FString& OutError, FString& OutMessage)
	{
		if (IsActive())
		{
			OutError = FString::Printf(TEXT("Script run %s is active; pie_script_stop it first"), *Run.RunId);
			return false;
		}
		if (Cfg.Script.Steps.Num() == 0)
		{
			OutError = TEXT("The script has no steps");
			return false;
		}

		TArray<FString> ProfileValues;
		if (!Cfg.Script.Profile.IsEmpty())
		{
			const UMCPPIEObservationProfile* Profile = LoadObject<UMCPPIEObservationProfile>(nullptr, *Cfg.Script.Profile);
			if (!Profile)
			{
				OutError = FString::Printf(TEXT("Profile not found: %s"), *Cfg.Script.Profile);
				return false;
			}
			for (const FMCPPIETrackedValueEntry& E : Profile->TrackedValues) ProfileValues.Add(E.Path);
		}

		FScriptRun NewRun;
		NewRun.RunId = MakeScriptRunId(TEXT("script"), Cfg.Script.Name);
		NewRun.ScriptPath = Cfg.Script.AssetPath.IsEmpty() ? FString(TEXT("inline")) : Cfg.Script.AssetPath;
		NewRun.ScriptName = Cfg.Script.Name.IsEmpty() ? FString(TEXT("inline")) : Cfg.Script.Name;
		NewRun.Description = Cfg.Script.Description;
		NewRun.Profile = Cfg.Script.Profile;
		NewRun.Map = Cfg.Script.Map;
		NewRun.DefaultWithinMs = Cfg.Script.DefaultWithinMs > 0 ? Cfg.Script.DefaultWithinMs : 5000;
		NewRun.bBlockOnManual = Cfg.bBlockOnManual;
		NewRun.ObservationRunId = NewRun.RunId;
		NewRun.OutputDir = DefaultScriptObservationsRoot() / NewRun.RunId;
		NewRun.ResultsPath = NewRun.OutputDir / TEXT("results.json");
		InitScriptRun(NewRun, Cfg.Script.Steps);

		if (Cfg.bManualMode)
		{
			// Nothing is observed or recorded, and PIE is left as it is: the verdicts are the run.
			NewRun.bManualMode = true;
			NewRun.bBlockOnManual = false;
			NewRun.ObservationRunId.Reset();
			NewRun.ManualStartSeconds = FPlatformTime::Seconds();
			NewRun.StartedAt = ScriptNowIso();
			NewRun.bClockStarted = true;
			NewRun.State = EScriptRunState::Armed;
			Run = MoveTemp(NewRun);
			bHasRun = true;

			TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
			D->SetStringField(TEXT("script"), Run.ScriptPath);
			D->SetStringField(TEXT("script_name"), Run.ScriptName);
			D->SetNumberField(TEXT("steps"), Run.Steps.Num());
			AddScriptEvent(Run, TEXT("run_started"), -1, D);
			StartScriptManualSteps(Run);
			if (AreScriptManualStepsSettled(Run))
			{
				Finish(EScriptRunState::Finished, TEXT("no manual steps"));
			}
			else
			{
				WriteResults();
			}
			OutMessage = FString::Printf(TEXT("Manual script run %s: %d steps awaiting a verdict, no PIE"), *Run.RunId, ScriptAwaitingSteps(Run).Num());
			UE_LOG(LogMCPPIE, Log, TEXT("[PIE-SCRIPT] %s"), *OutMessage);
			return true;
		}

		// Open the script's level before anything arms, so a refusal leaves nothing behind.
		if (!Cfg.Script.Map.IsEmpty() && !MCPLoadMapIfNeeded(Cfg.Script.Map, OutError))
		{
			OutError = FString::Printf(TEXT("Script %s plays in %s: %s"), *NewRun.ScriptName, *Cfg.Script.Map, *OutError);
			return false;
		}

		FObserverArmConfig ObsCfg;
		ObsCfg.ProfilePath = Cfg.Script.Profile;
		ObsCfg.RunId = NewRun.RunId;
		ObsCfg.ClientId = Cfg.ClientId;
		ObsCfg.PIEInstance = Cfg.PIEInstance;
		CollectScriptObservedNames(Cfg.Script.Steps, ProfileValues,
			ObsCfg.ExtraTrackedValues, ObsCfg.ExtraTrackedActors, ObsCfg.ExtraTrackedWidgets);

		FString ObsMsg;
		if (!FPIEObserver::Get().Arm(ObsCfg, OutError, ObsMsg))
		{
			return false;
		}

		if (Cfg.bRecord)
		{
			FRecorderArmConfig RecCfg;
			RecCfg.Id = NewRun.RunId;
			RecCfg.ClientId = Cfg.ClientId;
			RecCfg.PIEInstance = Cfg.PIEInstance;
			FString RecErr, RecMsg;
			if (!FPIEInputRecorder::Get().Arm(RecCfg, RecErr, RecMsg))
			{
				FPIEObserver::Get().StopRun(NewRun.RunId);
				OutError = FString::Printf(TEXT("Recorder could not arm (%s). Pass record=false to run without a recording."), *RecErr);
				return false;
			}
			NewRun.bOwnsRecorder = true;
			NewRun.RecordingId = NewRun.RunId;
			NewRun.RecordingDir = DefaultRecordingsRoot() / NewRun.RunId;
		}

		NewRun.State = EScriptRunState::Armed;
		StepDriver = MakeShared<FEditorScriptStepDriver>(NewRun.OutputDir);
		Run = MoveTemp(NewRun);
		bHasRun = true;

		TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
		D->SetStringField(TEXT("script"), Run.ScriptPath);
		D->SetStringField(TEXT("script_name"), Run.ScriptName);
		D->SetNumberField(TEXT("steps"), Run.Steps.Num());
		D->SetStringField(TEXT("observation_run_id"), Run.ObservationRunId);
		if (!Run.RecordingId.IsEmpty()) D->SetStringField(TEXT("recording_id"), Run.RecordingId);
		AddScriptEvent(Run, TEXT("run_started"), -1, D);

		if (Cfg.bStartPIE && GEditor && !GEditor->PlayWorld)
		{
			FRequestPlaySessionParams PlayParams;
			GEditor->RequestPlaySession(PlayParams);
			Run.bStartedPIE = true;
		}
		BindEndFrame();

		OutMessage = FString::Printf(TEXT("Script run %s armed: %d steps, %s"), *Run.RunId, Run.Steps.Num(), *ObsMsg);
		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-SCRIPT] %s"), *OutMessage);
		return true;
	}

	bool FPIEScriptRunner::StartReplay(const FScriptReplayConfig& Cfg, FString& OutError, FString& OutMessage)
	{
		if (IsActive())
		{
			OutError = FString::Printf(TEXT("Script run %s is active; pie_script_stop it first"), *Run.RunId);
			return false;
		}
		if (!GEditor)
		{
			OutError = TEXT("pie_script_replay requires the editor");
			return false;
		}
		if (GEditor->PlayWorld)
		{
			OutError = TEXT("A PIE session is running; stop it before pie_script_replay");
			return false;
		}

		FString ResultsPath = Cfg.ResultsPath;
		if (ResultsPath.IsEmpty())
		{
			FString SourceId = Cfg.SourceRunId;
			if (SourceId.IsEmpty() && bHasRun) SourceId = Run.RunId;
			if (SourceId.IsEmpty())
			{
				OutError = TEXT("Give run_id or results_path of the run to replay");
				return false;
			}
			ResultsPath = DefaultScriptObservationsRoot() / SourceId / TEXT("results.json");
		}
		const TSharedPtr<FJsonObject> Obj = LoadJsonFile(ResultsPath);
		if (!Obj.IsValid())
		{
			OutError = FString::Printf(TEXT("results.json not found or unreadable: %s"), *ResultsPath);
			return false;
		}
		FScriptRun Source;
		if (!ScriptRunFromResultsJson(Obj, Source, OutError)) return false;
		if (Source.RecordingId.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Run %s has no recording (it ran with record=false), so it cannot be replayed"), *Source.RunId);
			return false;
		}
		const FString RecDir = Source.RecordingDir.IsEmpty() ? DefaultRecordingsRoot() / Source.RecordingId : Source.RecordingDir;
		FSequence Seq;
		FString SeqErr;
		if (!LoadSequence(RecDir / TEXT("sequence.json"), Seq, SeqErr))
		{
			OutError = FString::Printf(TEXT("Recording of run %s is not replayable: %s"), *Source.RunId, *SeqErr);
			return false;
		}

		TArray<FString> ProfileValues;
		if (!Source.Profile.IsEmpty())
		{
			const UMCPPIEObservationProfile* Profile = LoadObject<UMCPPIEObservationProfile>(nullptr, *Source.Profile);
			if (!Profile)
			{
				OutError = FString::Printf(TEXT("Profile of the source run not found: %s"), *Source.Profile);
				return false;
			}
			for (const FMCPPIETrackedValueEntry& E : Profile->TrackedValues) ProfileValues.Add(E.Path);
		}

		FScriptRun NewRun;
		PrepareScriptReplayRun(Source, static_cast<double>(FMath::Max(0, Seq.SettleMs)), NewRun);
		NewRun.RunId = MakeScriptRunId(TEXT("script_replay"), Source.ScriptName);
		NewRun.TimeScale = Cfg.TimeScale;
		NewRun.ObservationRunId = NewRun.RunId;
		NewRun.OutputDir = DefaultScriptObservationsRoot() / NewRun.RunId;
		NewRun.ResultsPath = NewRun.OutputDir / TEXT("results.json");
		NewRun.RecordingId = Source.RecordingId;
		NewRun.RecordingDir = RecDir;

		if (!Source.Map.IsEmpty() && !MCPLoadMapIfNeeded(Source.Map, OutError))
		{
			OutError = FString::Printf(TEXT("Run %s played in %s: %s"), *Source.RunId, *Source.Map, *OutError);
			return false;
		}

		TArray<FScriptStepDef> Defs;
		for (const FScriptStepRun& S : NewRun.Steps) Defs.Add(S.Def);
		FObserverArmConfig ObsCfg;
		ObsCfg.ProfilePath = Source.Profile;
		ObsCfg.RunId = NewRun.RunId;
		ObsCfg.ClientId = Cfg.ClientId;
		ObsCfg.PIEInstance = Cfg.PIEInstance;
		CollectScriptObservedNames(Defs, ProfileValues,
			ObsCfg.ExtraTrackedValues, ObsCfg.ExtraTrackedActors, ObsCfg.ExtraTrackedWidgets);
		FString ObsMsg;
		if (!FPIEObserver::Get().Arm(ObsCfg, OutError, ObsMsg))
		{
			return false;
		}

		FReplayerArmConfig RepCfg;
		RepCfg.SourceRecordingId = Source.RecordingId;
		RepCfg.SourceDir = RecDir;
		// The replayer ends when its input runs out, which can be before the last step window; the run ends PIE itself.
		RepCfg.bAutoStopPIE = false;
		RepCfg.TimeScale = Cfg.TimeScale;
		RepCfg.ClientId = Cfg.ClientId;
		RepCfg.PIEInstance = Cfg.PIEInstance;
		FString RepErr, RepMsg;
		if (!FPIEInputReplayer::Get().Arm(RepCfg, RepErr, RepMsg))
		{
			FPIEObserver::Get().StopRun(NewRun.RunId);
			OutError = FString::Printf(TEXT("Replayer could not arm: %s"), *RepErr);
			return false;
		}
		NewRun.bOwnsReplayer = true;
		NewRun.bEndPIEWhenSettled = Cfg.bAutoStopPIE;
		NewRun.State = EScriptRunState::Armed;
		Run = MoveTemp(NewRun);
		bHasRun = true;

		TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
		D->SetStringField(TEXT("script"), Run.ScriptPath);
		D->SetStringField(TEXT("script_name"), Run.ScriptName);
		D->SetNumberField(TEXT("steps"), Run.Steps.Num());
		D->SetStringField(TEXT("replay_of"), Run.ReplayOf);
		D->SetStringField(TEXT("recording_id"), Run.RecordingId);
		D->SetStringField(TEXT("observation_run_id"), Run.ObservationRunId);
		AddScriptEvent(Run, TEXT("run_started"), -1, D);

		FRequestPlaySessionParams PlayParams;
		GEditor->RequestPlaySession(PlayParams);
		Run.bStartedPIE = true;
		BindEndFrame();

		OutMessage = FString::Printf(TEXT("Replaying run %s as %s (%s)"), *Run.ReplayOf, *Run.RunId, *RepMsg);
		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-SCRIPT] %s"), *OutMessage);
		return true;
	}

	bool FPIEScriptRunner::Stop(const FString& Reason, FString& OutError)
	{
		if (!IsActive())
		{
			OutError = TEXT("No script run is active");
			return false;
		}
		if (Run.bOwnsReplayer && FPIEInputReplayer::Get().IsActive())
		{
			FPIEInputReplayer::Get().ForceStop();
		}
		Finish(EScriptRunState::Stopped, Reason.IsEmpty() ? FString(TEXT("stopped")) : Reason);
		return true;
	}

	bool FPIEScriptRunner::Verdict(int32 Step, const FString& InVerdict, const FString& Note, const FString& Source,
		const FScriptVerdictEvidence& Evidence, bool& bOutUnchanged, FString& OutError)
	{
		if (!bHasRun)
		{
			OutError = TEXT("No script run to give a verdict on");
			return false;
		}
		if (Run.bManualMode && IsActive())
		{
			Run.NowMs = (FPlatformTime::Seconds() - Run.ManualStartSeconds) * 1000.0;
		}
		if (!ApplyScriptVerdict(Run, Step, InVerdict, Note, Source, Evidence, bOutUnchanged, OutError)) return false;
		if (bOutUnchanged) return true;
		if (Run.bManualMode)
		{
			if (IsActive() && AreScriptManualStepsSettled(Run)) Finish(EScriptRunState::Finished, TEXT("all manual steps settled"));
			else WriteResults();
		}
		else if (IsActive())
		{
			if (Run.State != EScriptRunState::Armed) AdvanceNow();
		}
		else
		{
			WriteResults();
		}
		return true;
	}

	void FPIEScriptRunner::OnEndFrame()
	{
		if (!IsActive())
		{
			UnbindEndFrame();
			return;
		}
		FObserverRunClock Clock;
		if (!FPIEObserver::Get().GetRunClock(Run.ObservationRunId, Clock))
		{
			Finish(EScriptRunState::Finished, TEXT("observation ended"));
			return;
		}
		if (Clock.State == EObserverState::Completed)
		{
			const FString Err = FPIEObserver::Get().GetStatus().LastError;
			Finish(EScriptRunState::Finished, Err.IsEmpty() ? FString(TEXT("observation ended")) : Err);
			return;
		}
		if (Clock.State != EObserverState::Observing || Clock.FramesSampled == 0) return;

		const int64 Frame = static_cast<int64>(Clock.LastFrame);
		if (Run.bClockStarted && Frame == Run.Frame) return;
		Run.NowMs = Clock.ElapsedSeconds * 1000.0;
		Run.Frame = Frame;
		if (!Run.bClockStarted)
		{
			Run.bClockStarted = true;
			Run.StartedAt = ScriptNowIso();
			Run.State = EScriptRunState::Running;
		}
		AdvanceNow();
	}

	void FPIEScriptRunner::OnEndPIE(bool /*bIsSimulating*/)
	{
		// A manual run does not watch PIE, so PIE ending does not end it.
		if (IsActive() && !Run.bManualMode)
		{
			Finish(EScriptRunState::Finished, TEXT("PIE ended"));
		}
	}

	void FPIEScriptRunner::Finish(EScriptRunState FinalState, const FString& Reason)
	{
		if (!IsActive()) return;
		if (StepDriver.IsValid())
		{
			StepDriver->Cancel();
			StepDriver.Reset();
		}
		SettleUnfinishedScriptSteps(Run, FString::Printf(TEXT("run ended before the step settled (%s)"), *Reason));

		if (Run.bManualMode)
		{
			Run.NowMs = (FPlatformTime::Seconds() - Run.ManualStartSeconds) * 1000.0;
		}
		else
		{
			FPIEObserver::Get().StopRun(Run.ObservationRunId);
		}

		if (Run.bOwnsRecorder)
		{
			FPIEInputRecorder& Rec = FPIEInputRecorder::Get();
			const FRecorderStatus RS = Rec.GetStatus();
			if (RS.Id == Run.RecordingId)
			{
				if (RS.State == ERecorderState::Recording || RS.State == ERecorderState::WaitingForPawn)
				{
					Rec.ForceStop();
				}
				else if (RS.State == ERecorderState::Armed)
				{
					FString Ignored;
					Rec.Disarm(Ignored);
				}
			}
		}

		Run.State = FinalState;
		Run.StopReason = Reason;
		Run.EndedAt = ScriptNowIso();
		if (Run.StartedAt.IsEmpty()) Run.StartedAt = Run.EndedAt;

		TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
		D->SetStringField(TEXT("state"), ScriptRunStateToString(FinalState));
		D->SetStringField(TEXT("reason"), Reason);
		D->SetObjectField(TEXT("summary"), ScriptSummaryToJson(SummarizeScriptRun(Run), Run.bReplay));
		D->SetStringField(TEXT("results_path"), Run.ResultsPath);
		TArray<int32> Awaiting = ScriptAwaitingSteps(Run);
		if (Awaiting.Num() > 0) D->SetArrayField(TEXT("awaiting"), ScriptIndicesToJson(Awaiting));
		AddScriptEvent(Run, TEXT("run_finished"), -1, D);

		WriteResults();
		UnbindEndFrame();
		if (Run.bEndPIEWhenSettled && FinalState == EScriptRunState::Finished && GEditor && GEditor->PlayWorld)
		{
			GEditor->RequestEndPlayMap();
		}
		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-SCRIPT] Run %s %s: %s -> %s"),
			*Run.RunId, *ScriptRunStateToString(FinalState), *Reason, *Run.ResultsPath);
	}

	bool FPIEScriptRunner::WriteResults()
	{
		IFileManager::Get().MakeDirectory(*Run.OutputDir, true);
		FString Json;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
		TSharedRef<FJsonObject> Results = ScriptRunToResultsJson(Run);
		// The events go to disk too, so a run the editor no longer holds still answers script_status.
		TArray<TSharedPtr<FJsonValue>> Events;
		for (const FScriptEvent& E : Run.Events) Events.Add(MakeShared<FJsonValueObject>(ScriptEventToJson(E, Run.RunId)));
		Results->SetArrayField(TEXT("events"), Events);
		Results->SetNumberField(TEXT("last_seq"), Run.NextSeq - 1);
		FJsonSerializer::Serialize(Results, Writer);
		const bool bOk = FFileHelper::SaveStringToFile(Json, *Run.ResultsPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		if (!bOk)
		{
			UE_LOG(LogMCPPIE, Warning, TEXT("[PIE-SCRIPT] results.json write failed: %s"), *Run.ResultsPath);
		}
		return bOk;
	}
}
