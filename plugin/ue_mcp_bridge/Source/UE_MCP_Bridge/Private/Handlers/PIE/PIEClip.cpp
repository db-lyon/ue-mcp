#include "Handlers/PIE/PIEClip.h"
#include "Handlers/PIE/PIEStepActions.h"
#include "Curves/RichCurve.h"
#include "Misc/FrameRate.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

namespace MCPPIE
{
	namespace
	{
		const TCHAR* const ClipValueTypeNames[] = { TEXT("bool"), TEXT("axis1d"), TEXT("axis2d"), TEXT("axis3d") };
		const TCHAR* const ClipInterpNames[] = { TEXT("linear"), TEXT("constant"), TEXT("cubic") };
		const TCHAR* const ClipCheckModeNames[] = { TEXT("by_end"), TEXT("throughout"), TEXT("hold") };
		const TCHAR* const ClipCheckStatusNames[] = { TEXT("pending"), TEXT("running"), TEXT("passed"), TEXT("failed"), TEXT("awaiting_verdict"), TEXT("skipped") };

		FClipIssue MakeIssue(const FString& Id, const FString& Field, const FString& Message, bool bBlocks)
		{
			FClipIssue I;
			I.Id = Id;
			I.Field = Field;
			I.Message = Message;
			I.bBlocksSave = bBlocks;
			return I;
		}

		FString FormatNumber(double V)
		{
			if (FMath::IsNearlyEqual(V, FMath::RoundToDouble(V), 1e-9))
			{
				return FString::Printf(TEXT("%lld"), static_cast<long long>(FMath::RoundToDouble(V)));
			}
			FString S = FString::Printf(TEXT("%.4f"), V);
			while (S.EndsWith(TEXT("0"))) S.LeftChopInline(1);
			if (S.EndsWith(TEXT("."))) S.LeftChopInline(1);
			return S;
		}

		TSharedPtr<FJsonValue> VectorToJson(const FVector& V, EClipValueType Type)
		{
			switch (Type)
			{
			case EClipValueType::Bool: return MakeShared<FJsonValueBoolean>(V.X != 0.0);
			case EClipValueType::Axis1D: return MakeShared<FJsonValueNumber>(V.X);
			case EClipValueType::Axis2D:
				return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y) });
			case EClipValueType::Axis3D:
				return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y), MakeShared<FJsonValueNumber>(V.Z) });
			}
			return MakeShared<FJsonValueNull>();
		}

		// A number, a boolean, [x, y] or [x, y, z].
		bool VectorFromJson(const TSharedPtr<FJsonValue>& J, FVector& Out, int32& OutDims)
		{
			if (!J.IsValid()) return false;
			if (J->Type == EJson::Number) { Out = FVector(J->AsNumber(), 0, 0); OutDims = 1; return true; }
			if (J->Type == EJson::Boolean) { Out = FVector(J->AsBool() ? 1.0 : 0.0, 0, 0); OutDims = 1; return true; }
			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			if (J->TryGetArray(Arr) && Arr && Arr->Num() >= 1 && Arr->Num() <= 3)
			{
				Out = FVector::ZeroVector;
				for (int32 i = 0; i < Arr->Num(); ++i)
				{
					if ((*Arr)[i]->Type != EJson::Number) return false;
					Out[i] = (*Arr)[i]->AsNumber();
				}
				OutDims = Arr->Num();
				return true;
			}
			return false;
		}

		FString VectorToText(const FVector& V, EClipValueType Type)
		{
			switch (Type)
			{
			case EClipValueType::Bool:
			case EClipValueType::Axis1D: return FormatNumber(V.X);
			case EClipValueType::Axis2D: return FormatNumber(V.X) + TEXT(",") + FormatNumber(V.Y);
			case EClipValueType::Axis3D: return FormatNumber(V.X) + TEXT(",") + FormatNumber(V.Y) + TEXT(",") + FormatNumber(V.Z);
			}
			return TEXT("0");
		}

		bool VectorFromText(const FString& S, FVector& Out, int32& OutDims)
		{
			TArray<FString> Parts;
			S.ParseIntoArray(Parts, TEXT(","), false);
			if (Parts.Num() < 1 || Parts.Num() > 3) return false;
			Out = FVector::ZeroVector;
			for (int32 i = 0; i < Parts.Num(); ++i)
			{
				if (!Parts[i].IsNumeric()) return false;
				Out[i] = FCString::Atod(*Parts[i]);
			}
			OutDims = Parts.Num();
			return true;
		}

		FString QuoteText(const FString& S)
		{
			FString E = S.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
			return TEXT("\"") + E + TEXT("\"");
		}

		// Splits a line into tokens: words, "quoted strings" (unescaped), and from the first { or [ that starts a
		// token to the end of the line as one token.
		TArray<FString> TokenizeClipLine(const FString& Line, bool bJsonTail)
		{
			TArray<FString> Out;
			int32 i = 0;
			const int32 N = Line.Len();
			while (i < N)
			{
				while (i < N && FChar::IsWhitespace(Line[i])) ++i;
				if (i >= N) break;
				if (bJsonTail && (Line[i] == TEXT('{')))
				{
					Out.Add(Line.Mid(i).TrimEnd());
					break;
				}
				FString Tok;
				bool bQuoted = false;
				while (i < N && !FChar::IsWhitespace(Line[i]))
				{
					if (Line[i] == TEXT('"'))
					{
						bQuoted = true;
						++i;
						while (i < N && Line[i] != TEXT('"'))
						{
							if (Line[i] == TEXT('\\') && i + 1 < N) { Tok.AppendChar(Line[i + 1]); i += 2; continue; }
							Tok.AppendChar(Line[i++]);
						}
						++i;
						continue;
					}
					Tok.AppendChar(Line[i++]);
				}
				if (!Tok.IsEmpty() || bQuoted) Out.Add(Tok);
			}
			return Out;
		}

		bool ParseFrames(const FString& S, double& Out)
		{
			if (!S.IsNumeric()) return false;
			Out = FCString::Atod(*S);
			return true;
		}

		TSharedPtr<FJsonObject> ParseJsonObject(const FString& Text)
		{
			TSharedPtr<FJsonObject> Obj;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
			if (!FJsonSerializer::Deserialize(Reader, Obj)) return nullptr;
			return Obj;
		}

		FString JsonObjectToCondensed(const TSharedPtr<FJsonObject>& Obj)
		{
			FString Out;
			if (!Obj.IsValid()) return TEXT("{}");
			TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
			FJsonSerializer::Serialize(Obj.ToSharedRef(), W);
			return Out;
		}

		// Parses a check's expectation with the script parser, so a check accepts exactly what a step's expect does.
		bool ParseCheckExpect(const TSharedPtr<FJsonObject>& Expect, bool bResolveInPIE, FScriptExpect& Out, FString& OutError, FString& OutField)
		{
			TSharedRef<FJsonObject> Step = MakeShared<FJsonObject>();
			Step->SetStringField(TEXT("instruction"), TEXT("check"));
			Step->SetObjectField(TEXT("expect"), Expect);
			if (bResolveInPIE) Step->SetBoolField(TEXT("resolve_in_pie"), true);
			FScriptStepDef Def;
			FString Field;
			if (!ParseScriptStepJson(MakeShared<FJsonValueObject>(Step), 0, Def, OutError, &Field))
			{
				OutError = OutError.Replace(TEXT("steps[0]."), TEXT("")).Replace(TEXT("steps[0]"), TEXT("check"));
				OutField = Field;
				return false;
			}
			Out = Def.Expect;
			return true;
		}

		TSharedRef<FJsonObject> TrackToJson(const FClipTrack& T)
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("action"), T.Action);
			O->SetStringField(TEXT("type"), ClipValueTypeToString(T.Type));
			TArray<TSharedPtr<FJsonValue>> Notes;
			for (const FClipNote& N : T.Notes)
			{
				TSharedRef<FJsonObject> NO = MakeShared<FJsonObject>();
				NO->SetStringField(TEXT("id"), N.Id);
				NO->SetNumberField(TEXT("start"), N.Start);
				NO->SetNumberField(TEXT("length"), N.Length);
				if (T.Type != EClipValueType::Bool) NO->SetField(TEXT("value"), VectorToJson(N.Value, T.Type));
				Notes.Add(MakeShared<FJsonValueObject>(NO));
			}
			O->SetArrayField(TEXT("notes"), Notes);
			if (T.Type != EClipValueType::Bool)
			{
				TArray<TSharedPtr<FJsonValue>> Keys;
				for (const FClipKey& K : T.Keys)
				{
					TSharedRef<FJsonObject> KO = MakeShared<FJsonObject>();
					KO->SetStringField(TEXT("id"), K.Id);
					KO->SetNumberField(TEXT("t"), K.Time);
					KO->SetField(TEXT("v"), VectorToJson(K.Value, T.Type));
					if (K.Interp != EClipInterp::Linear) KO->SetStringField(TEXT("interp"), ClipInterpToString(K.Interp));
					Keys.Add(MakeShared<FJsonValueObject>(KO));
				}
				O->SetArrayField(TEXT("keys"), Keys);
			}
			return O;
		}

		bool TrackFromJson(const TSharedPtr<FJsonObject>& O, int32 Index, FClipTrack& Out, TArray<FClipIssue>& Issues)
		{
			const FString Where = FString::Printf(TEXT("tracks[%d]"), Index);
			bool bOk = true;
			if (!O->TryGetStringField(TEXT("action"), Out.Action) || Out.Action.TrimStartAndEnd().IsEmpty())
			{
				Issues.Add(MakeIssue(Where, TEXT("action"), FString::Printf(TEXT("%s.action is required: an Input Action path such as /Game/Input/IA_Jump"), *Where), true));
				return false;
			}
			Out.Action = NormalizeScriptInputActionPath(Out.Action);
			FString Type;
			if (O->TryGetStringField(TEXT("type"), Type) && !ParseClipValueType(Type, Out.Type))
			{
				Issues.Add(MakeIssue(Out.Action, TEXT("type"), FString::Printf(TEXT("%s.type '%s' is unknown (bool, axis1d, axis2d, axis3d)"), *Where, *Type), true));
				bOk = false;
			}
			const TArray<TSharedPtr<FJsonValue>>* Notes = nullptr;
			if (O->TryGetArrayField(TEXT("notes"), Notes) && Notes)
			{
				for (int32 i = 0; i < Notes->Num(); ++i)
				{
					const TSharedPtr<FJsonObject>* NO = nullptr;
					FClipNote N;
					if (!(*Notes)[i]->TryGetObject(NO) || !NO || !(*NO)->TryGetNumberField(TEXT("start"), N.Start) || !(*NO)->TryGetNumberField(TEXT("length"), N.Length))
					{
						Issues.Add(MakeIssue(Out.Action, FString::Printf(TEXT("notes[%d]"), i), FString::Printf(TEXT("%s.notes[%d] needs start and length (frames)"), *Where, i), true));
						bOk = false;
						continue;
					}
					(*NO)->TryGetStringField(TEXT("id"), N.Id);
					if (Out.Type != EClipValueType::Bool)
					{
						int32 Dims = 0;
						const TSharedPtr<FJsonValue> V = (*NO)->TryGetField(TEXT("value"));
						if (!V.IsValid() || !VectorFromJson(V, N.Value, Dims))
						{
							Issues.Add(MakeIssue(N.Id.IsEmpty() ? Out.Action : N.Id, TEXT("value"), FString::Printf(TEXT("%s.notes[%d].value is required on an %s track: %s"), *Where, i, *ClipValueTypeToString(Out.Type),
								Out.Type == EClipValueType::Axis1D ? TEXT("a number") : Out.Type == EClipValueType::Axis2D ? TEXT("[x, y]") : TEXT("[x, y, z]")), true));
							bOk = false;
						}
					}
					Out.Notes.Add(N);
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
			if (O->TryGetArrayField(TEXT("keys"), Keys) && Keys)
			{
				for (int32 i = 0; i < Keys->Num(); ++i)
				{
					const TSharedPtr<FJsonObject>* KO = nullptr;
					FClipKey K;
					int32 Dims = 0;
					if (!(*Keys)[i]->TryGetObject(KO) || !KO || !(*KO)->TryGetNumberField(TEXT("t"), K.Time) || !VectorFromJson((*KO)->TryGetField(TEXT("v")), K.Value, Dims))
					{
						Issues.Add(MakeIssue(Out.Action, FString::Printf(TEXT("keys[%d]"), i), FString::Printf(TEXT("%s.keys[%d] needs t (frames) and v (the value)"), *Where, i), true));
						bOk = false;
						continue;
					}
					(*KO)->TryGetStringField(TEXT("id"), K.Id);
					FString Interp;
					if ((*KO)->TryGetStringField(TEXT("interp"), Interp) && !ParseClipInterp(Interp, K.Interp))
					{
						Issues.Add(MakeIssue(K.Id.IsEmpty() ? Out.Action : K.Id, TEXT("interp"), FString::Printf(TEXT("%s.keys[%d].interp '%s' is unknown (linear, constant, cubic)"), *Where, i, *Interp), true));
						bOk = false;
					}
					Out.Keys.Add(K);
				}
			}
			return bOk;
		}

		TSharedRef<FJsonObject> CheckToJson(const FClipCheck& C)
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("id"), C.Id);
			if (!C.Name.IsEmpty()) O->SetStringField(TEXT("name"), C.Name);
			O->SetNumberField(TEXT("start"), C.Start);
			O->SetNumberField(TEXT("length"), C.Length);
			O->SetStringField(TEXT("mode"), ClipCheckModeToString(C.Mode));
			if (C.bManual)
			{
				O->SetBoolField(TEXT("manual"), true);
				O->SetStringField(TEXT("instruction"), C.Instruction);
				if (!C.bAgentJudgeable) O->SetBoolField(TEXT("agent_judgeable"), false);
			}
			else
			{
				O->SetObjectField(TEXT("expect"), ScriptExpectToJson(C.Expect));
				if (!C.Instruction.IsEmpty()) O->SetStringField(TEXT("instruction"), C.Instruction);
			}
			if (C.bResolveInPIE) O->SetBoolField(TEXT("resolve_in_pie"), true);
			if (C.Recorded.bValid)
			{
				TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
				R->SetNumberField(TEXT("frame"), C.Recorded.Frame);
				R->SetNumberField(TEXT("game_ms"), C.Recorded.GameMs);
				R->SetNumberField(TEXT("value"), C.Recorded.Value);
				O->SetObjectField(TEXT("recorded"), R);
			}
			return O;
		}

		bool CheckFromJson(const TSharedPtr<FJsonObject>& O, int32 Index, FClipCheck& Out, TArray<FClipIssue>& Issues)
		{
			const FString Where = FString::Printf(TEXT("checks[%d]"), Index);
			O->TryGetStringField(TEXT("id"), Out.Id);
			const FString Id = Out.Id.IsEmpty() ? Where : Out.Id;
			O->TryGetStringField(TEXT("name"), Out.Name);
			if (!O->TryGetNumberField(TEXT("start"), Out.Start))
			{
				Issues.Add(MakeIssue(Id, TEXT("start"), FString::Printf(TEXT("%s.start is required (frames)"), *Where), true));
				return false;
			}
			O->TryGetNumberField(TEXT("length"), Out.Length);
			O->TryGetBoolField(TEXT("manual"), Out.bManual);
			O->TryGetStringField(TEXT("instruction"), Out.Instruction);
			O->TryGetBoolField(TEXT("agent_judgeable"), Out.bAgentJudgeable);
			O->TryGetBoolField(TEXT("resolve_in_pie"), Out.bResolveInPIE);
			FString Mode;
			if (O->TryGetStringField(TEXT("mode"), Mode))
			{
				if (Mode.Equals(TEXT("manual"), ESearchCase::IgnoreCase))
				{
					Out.bManual = true;
					Out.Mode = EClipCheckMode::Hold;
				}
				else if (!ParseClipCheckMode(Mode, Out.Mode))
				{
					FClipIssue I = MakeIssue(Id, TEXT("mode"), FString::Printf(TEXT("%s.mode '%s' is unknown (by_end, throughout, hold)"), *Where, *Mode), true);
					I.Candidates = { TEXT("by_end"), TEXT("throughout"), TEXT("hold") };
					Issues.Add(I);
					return false;
				}
			}
			if (Out.bManual)
			{
				Out.Mode = EClipCheckMode::Hold;
				if (Out.Instruction.TrimStartAndEnd().IsEmpty())
				{
					Issues.Add(MakeIssue(Id, TEXT("instruction"), FString::Printf(TEXT("%s: a manual check needs instruction, the question the verdict answers"), *Where), true));
					return false;
				}
				return true;
			}
			const TSharedPtr<FJsonObject>* Expect = nullptr;
			if (!O->TryGetObjectField(TEXT("expect"), Expect) || !Expect)
			{
				Issues.Add(MakeIssue(Id, TEXT("expect"), FString::Printf(TEXT("%s needs expect {value, op, target?}, or manual: true with an instruction"), *Where), true));
				return false;
			}
			FString Err, Field;
			if (!ParseCheckExpect(*Expect, Out.bResolveInPIE, Out.Expect, Err, Field))
			{
				FClipIssue I = MakeIssue(Id, Field.IsEmpty() ? FString(TEXT("expect")) : Field, FString::Printf(TEXT("%s: %s"), *Where, *Err), true);
				if (Field == TEXT("expect.op"))
				{
					I.Candidates = { TEXT("decreasing"), TEXT("increasing"), TEXT("eq"), TEXT("gt"), TEXT("lt"), TEXT("visible"), TEXT("exists"), TEXT("triggered") };
				}
				Issues.Add(I);
				return false;
			}
			const TSharedPtr<FJsonObject>* Rec = nullptr;
			if (O->TryGetObjectField(TEXT("recorded"), Rec) && Rec)
			{
				Out.Recorded.bValid = (*Rec)->TryGetNumberField(TEXT("frame"), Out.Recorded.Frame);
				(*Rec)->TryGetNumberField(TEXT("game_ms"), Out.Recorded.GameMs);
				(*Rec)->TryGetNumberField(TEXT("value"), Out.Recorded.Value);
			}
			return true;
		}

		// Finds the thing an id names, for edits.
		bool FindById(FClip& Clip, const FString& Id, FString& OutKind, int32& OutTrack, int32& OutIndex)
		{
			for (int32 t = 0; t < Clip.Tracks.Num(); ++t)
			{
				for (int32 i = 0; i < Clip.Tracks[t].Notes.Num(); ++i) if (Clip.Tracks[t].Notes[i].Id == Id) { OutKind = TEXT("note"); OutTrack = t; OutIndex = i; return true; }
				for (int32 i = 0; i < Clip.Tracks[t].Keys.Num(); ++i) if (Clip.Tracks[t].Keys[i].Id == Id) { OutKind = TEXT("key"); OutTrack = t; OutIndex = i; return true; }
			}
			for (int32 i = 0; i < Clip.Checks.Num(); ++i) if (Clip.Checks[i].Id == Id) { OutKind = TEXT("check"); OutTrack = INDEX_NONE; OutIndex = i; return true; }
			for (int32 i = 0; i < Clip.Events.Num(); ++i) if (Clip.Events[i].Id == Id) { OutKind = TEXT("event"); OutTrack = INDEX_NONE; OutIndex = i; return true; }
			for (int32 i = 0; i < Clip.Regions.Num(); ++i) if (Clip.Regions[i].Id == Id) { OutKind = TEXT("region"); OutTrack = INDEX_NONE; OutIndex = i; return true; }
			const FString Action = NormalizeScriptInputActionPath(Id);
			for (int32 t = 0; t < Clip.Tracks.Num(); ++t)
			{
				if (Clip.Tracks[t].Action == Action || Clip.Tracks[t].Action.EndsWith(TEXT(".") + Id)) { OutKind = TEXT("track"); OutTrack = t; OutIndex = INDEX_NONE; return true; }
			}
			return false;
		}

		TArray<FString> AllIds(const FClip& Clip)
		{
			TArray<FString> Out;
			for (const FClipTrack& T : Clip.Tracks)
			{
				for (const FClipNote& N : T.Notes) Out.Add(N.Id);
				for (const FClipKey& K : T.Keys) Out.Add(K.Id);
			}
			for (const FClipCheck& C : Clip.Checks) Out.Add(C.Id);
			for (const FClipEvent& E : Clip.Events) Out.Add(E.Id);
			for (const FClipRegion& R : Clip.Regions) Out.Add(R.Id);
			return Out;
		}

		void SortTrack(FClipTrack& T)
		{
			T.Notes.StableSort([](const FClipNote& A, const FClipNote& B) { return A.Start < B.Start; });
			T.Keys.StableSort([](const FClipKey& A, const FClipKey& B) { return A.Time < B.Time; });
		}

		void SortClip(FClip& Clip)
		{
			for (FClipTrack& T : Clip.Tracks) SortTrack(T);
			Clip.Checks.StableSort([](const FClipCheck& A, const FClipCheck& B) { return A.Start < B.Start; });
			Clip.Events.StableSort([](const FClipEvent& A, const FClipEvent& B) { return A.At < B.At; });
			Clip.Regions.StableSort([](const FClipRegion& A, const FClipRegion& B) { return A.Start < B.Start; });
		}

		// One step of the clip's grid in frames; on a free grid, Fallback.
		double ClipGridStep(const FClip& Clip, double Fallback)
		{
			const FString Grid = Clip.Grid.ToLower();
			if (Grid == TEXT("free")) return Fallback;
			if (Grid.StartsWith(TEXT("1/"))) return static_cast<double>(FMath::Max(1, Clip.Rate)) / FMath::Max(1, FCString::Atoi(*Grid.Mid(2)));
			return 1.0;
		}

		FRichCurve BuildCurve(const FClipTrack& T, int32 Axis)
		{
			FRichCurve Curve;
			for (const FClipKey& K : T.Keys)
			{
				const FKeyHandle H = Curve.AddKey(static_cast<float>(K.Time), static_cast<float>(K.Value[Axis]));
				Curve.SetKeyInterpMode(H, K.Interp == EClipInterp::Constant ? RCIM_Constant : K.Interp == EClipInterp::Cubic ? RCIM_Cubic : RCIM_Linear);
				if (K.Interp == EClipInterp::Cubic) Curve.SetKeyTangentMode(H, RCTM_Auto);
			}
			Curve.AutoSetTangents();
			return Curve;
		}
	}

	// ── Names ──────────────────────────────────────────────────────────

	FString ClipValueTypeToString(EClipValueType Type) { return ClipValueTypeNames[static_cast<int32>(Type)]; }
	bool ParseClipValueType(const FString& Str, EClipValueType& Out)
	{
		const FString S = Str.TrimStartAndEnd().ToLower();
		if (S == TEXT("boolean")) { Out = EClipValueType::Bool; return true; }
		for (int32 i = 0; i < 4; ++i) if (S == ClipValueTypeNames[i]) { Out = static_cast<EClipValueType>(i); return true; }
		return false;
	}
	FString ClipInterpToString(EClipInterp Interp) { return ClipInterpNames[static_cast<int32>(Interp)]; }
	bool ParseClipInterp(const FString& Str, EClipInterp& Out)
	{
		const FString S = Str.TrimStartAndEnd().ToLower();
		for (int32 i = 0; i < 3; ++i) if (S == ClipInterpNames[i]) { Out = static_cast<EClipInterp>(i); return true; }
		return false;
	}
	FString ClipCheckModeToString(EClipCheckMode Mode) { return ClipCheckModeNames[static_cast<int32>(Mode)]; }
	bool ParseClipCheckMode(const FString& Str, EClipCheckMode& Out)
	{
		const FString S = Str.TrimStartAndEnd().ToLower();
		if (S == TEXT("hold_until_true")) { Out = EClipCheckMode::Hold; return true; }
		for (int32 i = 0; i < 3; ++i) if (S == ClipCheckModeNames[i]) { Out = static_cast<EClipCheckMode>(i); return true; }
		return false;
	}
	int32 ClipValueDims(EClipValueType Type)
	{
		switch (Type)
		{
		case EClipValueType::Axis2D: return 2;
		case EClipValueType::Axis3D: return 3;
		default: return 1;
		}
	}
	FString ClipCheckStatusToString(EClipCheckStatus Status) { return ClipCheckStatusNames[static_cast<int32>(Status)]; }

	// ── Clip ───────────────────────────────────────────────────────────

	double FClip::EffectiveLength() const
	{
		if (Length > 0.0) return Length;
		double End = 0.0;
		for (const FClipTrack& T : Tracks)
		{
			for (const FClipNote& N : T.Notes) End = FMath::Max(End, N.End());
			for (const FClipKey& K : T.Keys) End = FMath::Max(End, K.Time);
		}
		for (const FClipCheck& C : Checks) End = FMath::Max(End, C.Mode == EClipCheckMode::Hold ? C.Start : C.End());
		for (const FClipEvent& E : Events) End = FMath::Max(End, E.At);
		for (const FClipRegion& R : Regions) End = FMath::Max(End, R.End);
		return End;
	}

	double FClip::NextFrameDelta(double Position) const
	{
		const double Step = 1.0 / FMath::Max(1, Rate);
		if (FrameTimes.Num() < 2 || Position < FrameTimes[0] - 1e-6 || Position >= FrameTimes.Last() - 1e-6) return Step;
		// The first recorded frame after Position. A replay that stood at a hold is off the recorded frames until it
		// reaches the next one.
		int32 Lo = 0, Hi = FrameTimes.Num() - 1;
		while (Lo < Hi)
		{
			const int32 Mid = (Lo + Hi) / 2;
			if (FrameTimes[Mid] > Position + 1e-6) Hi = Mid; else Lo = Mid + 1;
		}
		const double Delta = (FrameTimes[Lo] - Position) / FMath::Max(1, Rate);
		return FMath::Clamp(Delta, 0.0005, 0.25);
	}

	double FClip::SnapToFrameTime(double Position) const
	{
		if (FrameTimes.Num() == 0) return Position;
		int32 Lo = 0, Hi = FrameTimes.Num() - 1;
		while (Lo < Hi)
		{
			const int32 Mid = (Lo + Hi) / 2;
			if (FrameTimes[Mid] < Position) Lo = Mid + 1; else Hi = Mid;
		}
		for (int32 i = FMath::Max(0, Lo - 1); i <= FMath::Min(FrameTimes.Num() - 1, Lo); ++i)
		{
			if (FMath::Abs(FrameTimes[i] - Position) <= 1e-3) return FrameTimes[i];
		}
		return Position;
	}

	FClipTrack* FClip::FindTrack(const FString& Action)
	{
		const FString P = NormalizeScriptInputActionPath(Action);
		return Tracks.FindByPredicate([&P](const FClipTrack& T) { return T.Action == P; });
	}

	const FClipTrack* FClip::FindTrack(const FString& Action) const
	{
		return const_cast<FClip*>(this)->FindTrack(Action);
	}

	TSharedRef<FJsonObject> ClipIssueToJson(const FClipIssue& Issue)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		if (Issue.Id.IsEmpty()) O->SetField(TEXT("id"), MakeShared<FJsonValueNull>());
		else O->SetStringField(TEXT("id"), Issue.Id);
		O->SetStringField(TEXT("field"), Issue.Field);
		O->SetStringField(TEXT("message"), Issue.Message);
		if (Issue.Candidates.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> C;
			for (const FString& S : Issue.Candidates) C.Add(MakeShared<FJsonValueString>(S));
			O->SetArrayField(TEXT("candidates"), C);
		}
		return O;
	}

	TArray<TSharedPtr<FJsonValue>> ClipIssuesToJson(const TArray<FClipIssue>& Issues)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FClipIssue& I : Issues) Out.Add(MakeShared<FJsonValueObject>(ClipIssueToJson(I)));
		return Out;
	}

	bool ClipIssuesBlockSave(const TArray<FClipIssue>& Issues)
	{
		return Issues.ContainsByPredicate([](const FClipIssue& I) { return I.bBlocksSave; });
	}

	// ── JSON ───────────────────────────────────────────────────────────

	bool ClipFromJson(const TSharedPtr<FJsonObject>& Obj, FClip& Out, TArray<FClipIssue>& OutIssues)
	{
		Out = FClip();
		if (!Obj.IsValid())
		{
			OutIssues.Add(MakeIssue(FString(), TEXT("clip"), TEXT("the clip must be an object"), true));
			return false;
		}
		const int32 Before = OutIssues.Num();
		double Num = 0.0;
		if (Obj->TryGetNumberField(TEXT("version"), Num) && static_cast<int32>(Num) > kClipFormatVersion)
		{
			OutIssues.Add(MakeIssue(FString(), TEXT("version"), FString::Printf(TEXT("clip version %d is newer than this bridge reads (%d)"), static_cast<int32>(Num), kClipFormatVersion), true));
		}
		if (Obj->TryGetNumberField(TEXT("rate"), Num)) Out.Rate = static_cast<int32>(Num);
		Obj->TryGetNumberField(TEXT("length"), Out.Length);
		Obj->TryGetStringField(TEXT("grid"), Out.Grid);
		Obj->TryGetStringField(TEXT("description"), Out.Description);
		Obj->TryGetStringField(TEXT("map"), Out.Map);
		Obj->TryGetStringField(TEXT("profile"), Out.Profile);
		Obj->TryGetBoolField(TEXT("fixed_timestep"), Out.bFixedTimestep);
		if (Obj->TryGetNumberField(TEXT("seed"), Num)) Out.Seed = static_cast<int64>(Num);
		Obj->TryGetNumberField(TEXT("hold_timeout"), Out.HoldTimeout);
		Obj->TryGetStringField(TEXT("reference"), Out.Reference);
		const TArray<TSharedPtr<FJsonValue>>* Times = nullptr;
		if (Obj->TryGetArrayField(TEXT("frame_times"), Times) && Times)
		{
			for (const TSharedPtr<FJsonValue>& V : *Times) Out.FrameTimes.Add(V->AsNumber());
		}
		const TSharedPtr<FJsonObject>* Start = nullptr;
		if (Obj->TryGetObjectField(TEXT("start"), Start) && Start)
		{
			const TArray<TSharedPtr<FJsonValue>>* Loc = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Rot = nullptr;
			if ((*Start)->TryGetArrayField(TEXT("location"), Loc) && Loc && Loc->Num() == 3)
			{
				Out.bHasStartPose = true;
				Out.StartLocation = FVector((*Loc)[0]->AsNumber(), (*Loc)[1]->AsNumber(), (*Loc)[2]->AsNumber());
				if ((*Start)->TryGetArrayField(TEXT("rotation"), Rot) && Rot && Rot->Num() == 3)
				{
					Out.StartRotation = FRotator((*Rot)[0]->AsNumber(), (*Rot)[1]->AsNumber(), (*Rot)[2]->AsNumber());
				}
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Obj->TryGetArrayField(TEXT("regions"), Arr) && Arr)
		{
			for (int32 i = 0; i < Arr->Num(); ++i)
			{
				const TSharedPtr<FJsonObject>* RO = nullptr;
				FClipRegion R;
				if (!(*Arr)[i]->TryGetObject(RO) || !RO || !(*RO)->TryGetNumberField(TEXT("start"), R.Start) || !(*RO)->TryGetNumberField(TEXT("end"), R.End))
				{
					OutIssues.Add(MakeIssue(FString::Printf(TEXT("regions[%d]"), i), TEXT("start"), FString::Printf(TEXT("regions[%d] needs start and end (frames)"), i), true));
					continue;
				}
				(*RO)->TryGetStringField(TEXT("id"), R.Id);
				(*RO)->TryGetStringField(TEXT("name"), R.Name);
				Out.Regions.Add(R);
			}
		}
		if (Obj->TryGetArrayField(TEXT("tracks"), Arr) && Arr)
		{
			for (int32 i = 0; i < Arr->Num(); ++i)
			{
				const TSharedPtr<FJsonObject>* TO = nullptr;
				if (!(*Arr)[i]->TryGetObject(TO) || !TO)
				{
					OutIssues.Add(MakeIssue(FString::Printf(TEXT("tracks[%d]"), i), FString(), FString::Printf(TEXT("tracks[%d] must be an object {action, type, notes, keys}"), i), true));
					continue;
				}
				FClipTrack T;
				TrackFromJson(*TO, i, T, OutIssues);
				if (!T.Action.IsEmpty()) Out.Tracks.Add(MoveTemp(T));
			}
		}
		if (Obj->TryGetArrayField(TEXT("events"), Arr) && Arr)
		{
			for (int32 i = 0; i < Arr->Num(); ++i)
			{
				const TSharedPtr<FJsonObject>* EO = nullptr;
				const TSharedPtr<FJsonObject>* Do = nullptr;
				FClipEvent E;
				if (!(*Arr)[i]->TryGetObject(EO) || !EO || !(*EO)->TryGetNumberField(TEXT("at"), E.At) || !(*EO)->TryGetObjectField(TEXT("do"), Do) || !Do)
				{
					OutIssues.Add(MakeIssue(FString::Printf(TEXT("events[%d]"), i), TEXT("do"), FString::Printf(TEXT("events[%d] needs at (frames) and do (an act or arrange entry)"), i), true));
					continue;
				}
				(*EO)->TryGetStringField(TEXT("id"), E.Id);
				E.Do = *Do;
				Out.Events.Add(E);
			}
		}
		if (Obj->TryGetArrayField(TEXT("checks"), Arr) && Arr)
		{
			for (int32 i = 0; i < Arr->Num(); ++i)
			{
				const TSharedPtr<FJsonObject>* CO = nullptr;
				FClipCheck C;
				if (!(*Arr)[i]->TryGetObject(CO) || !CO)
				{
					OutIssues.Add(MakeIssue(FString::Printf(TEXT("checks[%d]"), i), FString(), FString::Printf(TEXT("checks[%d] must be an object"), i), true));
					continue;
				}
				if (CheckFromJson(*CO, i, C, OutIssues)) Out.Checks.Add(C);
			}
		}
		if (Obj->TryGetArrayField(TEXT("takes"), Arr) && Arr)
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				const TSharedPtr<FJsonObject>* TO = nullptr;
				if (!V->TryGetObject(TO) || !TO) continue;
				FClipTake Take;
				(*TO)->TryGetStringField(TEXT("id"), Take.Id);
				(*TO)->TryGetStringField(TEXT("recorded_at"), Take.RecordedAt);
				(*TO)->TryGetNumberField(TEXT("start"), Take.Start);
				(*TO)->TryGetNumberField(TEXT("end"), Take.End);
				const TArray<TSharedPtr<FJsonValue>>* Tracks = nullptr;
				if ((*TO)->TryGetArrayField(TEXT("tracks"), Tracks) && Tracks)
				{
					TArray<FClipIssue> Ignored;
					for (int32 i = 0; i < Tracks->Num(); ++i)
					{
						const TSharedPtr<FJsonObject>* TrO = nullptr;
						FClipTrack T;
						if ((*Tracks)[i]->TryGetObject(TrO) && TrO && TrackFromJson(*TrO, i, T, Ignored)) Take.Tracks.Add(MoveTemp(T));
					}
				}
				Out.Takes.Add(MoveTemp(Take));
			}
		}
		AssignClipIds(Out);
		SortClip(Out);
		return !ClipIssuesBlockSave(TArray<FClipIssue>(OutIssues.GetData() + Before, OutIssues.Num() - Before));
	}

	TSharedRef<FJsonObject> ClipToJson(const FClip& Clip)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("version"), Clip.Version);
		O->SetNumberField(TEXT("rate"), Clip.Rate);
		O->SetNumberField(TEXT("length"), Clip.Length);
		O->SetNumberField(TEXT("effective_length"), Clip.EffectiveLength());
		O->SetStringField(TEXT("grid"), Clip.Grid);
		if (!Clip.Description.IsEmpty()) O->SetStringField(TEXT("description"), Clip.Description);
		if (!Clip.Map.IsEmpty()) O->SetStringField(TEXT("map"), Clip.Map);
		if (!Clip.Profile.IsEmpty()) O->SetStringField(TEXT("profile"), Clip.Profile);
		O->SetBoolField(TEXT("fixed_timestep"), Clip.bFixedTimestep);
		O->SetNumberField(TEXT("seed"), static_cast<double>(Clip.Seed));
		O->SetNumberField(TEXT("hold_timeout"), Clip.HoldTimeout);
		if (!Clip.Reference.IsEmpty()) O->SetStringField(TEXT("reference"), Clip.Reference);
		if (Clip.FrameTimes.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Times;
			for (double T : Clip.FrameTimes) Times.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(T * 1000.0) / 1000.0));
			O->SetArrayField(TEXT("frame_times"), Times);
		}
		if (Clip.bHasStartPose)
		{
			TSharedRef<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetArrayField(TEXT("location"), { MakeShared<FJsonValueNumber>(Clip.StartLocation.X), MakeShared<FJsonValueNumber>(Clip.StartLocation.Y), MakeShared<FJsonValueNumber>(Clip.StartLocation.Z) });
			S->SetArrayField(TEXT("rotation"), { MakeShared<FJsonValueNumber>(Clip.StartRotation.Pitch), MakeShared<FJsonValueNumber>(Clip.StartRotation.Yaw), MakeShared<FJsonValueNumber>(Clip.StartRotation.Roll) });
			O->SetObjectField(TEXT("start"), S);
		}
		TArray<TSharedPtr<FJsonValue>> Regions;
		for (const FClipRegion& R : Clip.Regions)
		{
			TSharedRef<FJsonObject> RO = MakeShared<FJsonObject>();
			RO->SetStringField(TEXT("id"), R.Id);
			RO->SetStringField(TEXT("name"), R.Name);
			RO->SetNumberField(TEXT("start"), R.Start);
			RO->SetNumberField(TEXT("end"), R.End);
			Regions.Add(MakeShared<FJsonValueObject>(RO));
		}
		O->SetArrayField(TEXT("regions"), Regions);
		TArray<TSharedPtr<FJsonValue>> Tracks;
		for (const FClipTrack& T : Clip.Tracks) Tracks.Add(MakeShared<FJsonValueObject>(TrackToJson(T)));
		O->SetArrayField(TEXT("tracks"), Tracks);
		TArray<TSharedPtr<FJsonValue>> Events;
		for (const FClipEvent& E : Clip.Events)
		{
			TSharedRef<FJsonObject> EO = MakeShared<FJsonObject>();
			EO->SetStringField(TEXT("id"), E.Id);
			EO->SetNumberField(TEXT("at"), E.At);
			EO->SetObjectField(TEXT("do"), E.Do.IsValid() ? E.Do : MakeShared<FJsonObject>());
			Events.Add(MakeShared<FJsonValueObject>(EO));
		}
		O->SetArrayField(TEXT("events"), Events);
		TArray<TSharedPtr<FJsonValue>> Checks;
		for (const FClipCheck& C : Clip.Checks) Checks.Add(MakeShared<FJsonValueObject>(CheckToJson(C)));
		O->SetArrayField(TEXT("checks"), Checks);
		TArray<TSharedPtr<FJsonValue>> Takes;
		for (const FClipTake& Take : Clip.Takes)
		{
			TSharedRef<FJsonObject> TO = MakeShared<FJsonObject>();
			TO->SetStringField(TEXT("id"), Take.Id);
			TO->SetStringField(TEXT("recorded_at"), Take.RecordedAt);
			TO->SetNumberField(TEXT("start"), Take.Start);
			TO->SetNumberField(TEXT("end"), Take.End);
			TArray<TSharedPtr<FJsonValue>> TT;
			for (const FClipTrack& T : Take.Tracks) TT.Add(MakeShared<FJsonValueObject>(TrackToJson(T)));
			TO->SetArrayField(TEXT("tracks"), TT);
			Takes.Add(MakeShared<FJsonValueObject>(TO));
		}
		O->SetArrayField(TEXT("takes"), Takes);
		return O;
	}

	// ── Text ───────────────────────────────────────────────────────────

	FString ClipToText(const FClip& Clip)
	{
		TArray<FString> L;
		L.Add(FString::Printf(TEXT("clip rate=%d length=%s grid=%s fixed_timestep=%s seed=%lld hold_timeout=%s"),
			Clip.Rate, *FormatNumber(Clip.Length), *Clip.Grid, Clip.bFixedTimestep ? TEXT("true") : TEXT("false"),
			static_cast<long long>(Clip.Seed), *FormatNumber(Clip.HoldTimeout)));
		if (!Clip.Map.IsEmpty()) L.Add(TEXT("map ") + Clip.Map);
		if (!Clip.Profile.IsEmpty()) L.Add(TEXT("profile ") + Clip.Profile);
		if (!Clip.Description.IsEmpty()) L.Add(TEXT("description ") + QuoteText(Clip.Description));
		if (Clip.bHasStartPose)
		{
			L.Add(FString::Printf(TEXT("start %s,%s,%s %s,%s,%s"),
				*FormatNumber(Clip.StartLocation.X), *FormatNumber(Clip.StartLocation.Y), *FormatNumber(Clip.StartLocation.Z),
				*FormatNumber(Clip.StartRotation.Pitch), *FormatNumber(Clip.StartRotation.Yaw), *FormatNumber(Clip.StartRotation.Roll)));
		}
		if (!Clip.Reference.IsEmpty()) L.Add(TEXT("reference ") + Clip.Reference);
		if (Clip.FrameTimes.Num() > 0)
		{
			TArray<FString> Times;
			for (double T : Clip.FrameTimes) Times.Add(FormatNumber(FMath::RoundToDouble(T * 1000.0) / 1000.0));
			L.Add(TEXT("timing ") + FString::Join(Times, TEXT(",")));
		}
		for (const FClipRegion& R : Clip.Regions)
		{
			L.Add(FString::Printf(TEXT("region %s %s %s %s"), *R.Id, *FormatNumber(R.Start), *FormatNumber(R.End), *QuoteText(R.Name)));
		}
		for (const FClipTrack& T : Clip.Tracks)
		{
			L.Add(FString::Printf(TEXT("track %s %s"), *T.Action, *ClipValueTypeToString(T.Type)));
			for (const FClipKey& K : T.Keys)
			{
				FString Line = FString::Printf(TEXT("  key %s %s %s"), *K.Id, *FormatNumber(K.Time), *VectorToText(K.Value, T.Type));
				if (K.Interp != EClipInterp::Linear) Line += TEXT(" ") + ClipInterpToString(K.Interp);
				L.Add(Line);
			}
			for (const FClipNote& N : T.Notes)
			{
				FString Line = FString::Printf(TEXT("  note %s %s %s"), *N.Id, *FormatNumber(N.Start), *FormatNumber(N.Length));
				if (T.Type != EClipValueType::Bool) Line += TEXT(" ") + VectorToText(N.Value, T.Type);
				L.Add(Line);
			}
		}
		for (const FClipEvent& E : Clip.Events)
		{
			L.Add(FString::Printf(TEXT("event %s %s %s"), *E.Id, *FormatNumber(E.At), *JsonObjectToCondensed(E.Do)));
		}
		for (const FClipCheck& C : Clip.Checks)
		{
			FString Line = FString::Printf(TEXT("check %s %s %s "), *C.Id, *FormatNumber(C.Start), *FormatNumber(C.Length));
			if (C.bManual)
			{
				Line += TEXT("manual ") + QuoteText(C.Instruction);
				if (!C.bAgentJudgeable) Line += TEXT(" agent_judgeable=false");
			}
			else
			{
				Line += ClipCheckModeToString(C.Mode) + TEXT(" ");
				Line += (C.Expect.Value.Contains(TEXT(" ")) ? QuoteText(C.Expect.Value) : C.Expect.Value) + TEXT(" ") + ScriptExpectOpToString(C.Expect.Op);
				if (C.Expect.bHasTarget)
				{
					Line += TEXT(" ") + (IsScriptBooleanOp(C.Expect.Op) ? FString(C.Expect.Target != 0.0 ? TEXT("true") : TEXT("false")) : FormatNumber(C.Expect.Target));
				}
				if (C.Expect.Client > 0) Line += FString::Printf(TEXT(" client=%d"), C.Expect.Client);
				if (!C.Instruction.IsEmpty()) Line += TEXT(" instruction=") + QuoteText(C.Instruction);
			}
			if (!C.Name.IsEmpty()) Line += TEXT(" name=") + QuoteText(C.Name);
			if (C.bResolveInPIE) Line += TEXT(" resolve_in_pie=true");
			L.Add(Line);
		}
		for (const FClipCheck& C : Clip.Checks)
		{
			if (C.Recorded.bValid)
			{
				L.Add(FString::Printf(TEXT("recorded %s %s %s %s"), *C.Id, *FormatNumber(C.Recorded.Frame), *FormatNumber(C.Recorded.GameMs), *FormatNumber(C.Recorded.Value)));
			}
		}
		return FString::Join(L, TEXT("\n")) + TEXT("\n");
	}

	bool ClipFromText(const FString& Text, FClip& Out, TArray<FClipIssue>& OutIssues)
	{
		Out = FClip();
		const int32 Before = OutIssues.Num();
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, false);
		FClipTrack* Current = nullptr;
		auto LineIssue = [&OutIssues](int32 LineNo, const FString& Field, const FString& Msg)
		{
			OutIssues.Add(MakeIssue(FString::Printf(TEXT("line %d"), LineNo), Field, FString::Printf(TEXT("line %d: %s"), LineNo, *Msg), true));
		};
		auto KeyValue = [](const FString& Tok, FString& K, FString& V) { return Tok.Split(TEXT("="), &K, &V); };

		for (int32 Ln = 0; Ln < Lines.Num(); ++Ln)
		{
			const int32 LineNo = Ln + 1;
			const FString Raw = Lines[Ln];
			const FString Trimmed = Raw.TrimStartAndEnd();
			if (Trimmed.IsEmpty() || Trimmed.StartsWith(TEXT("#"))) continue;
			const bool bIndented = FChar::IsWhitespace(Raw[0]);
			const TArray<FString> T = TokenizeClipLine(Trimmed, Trimmed.StartsWith(TEXT("event ")));
			const FString Word = T[0].ToLower();

			if (bIndented && (Word == TEXT("note") || Word == TEXT("key")))
			{
				if (!Current)
				{
					LineIssue(LineNo, Word, FString::Printf(TEXT("a %s must follow a track line"), *Word));
					continue;
				}
				if (Word == TEXT("note"))
				{
					FClipNote N;
					if (T.Num() < 4 || !ParseFrames(T[2], N.Start) || !ParseFrames(T[3], N.Length))
					{
						LineIssue(LineNo, TEXT("note"), TEXT("expected: note <id> <start> <length> [value]"));
						continue;
					}
					N.Id = T[1] == TEXT("-") ? FString() : T[1];
					if (Current->Type != EClipValueType::Bool)
					{
						int32 Dims = 0;
						if (T.Num() < 5 || !VectorFromText(T[4], N.Value, Dims))
						{
							LineIssue(LineNo, TEXT("value"), FString::Printf(TEXT("a note on an %s track needs a value, comma-separated with no spaces"), *ClipValueTypeToString(Current->Type)));
							continue;
						}
					}
					Current->Notes.Add(N);
				}
				else
				{
					FClipKey K;
					int32 Dims = 0;
					if (T.Num() < 4 || !ParseFrames(T[2], K.Time) || !VectorFromText(T[3], K.Value, Dims))
					{
						LineIssue(LineNo, TEXT("key"), TEXT("expected: key <id> <time> <value> [linear|constant|cubic]"));
						continue;
					}
					K.Id = T[1] == TEXT("-") ? FString() : T[1];
					if (T.Num() >= 5 && !ParseClipInterp(T[4], K.Interp))
					{
						LineIssue(LineNo, TEXT("interp"), FString::Printf(TEXT("'%s' is not linear, constant or cubic"), *T[4]));
						continue;
					}
					Current->Keys.Add(K);
				}
				continue;
			}
			Current = nullptr;

			if (Word == TEXT("clip"))
			{
				for (int32 i = 1; i < T.Num(); ++i)
				{
					FString K, V;
					if (!KeyValue(T[i], K, V)) { LineIssue(LineNo, T[i], FString::Printf(TEXT("'%s' is not key=value"), *T[i])); continue; }
					if (K == TEXT("rate")) Out.Rate = FCString::Atoi(*V);
					else if (K == TEXT("length")) Out.Length = FCString::Atod(*V);
					else if (K == TEXT("grid")) Out.Grid = V;
					else if (K == TEXT("fixed_timestep")) Out.bFixedTimestep = V.ToBool();
					else if (K == TEXT("seed")) Out.Seed = FCString::Atoi64(*V);
					else if (K == TEXT("hold_timeout")) Out.HoldTimeout = FCString::Atod(*V);
					else if (K == TEXT("version")) {}
					else LineIssue(LineNo, K, FString::Printf(TEXT("unknown clip setting '%s' (rate, length, grid, fixed_timestep, seed, hold_timeout)"), *K));
				}
			}
			else if (Word == TEXT("map") && T.Num() >= 2) Out.Map = T[1];
			else if (Word == TEXT("profile") && T.Num() >= 2) Out.Profile = T[1];
			else if (Word == TEXT("description") && T.Num() >= 2) Out.Description = T[1];
			else if (Word == TEXT("reference") && T.Num() >= 2) Out.Reference = T[1];
			else if (Word == TEXT("timing") && T.Num() >= 2)
			{
				TArray<FString> Parts;
				T[1].ParseIntoArray(Parts, TEXT(","), true);
				for (const FString& P : Parts) Out.FrameTimes.Add(FCString::Atod(*P));
			}
			else if (Word == TEXT("start"))
			{
				FVector Loc, Rot;
				int32 D1 = 0, D2 = 0;
				if (T.Num() < 2 || !VectorFromText(T[1], Loc, D1) || D1 != 3 || (T.Num() >= 3 && (!VectorFromText(T[2], Rot, D2) || D2 != 3)))
				{
					LineIssue(LineNo, TEXT("start"), TEXT("expected: start <x,y,z> [<pitch,yaw,roll>]"));
					continue;
				}
				Out.bHasStartPose = true;
				Out.StartLocation = Loc;
				Out.StartRotation = T.Num() >= 3 ? FRotator(Rot.X, Rot.Y, Rot.Z) : FRotator::ZeroRotator;
			}
			else if (Word == TEXT("region"))
			{
				FClipRegion R;
				if (T.Num() < 4 || !ParseFrames(T[2], R.Start) || !ParseFrames(T[3], R.End))
				{
					LineIssue(LineNo, TEXT("region"), TEXT("expected: region <id> <start> <end> \"name\""));
					continue;
				}
				R.Id = T[1] == TEXT("-") ? FString() : T[1];
				R.Name = T.Num() >= 5 ? T[4] : R.Id;
				Out.Regions.Add(R);
			}
			else if (Word == TEXT("track"))
			{
				FClipTrack Tr;
				if (T.Num() < 3 || !ParseClipValueType(T[2], Tr.Type))
				{
					LineIssue(LineNo, TEXT("track"), TEXT("expected: track <Input Action path> <bool|axis1d|axis2d|axis3d>"));
					continue;
				}
				Tr.Action = NormalizeScriptInputActionPath(T[1]);
				Out.Tracks.Add(Tr);
				Current = &Out.Tracks.Last();
			}
			else if (Word == TEXT("event"))
			{
				FClipEvent E;
				if (T.Num() < 4 || !ParseFrames(T[2], E.At))
				{
					LineIssue(LineNo, TEXT("event"), TEXT("expected: event <id> <at> {json}"));
					continue;
				}
				E.Id = T[1] == TEXT("-") ? FString() : T[1];
				E.Do = ParseJsonObject(T[3]);
				if (!E.Do.IsValid())
				{
					LineIssue(LineNo, TEXT("do"), TEXT("the event's entry is not a JSON object"));
					continue;
				}
				Out.Events.Add(E);
			}
			else if (Word == TEXT("check"))
			{
				FClipCheck C;
				if (T.Num() < 6 || !ParseFrames(T[2], C.Start) || !ParseFrames(T[3], C.Length))
				{
					LineIssue(LineNo, TEXT("check"), TEXT("expected: check <id> <start> <length> <by_end|throughout|hold> <value> <op> [target] [name=\"...\"], or check <id> <start> <length> manual \"question\""));
					continue;
				}
				C.Id = T[1] == TEXT("-") ? FString() : T[1];
				int32 i = 5;
				if (T[4].Equals(TEXT("manual"), ESearchCase::IgnoreCase))
				{
					C.bManual = true;
					C.Mode = EClipCheckMode::Hold;
					C.Instruction = T[5];
					i = 6;
				}
				else
				{
					if (!ParseClipCheckMode(T[4], C.Mode))
					{
						FClipIssue Issue = MakeIssue(C.Id, TEXT("mode"), FString::Printf(TEXT("line %d: check mode '%s' is unknown (by_end, throughout, hold, manual)"), LineNo, *T[4]), true);
						Issue.Candidates = { TEXT("by_end"), TEXT("throughout"), TEXT("hold"), TEXT("manual") };
						OutIssues.Add(Issue);
						continue;
					}
					if (T.Num() < 7)
					{
						LineIssue(LineNo, TEXT("expect"), TEXT("a check needs a value and an op after its mode"));
						continue;
					}
					TSharedRef<FJsonObject> Expect = MakeShared<FJsonObject>();
					Expect->SetStringField(TEXT("value"), T[5]);
					Expect->SetStringField(TEXT("op"), T[6]);
					i = 7;
					if (i < T.Num() && !T[i].Contains(TEXT("=")))
					{
						if (T[i] == TEXT("true") || T[i] == TEXT("false")) Expect->SetBoolField(TEXT("target"), T[i] == TEXT("true"));
						else if (T[i].IsNumeric()) Expect->SetNumberField(TEXT("target"), FCString::Atod(*T[i]));
						else Expect->SetStringField(TEXT("target"), T[i]);
						++i;
					}
					for (int32 j = i; j < T.Num(); ++j)
					{
						FString K, V;
						if (KeyValue(T[j], K, V) && K == TEXT("client")) Expect->SetNumberField(TEXT("client"), FCString::Atoi(*V));
						if (KeyValue(T[j], K, V) && K == TEXT("resolve_in_pie")) C.bResolveInPIE = V.ToBool();
					}
					FString Err, Field;
					if (!ParseCheckExpect(Expect, C.bResolveInPIE, C.Expect, Err, Field))
					{
						FClipIssue Issue = MakeIssue(C.Id, Field.IsEmpty() ? FString(TEXT("expect")) : Field, FString::Printf(TEXT("line %d: %s"), LineNo, *Err), true);
						if (Field == TEXT("expect.op")) Issue.Candidates = { TEXT("decreasing"), TEXT("increasing"), TEXT("eq"), TEXT("gt"), TEXT("lt"), TEXT("visible"), TEXT("exists"), TEXT("triggered") };
						OutIssues.Add(Issue);
						continue;
					}
				}
				for (; i < T.Num(); ++i)
				{
					FString K, V;
					if (!KeyValue(T[i], K, V)) { LineIssue(LineNo, T[i], FString::Printf(TEXT("'%s' is not key=value"), *T[i])); continue; }
					if (K == TEXT("name")) C.Name = V;
					else if (K == TEXT("instruction")) C.Instruction = V;
					else if (K == TEXT("agent_judgeable")) C.bAgentJudgeable = V.ToBool();
					else if (K == TEXT("resolve_in_pie")) C.bResolveInPIE = V.ToBool();
					else if (K == TEXT("client")) {}
					else LineIssue(LineNo, K, FString::Printf(TEXT("unknown check setting '%s' (name, instruction, agent_judgeable, resolve_in_pie, client)"), *K));
				}
				Out.Checks.Add(C);
			}
			else if (Word == TEXT("recorded"))
			{
				FClipCheck* C = T.Num() >= 5 ? Out.Checks.FindByPredicate([&T](const FClipCheck& X) { return X.Id == T[1]; }) : nullptr;
				if (!C || !ParseFrames(T[2], C->Recorded.Frame) || !ParseFrames(T[3], C->Recorded.GameMs) || !ParseFrames(T[4], C->Recorded.Value))
				{
					LineIssue(LineNo, TEXT("recorded"), TEXT("expected: recorded <check id> <frame> <game_ms> <value>, after the check"));
					continue;
				}
				C->Recorded.bValid = true;
			}
			else
			{
				FClipIssue Issue = MakeIssue(FString::Printf(TEXT("line %d"), LineNo), Word, FString::Printf(TEXT("line %d: '%s' is not a clip line"), LineNo, *T[0]), true);
				Issue.Candidates = RankScriptCandidates(Word, { TEXT("clip"), TEXT("map"), TEXT("profile"), TEXT("description"), TEXT("start"), TEXT("reference"), TEXT("timing"), TEXT("region"), TEXT("track"), TEXT("note"), TEXT("key"), TEXT("event"), TEXT("check"), TEXT("recorded") }, 3);
				OutIssues.Add(Issue);
			}
		}
		AssignClipIds(Out);
		SortClip(Out);
		return !ClipIssuesBlockSave(TArray<FClipIssue>(OutIssues.GetData() + Before, OutIssues.Num() - Before));
	}

	// ── Structure ──────────────────────────────────────────────────────

	void AssignClipIds(FClip& Clip)
	{
		TMap<FString, int32> Highest;
		auto Note = [&Highest](const FString& Id)
		{
			int32 i = 0;
			while (i < Id.Len() && FChar::IsAlpha(Id[i])) ++i;
			if (i == 0 || i == Id.Len()) return;
			const FString Digits = Id.Mid(i);
			if (!Digits.IsNumeric()) return;
			int32& H = Highest.FindOrAdd(Id.Left(i));
			H = FMath::Max(H, FCString::Atoi(*Digits));
		};
		for (const FString& Id : AllIds(Clip)) Note(Id);
		auto Next = [&Highest](const TCHAR* Prefix) { return FString::Printf(TEXT("%s%d"), Prefix, ++Highest.FindOrAdd(Prefix)); };
		for (FClipTrack& T : Clip.Tracks)
		{
			for (FClipNote& N : T.Notes) if (N.Id.IsEmpty()) N.Id = Next(TEXT("n"));
			for (FClipKey& K : T.Keys) if (K.Id.IsEmpty()) K.Id = Next(TEXT("k"));
		}
		for (FClipCheck& C : Clip.Checks) if (C.Id.IsEmpty()) C.Id = Next(TEXT("c"));
		for (FClipEvent& E : Clip.Events) if (E.Id.IsEmpty()) E.Id = Next(TEXT("e"));
		for (FClipRegion& R : Clip.Regions)
		{
			if (R.Id.IsEmpty()) R.Id = Next(TEXT("r"));
			if (R.Name.IsEmpty()) R.Name = R.Id;
		}
	}

	FScriptStepDef ClipCheckToStep(const FClipCheck& Check)
	{
		FScriptStepDef S;
		S.Instruction = Check.Instruction.IsEmpty() ? (Check.Name.IsEmpty() ? Check.Id : Check.Name) : Check.Instruction;
		S.Judge = Check.bManual ? EScriptJudge::Manual : EScriptJudge::Auto;
		S.bHasExpect = !Check.bManual;
		S.Expect = Check.Expect;
		S.bAgentJudgeable = Check.bAgentJudgeable;
		S.bResolveInPIE = Check.bResolveInPIE && !Check.bManual;
		return S;
	}

	FScriptStepDef ClipEventToStep(const FClipEvent& Event)
	{
		FScriptStepDef S;
		S.Instruction = Event.Id;
		S.Judge = EScriptJudge::Auto;
		if (Event.Do.IsValid())
		{
			if (Event.IsArrange()) S.Arrange.Add(MakeShared<FJsonValueObject>(Event.Do));
			else S.Act.Add(MakeShared<FJsonValueObject>(Event.Do));
		}
		return S;
	}

	void ValidateClipStructure(const FClip& Clip, TArray<FClipIssue>& OutIssues)
	{
		if (Clip.Rate < 1 || Clip.Rate > 1000)
		{
			OutIssues.Add(MakeIssue(FString(), TEXT("rate"), FString::Printf(TEXT("rate %d is outside 1..1000 frames per second"), Clip.Rate), true));
		}
		if (Clip.Length < 0.0) OutIssues.Add(MakeIssue(FString(), TEXT("length"), TEXT("length cannot be negative"), true));
		if (Clip.HoldTimeout <= 0.0) OutIssues.Add(MakeIssue(FString(), TEXT("hold_timeout"), TEXT("hold_timeout must be above 0 frames"), true));
		const FString Grid = Clip.Grid.ToLower();
		if (Grid != TEXT("frame") && Grid != TEXT("free") && !(Grid.StartsWith(TEXT("1/")) && Grid.Mid(2).IsNumeric() && FCString::Atoi(*Grid.Mid(2)) > 0))
		{
			FClipIssue I = MakeIssue(FString(), TEXT("grid"), FString::Printf(TEXT("grid '%s' is not frame, free or 1/N (Nths of a second)"), *Clip.Grid), true);
			I.Candidates = { TEXT("frame"), TEXT("free"), TEXT("1/4") };
			OutIssues.Add(I);
		}

		for (int32 i = 1; i < Clip.FrameTimes.Num(); ++i)
		{
			if (Clip.FrameTimes[i] <= Clip.FrameTimes[i - 1] || Clip.FrameTimes[i - 1] < 0.0)
			{
				OutIssues.Add(MakeIssue(FString(), TEXT("frame_times"), FString::Printf(TEXT("frame_times must rise from 0 or later; entry %d (%s) does not"), i, *FormatNumber(Clip.FrameTimes[i])), true));
				break;
			}
		}

		TMap<FString, int32> IdCount;
		for (const FString& Id : AllIds(Clip)) IdCount.FindOrAdd(Id)++;
		for (const TPair<FString, int32>& P : IdCount)
		{
			if (P.Value > 1) OutIssues.Add(MakeIssue(P.Key, TEXT("id"), FString::Printf(TEXT("id '%s' is used %d times; every note, key, check, event and region needs its own"), *P.Key, P.Value), true));
		}

		const bool bFixedLength = Clip.Length > 0.0;
		auto Past = [&](const FString& Id, const FString& Field, double At)
		{
			if (bFixedLength && At > Clip.Length + 1e-6)
			{
				OutIssues.Add(MakeIssue(Id, Field, FString::Printf(TEXT("%s %s is past the clip's length %s"), *Id, *FormatNumber(At), *FormatNumber(Clip.Length)), false));
			}
		};

		// Regions tile without overlapping.
		for (int32 i = 0; i < Clip.Regions.Num(); ++i)
		{
			const FClipRegion& R = Clip.Regions[i];
			if (R.Start < 0.0 || R.End <= R.Start)
			{
				OutIssues.Add(MakeIssue(R.Id, TEXT("end"), FString::Printf(TEXT("region %s must start at 0 or later and end after it starts (%s..%s)"), *R.Id, *FormatNumber(R.Start), *FormatNumber(R.End)), true));
			}
			if (i > 0 && R.Start < Clip.Regions[i - 1].End - 1e-6)
			{
				OutIssues.Add(MakeIssue(R.Id, TEXT("start"), FString::Printf(TEXT("region %s starts at %s, inside region %s (until %s); regions may not overlap"),
					*R.Id, *FormatNumber(R.Start), *Clip.Regions[i - 1].Id, *FormatNumber(Clip.Regions[i - 1].End)), true));
			}
			Past(R.Id, TEXT("end"), R.End);
		}

		TSet<FString> Actions;
		for (const FClipTrack& T : Clip.Tracks)
		{
			if (Actions.Contains(T.Action))
			{
				OutIssues.Add(MakeIssue(T.Action, TEXT("action"), FString::Printf(TEXT("%s has two tracks; one track per Input Action"), *T.Action), true));
			}
			Actions.Add(T.Action);
			if (T.Type == EClipValueType::Bool && T.Keys.Num() > 0)
			{
				OutIssues.Add(MakeIssue(T.Action, TEXT("keys"), FString::Printf(TEXT("%s is a bool track: it takes notes, not curve keys"), *T.Action), true));
			}
			for (int32 i = 0; i < T.Notes.Num(); ++i)
			{
				const FClipNote& N = T.Notes[i];
				if (N.Start < 0.0) OutIssues.Add(MakeIssue(N.Id, TEXT("start"), FString::Printf(TEXT("note %s starts before frame 0"), *N.Id), true));
				if (N.Length <= 0.0) OutIssues.Add(MakeIssue(N.Id, TEXT("length"), FString::Printf(TEXT("note %s needs a length above 0 frames"), *N.Id), true));
				if (i > 0 && N.Start < T.Notes[i - 1].End() - 1e-6)
				{
					OutIssues.Add(MakeIssue(N.Id, TEXT("start"), FString::Printf(TEXT("note %s (from %s) overlaps note %s (until %s) on %s; notes on one track may not overlap"),
						*N.Id, *FormatNumber(N.Start), *T.Notes[i - 1].Id, *FormatNumber(T.Notes[i - 1].End()), *T.Action), true));
				}
				Past(N.Id, TEXT("start"), N.Start);
			}
			for (int32 i = 0; i < T.Keys.Num(); ++i)
			{
				const FClipKey& K = T.Keys[i];
				if (K.Time < 0.0) OutIssues.Add(MakeIssue(K.Id, TEXT("t"), FString::Printf(TEXT("key %s is before frame 0"), *K.Id), true));
				if (i > 0 && FMath::IsNearlyEqual(K.Time, T.Keys[i - 1].Time, 1e-6))
				{
					OutIssues.Add(MakeIssue(K.Id, TEXT("t"), FString::Printf(TEXT("keys %s and %s are both at frame %s on %s"), *T.Keys[i - 1].Id, *K.Id, *FormatNumber(K.Time), *T.Action), true));
				}
				Past(K.Id, TEXT("t"), K.Time);
			}
		}

		for (const FClipEvent& E : Clip.Events)
		{
			if (E.At < 0.0) OutIssues.Add(MakeIssue(E.Id, TEXT("at"), FString::Printf(TEXT("event %s is before frame 0"), *E.Id), true));
			Past(E.Id, TEXT("at"), E.At);
			if (!E.Do.IsValid()) continue;
			if (E.IsArrange())
			{
				FString Kind;
				E.Do->TryGetStringField(TEXT("kind"), Kind);
				if (Kind != TEXT("spawn") && Kind != TEXT("set") && Kind != TEXT("call"))
				{
					FClipIssue I = MakeIssue(E.Id, TEXT("do.kind"), FString::Printf(TEXT("event %s: arrange kind '%s' is not spawn, set or call"), *E.Id, *Kind), true);
					I.Candidates = { TEXT("spawn"), TEXT("set"), TEXT("call") };
					OutIssues.Add(I);
				}
			}
			else
			{
				FStepAction A;
				FString Err;
				if (E.Do->HasField(TEXT("wait_ms")))
				{
					OutIssues.Add(MakeIssue(E.Id, TEXT("do"), FString::Printf(TEXT("event %s: wait_ms has no meaning on a timeline; place the next event later instead"), *E.Id), true));
				}
				else if (!ParseStepAction(E.Do, A, Err))
				{
					FClipIssue I = MakeIssue(E.Id, TEXT("do"), FString::Printf(TEXT("event %s: %s"), *E.Id, *Err), true);
					I.Candidates = { TEXT("place"), TEXT("console"), TEXT("capture"), TEXT("key"), TEXT("press"), TEXT("hold"), TEXT("tape"), TEXT("inject") };
					OutIssues.Add(I);
				}
			}
		}

		for (const FClipCheck& C : Clip.Checks)
		{
			if (C.Start < 0.0) OutIssues.Add(MakeIssue(C.Id, TEXT("start"), FString::Printf(TEXT("check %s starts before frame 0"), *C.Id), true));
			if (C.Length < 0.0) OutIssues.Add(MakeIssue(C.Id, TEXT("length"), FString::Printf(TEXT("check %s has a negative length"), *C.Id), true));
			Past(C.Id, TEXT("start"), C.Mode == EClipCheckMode::Hold ? C.Start : C.End());
			if (!C.bManual && IsScriptTriggerOp(C.Expect.Op) && C.Mode == EClipCheckMode::Throughout)
			{
				OutIssues.Add(MakeIssue(C.Id, TEXT("mode"), FString::Printf(TEXT("check %s: triggered is a moment, so it cannot hold throughout; use by_end or hold"), *C.Id), true));
			}
		}
		if (Clip.Checks.Num() == 0)
		{
			OutIssues.Add(MakeIssue(FString(), TEXT("checks"), TEXT("the clip checks nothing: add a check note, or it passes whatever happens"), false));
		}
	}

	void ValidateClipReferences(const FClip& Clip, const FScriptValidationContext& Ctx,
		const TFunction<bool(const FString& Action, EClipValueType& OutType)>& ActionType, TArray<FClipIssue>& OutIssues)
	{
		for (const FClipTrack& T : Clip.Tracks)
		{
			EClipValueType Type;
			if (!ActionType(T.Action, Type))
			{
				FClipIssue I = MakeIssue(T.Action, TEXT("action"), FString::Printf(TEXT("no Input Action loads from %s"), *T.Action), false);
				if (Ctx.ResolveInputAction)
				{
					const FScriptLookup L = Ctx.ResolveInputAction(T.Action);
					I.Candidates = L.Candidates;
					if (!L.Message.IsEmpty()) I.Message = L.Message;
				}
				OutIssues.Add(I);
				continue;
			}
			if (Type != T.Type)
			{
				FClipIssue I = MakeIssue(T.Action, TEXT("type"), FString::Printf(TEXT("%s is %s, but its track says %s"), *T.Action, *ClipValueTypeToString(Type), *ClipValueTypeToString(T.Type)), false);
				I.Candidates = { ClipValueTypeToString(Type) };
				OutIssues.Add(I);
			}
		}

		// Checks and events go through the script validator as steps, so they resolve exactly as a step's would.
		TArray<FScriptStepDef> Steps;
		TArray<FString> Ids;
		for (const FClipCheck& C : Clip.Checks) { Steps.Add(ClipCheckToStep(C)); Ids.Add(C.Id); }
		for (const FClipEvent& E : Clip.Events) { Steps.Add(ClipEventToStep(E)); Ids.Add(E.Id); }
		TArray<FScriptStepIssue> StepIssues;
		ValidateScriptSteps(Steps, /*bManualOnly=*/true, Ctx, StepIssues);
		for (const FScriptStepIssue& SI : StepIssues)
		{
			if (!Ids.IsValidIndex(SI.Step)) continue;
			FString Field = SI.Field;
			FString Message = SI.Message;
			const bool bEvent = SI.Step >= Clip.Checks.Num();
			if (bEvent)
			{
				Field = Field.Replace(TEXT("act[0]."), TEXT("do.")).Replace(TEXT("arrange[0]."), TEXT("do.")).Replace(TEXT("act[0]"), TEXT("do")).Replace(TEXT("arrange[0]"), TEXT("do"));
			}
			Message = Message.Replace(*FString::Printf(TEXT("steps[%d]"), SI.Step), *Ids[SI.Step]);
			FClipIssue I = MakeIssue(Ids[SI.Step], Field, Message, SI.bBlocksSave);
			I.Candidates = SI.Candidates;
			OutIssues.Add(I);
		}
	}

	// ── Edits ──────────────────────────────────────────────────────────

	double SnapClipTime(const FClip& Clip, double Frames)
	{
		const FString Grid = Clip.Grid.ToLower();
		if (Grid == TEXT("free")) return Frames;
		if (Grid.StartsWith(TEXT("1/")))
		{
			const int32 Div = FMath::Max(1, FCString::Atoi(*Grid.Mid(2)));
			const double Step = static_cast<double>(Clip.Rate) / Div;
			return FMath::RoundToDouble(Frames / Step) * Step;
		}
		return FMath::RoundToDouble(Frames);
	}

	bool ApplyClipEdits(FClip& Clip, const TArray<TSharedPtr<FJsonValue>>& Ops, TArray<FString>& OutChanged, TArray<FClipIssue>& OutIssues)
	{
		FClip Work = Clip;
		TArray<FString> Changed;
		auto Fail = [&OutIssues](int32 Index, const FString& Id, const FString& Field, const FString& Msg, const TArray<FString>& Candidates = {})
		{
			FClipIssue I = MakeIssue(Id.IsEmpty() ? FString::Printf(TEXT("ops[%d]"), Index) : Id, Field, FString::Printf(TEXT("ops[%d]: %s"), Index, *Msg), true);
			I.Candidates = Candidates;
			OutIssues.Add(I);
			return false;
		};
		auto Snap = [&Work](double F) { return SnapClipTime(Work, F); };

		for (int32 OpIndex = 0; OpIndex < Ops.Num(); ++OpIndex)
		{
			const TSharedPtr<FJsonObject>* OPtr = nullptr;
			if (!Ops[OpIndex].IsValid() || !Ops[OpIndex]->TryGetObject(OPtr) || !OPtr) return Fail(OpIndex, FString(), TEXT("op"), TEXT("each op must be an object {op, ...}"));
			const TSharedPtr<FJsonObject>& O = *OPtr;
			FString Op;
			O->TryGetStringField(TEXT("op"), Op);
			Op = Op.ToLower();
			FString Id;
			O->TryGetStringField(TEXT("id"), Id);

			if (Op == TEXT("add"))
			{
				FString Kind;
				O->TryGetStringField(TEXT("kind"), Kind);
				Kind = Kind.ToLower();
				if (Kind == TEXT("track"))
				{
					FClipTrack T;
					FString Action, Type;
					if (!O->TryGetStringField(TEXT("action"), Action)) return Fail(OpIndex, Id, TEXT("action"), TEXT("add track needs action, an Input Action path"));
					T.Action = NormalizeScriptInputActionPath(Action);
					if (O->TryGetStringField(TEXT("type"), Type) && !ParseClipValueType(Type, T.Type)) return Fail(OpIndex, Id, TEXT("type"), FString::Printf(TEXT("type '%s' is unknown"), *Type), { TEXT("bool"), TEXT("axis1d"), TEXT("axis2d"), TEXT("axis3d") });
					if (Work.FindTrack(T.Action)) return Fail(OpIndex, T.Action, TEXT("action"), FString::Printf(TEXT("%s already has a track"), *T.Action));
					Work.Tracks.Add(T);
					Changed.Add(T.Action);
					continue;
				}
				if (Kind == TEXT("note") || Kind == TEXT("key"))
				{
					FString Action;
					if (!O->TryGetStringField(TEXT("track"), Action)) return Fail(OpIndex, Id, TEXT("track"), FString::Printf(TEXT("add %s needs track, the Input Action it goes on"), *Kind));
					FClipTrack* T = Work.FindTrack(Action);
					if (!T)
					{
						TArray<FString> Pool;
						for (const FClipTrack& X : Work.Tracks) Pool.Add(X.Action);
						return Fail(OpIndex, Action, TEXT("track"), FString::Printf(TEXT("the clip has no track for %s; add it first with {op: add, kind: track, action, type}"), *Action), RankScriptCandidates(Action, Pool, 3));
					}
					if (Kind == TEXT("note"))
					{
						FClipNote N;
						N.Id = Id;
						if (!O->TryGetNumberField(TEXT("start"), N.Start) || !O->TryGetNumberField(TEXT("length"), N.Length)) return Fail(OpIndex, Id, TEXT("start"), TEXT("add note needs start and length (frames)"));
						N.Start = Snap(N.Start);
						N.Length = FMath::Max(Snap(N.Length), ClipGridStep(Work, N.Length));
						int32 Dims = 0;
						const TSharedPtr<FJsonValue> V = O->TryGetField(TEXT("value"));
						if (T->Type != EClipValueType::Bool && (!V.IsValid() || !VectorFromJson(V, N.Value, Dims))) return Fail(OpIndex, Id, TEXT("value"), FString::Printf(TEXT("a note on the %s track %s needs value"), *ClipValueTypeToString(T->Type), *T->Action));
						T->Notes.Add(N);
						AssignClipIds(Work);
						Changed.Add(T->Notes.Last().Id);
					}
					else
					{
						if (T->Type == EClipValueType::Bool) return Fail(OpIndex, T->Action, TEXT("kind"), FString::Printf(TEXT("%s is a bool track; add notes to it, not keys"), *T->Action), { TEXT("note") });
						FClipKey K;
						K.Id = Id;
						int32 Dims = 0;
						if (!O->TryGetNumberField(TEXT("time"), K.Time) && !O->TryGetNumberField(TEXT("t"), K.Time)) return Fail(OpIndex, Id, TEXT("time"), TEXT("add key needs time (frames)"));
						if (!VectorFromJson(O->TryGetField(TEXT("value")), K.Value, Dims) && !VectorFromJson(O->TryGetField(TEXT("v")), K.Value, Dims)) return Fail(OpIndex, Id, TEXT("value"), TEXT("add key needs value"));
						K.Time = Snap(K.Time);
						FString Interp;
						if (O->TryGetStringField(TEXT("interp"), Interp) && !ParseClipInterp(Interp, K.Interp)) return Fail(OpIndex, Id, TEXT("interp"), FString::Printf(TEXT("interp '%s' is unknown"), *Interp), { TEXT("linear"), TEXT("constant"), TEXT("cubic") });
						T->Keys.Add(K);
						AssignClipIds(Work);
						Changed.Add(T->Keys.Last().Id);
					}
					continue;
				}
				if (Kind == TEXT("check"))
				{
					TArray<FClipIssue> CI;
					FClipCheck C;
					if (!CheckFromJson(O, OpIndex, C, CI))
					{
						for (FClipIssue& I : CI) { I.Message = FString::Printf(TEXT("ops[%d]: %s"), OpIndex, *I.Message); OutIssues.Add(I); }
						return false;
					}
					C.Start = Snap(C.Start);
					if (C.Length > 0.0) C.Length = Snap(C.Length);
					Work.Checks.Add(C);
					AssignClipIds(Work);
					Changed.Add(Work.Checks.Last().Id);
					continue;
				}
				if (Kind == TEXT("event"))
				{
					FClipEvent E;
					E.Id = Id;
					const TSharedPtr<FJsonObject>* Do = nullptr;
					if (!O->TryGetNumberField(TEXT("at"), E.At) || !O->TryGetObjectField(TEXT("do"), Do) || !Do) return Fail(OpIndex, Id, TEXT("do"), TEXT("add event needs at (frames) and do (an act or arrange entry)"));
					E.At = Snap(E.At);
					E.Do = *Do;
					Work.Events.Add(E);
					AssignClipIds(Work);
					Changed.Add(Work.Events.Last().Id);
					continue;
				}
				if (Kind == TEXT("region"))
				{
					FClipRegion R;
					R.Id = Id;
					O->TryGetStringField(TEXT("name"), R.Name);
					if (!O->TryGetNumberField(TEXT("start"), R.Start) || !O->TryGetNumberField(TEXT("end"), R.End)) return Fail(OpIndex, Id, TEXT("start"), TEXT("add region needs start and end (frames)"));
					R.Start = Snap(R.Start);
					R.End = Snap(R.End);
					Work.Regions.Add(R);
					AssignClipIds(Work);
					Changed.Add(Work.Regions.Last().Id);
					continue;
				}
				return Fail(OpIndex, Id, TEXT("kind"), FString::Printf(TEXT("add kind '%s' is unknown"), *Kind), { TEXT("note"), TEXT("key"), TEXT("track"), TEXT("check"), TEXT("event"), TEXT("region") });
			}

			if (Op == TEXT("set_clip"))
			{
				double Num = 0.0;
				if (O->TryGetNumberField(TEXT("rate"), Num)) Work.Rate = static_cast<int32>(Num);
				O->TryGetNumberField(TEXT("length"), Work.Length);
				O->TryGetStringField(TEXT("grid"), Work.Grid);
				O->TryGetStringField(TEXT("description"), Work.Description);
				O->TryGetStringField(TEXT("map"), Work.Map);
				O->TryGetStringField(TEXT("profile"), Work.Profile);
				O->TryGetBoolField(TEXT("fixed_timestep"), Work.bFixedTimestep);
				if (O->TryGetNumberField(TEXT("seed"), Num)) Work.Seed = static_cast<int64>(Num);
				O->TryGetNumberField(TEXT("hold_timeout"), Work.HoldTimeout);
				O->TryGetStringField(TEXT("reference"), Work.Reference);
				Changed.Add(TEXT("clip"));
				continue;
			}

			if (Op == TEXT("quantize"))
			{
				FString Grid;
				FClip GridClip = Work;
				if (O->TryGetStringField(TEXT("grid"), Grid)) GridClip.Grid = Grid;
				FString Track;
				O->TryGetStringField(TEXT("track"), Track);
				const TArray<TSharedPtr<FJsonValue>>* IdArr = nullptr;
				TSet<FString> Only;
				if (O->TryGetArrayField(TEXT("ids"), IdArr) && IdArr) for (const TSharedPtr<FJsonValue>& V : *IdArr) Only.Add(V->AsString());
				const FString TrackPath = Track.IsEmpty() ? FString() : NormalizeScriptInputActionPath(Track);
				auto Wanted = [&](const FString& ThingId, const FString& Action) { return (Only.Num() == 0 || Only.Contains(ThingId)) && (TrackPath.IsEmpty() || Action == TrackPath); };
				for (FClipTrack& T : Work.Tracks)
				{
					for (FClipNote& N : T.Notes)
					{
						if (!Wanted(N.Id, T.Action)) continue;
						// A note never quantizes to nothing: it keeps at least one grid step.
						const double S = SnapClipTime(GridClip, N.Start);
						const double E = SnapClipTime(GridClip, N.End());
						N.Length = FMath::Max(E - S, ClipGridStep(GridClip, N.Length));
						N.Start = S;
						Changed.Add(N.Id);
					}
					for (FClipKey& K : T.Keys)
					{
						if (!Wanted(K.Id, T.Action)) continue;
						K.Time = SnapClipTime(GridClip, K.Time);
						Changed.Add(K.Id);
					}
				}
				if (TrackPath.IsEmpty())
				{
					for (FClipCheck& C : Work.Checks) if (Only.Num() == 0 || Only.Contains(C.Id)) { C.Start = SnapClipTime(GridClip, C.Start); C.Length = SnapClipTime(GridClip, C.Length); Changed.Add(C.Id); }
					for (FClipEvent& E : Work.Events) if (Only.Num() == 0 || Only.Contains(E.Id)) { E.At = SnapClipTime(GridClip, E.At); Changed.Add(E.Id); }
					for (FClipRegion& R : Work.Regions) if (Only.Num() == 0 || Only.Contains(R.Id)) { R.Start = SnapClipTime(GridClip, R.Start); R.End = SnapClipTime(GridClip, R.End); Changed.Add(R.Id); }
				}
				continue;
			}

			if (Id.IsEmpty()) return Fail(OpIndex, Id, TEXT("id"), FString::Printf(TEXT("op '%s' needs id, the note, key, check, event or region it changes"), *Op));
			FString Kind;
			int32 Ti = INDEX_NONE, Ii = INDEX_NONE;
			if (!FindById(Work, Id, Kind, Ti, Ii))
			{
				return Fail(OpIndex, Id, TEXT("id"), FString::Printf(TEXT("nothing on the clip has id '%s'"), *Id), RankScriptCandidates(Id, AllIds(Work), 5));
			}

			if (Op == TEXT("delete"))
			{
				if (Kind == TEXT("note")) Work.Tracks[Ti].Notes.RemoveAt(Ii);
				else if (Kind == TEXT("key")) Work.Tracks[Ti].Keys.RemoveAt(Ii);
				else if (Kind == TEXT("check")) Work.Checks.RemoveAt(Ii);
				else if (Kind == TEXT("event")) Work.Events.RemoveAt(Ii);
				else if (Kind == TEXT("region")) Work.Regions.RemoveAt(Ii);
				else if (Kind == TEXT("track")) Work.Tracks.RemoveAt(Ti);
				Changed.Add(Id);
				continue;
			}

			if (Op == TEXT("move"))
			{
				double To = 0.0, By = 0.0;
				const bool bTo = O->TryGetNumberField(TEXT("to"), To);
				const bool bBy = O->TryGetNumberField(TEXT("by"), By);
				if (bTo == bBy) return Fail(OpIndex, Id, TEXT("to"), TEXT("move takes exactly one of to (a frame) or by (frames)"));
				auto Move = [&](double& At) { At = Snap(bTo ? To : At + By); };
				if (Kind == TEXT("note")) Move(Work.Tracks[Ti].Notes[Ii].Start);
				else if (Kind == TEXT("key")) Move(Work.Tracks[Ti].Keys[Ii].Time);
				else if (Kind == TEXT("check")) Move(Work.Checks[Ii].Start);
				else if (Kind == TEXT("event")) Move(Work.Events[Ii].At);
				else if (Kind == TEXT("region"))
				{
					FClipRegion& R = Work.Regions[Ii];
					const double Len = R.End - R.Start;
					Move(R.Start);
					R.End = R.Start + Len;
				}
				else return Fail(OpIndex, Id, TEXT("id"), TEXT("a track does not move; move its notes or keys"));
				Changed.Add(Id);
				continue;
			}

			if (Op == TEXT("trim"))
			{
				double S = 0.0, Len = 0.0, E = 0.0;
				const bool bS = O->TryGetNumberField(TEXT("start"), S);
				const bool bL = O->TryGetNumberField(TEXT("length"), Len);
				const bool bE = O->TryGetNumberField(TEXT("end"), E);
				if (!bS && !bL && !bE) return Fail(OpIndex, Id, TEXT("start"), TEXT("trim takes start, length or end (frames)"));
				if (bL && bE) return Fail(OpIndex, Id, TEXT("length"), TEXT("trim takes length or end, not both"));
				double* Start = nullptr;
				double* Length = nullptr;
				double* End = nullptr;
				if (Kind == TEXT("note")) { Start = &Work.Tracks[Ti].Notes[Ii].Start; Length = &Work.Tracks[Ti].Notes[Ii].Length; }
				else if (Kind == TEXT("check")) { Start = &Work.Checks[Ii].Start; Length = &Work.Checks[Ii].Length; }
				else if (Kind == TEXT("region")) { Start = &Work.Regions[Ii].Start; End = &Work.Regions[Ii].End; }
				else return Fail(OpIndex, Id, TEXT("id"), FString::Printf(TEXT("a %s has no span to trim"), *Kind));
				const double OldEnd = Length ? *Start + *Length : *End;
				if (bS) *Start = Snap(S);
				double NewEnd = OldEnd;
				if (bL) NewEnd = *Start + Snap(Len);
				if (bE) NewEnd = Snap(E);
				if (Length) *Length = NewEnd - *Start;
				else *End = NewEnd;
				Changed.Add(Id);
				continue;
			}

			if (Op == TEXT("set"))
			{
				if (Kind == TEXT("note"))
				{
					FClipNote& N = Work.Tracks[Ti].Notes[Ii];
					int32 Dims = 0;
					if (O->HasField(TEXT("start"))) N.Start = Snap(O->GetNumberField(TEXT("start")));
					if (O->HasField(TEXT("length"))) N.Length = Snap(O->GetNumberField(TEXT("length")));
					if (O->HasField(TEXT("value")) && !VectorFromJson(O->TryGetField(TEXT("value")), N.Value, Dims)) return Fail(OpIndex, Id, TEXT("value"), TEXT("value must be a number, [x, y] or [x, y, z]"));
				}
				else if (Kind == TEXT("key"))
				{
					FClipKey& K = Work.Tracks[Ti].Keys[Ii];
					int32 Dims = 0;
					if (O->HasField(TEXT("time"))) K.Time = Snap(O->GetNumberField(TEXT("time")));
					if (O->HasField(TEXT("value")) && !VectorFromJson(O->TryGetField(TEXT("value")), K.Value, Dims)) return Fail(OpIndex, Id, TEXT("value"), TEXT("value must be a number, [x, y] or [x, y, z]"));
					FString Interp;
					if (O->TryGetStringField(TEXT("interp"), Interp) && !ParseClipInterp(Interp, K.Interp)) return Fail(OpIndex, Id, TEXT("interp"), FString::Printf(TEXT("interp '%s' is unknown"), *Interp), { TEXT("linear"), TEXT("constant"), TEXT("cubic") });
				}
				else if (Kind == TEXT("check"))
				{
					// The check as it stands with the given fields laid over it, parsed again.
					TSharedRef<FJsonObject> Merged = CheckToJson(Work.Checks[Ii]);
					for (const auto& F : O->Values)
					{
						if (F.Key == TEXT("op") || F.Key == TEXT("id")) continue;
						if (F.Key == TEXT("expect"))
						{
							const TSharedPtr<FJsonObject>* E = nullptr;
							const TSharedPtr<FJsonObject>* Current = nullptr;
							if (F.Value->TryGetObject(E) && E && Merged->TryGetObjectField(TEXT("expect"), Current) && Current)
							{
								TSharedRef<FJsonObject> Exp = MakeShared<FJsonObject>();
								Exp->Values = (*Current)->Values;
								for (const auto& EF : (*E)->Values) Exp->SetField(EF.Key, EF.Value);
								Merged->SetObjectField(TEXT("expect"), Exp);
								continue;
							}
						}
						Merged->SetField(F.Key, F.Value);
					}
					FClipCheck C;
					TArray<FClipIssue> CI;
					if (!CheckFromJson(Merged, OpIndex, C, CI))
					{
						for (FClipIssue& I : CI) { I.Message = FString::Printf(TEXT("ops[%d]: %s"), OpIndex, *I.Message); OutIssues.Add(I); }
						return false;
					}
					C.Start = Snap(C.Start);
					C.Id = Id;
					Work.Checks[Ii] = C;
				}
				else if (Kind == TEXT("event"))
				{
					FClipEvent& E = Work.Events[Ii];
					if (O->HasField(TEXT("at"))) E.At = Snap(O->GetNumberField(TEXT("at")));
					const TSharedPtr<FJsonObject>* Do = nullptr;
					if (O->TryGetObjectField(TEXT("do"), Do) && Do) E.Do = *Do;
				}
				else if (Kind == TEXT("region"))
				{
					FClipRegion& R = Work.Regions[Ii];
					O->TryGetStringField(TEXT("name"), R.Name);
					if (O->HasField(TEXT("start"))) R.Start = Snap(O->GetNumberField(TEXT("start")));
					if (O->HasField(TEXT("end"))) R.End = Snap(O->GetNumberField(TEXT("end")));
				}
				else if (Kind == TEXT("track"))
				{
					FString Type;
					if (O->TryGetStringField(TEXT("type"), Type) && !ParseClipValueType(Type, Work.Tracks[Ti].Type)) return Fail(OpIndex, Id, TEXT("type"), FString::Printf(TEXT("type '%s' is unknown"), *Type));
				}
				Changed.Add(Id);
				continue;
			}

			return Fail(OpIndex, Id, TEXT("op"), FString::Printf(TEXT("op '%s' is unknown"), *Op), { TEXT("add"), TEXT("move"), TEXT("trim"), TEXT("set"), TEXT("delete"), TEXT("quantize"), TEXT("set_clip") });
		}
		SortClip(Work);
		// An edit that leaves the clip unreadable is refused whole; warnings come back with the result.
		TArray<FClipIssue> Structure;
		ValidateClipStructure(Work, Structure);
		if (ClipIssuesBlockSave(Structure))
		{
			for (const FClipIssue& I : Structure) if (I.bBlocksSave) OutIssues.Add(I);
			return false;
		}
		Clip = MoveTemp(Work);
		OutChanged = MoveTemp(Changed);
		return true;
	}

	// ── Playback values ────────────────────────────────────────────────

	FVector EvaluateClipTrack(const FClipTrack& Track, double Frame, bool& bOutActive)
	{
		for (const FClipNote& N : Track.Notes)
		{
			if (Frame >= N.Start - 1e-6 && Frame < N.End() - 1e-6)
			{
				bOutActive = true;
				return Track.Type == EClipValueType::Bool ? FVector(1.0, 0.0, 0.0) : N.Value;
			}
		}
		if (Track.Type == EClipValueType::Bool || Track.Keys.Num() == 0 || Frame < Track.Keys[0].Time - 1e-6)
		{
			bOutActive = false;
			return FVector::ZeroVector;
		}
		FVector V = FVector::ZeroVector;
		const int32 Dims = ClipValueDims(Track.Type);
		if (Track.Keys.Num() == 1)
		{
			V = Track.Keys[0].Value;
		}
		else
		{
			for (int32 Axis = 0; Axis < Dims; ++Axis)
			{
				V[Axis] = BuildCurve(Track, Axis).Eval(static_cast<float>(Frame));
			}
		}
		bOutActive = !V.IsNearlyZero(1e-6);
		return V;
	}

	// ── Recording ──────────────────────────────────────────────────────

	void CompClipTake(FClip& Clip, const TArray<FString>& ArmedActions, const TMap<FString, EClipValueType>& Types,
		double Start, double End, const TArray<FClipTakeSample>& Samples, const FString& TakeId, const FString& RecordedAt)
	{
		FClipTake Take;
		Take.Id = TakeId;
		Take.RecordedAt = RecordedAt;
		Take.Start = Start;
		Take.End = End;
		const double Rate = FMath::Max(1, Clip.Rate);

		for (const FString& RawAction : ArmedActions)
		{
			const FString Action = NormalizeScriptInputActionPath(RawAction);
			const EClipValueType* TypePtr = Types.Find(Action);
			FClipTrack* Track = Clip.FindTrack(Action);
			if (!Track)
			{
				FClipTrack NewTrack;
				NewTrack.Action = Action;
				NewTrack.Type = TypePtr ? *TypePtr : EClipValueType::Bool;
				Clip.Tracks.Add(NewTrack);
				Track = &Clip.Tracks.Last();
			}
			// What the range held goes into the take, whole.
			FClipTrack Old;
			Old.Action = Track->Action;
			Old.Type = Track->Type;
			for (int32 i = Track->Notes.Num() - 1; i >= 0; --i)
			{
				const FClipNote& N = Track->Notes[i];
				if (N.Start < End && N.End() > Start)
				{
					Old.Notes.Insert(N, 0);
					Track->Notes.RemoveAt(i);
				}
			}
			for (int32 i = Track->Keys.Num() - 1; i >= 0; --i)
			{
				if (Track->Keys[i].Time >= Start && Track->Keys[i].Time < End)
				{
					Old.Keys.Insert(Track->Keys[i], 0);
					Track->Keys.RemoveAt(i);
				}
			}
			if (Old.Notes.Num() > 0 || Old.Keys.Num() > 0) Take.Tracks.Add(MoveTemp(Old));

			const FString ValueName = Action;
			auto ValueAt = [&ValueName](const FClipTakeSample& S) { const FVector* V = S.Values.Find(ValueName); return V ? *V : FVector::ZeroVector; };

			// The take keeps the frames the values were read on, exactly. The clip keeps those frames' times too
			// (FrameTimes), so a fixed-timestep replay lands on them and reads each value on the frame it was played.
			if (Track->Type == EClipValueType::Bool)
			{
				// Notes from the value's edges: from the first frame down to the first frame up again.
				double NoteStart = -1.0;
				for (int32 i = 0; i < Samples.Num(); ++i)
				{
					const bool bDown = ValueAt(Samples[i]).X != 0.0;
					if (bDown && NoteStart < 0.0) NoteStart = Samples[i].Frame;
					const bool bLast = i == Samples.Num() - 1;
					if (NoteStart >= 0.0 && (!bDown || bLast))
					{
						FClipNote N;
						N.Start = NoteStart;
						const double NoteEnd = bDown ? FMath::Min(End, Samples[i].Frame + 1.0) : Samples[i].Frame;
						N.Length = FMath::Max(1e-3, NoteEnd - N.Start);
						Track->Notes.Add(N);
						NoteStart = -1.0;
					}
				}
			}
			else
			{
				// A key on every frame read, reduced with FRichCurve to the keys the curve needs.
				const int32 Dims = ClipValueDims(Track->Type);
				TArray<double> Frames;
				TArray<FVector> Values;
				for (const FClipTakeSample& S : Samples)
				{
					if (S.Frame < Start - 1e-6 || S.Frame >= End - 1e-6) continue;
					Frames.Add(S.Frame);
					Values.Add(ValueAt(S));
				}
				// The frames the reduced curves keep, by index into Frames.
				TSet<int32> KeyIdx;
				auto Nearest = [&Frames](double T)
				{
					int32 Lo = 0, Hi = Frames.Num() - 1;
					while (Lo < Hi) { const int32 Mid = (Lo + Hi) / 2; if (Frames[Mid] < T) Lo = Mid + 1; else Hi = Mid; }
					return (Lo > 0 && FMath::Abs(Frames[Lo - 1] - T) < FMath::Abs(Frames[Lo] - T)) ? Lo - 1 : Lo;
				};
				TArray<FRichCurve> Curves;
				Curves.SetNum(Dims);
				for (int32 Axis = 0; Axis < Dims && Frames.Num() > 0; ++Axis)
				{
					for (int32 i = 0; i < Frames.Num(); ++i)
					{
						const FKeyHandle H = Curves[Axis].AddKey(static_cast<float>(Frames[i]), static_cast<float>(Values[i][Axis]));
						Curves[Axis].SetKeyInterpMode(H, RCIM_Linear);
					}
					// A tight tolerance: an error in a stick's value is integrated into the pawn's velocity every frame,
					// and at 0.01 a stick circled for five seconds put the character 1.6 m off its recorded path.
					Curves[Axis].RemoveRedundantKeys(1e-4f, FFrameRate(static_cast<int32>(Rate), 1));
					for (auto It = Curves[Axis].GetKeyIterator(); It; ++It) KeyIdx.Add(Nearest(It->Time));
				}
				// Every frame where the value leaves or returns to zero is a key, so the curve is not smeared across a release.
				for (int32 i = 1; i < Values.Num(); ++i)
				{
					if (Values[i].IsNearlyZero(1e-6) != Values[i - 1].IsNearlyZero(1e-6))
					{
						KeyIdx.Add(i - 1);
						KeyIdx.Add(i);
					}
				}
				if (Frames.Num() > 0) { KeyIdx.Add(0); KeyIdx.Add(Frames.Num() - 1); }
				TArray<int32> Sorted = KeyIdx.Array();
				Sorted.Sort();
				for (int32 Idx : Sorted)
				{
					FClipKey K;
					K.Time = Frames[Idx];
					K.Value = Values[Idx];
					K.Interp = EClipInterp::Linear;
					Track->Keys.Add(K);
				}
				// A value released at the end of the range returns to what the track held after it.
				if (Values.Num() > 0 && !Values.Last().IsNearlyZero(1e-6) && Track->Keys.Num() > 0 && End < Clip.EffectiveLength())
				{
					FClipKey K;
					K.Time = End;
					K.Value = FVector::ZeroVector;
					Track->Keys.Add(K);
				}
			}
			SortTrack(*Track);
		}
		AssignClipIds(Clip);
		if (Take.Tracks.Num() > 0) Clip.Takes.Add(MoveTemp(Take));
		// The take's frames replace the frame times over its range, so a replay lands on the frames it was read on.
		if (Samples.Num() > 0)
		{
			Clip.FrameTimes.RemoveAll([Start, End](double T) { return T >= Start - 1e-6 && T < End - 1e-6; });
			for (const FClipTakeSample& S : Samples)
			{
				if (S.Frame >= Start - 1e-6 && S.Frame < End - 1e-6) Clip.FrameTimes.Add(S.Frame);
			}
			Clip.FrameTimes.Sort();
			for (int32 i = Clip.FrameTimes.Num() - 1; i > 0; --i)
			{
				if (Clip.FrameTimes[i] - Clip.FrameTimes[i - 1] < 1e-4) Clip.FrameTimes.RemoveAt(i);
			}
		}
		if (Clip.Length > 0.0 && End > Clip.Length) Clip.Length = End;
	}

	// ── Runs ───────────────────────────────────────────────────────────

	namespace
	{
		bool CheckHolds(const FClipCheckRun& Run, const FClipCheckFrame& F)
		{
			if (!F.bObserved) return false;
			if (IsScriptTriggerOp(Run.Def.Expect.Op)) return F.Value != 0.0;
			return EvaluateScriptCondition(Run.Def.Expect, Run.AtStart, F.Value);
		}

		void Settle(FClipCheckRun& Run, EClipCheckStatus Status, const FClipCheckFrame& F, const FString& Reason)
		{
			Run.Status = Status;
			Run.EndedFrame = F.Frame;
			Run.EndedMs = F.GameMs;
			Run.Reason = Reason;
		}
	}

	void AdvanceClipCheck(FClipCheckRun& Run, const FClipCheckFrame& F)
	{
		const FClipCheck& C = Run.Def;
		if (Run.Status == EClipCheckStatus::Pending)
		{
			if (F.Frame < C.Start - 1e-6) return;
			Run.Status = C.bManual ? EClipCheckStatus::AwaitingVerdict : EClipCheckStatus::Running;
			Run.StartedFrame = F.Frame;
			Run.StartedMs = F.GameMs;
			if (C.bManual) return;
		}
		if (Run.Status != EClipCheckStatus::Running) return;
		if (F.bObserved)
		{
			if (!Run.bHasAtStart) { Run.bHasAtStart = true; Run.AtStart = F.Value; }
			Run.bHasLast = true;
			Run.Last = F.Value;
			++Run.Samples;
		}
		const bool bHolds = CheckHolds(Run, F);
		const FString What = FString::Printf(TEXT("%s %s%s"), *C.Expect.Value, *ScriptExpectOpToString(C.Expect.Op),
			C.Expect.bHasTarget ? *(TEXT(" ") + FString::SanitizeFloat(C.Expect.Target)) : TEXT(""));
		switch (C.Mode)
		{
		case EClipCheckMode::ByEnd:
			if (bHolds) Settle(Run, EClipCheckStatus::Passed, F, FString());
			else if (F.Frame >= C.End() - 1e-6)
			{
				Settle(Run, EClipCheckStatus::Failed, F, F.bObserved
					? FString::Printf(TEXT("%s was not true by frame %s (last value %s)"), *What, *FString::SanitizeFloat(C.End()), *FString::SanitizeFloat(Run.Last))
					: FString::Printf(TEXT("%s could not be read by frame %s"), *What, *FString::SanitizeFloat(C.End())));
			}
			break;
		case EClipCheckMode::Throughout:
			if (!bHolds)
			{
				Settle(Run, EClipCheckStatus::Failed, F, F.bObserved
					? FString::Printf(TEXT("%s stopped holding at frame %s (value %s)"), *What, *FString::SanitizeFloat(F.Frame), *FString::SanitizeFloat(F.Value))
					: FString::Printf(TEXT("%s could not be read at frame %s"), *What, *FString::SanitizeFloat(F.Frame)));
			}
			else if (F.Frame >= C.End() - 1e-6) Settle(Run, EClipCheckStatus::Passed, F, FString());
			break;
		case EClipCheckMode::Hold:
			if (bHolds) Settle(Run, EClipCheckStatus::Passed, F, FString());
			break;
		}
	}

	void SettleClipCheckAtEnd(FClipCheckRun& Run, const FString& Reason)
	{
		if (Run.Status == EClipCheckStatus::Running || Run.Status == EClipCheckStatus::AwaitingVerdict)
		{
			Run.Status = EClipCheckStatus::Failed;
			Run.Reason = Reason;
		}
		else if (Run.Status == EClipCheckStatus::Pending)
		{
			Run.Status = EClipCheckStatus::Skipped;
			Run.Reason = Reason;
		}
	}

	TSharedRef<FJsonObject> ClipCheckRunToJson(const FClipCheckRun& Run)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"), Run.Def.Id);
		if (!Run.Def.Name.IsEmpty()) O->SetStringField(TEXT("name"), Run.Def.Name);
		O->SetStringField(TEXT("mode"), Run.Def.bManual ? FString(TEXT("manual")) : ClipCheckModeToString(Run.Def.Mode));
		O->SetStringField(TEXT("status"), ClipCheckStatusToString(Run.Status));
		O->SetNumberField(TEXT("start"), Run.Def.Start);
		O->SetNumberField(TEXT("length"), Run.Def.Length);
		if (!Run.Def.bManual) O->SetObjectField(TEXT("expect"), ScriptExpectToJson(Run.Def.Expect));
		else O->SetStringField(TEXT("instruction"), Run.Def.Instruction);
		if (Run.StartedFrame >= 0.0) { O->SetNumberField(TEXT("started_frame"), Run.StartedFrame); O->SetNumberField(TEXT("started_ms"), Run.StartedMs); }
		if (Run.EndedFrame >= 0.0) { O->SetNumberField(TEXT("ended_frame"), Run.EndedFrame); O->SetNumberField(TEXT("ended_ms"), Run.EndedMs); }
		TSharedRef<FJsonObject> Ev = MakeShared<FJsonObject>();
		if (Run.bHasAtStart) Ev->SetNumberField(TEXT("at_start"), Run.AtStart); else Ev->SetField(TEXT("at_start"), MakeShared<FJsonValueNull>());
		if (Run.bHasLast) Ev->SetNumberField(TEXT("last"), Run.Last); else Ev->SetField(TEXT("last"), MakeShared<FJsonValueNull>());
		Ev->SetNumberField(TEXT("samples"), Run.Samples);
		if (!Run.Reason.IsEmpty()) Ev->SetStringField(TEXT("reason"), Run.Reason);
		if (!Run.EvidenceNote.IsEmpty())
		{
			Ev->SetStringField(TEXT("note"), Run.EvidenceNote);
			TArray<TSharedPtr<FJsonValue>> Caps;
			for (const FString& C : Run.EvidenceCaptures) Caps.Add(MakeShared<FJsonValueString>(C));
			Ev->SetArrayField(TEXT("captures"), Caps);
		}
		O->SetObjectField(TEXT("evidence"), Ev);
		if (Run.Def.Mode == EClipCheckMode::Hold && Run.StartedFrame >= 0.0)
		{
			TSharedRef<FJsonObject> H = MakeShared<FJsonObject>();
			H->SetNumberField(TEXT("held_ms"), Run.HeldMs);
			if (Run.Status == EClipCheckStatus::Passed)
			{
				H->SetNumberField(TEXT("resolved_frame"), Run.EndedFrame);
				H->SetNumberField(TEXT("resolved_ms"), Run.EndedMs);
				if (Run.bHasLast) H->SetNumberField(TEXT("value"), Run.Last);
				if (Run.Def.Recorded.bValid)
				{
					H->SetNumberField(TEXT("recorded_ms"), Run.Def.Recorded.GameMs);
					H->SetNumberField(TEXT("drift_ms"), Run.EndedMs - Run.Def.Recorded.GameMs);
				}
			}
			O->SetObjectField(TEXT("hold"), H);
		}
		if (!Run.Note.IsEmpty()) O->SetStringField(TEXT("note"), Run.Note);
		if (!Run.VerdictSource.IsEmpty()) O->SetStringField(TEXT("verdict_source"), Run.VerdictSource);
		return O;
	}

	bool ClipCheckStartsInRegion(const FClip& Clip, int32 RegionIndex, double Start)
	{
		if (!Clip.Regions.IsValidIndex(RegionIndex)) return false;
		const FClipRegion& R = Clip.Regions[RegionIndex];
		const bool bLast = RegionIndex == Clip.Regions.Num() - 1;
		return Start >= R.Start - 1e-6 && (Start < R.End - 1e-6 || (bLast && Start <= R.End + 1e-6));
	}

	TArray<FClipRegionResult> SummarizeClipRegions(const FClip& Clip, const TArray<FClipCheckRun>& Checks,
		const TArray<FClipDriftSample>& Drift, float ThrPosCm, float ThrVelCms)
	{
		TArray<FClipRegionResult> Out;
		for (int32 r = 0; r < Clip.Regions.Num(); ++r)
		{
			const FClipRegion& R = Clip.Regions[r];
			const bool bLast = r == Clip.Regions.Num() - 1;
			FClipRegionResult Res;
			Res.Def = R;
			bool bAnyFailed = false, bAnyOpen = false, bAnyPassed = false;
			for (const FClipCheckRun& C : Checks)
			{
				if (!ClipCheckStartsInRegion(Clip, r, C.Def.Start)) continue;
				Res.CheckIds.Add(C.Def.Id);
				if (C.Status == EClipCheckStatus::Failed) bAnyFailed = true;
				else if (C.Status == EClipCheckStatus::Passed) bAnyPassed = true;
				else if (C.Status != EClipCheckStatus::Skipped) bAnyOpen = true;
			}
			// A region whose checks were all skipped, such as one outside the region a run played, did not pass.
			Res.Status = bAnyFailed ? TEXT("failed") : bAnyOpen ? TEXT("pending") : bAnyPassed ? TEXT("passed")
				: Res.CheckIds.Num() > 0 ? TEXT("skipped") : TEXT("empty");
			for (const FClipDriftSample& D : Drift)
			{
				if (D.Frame < R.Start - 1e-6 || D.Frame >= R.End + (bLast ? 1.0 : 0.0) - 1e-6) continue;
				Res.bHasDrift = true;
				Res.FramesCompared++;
				Res.MaxPositionCm = FMath::Max(Res.MaxPositionCm, D.PositionCm);
				Res.MaxVelocityCms = FMath::Max(Res.MaxVelocityCms, D.VelocityCms);
				if (Res.FirstOverFrame < 0.0 && (D.PositionCm > ThrPosCm || D.VelocityCms > ThrVelCms)) Res.FirstOverFrame = D.Frame;
			}
			Out.Add(Res);
		}
		return Out;
	}
}
