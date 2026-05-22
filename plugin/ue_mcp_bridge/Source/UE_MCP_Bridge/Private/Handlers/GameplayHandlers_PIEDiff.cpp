// Pure offline diff of two PIE recordings. Reads both recording.csv files,
// walks rows in lockstep by frame index, and emits a summary matching the
// drift.json shape (without writing to disk).

#include "GameplayHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "PIE/PIESequenceFormat.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	using namespace UEMCPPIE;

	struct FCSVPawn
	{
		uint64 Frame = 0;
		double Time = 0;
		FVector Loc = FVector::ZeroVector;
		FRotator Rot = FRotator::ZeroRotator;
		FVector Vel = FVector::ZeroVector;
		float Speed2D = 0.f;
		FString Montage;
		TMap<FString, double> Tracked;
	};

	bool LoadPawnFrames(const FString& CSVPath, TArray<FCSVPawn>& Out, TArray<FString>& OutTrackedPaths, FString& OutError)
	{
		FString Raw;
		if (!FFileHelper::LoadFileToString(Raw, *CSVPath))
		{
			OutError = FString::Printf(TEXT("read failed: %s"), *CSVPath);
			return false;
		}
		TArray<FString> Lines;
		Raw.ParseIntoArrayLines(Lines, false);
		int32 Hdr = 0;
		while (Hdr < Lines.Num() && Lines[Hdr].StartsWith(TEXT("#"))) Hdr++;
		if (Hdr >= Lines.Num()) { OutError = TEXT("no header row"); return false; }
		TArray<FString> H;
		Lines[Hdr].ParseIntoArray(H, TEXT(","), false);
		auto Col = [&H](const TCHAR* N)
		{
			for (int32 i = 0; i < H.Num(); ++i) if (H[i] == N) return i;
			return -1;
		};
		const int32 CF = Col(TEXT("frame"));
		const int32 CT = Col(TEXT("time"));
		const int32 CPx = Col(TEXT("pos_x"));
		const int32 CPy = Col(TEXT("pos_y"));
		const int32 CPz = Col(TEXT("pos_z"));
		const int32 CRy = Col(TEXT("rot_yaw"));
		const int32 CRp = Col(TEXT("rot_pitch"));
		const int32 CRr = Col(TEXT("rot_roll"));
		const int32 CVx = Col(TEXT("vel_x"));
		const int32 CVy = Col(TEXT("vel_y"));
		const int32 CVz = Col(TEXT("vel_z"));
		const int32 CS2 = Col(TEXT("speed2d"));
		const int32 CMo = Col(TEXT("montage"));

		// Discover tracked-value columns by their "t:" prefix.
		TArray<TPair<int32, FString>> TrackedCols;
		for (int32 i = 0; i < H.Num(); ++i)
		{
			if (H[i].StartsWith(TEXT("t:")))
			{
				const FString Path = H[i].Mid(2);
				TrackedCols.Add(TPair<int32, FString>(i, Path));
				OutTrackedPaths.AddUnique(Path);
			}
		}

		auto Read = [](const TArray<FString>& Cs, int32 Idx)
		{
			if (Idx < 0 || Idx >= Cs.Num()) return 0.0;
			return FCString::Atod(*Cs[Idx]);
		};

		for (int32 i = Hdr + 1; i < Lines.Num(); ++i)
		{
			if (Lines[i].IsEmpty()) continue;
			TArray<FString> Cols;
			Lines[i].ParseIntoArray(Cols, TEXT(","), false);
			FCSVPawn P;
			P.Frame = static_cast<uint64>(Read(Cols, CF));
			P.Time = Read(Cols, CT);
			P.Loc = FVector(Read(Cols, CPx), Read(Cols, CPy), Read(Cols, CPz));
			P.Rot = FRotator(Read(Cols, CRp), Read(Cols, CRy), Read(Cols, CRr));
			P.Vel = FVector(Read(Cols, CVx), Read(Cols, CVy), Read(Cols, CVz));
			P.Speed2D = static_cast<float>(Read(Cols, CS2));
			if (CMo >= 0 && CMo < Cols.Num()) P.Montage = Cols[CMo];
			for (const TPair<int32, FString>& TC : TrackedCols)
			{
				P.Tracked.Add(TC.Value, Read(Cols, TC.Key));
			}
			Out.Add(P);
		}
		return true;
	}
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieRecordDiff(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString A, B;
	if (auto E = RequireString(Params, TEXT("a_id"), A)) return E;
	if (auto E = RequireString(Params, TEXT("b_id"), B)) return E;
	const FString Root = OptionalString(Params, TEXT("recording_dir"), DefaultRecordingsRoot());
	const double ThrPos = OptionalNumber(Params, TEXT("position_cm"), 5.0);
	const double ThrRot = OptionalNumber(Params, TEXT("rotation_deg"), 2.0);
	const double ThrVel = OptionalNumber(Params, TEXT("velocity_cms"), 25.0);
	const double ThrTrackedDefault = OptionalNumber(Params, TEXT("tracked_default"), 0.0);

	// Per-tracked-path thresholds. Falls back to ThrTrackedDefault when a path
	// is sampled in both CSVs but missing from this map. <= 0 means tracked
	// deltas don't trip frames_over_threshold.
	TMap<FString, double> TrackedThresholds;
	const TSharedPtr<FJsonObject>* TT = nullptr;
	if (Params->TryGetObjectField(TEXT("tracked_thresholds"), TT) && TT)
	{
		for (const auto& KV : (*TT)->Values)
		{
			double D = 0;
			if (KV.Value.IsValid() && KV.Value->TryGetNumber(D))
			{
				TrackedThresholds.Add(KV.Key, D);
			}
		}
	}

	const FString CsvA = Root / A / TEXT("recording.csv");
	const FString CsvB = Root / B / TEXT("recording.csv");
	if (!FPaths::FileExists(CsvA)) return MCPError(FString::Printf(TEXT("not found: %s"), *CsvA));
	if (!FPaths::FileExists(CsvB)) return MCPError(FString::Printf(TEXT("not found: %s"), *CsvB));

	TArray<FCSVPawn> RowsA, RowsB;
	TArray<FString> TrackedA, TrackedB;
	FString Err;
	if (!LoadPawnFrames(CsvA, RowsA, TrackedA, Err)) return MCPError(Err);
	if (!LoadPawnFrames(CsvB, RowsB, TrackedB, Err)) return MCPError(Err);

	// Tracked-value paths shared by both recordings.
	TArray<FString> SharedTracked;
	for (const FString& P : TrackedA)
	{
		if (TrackedB.Contains(P)) SharedTracked.Add(P);
	}

	const int32 N = FMath::Min(RowsA.Num(), RowsB.Num());
	float MaxPos = 0.f, MaxVel = 0.f, MaxRot = 0.f;
	uint64 MaxPosFrame = 0;
	int32 MontageMismatches = 0;
	TArray<TSharedPtr<FJsonValue>> Over;
	TMap<FString, double> MaxTrackedDeltas;
	for (const FString& P : SharedTracked) MaxTrackedDeltas.Add(P, 0.0);

	for (int32 i = 0; i < N; ++i)
	{
		const float P = static_cast<float>((RowsA[i].Loc - RowsB[i].Loc).Size());
		const float V = static_cast<float>((RowsA[i].Vel - RowsB[i].Vel).Size());
		const float R = static_cast<float>(FMath::Abs((RowsA[i].Rot - RowsB[i].Rot).Euler().Size()));
		if (P > MaxPos) { MaxPos = P; MaxPosFrame = RowsA[i].Frame; }
		if (V > MaxVel) MaxVel = V;
		if (R > MaxRot) MaxRot = R;
		if (RowsA[i].Montage != RowsB[i].Montage) MontageMismatches++;

		bool bTrackedOver = false;
		for (const FString& Path : SharedTracked)
		{
			const double* VA = RowsA[i].Tracked.Find(Path);
			const double* VB = RowsB[i].Tracked.Find(Path);
			const double D = FMath::Abs((VA ? *VA : 0.0) - (VB ? *VB : 0.0));
			double& Max = MaxTrackedDeltas.FindOrAdd(Path, 0.0);
			if (D > Max) Max = D;
			const double* PerPath = TrackedThresholds.Find(Path);
			const double Thr = PerPath ? *PerPath : ThrTrackedDefault;
			if (Thr > 0.0 && D > Thr) bTrackedOver = true;
		}

		if (P > ThrPos || V > ThrVel || R > ThrRot || bTrackedOver)
		{
			TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
			E->SetNumberField(TEXT("frame"), static_cast<double>(RowsA[i].Frame));
			E->SetNumberField(TEXT("position_delta_cm"), P);
			E->SetNumberField(TEXT("velocity_delta_cms"), V);
			E->SetNumberField(TEXT("rotation_delta_deg"), R);
			Over.Add(MakeShared<FJsonValueObject>(E));
		}
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("a_id"), A);
	Result->SetStringField(TEXT("b_id"), B);
	Result->SetNumberField(TEXT("frames_compared"), N);
	Result->SetNumberField(TEXT("frames_a"), RowsA.Num());
	Result->SetNumberField(TEXT("frames_b"), RowsB.Num());
	Result->SetNumberField(TEXT("max_position_drift_cm"), MaxPos);
	Result->SetNumberField(TEXT("max_position_drift_frame"), static_cast<double>(MaxPosFrame));
	Result->SetNumberField(TEXT("max_velocity_drift_cms"), MaxVel);
	Result->SetNumberField(TEXT("max_rotation_drift_deg"), MaxRot);
	Result->SetNumberField(TEXT("montage_section_mismatches"), MontageMismatches);

	TSharedRef<FJsonObject> TVD = MakeShared<FJsonObject>();
	for (const TPair<FString, double>& KV : MaxTrackedDeltas)
	{
		TVD->SetNumberField(KV.Key, KV.Value);
	}
	Result->SetObjectField(TEXT("tracked_value_max_deltas"), TVD);

	// Actor-scoped diff: optional tracked.jsonl sidecar in both recordings.
	// Walks in lockstep by row index (frame). Per-actor entries that resolved
	// in both rows contribute to max position / rotation / velocity deltas;
	// unresolved frames are counted so consumers can spot intermittent
	// resolution.
	const FString JsonlA = Root / A / TEXT("tracked.jsonl");
	const FString JsonlB = Root / B / TEXT("tracked.jsonl");
	if (FPaths::FileExists(JsonlA) && FPaths::FileExists(JsonlB))
	{
		TArray<FTrackedActorRow> RowsTA, RowsTB;
		FString TErr;
		if (LoadTrackedActorsJSONL(JsonlA, RowsTA, TErr) && LoadTrackedActorsJSONL(JsonlB, RowsTB, TErr))
		{
			const int32 NT = FMath::Min(RowsTA.Num(), RowsTB.Num());
			TMap<FString, FActorDrift> ActorAcc;
			for (int32 i = 0; i < NT; ++i)
			{
				for (const TPair<FString, FActorState>& KV : RowsTA[i].Actors)
				{
					FActorDrift& Acc = ActorAcc.FindOrAdd(KV.Key);
					const FActorState* SB = RowsTB[i].Actors.Find(KV.Key);
					if (!KV.Value.bResolved) { Acc.FramesUnresolvedInSource++; continue; }
					if (!SB || !SB->bResolved) { Acc.FramesUnresolvedInReplay++; continue; }
					const float DPos = static_cast<float>((KV.Value.Location - SB->Location).Size());
					const float DVel = static_cast<float>((KV.Value.Velocity - SB->Velocity).Size());
					const float DRot = static_cast<float>(FMath::Abs((KV.Value.Rotation - SB->Rotation).Euler().Size()));
					if (DPos > Acc.MaxPositionCm) Acc.MaxPositionCm = DPos;
					if (DVel > Acc.MaxVelocityCms) Acc.MaxVelocityCms = DVel;
					if (DRot > Acc.MaxRotationDeg) Acc.MaxRotationDeg = DRot;
				}
			}
			TSharedRef<FJsonObject> AD = MakeShared<FJsonObject>();
			for (const TPair<FString, FActorDrift>& KV : ActorAcc)
			{
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetNumberField(TEXT("max_position_cm"), KV.Value.MaxPositionCm);
				Item->SetNumberField(TEXT("max_rotation_deg"), KV.Value.MaxRotationDeg);
				Item->SetNumberField(TEXT("max_velocity_cms"), KV.Value.MaxVelocityCms);
				Item->SetNumberField(TEXT("frames_unresolved_in_source"), KV.Value.FramesUnresolvedInSource);
				Item->SetNumberField(TEXT("frames_unresolved_in_replay"), KV.Value.FramesUnresolvedInReplay);
				AD->SetObjectField(KV.Key, Item);
			}
			Result->SetObjectField(TEXT("actor_drift"), AD);
		}
	}

	Result->SetArrayField(TEXT("frames_over_threshold"), Over);
	return MCPResult(Result);
}
