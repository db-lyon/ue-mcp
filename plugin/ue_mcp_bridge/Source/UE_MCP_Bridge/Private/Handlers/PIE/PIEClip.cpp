#include "Handlers/PIE/PIEClip.h"
#include "Curves/RichCurve.h"
#include "Misc/FrameRate.h"

namespace MCPPIE
{
	namespace
	{
		const TCHAR* const ClipValueTypeNames[] = { TEXT("bool"), TEXT("axis1d"), TEXT("axis2d"), TEXT("axis3d") };
		const TCHAR* const ClipInterpNames[] = { TEXT("linear"), TEXT("constant"), TEXT("cubic") };
		const TCHAR* const ClipPerformerKindNames[] = { TEXT("player") };
		constexpr int32 kClipPerformerClientMax = 15;

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

		// A number, a boolean, [x], [x, y] or [x, y, z].
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

		FRichCurve BuildKeyCurve(const TArray<FClipKey>& Keys, int32 Axis)
		{
			FRichCurve Curve;
			for (const FClipKey& K : Keys)
			{
				const FKeyHandle H = Curve.AddKey(static_cast<float>(K.Time), static_cast<float>(K.Value[Axis]));
				Curve.SetKeyInterpMode(H, K.Interp == EClipInterp::Constant ? RCIM_Constant : K.Interp == EClipInterp::Cubic ? RCIM_Cubic : RCIM_Linear);
				if (K.Interp == EClipInterp::Cubic) Curve.SetKeyTangentMode(H, RCTM_Auto);
			}
			Curve.AutoSetTangents();
			return Curve;
		}

		TSharedRef<FJsonObject> KeyToJson(const FClipKey& K, EClipValueType Type)
		{
			TSharedRef<FJsonObject> KO = MakeShared<FJsonObject>();
			KO->SetStringField(TEXT("id"), K.Id);
			KO->SetNumberField(TEXT("t"), K.Time);
			KO->SetField(TEXT("v"), VectorToJson(K.Value, Type));
			if (K.Interp != EClipInterp::Linear) KO->SetStringField(TEXT("interp"), ClipInterpToString(K.Interp));
			return KO;
		}

		bool KeysFromJson(const TSharedPtr<FJsonObject>& O, const FString& Where, TArray<FClipKey>& Out, FString& OutError)
		{
			const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
			if (!O->TryGetArrayField(TEXT("keys"), Keys) || !Keys) return true;
			for (int32 i = 0; i < Keys->Num(); ++i)
			{
				const TSharedPtr<FJsonObject>* KO = nullptr;
				FClipKey K;
				int32 Dims = 0;
				if (!(*Keys)[i]->TryGetObject(KO) || !KO || !(*KO)->TryGetNumberField(TEXT("t"), K.Time) || !VectorFromJson((*KO)->TryGetField(TEXT("v")), K.Value, Dims))
				{
					OutError = FString::Printf(TEXT("%s.keys[%d] needs t (frames) and v (the value)"), *Where, i);
					return false;
				}
				(*KO)->TryGetStringField(TEXT("id"), K.Id);
				FString Interp;
				if ((*KO)->TryGetStringField(TEXT("interp"), Interp) && !ParseClipInterp(Interp, K.Interp))
				{
					OutError = FString::Printf(TEXT("%s.keys[%d].interp '%s' is not linear, constant or cubic"), *Where, i, *Interp);
					return false;
				}
				Out.Add(K);
			}
			return true;
		}

		bool InputFromJson(const TSharedPtr<FJsonObject>& O, int32 Index, FClipTrack& Out, FString& OutError)
		{
			const FString Where = FString::Printf(TEXT("input[%d]"), Index);
			if (!O->TryGetStringField(TEXT("action"), Out.Action) || Out.Action.TrimStartAndEnd().IsEmpty())
			{
				OutError = FString::Printf(TEXT("%s.action is an Input Action path such as /Game/Input/IA_Jump"), *Where);
				return false;
			}
			Out.Action = NormalizeInputActionPath(Out.Action);
			FString Type;
			if (O->TryGetStringField(TEXT("type"), Type) && !ParseClipValueType(Type, Out.Type))
			{
				OutError = FString::Printf(TEXT("%s.type '%s' is not bool, axis1d, axis2d or axis3d"), *Where, *Type);
				return false;
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
						OutError = FString::Printf(TEXT("%s.notes[%d] needs start and length (frames)"), *Where, i);
						return false;
					}
					(*NO)->TryGetStringField(TEXT("id"), N.Id);
					int32 Dims = 0;
					const TSharedPtr<FJsonValue> V = (*NO)->TryGetField(TEXT("value"));
					if (Out.Type != EClipValueType::Bool && V.IsValid() && !VectorFromJson(V, N.Value, Dims))
					{
						OutError = FString::Printf(TEXT("%s.notes[%d].value is a number or a list of up to three"), *Where, i);
						return false;
					}
					Out.Notes.Add(N);
				}
			}
			return KeysFromJson(O, Where, Out.Keys, OutError);
		}

		TSharedRef<FJsonObject> InputToJson(const FClipTrack& T)
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
			TArray<TSharedPtr<FJsonValue>> Keys;
			if (T.Type != EClipValueType::Bool) for (const FClipKey& K : T.Keys) Keys.Add(MakeShared<FJsonValueObject>(KeyToJson(K, T.Type)));
			O->SetArrayField(TEXT("keys"), Keys);
			return O;
		}

		void SortClip(FClip& Clip)
		{
			for (FClipTrack& T : Clip.Input)
			{
				T.Notes.StableSort([](const FClipNote& A, const FClipNote& B) { return A.Start < B.Start; });
				T.Keys.StableSort([](const FClipKey& A, const FClipKey& B) { return A.Time < B.Time; });
			}
		}

		// Fills empty ids with Prefix<N>, N after the highest Prefix<N> already used.
		void FillIds(TArray<FString*>& Ids, const TCHAR* Prefix)
		{
			int32 Next = 1;
			const FString P(Prefix);
			for (const FString* Id : Ids)
			{
				if (Id->StartsWith(P) && Id->Mid(P.Len()).IsNumeric()) Next = FMath::Max(Next, FCString::Atoi(*Id->Mid(P.Len())) + 1);
			}
			for (FString* Id : Ids) if (Id->IsEmpty()) *Id = FString::Printf(TEXT("%s%d"), Prefix, Next++);
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
	int32 ClipValueDims(EClipValueType Type)
	{
		switch (Type)
		{
		case EClipValueType::Axis2D: return 2;
		case EClipValueType::Axis3D: return 3;
		default: return 1;
		}
	}

	FString NormalizeInputActionPath(const FString& Path)
	{
		const FString Trimmed = Path.TrimStartAndEnd();
		if (!Trimmed.StartsWith(TEXT("/"))) return Trimmed;
		int32 LastSlash = INDEX_NONE;
		Trimmed.FindLastChar(TEXT('/'), LastSlash);
		const FString Leaf = Trimmed.Mid(LastSlash + 1);
		if (Leaf.IsEmpty() || Leaf.Contains(TEXT("."))) return Trimmed;
		return Trimmed + TEXT(".") + Leaf;
	}

	// ── Performers ─────────────────────────────────────────────────────

	FString ClipPerformerKindToString(EClipPerformerKind Kind) { return ClipPerformerKindNames[static_cast<int32>(Kind)]; }
	bool ParseClipPerformerKind(const FString& Str, EClipPerformerKind& Out)
	{
		const FString S = Str.TrimStartAndEnd().ToLower();
		for (int32 i = 0; i < UE_ARRAY_COUNT(ClipPerformerKindNames); ++i) if (S == ClipPerformerKindNames[i]) { Out = static_cast<EClipPerformerKind>(i); return true; }
		return false;
	}

	FString FClipPerformer::Key() const
	{
		return FString::Printf(TEXT("player:%d"), Client);
	}

	FString FClipPerformer::Label() const
	{
		return Client == 0 ? FString(TEXT("Editor player")) : FString::Printf(TEXT("Client %d"), Client);
	}

	bool ClipPerformerFromJson(const TSharedPtr<FJsonValue>& Value, FClipPerformer& Out, FString& OutError)
	{
		const TSharedPtr<FJsonObject>* O = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(O) || !O)
		{
			OutError = TEXT("a performer is {kind: player, client}");
			return false;
		}
		FString Kind;
		(*O)->TryGetStringField(TEXT("kind"), Kind);
		if (Kind == TEXT("player"))
		{
			double Client = 0.0;
			(*O)->TryGetNumberField(TEXT("client"), Client);
			if (Client < 0.0 || Client > kClipPerformerClientMax || Client != FMath::FloorToDouble(Client))
			{
				OutError = FString::Printf(TEXT("a player performer's client is 0 (the editor's own player) or a remote client from 1 to %d"), kClipPerformerClientMax);
				return false;
			}
			Out = FClipPerformer::Player(static_cast<int32>(Client));
			return true;
		}
		OutError = FString::Printf(TEXT("performer kind '%s' is not player, the one kind a track drives"), *Kind);
		return false;
	}

	TSharedRef<FJsonObject> ClipPerformerToJson(const FClipPerformer& Performer)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("kind"), ClipPerformerKindToString(Performer.Kind));
		O->SetNumberField(TEXT("client"), Performer.Client);
		return O;
	}

	// ── Clip ───────────────────────────────────────────────────────────

	double FClip::EffectiveLength() const
	{
		double End = Length;
		for (const FClipTrack& T : Input)
		{
			for (const FClipNote& N : T.Notes) End = FMath::Max(End, N.End());
			for (const FClipKey& K : T.Keys) End = FMath::Max(End, K.Time);
		}
		return End;
	}

	FClipTrack* FClip::FindInput(const FString& Action)
	{
		const FString P = NormalizeInputActionPath(Action);
		return Input.FindByPredicate([&P](const FClipTrack& T) { return T.Action == P; });
	}

	// ── JSON ───────────────────────────────────────────────────────────

	bool ClipFromJson(const TSharedPtr<FJsonObject>& Obj, FClip& Out, FString& OutError)
	{
		Out = FClip();
		if (!Obj.IsValid())
		{
			OutError = TEXT("a clip is a JSON object");
			return false;
		}
		Obj->TryGetNumberField(TEXT("rate"), Out.Rate);
		if (Out.Rate <= 0.0 || Out.Rate > 1000.0)
		{
			OutError = TEXT("rate is frames per second, more than 0 and at most 1000");
			return false;
		}
		Obj->TryGetNumberField(TEXT("length"), Out.Length);
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Obj->TryGetArrayField(TEXT("input"), Arr) && Arr)
		{
			for (int32 i = 0; i < Arr->Num(); ++i)
			{
				const TSharedPtr<FJsonObject>* O = nullptr;
				FClipTrack T;
				if (!(*Arr)[i]->TryGetObject(O) || !O) { OutError = FString::Printf(TEXT("input[%d] is an object"), i); return false; }
				if (!InputFromJson(*O, i, T, OutError)) return false;
				Out.Input.Add(MoveTemp(T));
			}
		}
		if (Obj->TryGetArrayField(TEXT("lanes"), Arr) && Arr && Arr->Num() > 0)
		{
			OutError = TEXT("a clip holds Input Action lanes (input) only; move, action and value lanes are not part of the hub");
			return false;
		}
		SortClip(Out);
		AssignClipIds(Out);
		return true;
	}

	TSharedRef<FJsonObject> ClipToJson(const FClip& Clip)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("rate"), Clip.Rate);
		O->SetNumberField(TEXT("length"), FMath::RoundToDouble(Clip.EffectiveLength() * 1000.0) / 1000.0);
		TArray<TSharedPtr<FJsonValue>> Input;
		for (const FClipTrack& T : Clip.Input) Input.Add(MakeShared<FJsonValueObject>(InputToJson(T)));
		O->SetArrayField(TEXT("input"), Input);
		return O;
	}

	void AssignClipIds(FClip& Clip)
	{
		TArray<FString*> Notes, Keys;
		for (FClipTrack& T : Clip.Input)
		{
			for (FClipNote& N : T.Notes) Notes.Add(&N.Id);
			for (FClipKey& K : T.Keys) Keys.Add(&K.Id);
		}
		FillIds(Notes, TEXT("n"));
		FillIds(Keys, TEXT("k"));
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
		if (Track.Keys.Num() == 1) V = Track.Keys[0].Value;
		else for (int32 Axis = 0; Axis < ClipValueDims(Track.Type); ++Axis) V[Axis] = BuildKeyCurve(Track.Keys, Axis).Eval(static_cast<float>(Frame));
		bOutActive = !V.IsNearlyZero(1e-6);
		return V;
	}

	// ── Recording ──────────────────────────────────────────────────────

	void PunchClipInput(FClip& Clip, const TArray<FString>& ArmedActions, const TMap<FString, EClipValueType>& Types,
		double Start, double End, const TArray<FClipTakeSample>& Samples)
	{
		const double Rate = FMath::Max(1.0, Clip.Rate);
		const double ClipEnd = Clip.EffectiveLength();
		for (const FString& RawAction : ArmedActions)
		{
			const FString Action = NormalizeInputActionPath(RawAction);
			const EClipValueType* TypePtr = Types.Find(Action);
			FClipTrack* Track = Clip.FindInput(Action);
			if (!Track)
			{
				FClipTrack NewTrack;
				NewTrack.Action = Action;
				NewTrack.Type = TypePtr ? *TypePtr : EClipValueType::Bool;
				Clip.Input.Add(NewTrack);
				Track = &Clip.Input.Last();
			}
			// What the range held is replaced. A note that crosses an edge is cut at it.
			const FClipTrack Old = *Track;
			TArray<FClipNote> Kept;
			for (const FClipNote& N : Track->Notes)
			{
				if (N.End() <= Start + 1e-6 || N.Start >= End - 1e-6) { Kept.Add(N); continue; }
				if (N.Start < Start) { FClipNote A = N; A.Length = Start - N.Start; Kept.Add(A); }
				if (N.End() > End) { FClipNote B = N; B.Id.Reset(); B.Start = End; B.Length = N.End() - End; Kept.Add(B); }
			}
			Track->Notes = MoveTemp(Kept);
			Track->Keys.RemoveAll([Start, End](const FClipKey& K) { return K.Time >= Start - 1e-6 && K.Time < End - 1e-6; });

			auto ValueAt = [&Action](const FClipTakeSample& S) { const FVector* V = S.Values.Find(Action); return V ? *V : FVector::ZeroVector; };
			TArray<const FClipTakeSample*> In;
			for (const FClipTakeSample& S : Samples) if (S.Frame >= Start - 1e-6 && S.Frame < End - 1e-6) In.Add(&S);

			if (Track->Type == EClipValueType::Bool)
			{
				// Notes from the value's edges: from the first frame down to the first frame up again.
				double NoteStart = -1.0;
				for (int32 i = 0; i < In.Num(); ++i)
				{
					const bool bDown = ValueAt(*In[i]).X != 0.0;
					if (bDown && NoteStart < 0.0) NoteStart = In[i]->Frame;
					const bool bLast = i == In.Num() - 1;
					if (NoteStart >= 0.0 && (!bDown || bLast))
					{
						FClipNote N;
						N.Start = NoteStart;
						const double NoteEnd = bDown ? FMath::Min(End, In[i]->Frame + 1.0) : In[i]->Frame;
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
				for (const FClipTakeSample* S : In) { Frames.Add(S->Frame); Values.Add(ValueAt(*S)); }
				TSet<int32> KeyIdx;
				auto Nearest = [&Frames](double T)
				{
					int32 Lo = 0, Hi = Frames.Num() - 1;
					while (Lo < Hi) { const int32 Mid = (Lo + Hi) / 2; if (Frames[Mid] < T) Lo = Mid + 1; else Hi = Mid; }
					return (Lo > 0 && FMath::Abs(Frames[Lo - 1] - T) < FMath::Abs(Frames[Lo] - T)) ? Lo - 1 : Lo;
				};
				for (int32 Axis = 0; Axis < Dims && Frames.Num() > 0; ++Axis)
				{
					FRichCurve Curve;
					for (int32 i = 0; i < Frames.Num(); ++i) Curve.SetKeyInterpMode(Curve.AddKey(static_cast<float>(Frames[i]), static_cast<float>(Values[i][Axis])), RCIM_Linear);
					// A tight tolerance: an error in a stick's value is integrated into the pawn's velocity every frame.
					Curve.RemoveRedundantKeys(1e-4f, FFrameRate(static_cast<int32>(Rate), 1));
					for (auto It = Curve.GetKeyIterator(); It; ++It) KeyIdx.Add(Nearest(It->Time));
				}
				// Every frame where the value leaves or returns to zero is a key, so the curve is not smeared across a release.
				for (int32 i = 1; i < Values.Num(); ++i)
				{
					if (Values[i].IsNearlyZero(1e-6) != Values[i - 1].IsNearlyZero(1e-6)) { KeyIdx.Add(i - 1); KeyIdx.Add(i); }
				}
				if (Frames.Num() > 0) { KeyIdx.Add(0); KeyIdx.Add(Frames.Num() - 1); }
				TArray<int32> Sorted = KeyIdx.Array();
				Sorted.Sort();
				for (int32 Idx : Sorted)
				{
					FClipKey K;
					K.Time = Frames[Idx];
					K.Value = Values[Idx];
					Track->Keys.Add(K);
				}
				// The old curve keeps its shape up to the punch-in and from the punch-out: a key on each edge holds the
				// value it had there. With nothing after, a value held at the end returns to zero there.
				bool bActive = false;
				if (Old.Keys.ContainsByPredicate([Start](const FClipKey& K) { return K.Time < Start - 1e-6; }))
				{
					FClipKey K;
					K.Time = Start - 1e-3;
					K.Value = EvaluateClipTrack(Old, K.Time, bActive);
					Track->Keys.Add(K);
				}
				if (Old.Keys.ContainsByPredicate([End](const FClipKey& K) { return K.Time >= End - 1e-6; }))
				{
					FClipKey K;
					K.Time = End;
					K.Value = EvaluateClipTrack(Old, End, bActive);
					Track->Keys.Add(K);
				}
				else if (Values.Num() > 0 && !Values.Last().IsNearlyZero(1e-6) && End < ClipEnd)
				{
					FClipKey K;
					K.Time = End;
					Track->Keys.Add(K);
				}
			}
		}
		SortClip(Clip);
		AssignClipIds(Clip);
		if (End > Clip.Length) Clip.Length = End;
	}

}
