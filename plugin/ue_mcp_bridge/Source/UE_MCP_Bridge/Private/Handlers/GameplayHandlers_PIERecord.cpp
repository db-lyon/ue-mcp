// PIE recording handlers: pie_record_arm / _disarm / _stop / _status /
// _list / _read / _delete / pie_mark. Members of FGameplayHandlers.

#include "GameplayHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "PIE/PIEInputRecorder.h"
#include "PIE/PIESequenceFormat.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	using namespace UEMCPPIE;

	FString StateToString(ERecorderState S)
	{
		switch (S)
		{
		case ERecorderState::Idle:           return TEXT("idle");
		case ERecorderState::Armed:          return TEXT("armed");
		case ERecorderState::WaitingForPawn: return TEXT("waiting_for_pawn");
		case ERecorderState::Recording:      return TEXT("recording");
		}
		return TEXT("idle");
	}

	TArray<FString> JsonArrayToStringList(const TArray<TSharedPtr<FJsonValue>>* Arr)
	{
		TArray<FString> Out;
		if (!Arr) return Out;
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S)) Out.Add(S);
		}
		return Out;
	}

	void WriteStatusFields(TSharedPtr<FJsonObject> R, const FRecorderStatus& S)
	{
		R->SetStringField(TEXT("state"), StateToString(S.State));
		R->SetStringField(TEXT("id"), S.Id);
		R->SetStringField(TEXT("recording_dir"), S.RecordingDir);
		R->SetNumberField(TEXT("current_frame"), S.CurrentFrame);
		R->SetNumberField(TEXT("elapsed_seconds"), S.ElapsedSeconds);
		R->SetNumberField(TEXT("tracked_action_count"), S.TrackedActionCount);
	}

	void WriteFinishFields(TSharedPtr<FJsonObject> R, const FRecorderFinishResult& F)
	{
		R->SetStringField(TEXT("id"), F.Id);
		R->SetStringField(TEXT("recording_dir"), F.RecordingDir);
		R->SetStringField(TEXT("manifest_path"), F.ManifestPath);
		R->SetStringField(TEXT("csv_path"), F.CSVPath);
		R->SetStringField(TEXT("sequence_path"), F.SequencePath);
		R->SetNumberField(TEXT("total_frames"), F.TotalFrames);
		R->SetNumberField(TEXT("duration_seconds"), F.DurationSeconds);

		TArray<TSharedPtr<FJsonValue>> Acts;
		for (const FString& A : F.DiscoveredActions) Acts.Add(MakeShared<FJsonValueString>(A));
		R->SetArrayField(TEXT("discovered_actions"), Acts);

		TArray<TSharedPtr<FJsonValue>> Marks;
		for (const FMarker& M : F.Markers)
		{
			TSharedRef<FJsonObject> MO = MakeShared<FJsonObject>();
			MO->SetNumberField(TEXT("frame"), static_cast<double>(M.Frame));
			MO->SetNumberField(TEXT("time"), M.Time);
			MO->SetStringField(TEXT("label"), M.Label);
			Marks.Add(MakeShared<FJsonValueObject>(MO));
		}
		R->SetArrayField(TEXT("markers"), Marks);
	}
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieRecordArm(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FRecorderArmConfig Cfg;

	const TArray<TSharedPtr<FJsonValue>>* ActionsArr = nullptr;
	if (Params->TryGetArrayField(TEXT("actions"), ActionsArr))
	{
		Cfg.ActionPaths = JsonArrayToStringList(ActionsArr);
	}
	const TArray<TSharedPtr<FJsonValue>>* TrackedArr = nullptr;
	if (Params->TryGetArrayField(TEXT("track_values"), TrackedArr))
	{
		Cfg.TrackedValuePaths = JsonArrayToStringList(TrackedArr);
	}
	const TArray<TSharedPtr<FJsonValue>>* TrackedActorsArr = nullptr;
	if (Params->TryGetArrayField(TEXT("track_actors"), TrackedActorsArr))
	{
		Cfg.TrackedActorIds = JsonArrayToStringList(TrackedActorsArr);
	}

	double AT = 0.15;
	Params->TryGetNumberField(TEXT("axis_threshold"), AT);
	Cfg.AxisThreshold = static_cast<float>(AT);

	int32 Hz = 60;
	Params->TryGetNumberField(TEXT("sample_hz"), Hz);
	Cfg.SampleHz = Hz > 0 ? Hz : 60;

	int32 PinFPS = Cfg.SampleHz;
	if (Params->HasField(TEXT("pin_fps")))
	{
		Params->TryGetNumberField(TEXT("pin_fps"), PinFPS);
	}
	Cfg.PinFPS = PinFPS;

	bool BV = true;
	if (Params->TryGetBoolField(TEXT("capture_pawn_state"), BV)) Cfg.bCapturePawnState = BV;
	if (Params->TryGetBoolField(TEXT("capture_montage"), BV))    Cfg.bCaptureMontage = BV;

	if (Params->HasField(TEXT("rng_seed")))
	{
		double SD = 0;
		if (Params->TryGetNumberField(TEXT("rng_seed"), SD))
		{
			Cfg.RngSeed = static_cast<int64>(SD);
			Cfg.bUserSuppliedSeed = true;
		}
	}

	int32 GapFrames = 6;
	if (Params->TryGetNumberField(TEXT("run_gap_frames"), GapFrames)) Cfg.RunGapFrames = GapFrames;

	Cfg.RecordingsRoot = OptionalString(Params, TEXT("recording_dir"));
	Cfg.Id = OptionalString(Params, TEXT("id"));

	FString Err, Msg;
	if (!FPIEInputRecorder::Get().Arm(Cfg, Err, Msg))
	{
		return MCPError(Err);
	}

	const FRecorderStatus S = FPIEInputRecorder::Get().GetStatus();
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("armed"), true);
	Result->SetStringField(TEXT("id"), S.Id);
	Result->SetStringField(TEXT("recording_dir"), S.RecordingDir);
	Result->SetNumberField(TEXT("rng_seed"), static_cast<double>(Cfg.RngSeed));
	Result->SetStringField(TEXT("message"), Msg);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	MCPSetRollback(Result, TEXT("pie_record_disarm"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieRecordDisarm(const TSharedPtr<FJsonObject>& /*Params*/)
{
	MCP_CHECK_GAME_THREAD();
	FString Err;
	const bool OK = FPIEInputRecorder::Get().Disarm(Err);
	if (!OK) return MCPError(Err);
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("disarmed"), true);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieRecordStop(const TSharedPtr<FJsonObject>& /*Params*/)
{
	MCP_CHECK_GAME_THREAD();
	const FRecorderFinishResult F = FPIEInputRecorder::Get().ForceStop();
	if (!F.bSuccess && !F.Error.IsEmpty())
	{
		return MCPError(F.Error);
	}
	auto Result = MCPSuccess();
	WriteFinishFields(Result, F);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieRecordStatus(const TSharedPtr<FJsonObject>& /*Params*/)
{
	MCP_CHECK_GAME_THREAD();
	const FRecorderStatus S = FPIEInputRecorder::Get().GetStatus();
	auto Result = MCPSuccess();
	WriteStatusFields(Result, S);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieRecordList(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	const FString Root = OptionalString(Params, TEXT("recording_dir"), DefaultRecordingsRoot());
	const int32 Limit  = OptionalInt(Params, TEXT("limit"), 200);
	const int32 Offset = OptionalInt(Params, TEXT("offset"), 0);

	TArray<FString> Dirs;
	IFileManager::Get().FindFiles(Dirs, *(Root / TEXT("*")), false, true);
	Dirs.Sort([](const FString& A, const FString& B) { return A > B; }); // newest first

	TArray<TSharedPtr<FJsonValue>> Out;
	int32 Seen = 0;
	for (const FString& D : Dirs)
	{
		const FString ManifestPath = Root / D / TEXT("manifest.json");
		if (!FPaths::FileExists(ManifestPath)) continue;
		if (Seen < Offset) { Seen++; continue; }
		if (Out.Num() >= Limit) break;

		FManifest M;
		FString Err;
		if (!LoadManifest(ManifestPath, M, Err))
		{
			Seen++;
			continue;
		}
		TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("id"), M.Id);
		Row->SetStringField(TEXT("started_at"), M.StartedAt);
		Row->SetNumberField(TEXT("duration_seconds"), M.DurationSeconds);
		Row->SetNumberField(TEXT("total_frames"), M.TotalFrames);
		Row->SetStringField(TEXT("pawn_class"), M.PawnClass);
		Row->SetStringField(TEXT("pie_world"), M.PIEWorld);
		Row->SetStringField(TEXT("dir"), Root / D);
		Out.Add(MakeShared<FJsonValueObject>(Row));
		Seen++;
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("recordings"), Out);
	Result->SetStringField(TEXT("root"), Root);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieRecordRead(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Id;
	if (auto E = RequireString(Params, TEXT("id"), Id)) return E;
	const FString File = OptionalString(Params, TEXT("file"), TEXT("manifest"));
	const FString Root = OptionalString(Params, TEXT("recording_dir"), DefaultRecordingsRoot());

	FString Filename;
	if      (File == TEXT("manifest")) Filename = TEXT("manifest.json");
	else if (File == TEXT("sequence")) Filename = TEXT("sequence.json");
	else if (File == TEXT("csv"))      Filename = TEXT("recording.csv");
	else if (File == TEXT("drift"))    Filename = TEXT("drift.json");
	else if (File == TEXT("tracked"))  Filename = TEXT("tracked.jsonl");
	else return MCPError(FString::Printf(TEXT("Unknown file '%s' (manifest|sequence|csv|drift|tracked)"), *File));

	const FString Path = Root / Id / Filename;
	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *Path))
	{
		return MCPError(FString::Printf(TEXT("Not found: %s"), *Path));
	}

	// CSV gets row-window slicing; JSON returns raw content.
	if (File == TEXT("csv"))
	{
		const int32 Limit  = OptionalInt(Params, TEXT("limit"), 1000);
		const int32 Offset = OptionalInt(Params, TEXT("offset"), 0);
		TArray<FString> Lines;
		Raw.ParseIntoArrayLines(Lines, false);
		FString Out;
		// Preserve the leading '#' comment + header row.
		int32 Header = 0;
		while (Header < Lines.Num() && (Lines[Header].StartsWith(TEXT("#")) || Header == 0))
		{
			Out += Lines[Header] + TEXT("\n");
			Header++;
			if (Header >= 2) break; // comment + header
		}
		const int32 Start = Header + Offset;
		const int32 End = FMath::Min(Lines.Num(), Start + Limit);
		for (int32 i = Start; i < End; ++i) Out += Lines[i] + TEXT("\n");

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("path"), Path);
		Result->SetStringField(TEXT("content"), Out);
		Result->SetNumberField(TEXT("row_count"), Lines.Num() - Header);
		return MCPResult(Result);
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), Path);
	Result->SetStringField(TEXT("content"), Raw);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieRecordDelete(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Id;
	if (auto E = RequireString(Params, TEXT("id"), Id)) return E;
	const bool bConfirm = OptionalBool(Params, TEXT("confirm"), false);
	const FString Root = OptionalString(Params, TEXT("recording_dir"), DefaultRecordingsRoot());
	const FString Dir = Root / Id;

	if (!FPaths::DirectoryExists(Dir))
	{
		auto R = MCPSuccess();
		R->SetBoolField(TEXT("alreadyDeleted"), true);
		R->SetStringField(TEXT("dir"), Dir);
		return MCPResult(R);
	}
	if (!bConfirm)
	{
		return MCPError(FString::Printf(TEXT("Refusing to delete '%s' without confirm=true"), *Dir));
	}
	const bool OK = IFileManager::Get().DeleteDirectory(*Dir, false, true);
	if (!OK) return MCPError(FString::Printf(TEXT("Delete failed: %s"), *Dir));

	auto R = MCPSuccess();
	R->SetStringField(TEXT("dir"), Dir);
	R->SetBoolField(TEXT("deleted"), true);
	return MCPResult(R);
}

TSharedPtr<FJsonValue> FGameplayHandlers::PieMark(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Label;
	if (auto E = RequireString(Params, TEXT("label"), Label)) return E;
	FRecorderStatus S;
	const bool Active = FPIEInputRecorder::Get().Mark(Label, S);

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("active"), Active);
	Result->SetStringField(TEXT("label"), Label);
	WriteStatusFields(Result, S);
	return MCPResult(Result);
}
