#include "Handlers/PIE/PIEPredicateEvaluator.h"
#include "Handlers/PIE/PIESequenceFormat.h"   // SplitCSVLine
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Templates/Function.h"

namespace MCPPIE
{
	namespace
	{
		bool ParseOp(const FString& S, EPredOp& Out)
		{
			const FString L = S.ToLower();
			if (L == TEXT("eq"))        { Out = EPredOp::Eq; return true; }
			if (L == TEXT("ne"))        { Out = EPredOp::Ne; return true; }
			if (L == TEXT("lt"))        { Out = EPredOp::Lt; return true; }
			if (L == TEXT("lte"))       { Out = EPredOp::Lte; return true; }
			if (L == TEXT("gt"))        { Out = EPredOp::Gt; return true; }
			if (L == TEXT("gte"))       { Out = EPredOp::Gte; return true; }
			if (L == TEXT("approx"))    { Out = EPredOp::Approx; return true; }
			if (L == TEXT("between"))   { Out = EPredOp::Between; return true; }
			if (L == TEXT("changed"))   { Out = EPredOp::Changed; return true; }
			if (L == TEXT("increased")) { Out = EPredOp::Increased; return true; }
			if (L == TEXT("decreased")) { Out = EPredOp::Decreased; return true; }
			if (L == TEXT("crossed"))   { Out = EPredOp::Crossed; return true; }
			return false;
		}

		bool ParseHold(const FString& S, EPredHold& Out)
		{
			const FString L = S.ToLower();
			if (L == TEXT("always"))           { Out = EPredHold::Always; return true; }
			if (L == TEXT("never"))            { Out = EPredHold::Never; return true; }
			if (L == TEXT("eventually"))       { Out = EPredHold::Eventually; return true; }
			if (L == TEXT("at_end"))           { Out = EPredHold::AtEnd; return true; }
			if (L == TEXT("once_then_always")) { Out = EPredHold::OnceThenAlways; return true; }
			return false;
		}

		const TCHAR* OpToStr(EPredOp Op)
		{
			switch (Op)
			{
			case EPredOp::Eq: return TEXT("==");        case EPredOp::Ne: return TEXT("!=");
			case EPredOp::Lt: return TEXT("<");         case EPredOp::Lte: return TEXT("<=");
			case EPredOp::Gt: return TEXT(">");         case EPredOp::Gte: return TEXT(">=");
			case EPredOp::Approx: return TEXT("~=");    case EPredOp::Between: return TEXT("between");
			case EPredOp::Changed: return TEXT("changed"); case EPredOp::Increased: return TEXT("increased");
			case EPredOp::Decreased: return TEXT("decreased"); case EPredOp::Crossed: return TEXT("crossed");
			}
			return TEXT("?");
		}

		const TCHAR* HoldToStr(EPredHold H)
		{
			switch (H)
			{
			case EPredHold::Always: return TEXT("always");
			case EPredHold::Never: return TEXT("never");
			case EPredHold::Eventually: return TEXT("eventually");
			case EPredHold::AtEnd: return TEXT("at_end");
			case EPredHold::OnceThenAlways: return TEXT("once_then_always");
			}
			return TEXT("?");
		}

		TSharedPtr<FJsonObject> LoadJsonObject(const FString& Path)
		{
			FString Str;
			if (Path.IsEmpty() || !FFileHelper::LoadFileToString(Str, *Path)) return nullptr;
			TSharedPtr<FJsonObject> Obj;
			TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Str);
			if (!FJsonSerializer::Deserialize(R, Obj) || !Obj.IsValid()) return nullptr;
			return Obj;
		}
	}

	int32 FSeriesTable::ResolveColumn(const FString& Channel) const
	{
		if (const int32* P = ColIndex.Find(Channel)) return *P;
		if (const int32* P = ColIndex.Find(FString(TEXT("t:")) + Channel)) return *P;
		return INDEX_NONE;
	}

	bool FPIEPredicateEvaluator::ParsePredicates(const TArray<TSharedPtr<FJsonValue>>& In,
	                                             TArray<FPredicate>& Out, FString& OutError)
	{
		for (int32 Idx = 0; Idx < In.Num(); ++Idx)
		{
			const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
			if (!In[Idx].IsValid() || !In[Idx]->TryGetObject(ObjPtr) || !ObjPtr)
			{
				OutError = FString::Printf(TEXT("assertion[%d] is not an object"), Idx);
				return false;
			}
			const TSharedPtr<FJsonObject>& O = *ObjPtr;

			FPredicate P;
			O->TryGetStringField(TEXT("name"), P.Name);
			if (P.Name.IsEmpty()) P.Name = FString::Printf(TEXT("assertion_%d"), Idx);

			// Severity.
			FString Sev;
			if (O->TryGetStringField(TEXT("severity"), Sev) && Sev.ToLower() == TEXT("warn"))
				P.Severity = EPredSeverity::Warn;

			// Kind: event or channel.
			FString EventStr;
			const bool bIsEvent = O->TryGetStringField(TEXT("event"), EventStr);
			if (bIsEvent)
			{
				const FString E = EventStr.ToLower();
				if (E == TEXT("montage"))
				{
					P.Kind = EPredKind::Montage;
					if (!O->TryGetStringField(TEXT("montage"), P.Montage) || P.Montage.IsEmpty())
					{
						OutError = FString::Printf(TEXT("assertion[%d] '%s': event montage needs a 'montage' name"), Idx, *P.Name);
						return false;
					}
					O->TryGetStringField(TEXT("phase"), P.MontagePhase);
				}
				else if (E == TEXT("marker"))
				{
					P.Kind = EPredKind::Marker;
					if (!O->TryGetStringField(TEXT("marker"), P.Marker) || P.Marker.IsEmpty())
					{
						OutError = FString::Printf(TEXT("assertion[%d] '%s': event marker needs a 'marker' label"), Idx, *P.Name);
						return false;
					}
				}
				else if (E == TEXT("error"))
				{
					P.Kind = EPredKind::Error;
					O->TryGetStringField(TEXT("category"), P.ErrorCategory);
					O->TryGetStringField(TEXT("contains"), P.ErrorContains);
				}
				else
				{
					OutError = FString::Printf(TEXT("assertion[%d] '%s': unknown event '%s' (montage|marker|error)"), Idx, *P.Name, *EventStr);
					return false;
				}
				// Events default to Eventually.
				P.Hold = EPredHold::Eventually;
			}
			else
			{
				P.Kind = EPredKind::Channel;
				if (!O->TryGetStringField(TEXT("channel"), P.Channel) || P.Channel.IsEmpty())
				{
					OutError = FString::Printf(TEXT("assertion[%d] '%s': needs a 'channel' or an 'event'"), Idx, *P.Name);
					return false;
				}
				FString OpStr;
				if (!O->TryGetStringField(TEXT("op"), OpStr) || !ParseOp(OpStr, P.Op))
				{
					OutError = FString::Printf(TEXT("assertion[%d] '%s': missing/unknown op '%s'"), Idx, *P.Name, *OpStr);
					return false;
				}
				if (P.Op == EPredOp::Between)
				{
					if (!O->TryGetNumberField(TEXT("min"), P.Min) || !O->TryGetNumberField(TEXT("max"), P.Max))
					{
						OutError = FString::Printf(TEXT("assertion[%d] '%s': op between needs 'min' and 'max'"), Idx, *P.Name);
						return false;
					}
					P.bHasMinMax = true;
				}
				else if (P.Op == EPredOp::Changed || P.Op == EPredOp::Increased || P.Op == EPredOp::Decreased)
				{
					// No operand needed (frame-over-frame).
				}
				else
				{
					if (!O->TryGetNumberField(TEXT("value"), P.Value))
					{
						OutError = FString::Printf(TEXT("assertion[%d] '%s': op %s needs 'value'"), Idx, *P.Name, OpToStr(P.Op));
						return false;
					}
					P.bHasValue = true;
					if (P.Op == EPredOp::Approx && !O->TryGetNumberField(TEXT("tol"), P.Tol))
					{
						OutError = FString::Printf(TEXT("assertion[%d] '%s': op approx needs 'tol'"), Idx, *P.Name);
						return false;
					}
				}
				P.Hold = EPredHold::Always;   // channel default
			}

			// Explicit hold overrides the default.
			FString HoldStr;
			if (O->TryGetStringField(TEXT("hold"), HoldStr) && !ParseHold(HoldStr, P.Hold))
			{
				OutError = FString::Printf(TEXT("assertion[%d] '%s': unknown hold '%s'"), Idx, *P.Name, *HoldStr);
				return false;
			}

			// Window + deadline.
			double D; int64 I;
			if (O->TryGetNumberField(TEXT("from_s"), D))     { P.FromS = D; P.bHasFromS = true; }
			if (O->TryGetNumberField(TEXT("to_s"), D))       { P.ToS = D; P.bHasToS = true; }
			if (O->TryGetNumberField(TEXT("from_frame"), I)) { P.FromFrame = I; P.bHasFromFrame = true; }
			if (O->TryGetNumberField(TEXT("to_frame"), I))   { P.ToFrame = I; P.bHasToFrame = true; }
			if (O->TryGetNumberField(TEXT("within_s"), D))     { P.WithinS = D; P.bHasWithinS = true; }
			if (O->TryGetNumberField(TEXT("within_frames"), I)){ P.WithinFrames = I; P.bHasWithinFrames = true; }

			const TSharedPtr<FJsonObject>* AfterObj = nullptr;
			if (O->TryGetObjectField(TEXT("after_event"), AfterObj) && AfterObj)
				(*AfterObj)->TryGetStringField(TEXT("marker"), P.AfterMarker);

			Out.Add(MoveTemp(P));
		}
		return true;
	}

	bool FPIEPredicateEvaluator::LoadTable(const FString& CsvPath, FSeriesTable& Out, FString& OutError)
	{
		FString CSV;
		if (!FFileHelper::LoadFileToString(CSV, *CsvPath))
		{
			OutError = FString::Printf(TEXT("series CSV not found: %s"), *CsvPath);
			return false;
		}

		TArray<FString> Lines;
		CSV.ParseIntoArrayLines(Lines);
		int32 HeaderIdx = INDEX_NONE;
		for (int32 i = 0; i < Lines.Num(); ++i)
		{
			if (!Lines[i].StartsWith(TEXT("#"))) { HeaderIdx = i; break; }
		}
		if (HeaderIdx == INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("series CSV has no header: %s"), *CsvPath);
			return false;
		}

		const TArray<FString> Cols = SplitCSVLine(Lines[HeaderIdx]);
		for (int32 i = 0; i < Cols.Num(); ++i) Out.ColIndex.Add(Cols[i], i);

		const int32 CFrame = Out.ColIndex.Contains(TEXT("frame")) ? Out.ColIndex[TEXT("frame")] : INDEX_NONE;
		const int32 CTime  = Out.ColIndex.Contains(TEXT("time"))  ? Out.ColIndex[TEXT("time")]  : INDEX_NONE;

		for (int32 i = HeaderIdx + 1; i < Lines.Num(); ++i)
		{
			if (Lines[i].IsEmpty() || Lines[i].StartsWith(TEXT("#"))) continue;
			TArray<FString> F = SplitCSVLine(Lines[i]);
			const int64 Fr = (CFrame != INDEX_NONE && CFrame < F.Num())
				? FCString::Atoi64(*F[CFrame]) : Out.Frames.Num();
			const double Tm = (CTime != INDEX_NONE && CTime < F.Num())
				? FCString::Atod(*F[CTime]) : 0.0;
			Out.Frames.Add(Fr);
			Out.Times.Add(Tm);
			Out.Cells.Add(MoveTemp(F));
		}
		return true;
	}

	namespace
	{
		using namespace MCPPIE;

		// Resolve the [start,end) row window for a predicate. Returns false if an anchor
		// marker was requested but not found (Reason explains).
		bool ResolveWindow(const FSeriesTable& T, const FPredicate& P,
		                   const TMap<FString, int64>& MarkerFrames,
		                   int32& OutStart, int32& OutEnd, FString& OutReason)
		{
			const int32 N = T.Cells.Num();
			OutStart = 0;
			OutEnd = N;

			int64 StartFrameFloor = TNumericLimits<int64>::Lowest();
			if (P.bHasFromFrame) StartFrameFloor = FMath::Max(StartFrameFloor, P.FromFrame);

			if (!P.AfterMarker.IsEmpty())
			{
				const int64* MF = MarkerFrames.Find(P.AfterMarker);
				if (!MF)
				{
					OutReason = FString::Printf(TEXT("anchor marker '%s' not found in run"), *P.AfterMarker);
					return false;
				}
				StartFrameFloor = FMath::Max(StartFrameFloor, *MF);
			}

			for (int32 i = 0; i < N; ++i)
			{
				bool bAfterStart = (T.Frames[i] >= StartFrameFloor);
				if (P.bHasFromS) bAfterStart = bAfterStart && (T.Times[i] >= P.FromS);
				if (bAfterStart) { OutStart = i; break; }
				if (i == N - 1) OutStart = N;   // nothing in window
			}

			for (int32 i = N - 1; i >= OutStart; --i)
			{
				bool bBeforeEnd = true;
				if (P.bHasToFrame) bBeforeEnd = bBeforeEnd && (T.Frames[i] <= P.ToFrame);
				if (P.bHasToS)     bBeforeEnd = bBeforeEnd && (T.Times[i] <= P.ToS);
				if (bBeforeEnd) { OutEnd = i + 1; break; }
				if (i == OutStart) OutEnd = OutStart;   // nothing in window
			}
			return true;
		}

		bool ScalarOp(const FPredicate& P, double Cur, double Prev, bool bHasPrev)
		{
			switch (P.Op)
			{
			case EPredOp::Eq:      return Cur == P.Value;
			case EPredOp::Ne:      return Cur != P.Value;
			case EPredOp::Lt:      return Cur <  P.Value;
			case EPredOp::Lte:     return Cur <= P.Value;
			case EPredOp::Gt:      return Cur >  P.Value;
			case EPredOp::Gte:     return Cur >= P.Value;
			case EPredOp::Approx:  return FMath::Abs(Cur - P.Value) <= P.Tol;
			case EPredOp::Between: return Cur >= P.Min && Cur <= P.Max;
			case EPredOp::Changed:   return bHasPrev && Cur != Prev;
			case EPredOp::Increased: return bHasPrev && Cur >  Prev;
			case EPredOp::Decreased: return bHasPrev && Cur <  Prev;
			case EPredOp::Crossed:   return bHasPrev && ((Prev < P.Value && Cur >= P.Value) ||
			                                             (Prev > P.Value && Cur <= P.Value));
			}
			return false;
		}

		FString ExpectedStr(const FPredicate& P)
		{
			if (P.Op == EPredOp::Between)
				return FString::Printf(TEXT("%s between [%g, %g] (%s)"), *P.Channel, P.Min, P.Max, HoldToStr(P.Hold));
			if (P.Op == EPredOp::Changed || P.Op == EPredOp::Increased || P.Op == EPredOp::Decreased)
				return FString::Printf(TEXT("%s %s (%s)"), *P.Channel, OpToStr(P.Op), HoldToStr(P.Hold));
			return FString::Printf(TEXT("%s %s %g (%s)"), *P.Channel, OpToStr(P.Op), P.Value, HoldToStr(P.Hold));
		}

		// Evaluate a per-frame boolean series [Start,End) under the hold quantifier.
		// TruthAt(i) gives the frame predicate; ValueAt(i) gives the reported actual.
		void ApplyHold(const FSeriesTable& T, const FPredicate& P, int32 Start, int32 End,
		               TFunctionRef<bool(int32)> TruthAt, TFunctionRef<double(int32)> ValueAt,
		               FPredicateResult& R)
		{
			auto SetWitness = [&](int32 i)
			{
				if (i >= 0 && i < T.Frames.Num())
				{
					R.WitnessFrame = T.Frames[i];
					R.WitnessTime = T.Times[i];
					R.Actual = ValueAt(i);
					R.bHasActual = true;
				}
			};

			if (Start >= End)
			{
				R.bPassed = (P.Hold == EPredHold::Never);   // vacuous: nothing to violate
				R.Message = R.bPassed ? TEXT("no frames in window (vacuously true)")
				                      : TEXT("no frames in window to satisfy predicate");
				return;
			}

			switch (P.Hold)
			{
			case EPredHold::Always:
			{
				for (int32 i = Start; i < End; ++i)
				{
					if (!TruthAt(i)) { R.bPassed = false; SetWitness(i);
						R.Message = FString::Printf(TEXT("first violated at frame %lld (actual %g)"), R.WitnessFrame, R.Actual);
						return; }
				}
				R.bPassed = true; R.Message = TEXT("held on every frame in window");
				return;
			}
			case EPredHold::Never:
			{
				for (int32 i = Start; i < End; ++i)
				{
					if (TruthAt(i)) { R.bPassed = false; SetWitness(i);
						R.Message = FString::Printf(TEXT("occurred at frame %lld (actual %g)"), R.WitnessFrame, R.Actual);
						return; }
				}
				R.bPassed = true; R.Message = TEXT("never occurred in window");
				return;
			}
			case EPredHold::Eventually:
			{
				int32 First = INDEX_NONE;
				for (int32 i = Start; i < End; ++i) { if (TruthAt(i)) { First = i; break; } }
				if (First == INDEX_NONE)
				{
					R.bPassed = false; R.Message = TEXT("never became true in window");
					return;
				}
				SetWitness(First);
				// Deadline check, measured from window start.
				bool bLate = false;
				if (P.bHasWithinS)      bLate = bLate || (T.Times[First]  - T.Times[Start]  > P.WithinS);
				if (P.bHasWithinFrames) bLate = bLate || (T.Frames[First] - T.Frames[Start] > P.WithinFrames);
				if (bLate)
				{
					R.bPassed = false;
					R.Message = FString::Printf(TEXT("became true at frame %lld (t=%.3f), past the deadline"), R.WitnessFrame, R.WitnessTime);
				}
				else
				{
					R.bPassed = true;
					R.Message = FString::Printf(TEXT("became true at frame %lld (t=%.3f)"), R.WitnessFrame, R.WitnessTime);
				}
				return;
			}
			case EPredHold::AtEnd:
			{
				const int32 i = End - 1;
				R.bPassed = TruthAt(i); SetWitness(i);
				R.Message = R.bPassed ? FString::Printf(TEXT("held at final frame %lld (actual %g)"), R.WitnessFrame, R.Actual)
				                      : FString::Printf(TEXT("failed at final frame %lld (actual %g)"), R.WitnessFrame, R.Actual);
				return;
			}
			case EPredHold::OnceThenAlways:
			{
				int32 First = INDEX_NONE;
				for (int32 i = Start; i < End; ++i) { if (TruthAt(i)) { First = i; break; } }
				if (First == INDEX_NONE)
				{
					R.bPassed = false; R.Message = TEXT("never became true in window");
					return;
				}
				for (int32 i = First; i < End; ++i)
				{
					if (!TruthAt(i)) { R.bPassed = false; SetWitness(i);
						R.Message = FString::Printf(TEXT("became true at frame %lld then broke at frame %lld"), T.Frames[First], R.WitnessFrame);
						return; }
				}
				R.bPassed = true; SetWitness(First);
				R.Message = FString::Printf(TEXT("became true at frame %lld and held to the end"), R.WitnessFrame);
				return;
			}
			}
		}

		void EvalChannel(const FSeriesTable& T, const FPredicate& P, int32 Start, int32 End, FPredicateResult& R)
		{
			// Pseudo-channels resolve to a constant or a built-in column.
			const bool bPseudo = P.Channel.StartsWith(TEXT("$"));
			int32 Col = INDEX_NONE;
			double Constant = 0.0;
			bool bConstant = false;

			if (bPseudo)
			{
				if (P.Channel == TEXT("$frame") || P.Channel == TEXT("$time"))
				{
					// handled directly in ValueAt below
				}
				else
				{
					// $errors / $warnings are injected by the caller as constant columns
					// via ColIndex sentinel; if absent, treat as 0.
					if (const int32* Pc = T.ColIndex.Find(P.Channel)) Col = *Pc;
					else { bConstant = true; Constant = 0.0; }
				}
			}
			else
			{
				Col = T.ResolveColumn(P.Channel);
				if (Col == INDEX_NONE)
				{
					R.bPassed = false;
					R.Expected = ExpectedStr(P);
					R.Message = FString::Printf(TEXT("channel '%s' not found in series (no matching column)"), *P.Channel);
					return;
				}
			}

			auto ValueAt = [&](int32 i) -> double
			{
				if (P.Channel == TEXT("$frame")) return static_cast<double>(T.Frames[i]);
				if (P.Channel == TEXT("$time"))  return T.Times[i];
				if (bConstant) return Constant;
				const TArray<FString>& Row = T.Cells[i];
				return (Col != INDEX_NONE && Col < Row.Num()) ? FCString::Atod(*Row[Col]) : 0.0;
			};

			auto TruthAt = [&](int32 i) -> bool
			{
				const double Cur = ValueAt(i);
				const bool bHasPrev = (i > Start);
				const double Prev = bHasPrev ? ValueAt(i - 1) : 0.0;
				return ScalarOp(P, Cur, Prev, bHasPrev);
			};

			R.Expected = ExpectedStr(P);
			ApplyHold(T, P, Start, End, TruthAt, ValueAt, R);
		}

		void EvalMontage(const FSeriesTable& T, const FPredicate& P, int32 Start, int32 End, FPredicateResult& R)
		{
			const int32 Col = T.ColIndex.Contains(TEXT("montage")) ? T.ColIndex[TEXT("montage")] : INDEX_NONE;
			if (Col == INDEX_NONE)
			{
				R.bPassed = false;
				R.Message = TEXT("no 'montage' column in series (enable capture_montage on the profile)");
				return;
			}
			const FString Phase = P.MontagePhase.IsEmpty() ? TEXT("playing") : P.MontagePhase.ToLower();

			auto CellAt = [&](int32 i) -> FString
			{
				const TArray<FString>& Row = T.Cells[i];
				return (Col < Row.Num()) ? Row[Col] : FString();
			};
			auto TruthAt = [&](int32 i) -> bool
			{
				const bool bIsNow = (CellAt(i) == P.Montage);
				if (Phase == TEXT("started"))
					return bIsNow && (i == Start || CellAt(i - 1) != P.Montage);
				if (Phase == TEXT("completed"))
					return !bIsNow && i > Start && CellAt(i - 1) == P.Montage;
				return bIsNow;   // playing
			};
			auto ValueAt = [&](int32) -> double { return 0.0; };

			R.Expected = FString::Printf(TEXT("montage '%s' %s (%s)"), *P.Montage, *Phase, HoldToStr(P.Hold));
			ApplyHold(T, P, Start, End, TruthAt, ValueAt, R);
		}

		void EvalError(const FPredicate& P, const TSharedPtr<FJsonObject>& ErrorsJson,
		               int64 WindowStartFrame, int64 WindowEndFrame, FPredicateResult& R)
		{
			R.Expected = FString::Printf(TEXT("error%s%s (%s)"),
				P.ErrorCategory.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" cat=%s"), *P.ErrorCategory),
				P.ErrorContains.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" ~'%s'"), *P.ErrorContains),
				HoldToStr(P.Hold));

			if (P.Hold != EPredHold::Eventually && P.Hold != EPredHold::Never)
			{
				R.bPassed = false;
				R.Message = TEXT("error events support only 'eventually' or 'never'");
				return;
			}
			if (!ErrorsJson.IsValid())
			{
				// No session_errors.json: no errors were captured -> zero matches.
				R.bPassed = (P.Hold == EPredHold::Never);
				R.Message = R.bPassed ? TEXT("no errors captured") : TEXT("no session_errors.json to satisfy 'eventually'");
				return;
			}

			const TArray<TSharedPtr<FJsonValue>>* Issues = nullptr;
			int64 FirstMatchFrame = -1;
			if (ErrorsJson->TryGetArrayField(TEXT("issues"), Issues) && Issues)
			{
				for (const TSharedPtr<FJsonValue>& V : *Issues)
				{
					const TSharedPtr<FJsonObject>* O = nullptr;
					if (!V.IsValid() || !V->TryGetObject(O) || !O) continue;
					FString Cat, Msg;
					(*O)->TryGetStringField(TEXT("category"), Cat);
					(*O)->TryGetStringField(TEXT("message"), Msg);
					if (!P.ErrorCategory.IsEmpty() && Cat != P.ErrorCategory) continue;
					if (!P.ErrorContains.IsEmpty() && !Msg.Contains(P.ErrorContains)) continue;
					double FF = 0;
					(*O)->TryGetNumberField(TEXT("first_frame"), FF);
					const int64 Frame = static_cast<int64>(FF);
					if (Frame < WindowStartFrame || Frame > WindowEndFrame) continue;
					if (FirstMatchFrame < 0 || Frame < FirstMatchFrame) FirstMatchFrame = Frame;
				}
			}

			const bool bMatched = FirstMatchFrame >= 0;
			if (P.Hold == EPredHold::Never)
			{
				R.bPassed = !bMatched;
				R.WitnessFrame = FirstMatchFrame;
				R.Message = bMatched ? FString::Printf(TEXT("matching error first logged at frame %lld"), FirstMatchFrame)
				                     : TEXT("no matching error in window");
			}
			else // Eventually
			{
				R.bPassed = bMatched;
				R.WitnessFrame = FirstMatchFrame;
				R.Message = bMatched ? FString::Printf(TEXT("matching error logged at frame %lld"), FirstMatchFrame)
				                     : TEXT("no matching error was logged");
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> FPIEPredicateEvaluator::DeriveStarterPredicates(const FString& CsvPath, int32 MaxErrors)
	{
		TArray<TSharedPtr<FJsonValue>> Out;

		auto AddBetween = [&Out](const FString& Name, const FString& Channel, double Lo, double Hi)
		{
			const double Margin = FMath::Max(1.0, (Hi - Lo) * 0.1);
			TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("name"), Name);
			P->SetStringField(TEXT("channel"), Channel);
			P->SetStringField(TEXT("op"), TEXT("between"));
			P->SetNumberField(TEXT("min"), Lo - Margin);
			P->SetNumberField(TEXT("max"), Hi + Margin);
			P->SetStringField(TEXT("hold"), TEXT("always"));
			Out.Add(MakeShared<FJsonValueObject>(P));
		};

		FSeriesTable T; FString Err;
		if (LoadTable(CsvPath, T, Err) && T.HasRows())
		{
			TArray<TPair<FString, int32>> RangeCols;
			for (const TPair<FString, int32>& KV : T.ColIndex)
			{
				if (KV.Key == TEXT("pos_z") || KV.Key.StartsWith(TEXT("t:")))
					RangeCols.Add(KV);
			}
			for (const TPair<FString, int32>& KV : RangeCols)
			{
				double Lo = TNumericLimits<double>::Max(), Hi = TNumericLimits<double>::Lowest();
				for (const TArray<FString>& Row : T.Cells)
				{
					if (KV.Value >= Row.Num()) continue;
					const double V = FCString::Atod(*Row[KV.Value]);
					Lo = FMath::Min(Lo, V); Hi = FMath::Max(Hi, V);
				}
				if (Lo <= Hi)
				{
					FString Ch = KV.Key; Ch.RemoveFromStart(TEXT("t:"));
					AddBetween(FString::Printf(TEXT("%s in observed range"), *Ch), Ch, Lo, Hi);
				}
			}

			if (const int32* Mc = T.ColIndex.Find(TEXT("montage")))
			{
				TSet<FString> Seen;
				for (const TArray<FString>& Row : T.Cells)
				{
					if (*Mc >= Row.Num()) continue;
					const FString M = Row[*Mc];
					if (!M.IsEmpty() && M != TEXT("None") && !Seen.Contains(M))
					{
						Seen.Add(M);
						TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
						P->SetStringField(TEXT("name"), FString::Printf(TEXT("montage %s plays"), *M));
						P->SetStringField(TEXT("event"), TEXT("montage"));
						P->SetStringField(TEXT("montage"), M);
						P->SetStringField(TEXT("hold"), TEXT("eventually"));
						Out.Add(MakeShared<FJsonValueObject>(P));
					}
				}
			}
		}

		TSharedRef<FJsonObject> NoErr = MakeShared<FJsonObject>();
		NoErr->SetStringField(TEXT("name"), TEXT("no new errors"));
		NoErr->SetStringField(TEXT("channel"), TEXT("$errors"));
		NoErr->SetStringField(TEXT("op"), TEXT("lte"));
		NoErr->SetNumberField(TEXT("value"), MaxErrors);
		NoErr->SetStringField(TEXT("hold"), TEXT("always"));
		Out.Add(MakeShared<FJsonValueObject>(NoErr));

		return Out;
	}

	bool FPIEPredicateEvaluator::Evaluate(const FString& CsvPath, const FString& ErrorsJsonPath,
	                                      const FString& ManifestPath, const TArray<FPredicate>& Preds,
	                                      TArray<FPredicateResult>& Out, FString& OutError)
	{
		FSeriesTable T;
		if (!LoadTable(CsvPath, T, OutError)) return false;

		// Inject $errors / $warnings as constant pseudo-columns so channel predicates can use them.
		TSharedPtr<FJsonObject> ErrorsJson = LoadJsonObject(ErrorsJsonPath);
		if (ErrorsJson.IsValid())
		{
			double EC = 0, WC = 0;
			ErrorsJson->TryGetNumberField(TEXT("error_count"), EC);
			ErrorsJson->TryGetNumberField(TEXT("warning_count"), WC);
			const int32 ErrCol = T.Cells.Num() > 0 ? T.Cells[0].Num() : 0;
			// Append constant cells; record sentinel columns beyond the real header.
			T.ColIndex.Add(TEXT("$errors"), ErrCol);
			T.ColIndex.Add(TEXT("$warnings"), ErrCol + 1);
			for (TArray<FString>& Row : T.Cells)
			{
				Row.Add(FString::SanitizeFloat(EC));
				Row.Add(FString::SanitizeFloat(WC));
			}
		}

		// Markers from the manifest, for after_event anchoring.
		TMap<FString, int64> MarkerFrames;
		if (TSharedPtr<FJsonObject> Manifest = LoadJsonObject(ManifestPath))
		{
			const TArray<TSharedPtr<FJsonValue>>* Markers = nullptr;
			if (Manifest->TryGetArrayField(TEXT("markers"), Markers) && Markers)
			{
				for (const TSharedPtr<FJsonValue>& V : *Markers)
				{
					const TSharedPtr<FJsonObject>* O = nullptr;
					if (!V.IsValid() || !V->TryGetObject(O) || !O) continue;
					FString Label; double Fr = 0;
					(*O)->TryGetStringField(TEXT("label"), Label);
					(*O)->TryGetNumberField(TEXT("frame"), Fr);
					if (!Label.IsEmpty()) MarkerFrames.Add(Label, static_cast<int64>(Fr));
				}
			}
		}

		for (const FPredicate& P : Preds)
		{
			FPredicateResult R;
			R.Name = P.Name;
			R.Severity = P.Severity;

			int32 Start = 0, End = T.Cells.Num();
			FString Reason;
			if (!ResolveWindow(T, P, MarkerFrames, Start, End, Reason))
			{
				R.bPassed = false;
				R.Message = Reason;
				Out.Add(MoveTemp(R));
				continue;
			}

			switch (P.Kind)
			{
			case EPredKind::Channel: EvalChannel(T, P, Start, End, R); break;
			case EPredKind::Montage: EvalMontage(T, P, Start, End, R); break;
			case EPredKind::Marker:
			{
				const int64* MF = MarkerFrames.Find(P.Marker);
				const bool bHit = MF && (*MF >= (Start < T.Frames.Num() ? T.Frames[Start] : 0)) &&
				                        (End > 0 && *MF <= T.Frames[End - 1]);
				R.Expected = FString::Printf(TEXT("marker '%s' (%s)"), *P.Marker, HoldToStr(P.Hold));
				if (P.Hold == EPredHold::Never) { R.bPassed = !bHit; }
				else { R.bPassed = bHit; }
				if (MF) R.WitnessFrame = *MF;
				R.Message = bHit ? FString::Printf(TEXT("marker '%s' at frame %lld"), *P.Marker, MF ? *MF : -1)
				                 : FString::Printf(TEXT("marker '%s' not seen in window"), *P.Marker);
				break;
			}
			case EPredKind::Error:
			{
				const int64 WStart = (Start < T.Frames.Num()) ? T.Frames[Start] : 0;
				const int64 WEnd   = (End > 0) ? T.Frames[End - 1] : TNumericLimits<int64>::Max();
				EvalError(P, ErrorsJson, WStart, WEnd, R);
				break;
			}
			}
			Out.Add(MoveTemp(R));
		}
		return true;
	}

	TSharedRef<FJsonObject> FPIEPredicateEvaluator::ResultsToJson(const TArray<FPredicateResult>& Results,
	                                                              bool& bAllErrorsPassed)
	{
		bAllErrorsPassed = true;
		TArray<TSharedPtr<FJsonValue>> Arr;
		int32 Passed = 0, Failed = 0;
		for (const FPredicateResult& R : Results)
		{
			if (R.Severity == EPredSeverity::Error && !R.bPassed) bAllErrorsPassed = false;
			R.bPassed ? ++Passed : ++Failed;

			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("name"), R.Name);
			O->SetBoolField(TEXT("passed"), R.bPassed);
			O->SetStringField(TEXT("severity"), R.Severity == EPredSeverity::Warn ? TEXT("warn") : TEXT("error"));
			O->SetStringField(TEXT("expected"), R.Expected);
			O->SetStringField(TEXT("message"), R.Message);
			if (R.WitnessFrame >= 0) O->SetNumberField(TEXT("witness_frame"), static_cast<double>(R.WitnessFrame));
			if (R.WitnessTime >= 0)  O->SetNumberField(TEXT("witness_time"), R.WitnessTime);
			if (R.bHasActual)        O->SetNumberField(TEXT("actual"), R.Actual);
			Arr.Add(MakeShared<FJsonValueObject>(O));
		}

		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("passed"), bAllErrorsPassed);
		Root->SetArrayField(TEXT("results"), Arr);
		Root->SetNumberField(TEXT("assertions_total"), Results.Num());
		Root->SetNumberField(TEXT("assertions_passed"), Passed);
		Root->SetNumberField(TEXT("assertions_failed"), Failed);
		return Root;
	}
}
