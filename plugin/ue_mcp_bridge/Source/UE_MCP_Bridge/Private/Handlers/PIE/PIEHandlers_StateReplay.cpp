// State replay handler: replay_state (roadmap item 2a).
// Reads a recording's baked per-frame transforms and returns the exact recorded
// pawn/actor state at a given time (scrub/snapshot). Deterministic by
// construction: nothing is simulated, state is read back from recorded keys.
// Optionally applies the recorded pose to the live PIE pawn and captures a frame.

#include "Handlers/PIE/PIEHandlers.h"
#include "HandlerUtils.h"
#include "Handlers/PIE/PIESequenceFormat.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	using MCPPIE::SplitCSVLine;

	struct FStateFrame
	{
		double Time = 0.0;
		uint64 Frame = 0;
		FVector Loc = FVector::ZeroVector;
		FRotator Rot = FRotator::ZeroRotator;
		FVector Vel = FVector::ZeroVector;
	};

	TSharedRef<FJsonObject> TransformJson(const FVector& L, const FRotator& R, const FVector& V)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> Loc = MakeShared<FJsonObject>();
		Loc->SetNumberField(TEXT("x"), L.X); Loc->SetNumberField(TEXT("y"), L.Y); Loc->SetNumberField(TEXT("z"), L.Z);
		O->SetObjectField(TEXT("location"), Loc);
		TSharedRef<FJsonObject> Rot = MakeShared<FJsonObject>();
		Rot->SetNumberField(TEXT("pitch"), R.Pitch); Rot->SetNumberField(TEXT("yaw"), R.Yaw); Rot->SetNumberField(TEXT("roll"), R.Roll);
		O->SetObjectField(TEXT("rotation"), Rot);
		TSharedRef<FJsonObject> Vel = MakeShared<FJsonObject>();
		Vel->SetNumberField(TEXT("x"), V.X); Vel->SetNumberField(TEXT("y"), V.Y); Vel->SetNumberField(TEXT("z"), V.Z);
		O->SetObjectField(TEXT("velocity"), Vel);
		return O;
	}
}

TSharedPtr<FJsonValue> FPIEHandlers::PieReplayState(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	const FString Id = OptionalString(Params, TEXT("recording_id"));
	const FString RecDir = OptionalString(Params, TEXT("recording_dir"));

	FString Folder;
	if (!RecDir.IsEmpty() && Id.IsEmpty()) Folder = RecDir;
	else Folder = (RecDir.IsEmpty() ? (FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("MCPRecordings")) : RecDir) / Id;
	Folder.RemoveFromEnd(TEXT("/"));
	Folder.RemoveFromEnd(TEXT("\\"));

	FString Csv;
	if (!FFileHelper::LoadFileToString(Csv, *(Folder / TEXT("recording.csv"))))
	{
		return MCPError(FString::Printf(TEXT("recording.csv not found in %s"), *Folder));
	}

	TArray<FString> Lines;
	Csv.ParseIntoArrayLines(Lines);
	int32 H = INDEX_NONE;
	for (int32 i = 0; i < Lines.Num(); ++i) { if (!Lines[i].StartsWith(TEXT("#"))) { H = i; break; } }
	if (H == INDEX_NONE) return MCPError(TEXT("recording.csv has no header"));

	const TArray<FString> Cols = SplitCSVLine(Lines[H]);
	auto Col = [&Cols](const TCHAR* N) -> int32 { for (int32 i = 0; i < Cols.Num(); ++i) if (Cols[i] == N) return i; return INDEX_NONE; };
	const int32 CT = Col(TEXT("time")), CF = Col(TEXT("frame"));
	const int32 CPX = Col(TEXT("pos_x")), CPY = Col(TEXT("pos_y")), CPZ = Col(TEXT("pos_z"));
	const int32 CRY = Col(TEXT("rot_yaw")), CRP = Col(TEXT("rot_pitch")), CRR = Col(TEXT("rot_roll"));
	const int32 CVX = Col(TEXT("vel_x")), CVY = Col(TEXT("vel_y")), CVZ = Col(TEXT("vel_z"));
	if (CT == INDEX_NONE || CPX == INDEX_NONE) return MCPError(TEXT("recording.csv missing time/pos columns"));

	TArray<FStateFrame> Frames;
	for (int32 i = H + 1; i < Lines.Num(); ++i)
	{
		if (Lines[i].IsEmpty() || Lines[i].StartsWith(TEXT("#"))) continue;
		const TArray<FString> F = SplitCSVLine(Lines[i]);
		auto G = [&F](int32 Ci) -> double { return (Ci != INDEX_NONE && Ci < F.Num()) ? FCString::Atod(*F[Ci]) : 0.0; };
		FStateFrame S;
		S.Time = G(CT);
		S.Frame = (CF != INDEX_NONE && CF < F.Num()) ? static_cast<uint64>(FCString::Atoi64(*F[CF])) : 0;
		S.Loc = FVector(G(CPX), G(CPY), G(CPZ));
		S.Rot = FRotator(G(CRP), G(CRY), G(CRR));
		S.Vel = FVector(G(CVX), G(CVY), G(CVZ));
		Frames.Add(S);
	}
	if (Frames.Num() == 0) return MCPError(TEXT("recording.csv has no data rows"));

	// Resolve the sample: at_time interpolates; at_frame indexes; default = last.
	FStateFrame Out;
	if (HasParam(Params, TEXT("at_time")))
	{
		const double T = OptionalNumber(Params, TEXT("at_time"), 0.0);
		if (T <= Frames[0].Time) { Out = Frames[0]; }
		else if (T >= Frames.Last().Time) { Out = Frames.Last(); }
		else
		{
			int32 i = 0;
			while (i + 1 < Frames.Num() && Frames[i + 1].Time < T) ++i;
			const FStateFrame& A = Frames[i];
			const FStateFrame& B = Frames[FMath::Min(i + 1, Frames.Num() - 1)];
			const double Span = B.Time - A.Time;
			const double Alpha = Span > KINDA_SMALL_NUMBER ? (T - A.Time) / Span : 0.0;
			Out.Time = T;
			Out.Frame = A.Frame;
			Out.Loc = FMath::Lerp(A.Loc, B.Loc, Alpha);
			Out.Rot = FMath::Lerp(A.Rot, B.Rot, Alpha);
			Out.Vel = FMath::Lerp(A.Vel, B.Vel, Alpha);
		}
	}
	else if (HasParam(Params, TEXT("at_frame")))
	{
		const int32 Fi = FMath::Clamp(OptionalInt(Params, TEXT("at_frame"), 0), 0, Frames.Num() - 1);
		Out = Frames[Fi];
	}
	else
	{
		Out = Frames.Last();
	}

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("frame"), static_cast<double>(Out.Frame));
	Result->SetNumberField(TEXT("time"), Out.Time);
	Result->SetNumberField(TEXT("total_frames"), Frames.Num());
	Result->SetNumberField(TEXT("duration"), Frames.Last().Time);
	Result->SetObjectField(TEXT("pawn_state"), TransformJson(Out.Loc, Out.Rot, Out.Vel));
	Result->SetBoolField(TEXT("deterministic"), true);

	// Optionally apply the recorded pose to the live PIE pawn (state replay).
	if (OptionalBool(Params, TEXT("apply"), false))
	{
		UWorld* PIEWorld = GEditor ? GEditor->PlayWorld : nullptr;
		if (!PIEWorld)
		{
			Result->SetBoolField(TEXT("applied"), false);
			Result->SetStringField(TEXT("apply_note"), TEXT("no live PIE session; state not applied"));
		}
		else
		{
			APlayerController* PC = PIEWorld->GetFirstPlayerController();
			APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			if (Pawn)
			{
				const FVector PreviousLoc = Pawn->GetActorLocation();
				const FRotator PreviousRot = Pawn->GetActorRotation();
				Pawn->TeleportTo(Out.Loc, Out.Rot, /*bIsATest*/false, /*bNoCheck*/true);
				Result->SetBoolField(TEXT("applied"), true);
				Result->SetBoolField(TEXT("changed"), !PreviousLoc.Equals(Pawn->GetActorLocation()) || !PreviousRot.Equals(Pawn->GetActorRotation()));
				Result->SetObjectField(TEXT("previous_pawn_state"), TransformJson(PreviousLoc, PreviousRot, FVector::ZeroVector));
			}
			else
			{
				Result->SetBoolField(TEXT("applied"), false);
				Result->SetStringField(TEXT("apply_note"), TEXT("no pawn to apply to"));
			}
		}
		MCPSetNoRollback(Result, TEXT("The pawn's previous pose is reported as previous_pawn_state; no action teleports it back"));
	}
	else
	{
		Result->SetBoolField(TEXT("changed"), false);
		MCPSetNoRollback(Result, TEXT("Nothing was applied"));
	}

	return MCPResult(Result);
}
