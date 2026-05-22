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
	};

	bool LoadPawnFrames(const FString& CSVPath, TArray<FCSVPawn>& Out, FString& OutError)
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

	const FString CsvA = Root / A / TEXT("recording.csv");
	const FString CsvB = Root / B / TEXT("recording.csv");
	if (!FPaths::FileExists(CsvA)) return MCPError(FString::Printf(TEXT("not found: %s"), *CsvA));
	if (!FPaths::FileExists(CsvB)) return MCPError(FString::Printf(TEXT("not found: %s"), *CsvB));

	TArray<FCSVPawn> RowsA, RowsB;
	FString Err;
	if (!LoadPawnFrames(CsvA, RowsA, Err)) return MCPError(Err);
	if (!LoadPawnFrames(CsvB, RowsB, Err)) return MCPError(Err);

	const int32 N = FMath::Min(RowsA.Num(), RowsB.Num());
	float MaxPos = 0.f, MaxVel = 0.f, MaxRot = 0.f;
	uint64 MaxPosFrame = 0;
	int32 MontageMismatches = 0;
	TArray<TSharedPtr<FJsonValue>> Over;

	for (int32 i = 0; i < N; ++i)
	{
		const float P = static_cast<float>((RowsA[i].Loc - RowsB[i].Loc).Size());
		const float V = static_cast<float>((RowsA[i].Vel - RowsB[i].Vel).Size());
		const float R = static_cast<float>(FMath::Abs((RowsA[i].Rot - RowsB[i].Rot).Euler().Size()));
		if (P > MaxPos) { MaxPos = P; MaxPosFrame = RowsA[i].Frame; }
		if (V > MaxVel) MaxVel = V;
		if (R > MaxRot) MaxRot = R;
		if (RowsA[i].Montage != RowsB[i].Montage) MontageMismatches++;
		if (P > ThrPos || V > ThrVel || R > ThrRot)
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
	Result->SetArrayField(TEXT("frames_over_threshold"), Over);
	return MCPResult(Result);
}
