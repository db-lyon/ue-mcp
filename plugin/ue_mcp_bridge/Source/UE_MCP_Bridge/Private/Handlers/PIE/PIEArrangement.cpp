#include "Handlers/PIE/PIEArrangement.h"

namespace MCPPIE
{
	namespace
	{
		const TCHAR* const HubExpectOpNames[] = { TEXT("decreasing"), TEXT("increasing"), TEXT("eq"), TEXT("gt"), TEXT("lt"), TEXT("visible"), TEXT("exists"), TEXT("triggered") };
		const TCHAR* const HubCheckModeNames[] = { TEXT("by_end"), TEXT("throughout"), TEXT("hold"), TEXT("manual") };
		const TCHAR* const HubCheckStatusNames[] = { TEXT("pending"), TEXT("running"), TEXT("passed"), TEXT("failed"), TEXT("awaiting_verdict"), TEXT("skipped") };
		constexpr double TimelineEps = 1e-6;

		FString Num(double V)
		{
			const double R = FMath::RoundToDouble(V * 1000.0) / 1000.0;
			return FMath::IsNearlyEqual(R, FMath::RoundToDouble(R)) ? FString::Printf(TEXT("%lld"), static_cast<long long>(FMath::RoundToDouble(R))) : FString::SanitizeFloat(R);
		}

		void Settle(FHubCheckRun& Run, EHubCheckStatus Status, double Frame, const FString& Message)
		{
			Run.Status = Status;
			Run.Frame = Frame;
			Run.Message = Message;
		}

		// An input lane's times moved to the instance's frame and scaled to the arrangement's rate.
		void PlaceInput(FClipTrack& T, double At, double Scale)
		{
			for (FClipNote& N : T.Notes) { N.Start = At + N.Start * Scale; N.Length *= Scale; }
			for (FClipKey& K : T.Keys) K.Time = At + K.Time * Scale;
		}
	}

	// ── Expectations ───────────────────────────────────────────────────

	FString HubExpectOpToString(EHubExpectOp Op) { return HubExpectOpNames[static_cast<int32>(Op)]; }

	FString FHubExpect::Path() const
	{
		return Subject.IsEmpty() ? Value : Subject + TEXT(".") + Value;
	}

	FString FHubExpect::Describe() const
	{
		FString S = FString::Printf(TEXT("%s %s"), *Path(), *HubExpectOpToString(Op));
		if (bHasTarget) S += TEXT(" ") + Num(Target);
		return S;
	}

	bool ParseHubExpect(const TSharedPtr<FJsonObject>& Obj, FHubExpect& Out, FString& OutError)
	{
		if (!Obj.IsValid())
		{
			OutError = TEXT("expect is {value, op, target?, subject?}");
			return false;
		}
		FString Op;
		Obj->TryGetStringField(TEXT("op"), Op);
		bool bOp = false;
		for (int32 i = 0; i < UE_ARRAY_COUNT(HubExpectOpNames); ++i)
		{
			if (Op.Equals(HubExpectOpNames[i], ESearchCase::IgnoreCase)) { Out.Op = static_cast<EHubExpectOp>(i); bOp = true; break; }
		}
		if (!bOp)
		{
			OutError = FString::Printf(TEXT("expect.op '%s' is not one of decreasing, increasing, eq, gt, lt, visible, exists, triggered"), *Op);
			return false;
		}
		if (!Obj->TryGetStringField(TEXT("value"), Out.Value) || Out.Value.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("expect.value names what the check reads");
			return false;
		}
		Out.Value.TrimStartAndEndInline();
		if (Out.Op == EHubExpectOp::Triggered) Out.Value = NormalizeInputActionPath(Out.Value);
		Obj->TryGetStringField(TEXT("subject"), Out.Subject);
		Out.Subject.TrimStartAndEndInline();
		const TSharedPtr<FJsonValue> Target = Obj->TryGetField(TEXT("target"));
		if (Target.IsValid() && !Target->IsNull())
		{
			if (Target->Type == EJson::Boolean) Out.Target = Target->AsBool() ? 1.0 : 0.0;
			else if (Target->Type == EJson::Number) Out.Target = Target->AsNumber();
			else
			{
				OutError = TEXT("expect.target is a number or a boolean");
				return false;
			}
			Out.bHasTarget = true;
		}
		else if (Out.Op == EHubExpectOp::Eq || Out.Op == EHubExpectOp::Gt || Out.Op == EHubExpectOp::Lt)
		{
			OutError = FString::Printf(TEXT("expect.op %s needs a target"), *Op);
			return false;
		}
		return true;
	}

	bool EvaluateHubExpect(const FHubExpect& Expect, double AtStart, double Current)
	{
		switch (Expect.Op)
		{
		case EHubExpectOp::Decreasing: return Expect.bHasTarget ? AtStart - Current >= Expect.Target : Current < AtStart;
		case EHubExpectOp::Increasing: return Expect.bHasTarget ? Current - AtStart >= Expect.Target : Current > AtStart;
		case EHubExpectOp::Eq:
		{
			const double Tolerance = FMath::Max(1e-4, 1e-4 * FMath::Max(FMath::Abs(Current), FMath::Abs(Expect.Target)));
			return FMath::Abs(Current - Expect.Target) <= Tolerance;
		}
		case EHubExpectOp::Gt: return Current > Expect.Target;
		case EHubExpectOp::Lt: return Current < Expect.Target;
		case EHubExpectOp::Visible:
		case EHubExpectOp::Exists:
			return (Current != 0.0) == (!Expect.bHasTarget || Expect.Target != 0.0);
		case EHubExpectOp::Triggered:
			return Current != 0.0;
		}
		return false;
	}

	// ── Checks ─────────────────────────────────────────────────────────

	FString HubCheckModeToString(EHubCheckMode Mode) { return HubCheckModeNames[static_cast<int32>(Mode)]; }
	bool ParseHubCheckMode(const FString& Str, EHubCheckMode& Out)
	{
		for (int32 i = 0; i < UE_ARRAY_COUNT(HubCheckModeNames); ++i)
		{
			if (Str.Equals(HubCheckModeNames[i], ESearchCase::IgnoreCase)) { Out = static_cast<EHubCheckMode>(i); return true; }
		}
		return false;
	}
	FString HubCheckStatusToString(EHubCheckStatus Status) { return HubCheckStatusNames[static_cast<int32>(Status)]; }

	double FHubCheck::HoldLimit(double Rate) const
	{
		if (Length > 0.0) return Length;
		if (Timeout > 0.0) return Timeout;
		return 10.0 * Rate;
	}

	bool HubCheckFromJson(const TSharedPtr<FJsonObject>& Obj, int32 Index, FHubCheck& Out, FString& OutError)
	{
		const FString Where = FString::Printf(TEXT("checks[%d]"), Index);
		if (!Obj.IsValid())
		{
			OutError = Where + TEXT(" is an object");
			return false;
		}
		if (!Obj->TryGetStringField(TEXT("id"), Out.Id) || Out.Id.IsEmpty()) Out.Id = FString::Printf(TEXT("c%d"), Index + 1);
		if (!Obj->TryGetStringField(TEXT("title"), Out.Title) || Out.Title.IsEmpty()) Out.Title = Out.Id;
		Obj->TryGetNumberField(TEXT("at"), Out.At);
		Obj->TryGetNumberField(TEXT("length"), Out.Length);
		Obj->TryGetNumberField(TEXT("timeout"), Out.Timeout);
		if (Out.At < 0.0 || Out.Length < 0.0)
		{
			OutError = FString::Printf(TEXT("%s (%s): at and length are 0 or more"), *Where, *Out.Id);
			return false;
		}
		FString Mode;
		Obj->TryGetStringField(TEXT("mode"), Mode);
		if (!ParseHubCheckMode(Mode, Out.Mode))
		{
			OutError = FString::Printf(TEXT("%s (%s): mode '%s' is not by_end, throughout, hold or manual"), *Where, *Out.Id, *Mode);
			return false;
		}
		Obj->TryGetStringField(TEXT("instruction"), Out.Instruction);
		Obj->TryGetBoolField(TEXT("personOnly"), Out.bPersonOnly);
		if (Out.Mode == EHubCheckMode::Manual) return true;
		const TSharedPtr<FJsonObject>* Expect = nullptr;
		if (!Obj->TryGetObjectField(TEXT("expect"), Expect) || !Expect)
		{
			OutError = FString::Printf(TEXT("%s (%s): a %s check needs expect {value, op, target?}"), *Where, *Out.Id, *Mode);
			return false;
		}
		FString Err;
		if (!ParseHubExpect(*Expect, Out.Expect, Err))
		{
			OutError = FString::Printf(TEXT("%s (%s): %s"), *Where, *Out.Id, *Err);
			return false;
		}
		return true;
	}

	TSharedRef<FJsonObject> HubCheckRunToJson(const FHubCheckRun& Run)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"), Run.Def.Id);
		O->SetStringField(TEXT("status"), HubCheckStatusToString(Run.Status));
		if (Run.Frame >= 0.0) O->SetNumberField(TEXT("frame"), FMath::RoundToDouble(Run.Frame * 1000.0) / 1000.0);
		FString Message = Run.Message;
		if (!Run.Note.IsEmpty()) Message = Message.IsEmpty() ? Run.Note : Message + TEXT(": ") + Run.Note;
		if (Run.Status == EHubCheckStatus::AwaitingVerdict && Message.IsEmpty()) Message = Run.Def.Instruction;
		if (!Message.IsEmpty()) O->SetStringField(TEXT("message"), Message);
		return O;
	}

	// ── Compile ────────────────────────────────────────────────────────

	bool CompileArrangement(const TSharedPtr<FJsonObject>& Arrangement, const TSharedPtr<FJsonObject>& Clips,
		const FString& Region, FArrangementPlan& Out, FString& OutError)
	{
		Out = FArrangementPlan();
		if (!Arrangement.IsValid())
		{
			OutError = TEXT("arrangement is the arrangement to play, as an object");
			return false;
		}
		Arrangement->TryGetStringField(TEXT("id"), Out.Id);
		Arrangement->TryGetStringField(TEXT("level"), Out.Level);
		Arrangement->TryGetNumberField(TEXT("rate"), Out.Rate);
		if (Out.Rate <= 0.0 || Out.Rate > 1000.0)
		{
			OutError = TEXT("the arrangement's rate is frames per second, more than 0 and at most 1000");
			return false;
		}
		Out.RegionId = Region;

		// Regions, and the one played.
		double RegionStart = 0.0, RegionEnd = -1.0, RegionsEnd = 0.0;
		TArray<FString> RegionIds;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Arrangement->TryGetArrayField(TEXT("regions"), Arr) && Arr)
		{
			for (int32 i = 0; i < Arr->Num(); ++i)
			{
				const TSharedPtr<FJsonObject>* R = nullptr;
				FString Id;
				double Start = 0.0, End = 0.0;
				if (!(*Arr)[i]->TryGetObject(R) || !R || !(*R)->TryGetNumberField(TEXT("start"), Start) || !(*R)->TryGetNumberField(TEXT("end"), End) || End <= Start)
				{
					OutError = FString::Printf(TEXT("regions[%d] needs start and an end after it"), i);
					return false;
				}
				(*R)->TryGetStringField(TEXT("id"), Id);
				RegionIds.Add(Id);
				RegionsEnd = FMath::Max(RegionsEnd, End);
				if (!Region.IsEmpty() && Id == Region) { RegionStart = Start; RegionEnd = End; }
			}
		}
		if (!Region.IsEmpty() && RegionEnd < 0.0)
		{
			OutError = FString::Printf(TEXT("the arrangement has no region '%s' (regions: %s)"), *Region, RegionIds.Num() ? *FString::Join(RegionIds, TEXT(", ")) : TEXT("none"));
			return false;
		}

		// Tracks: the audible ones, each instance's clip placed on its timeline.
		double ContentEnd = 0.0;
		if (Arrangement->TryGetArrayField(TEXT("tracks"), Arr) && Arr)
		{
			bool bSolo = false;
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				const TSharedPtr<FJsonObject>* T = nullptr;
				bool b = false;
				if (V->TryGetObject(T) && T && (*T)->TryGetBoolField(TEXT("solo"), b) && b) bSolo = true;
			}
			for (int32 ti = 0; ti < Arr->Num(); ++ti)
			{
				const TSharedPtr<FJsonObject>* T = nullptr;
				if (!(*Arr)[ti]->TryGetObject(T) || !T)
				{
					OutError = FString::Printf(TEXT("tracks[%d] is an object"), ti);
					return false;
				}
				bool bMute = false, bTrackSolo = false;
				(*T)->TryGetBoolField(TEXT("mute"), bMute);
				(*T)->TryGetBoolField(TEXT("solo"), bTrackSolo);
				if (bSolo ? !bTrackSolo : bMute) continue;
				FString TrackId;
				if (!(*T)->TryGetStringField(TEXT("id"), TrackId) || TrackId.IsEmpty()) TrackId = FString::Printf(TEXT("t%d"), ti + 1);
				FClipPerformer Performer;
				FString Err;
				if (!ClipPerformerFromJson((*T)->TryGetField(TEXT("performer")), Performer, Err))
				{
					OutError = FString::Printf(TEXT("track %s: %s"), *TrackId, *Err);
					return false;
				}
				Out.Performers.AddUnique(Performer);
				const TArray<TSharedPtr<FJsonValue>>* Instances = nullptr;
				if (!(*T)->TryGetArrayField(TEXT("instances"), Instances) || !Instances) continue;
				for (int32 ii = 0; ii < Instances->Num(); ++ii)
				{
					const TSharedPtr<FJsonObject>* I = nullptr;
					FString InstId, ClipId;
					double At = 0.0;
					if (!(*Instances)[ii]->TryGetObject(I) || !I || !(*I)->TryGetStringField(TEXT("clip"), ClipId) || !(*I)->TryGetNumberField(TEXT("at"), At) || At < 0.0)
					{
						OutError = FString::Printf(TEXT("track %s: instances[%d] needs clip and at"), *TrackId, ii);
						return false;
					}
					if (!(*I)->TryGetStringField(TEXT("id"), InstId) || InstId.IsEmpty()) InstId = FString::Printf(TEXT("i%d"), ii + 1);
					const TSharedPtr<FJsonObject>* ClipObj = nullptr;
					if (!Clips.IsValid() || !Clips->TryGetObjectField(ClipId, ClipObj) || !ClipObj)
					{
						OutError = FString::Printf(TEXT("track %s: instance %s places clip %s, which clips does not hold"), *TrackId, *InstId, *ClipId);
						return false;
					}
					FString ClipKind;
					EClipPerformerKind Kind;
					if ((*ClipObj)->TryGetStringField(TEXT("performer"), ClipKind) && ParseClipPerformerKind(ClipKind, Kind) && Kind != Performer.Kind)
					{
						OutError = FString::Printf(TEXT("track %s: clip %s is for a %s, and the track drives %s"), *TrackId, *ClipId, *ClipKind, *Performer.Label());
						return false;
					}
					FClip Clip;
					if (!ClipFromJson(*ClipObj, Clip, Err))
					{
						OutError = FString::Printf(TEXT("clip %s: %s"), *ClipId, *Err);
						return false;
					}
					const double Scale = Out.Rate / Clip.Rate;
					const double InstEnd = At + Clip.EffectiveLength() * Scale;
					ContentEnd = FMath::Max(ContentEnd, InstEnd);
					for (FClipTrack& Track : Clip.Input)
					{
						FPlacedInput P;
						P.Track = MoveTemp(Track);
						P.Track.Client = Performer.Client;
						PlaceInput(P.Track, At, Scale);
						P.From = At;
						P.To = InstEnd;
						Out.Input.Add(MoveTemp(P));
					}
				}
			}
		}

		// The Checks track: those of the region played, or all.
		double ChecksEnd = 0.0;
		if (Arrangement->TryGetArrayField(TEXT("checks"), Arr) && Arr)
		{
			for (int32 i = 0; i < Arr->Num(); ++i)
			{
				const TSharedPtr<FJsonObject>* C = nullptr;
				FHubCheckRun Run;
				if (!(*Arr)[i]->TryGetObject(C) || !C || !HubCheckFromJson(*C, i, Run.Def, OutError)) return false;
				if (!Region.IsEmpty() && (Run.Def.At < RegionStart - TimelineEps || Run.Def.At >= RegionEnd - TimelineEps))
				{
					Run.Status = EHubCheckStatus::Skipped;
					Run.Message = FString::Printf(TEXT("outside region %s, the one played"), *Region);
					Run.bOutOfPlay = true;
				}
				else ChecksEnd = FMath::Max(ChecksEnd, Run.Def.Mode == EHubCheckMode::Hold || Run.Def.Mode == EHubCheckMode::Manual ? Run.Def.At : Run.Def.End());
				Out.Checks.Add(MoveTemp(Run));
			}
			Out.Checks.StableSort([](const FHubCheckRun& A, const FHubCheckRun& B) { return A.Def.At < B.Def.At; });
		}
		Out.End = Region.IsEmpty() ? FMath::Max3(ContentEnd, ChecksEnd, RegionsEnd) : FMath::Max(RegionEnd, ChecksEnd);
		Out.RegionStart = Region.IsEmpty() ? 0.0 : RegionStart;
		Out.LoopStart = Out.RegionStart;
		return true;
	}

	bool SetPlayhead(FArrangementPlan& Plan, double From, bool bLoop, FString& OutError)
	{
		if (From < 0.0 || (From > 0.0 && From >= Plan.End - TimelineEps))
		{
			OutError = FString::Printf(TEXT("from is a frame from 0 up to the end at %s, not %s"), *Num(Plan.End), *Num(From));
			return false;
		}
		Plan.Start = From;
		Plan.bLoop = bLoop;
		Plan.LoopStart = From > 0.0 ? From : Plan.RegionStart;
		if (bLoop && Plan.End - Plan.LoopStart < 1.0)
		{
			OutError = FString::Printf(TEXT("a loop needs at least one frame between its start at %s and the end at %s"), *Num(Plan.LoopStart), *Num(Plan.End));
			return false;
		}
		for (FHubCheckRun& C : Plan.Checks)
		{
			if (C.IsSettled() || C.Def.At >= From - TimelineEps) continue;
			Settle(C, EHubCheckStatus::Skipped, -1.0, FString::Printf(TEXT("starts before the playhead at frame %s"), *Num(From)));
		}
		return true;
	}

	// ── Timeline ───────────────────────────────────────────────────────

	void FArrangementTimeline::Advance(double Frames)
	{
		if (Frames <= 0.0) return;
		if (Holding != INDEX_NONE)
		{
			Plan.Checks[Holding].HeldFrames += Frames;
			return;
		}
		double Next = Position + Frames;
		for (int32 i = 0; i < Plan.Checks.Num(); ++i)
		{
			const FHubCheckRun& C = Plan.Checks[i];
			if (C.IsSettled() || (C.Def.Mode != EHubCheckMode::Hold && C.Def.Mode != EHubCheckMode::Manual)) continue;
			if (C.Def.At > Position - TimelineEps && C.Def.At <= Next + TimelineEps)
			{
				Next = C.Def.At;
				Holding = i;
				break;
			}
		}
		Position = Next;
	}

	void FArrangementTimeline::Judge(TFunctionRef<bool(const FHubCheckRun&, double&)> Read)
	{
		for (int32 i = 0; i < Plan.Checks.Num(); ++i)
		{
			FHubCheckRun& C = Plan.Checks[i];
			if (C.IsSettled()) continue;
			const FHubCheck& D = C.Def;
			if (C.Status == EHubCheckStatus::Pending)
			{
				if (Position < D.At - TimelineEps) continue;
				if (D.Mode == EHubCheckMode::Manual)
				{
					C.Status = EHubCheckStatus::AwaitingVerdict;
					if (Holding == INDEX_NONE) Holding = i;
					continue;
				}
				C.Status = EHubCheckStatus::Running;
				if (D.Mode == EHubCheckMode::Hold && Holding == INDEX_NONE && FMath::IsNearlyEqual(Position, D.At, 1e-3)) Holding = i;
			}
			if (C.Status != EHubCheckStatus::Running) continue;
			double Value = 0.0;
			const bool bObserved = Read(C, Value);
			if (bObserved)
			{
				if (!C.bHasAtStart) { C.bHasAtStart = true; C.AtStart = Value; }
				C.bHasLast = true;
				C.Last = Value;
			}
			const bool bHolds = bObserved && EvaluateHubExpect(D.Expect, C.AtStart, Value);
			const FString What = D.Expect.Describe();
			switch (D.Mode)
			{
			case EHubCheckMode::ByEnd:
				if (bHolds) Settle(C, EHubCheckStatus::Passed, Position, FString());
				else if (Position >= D.End() - TimelineEps)
				{
					Settle(C, EHubCheckStatus::Failed, Position, C.bHasLast
						? FString::Printf(TEXT("%s was not true by frame %s (last value %s)"), *What, *Num(D.End()), *Num(C.Last))
						: FString::Printf(TEXT("%s could not be read by frame %s"), *What, *Num(D.End())));
				}
				break;
			case EHubCheckMode::Throughout:
				if (!bHolds)
				{
					Settle(C, EHubCheckStatus::Failed, Position, bObserved
						? FString::Printf(TEXT("%s stopped holding at frame %s (value %s)"), *What, *Num(Position), *Num(Value))
						: FString::Printf(TEXT("%s could not be read at frame %s"), *What, *Num(Position)));
				}
				else if (Position >= D.End() - TimelineEps) Settle(C, EHubCheckStatus::Passed, Position, FString());
				break;
			case EHubCheckMode::Hold:
				if (bHolds) Settle(C, EHubCheckStatus::Passed, Position, FString());
				else if (C.HeldFrames >= D.HoldLimit(Plan.Rate) - TimelineEps)
				{
					Settle(C, EHubCheckStatus::Failed, Position, FString::Printf(TEXT("%s did not become true within %s frames held at frame %s%s"),
						*What, *Num(D.HoldLimit(Plan.Rate)), *Num(Position), C.bHasLast ? *FString::Printf(TEXT(" (last value %s)"), *Num(C.Last)) : TEXT("")));
				}
				break;
			case EHubCheckMode::Manual:
				break;
			}
			if (Holding == i && C.IsSettled()) Holding = INDEX_NONE;
		}
	}

	bool FArrangementTimeline::AwaitingVerdict() const
	{
		return Plan.Checks.ContainsByPredicate([](const FHubCheckRun& C) { return C.Status == EHubCheckStatus::AwaitingVerdict; });
	}

	bool FArrangementTimeline::Done() const
	{
		if (Plan.bLoop || Position < Plan.End - TimelineEps || Holding != INDEX_NONE) return false;
		return !Plan.Checks.ContainsByPredicate([](const FHubCheckRun& C) { return C.Status == EHubCheckStatus::AwaitingVerdict; });
	}

	bool FArrangementTimeline::Wrap()
	{
		if (!Plan.bLoop || Position < Plan.End - TimelineEps || Holding != INDEX_NONE || AwaitingVerdict()) return false;
		// A check the region play skips stays skipped; every other one from the loop start is judged again.
		for (FHubCheckRun& C : Plan.Checks)
		{
			if (C.bOutOfPlay || C.Def.At < Plan.LoopStart - TimelineEps) continue;
			FHubCheckRun Fresh;
			Fresh.Def = C.Def;
			C = MoveTemp(Fresh);
		}
		Position = Plan.LoopStart;
		++Pass;
		return true;
	}

	bool FArrangementTimeline::Verdict(const FString& CheckId, const FString& InVerdict, const FString& Note, FString& OutError)
	{
		const int32 Index = Plan.Checks.IndexOfByPredicate([&CheckId](const FHubCheckRun& C) { return C.Def.Id == CheckId; });
		if (Index == INDEX_NONE)
		{
			TArray<FString> Ids;
			for (const FHubCheckRun& C : Plan.Checks) if (C.Def.Mode == EHubCheckMode::Manual) Ids.Add(C.Def.Id);
			OutError = FString::Printf(TEXT("the run has no check '%s' (manual checks: %s)"), *CheckId, Ids.Num() ? *FString::Join(Ids, TEXT(", ")) : TEXT("none"));
			return false;
		}
		FHubCheckRun& C = Plan.Checks[Index];
		if (C.Def.Mode != EHubCheckMode::Manual)
		{
			OutError = FString::Printf(TEXT("check %s is a %s check, judged from what the game reads, not by a verdict"), *CheckId, *HubCheckModeToString(C.Def.Mode));
			return false;
		}
		if (C.Status != EHubCheckStatus::AwaitingVerdict)
		{
			OutError = FString::Printf(TEXT("check %s is %s, not awaiting a verdict"), *CheckId, *HubCheckStatusToString(C.Status));
			return false;
		}
		const FString V = InVerdict.ToLower();
		if (V != TEXT("pass") && V != TEXT("fail"))
		{
			OutError = FString::Printf(TEXT("verdict is pass or fail, not '%s'"), *InVerdict);
			return false;
		}
		Settle(C, V == TEXT("pass") ? EHubCheckStatus::Passed : EHubCheckStatus::Failed, Position, FString());
		C.Note = Note.TrimStartAndEnd();
		if (Holding == Index) Holding = INDEX_NONE;
		return true;
	}

	void FArrangementTimeline::SettleAtEnd(const FString& Reason)
	{
		for (FHubCheckRun& C : Plan.Checks)
		{
			if (C.Status == EHubCheckStatus::Running || C.Status == EHubCheckStatus::AwaitingVerdict) Settle(C, EHubCheckStatus::Failed, Position, Reason);
			else if (C.Status == EHubCheckStatus::Pending) Settle(C, EHubCheckStatus::Skipped, -1.0, Reason);
		}
		Holding = INDEX_NONE;
	}
}
