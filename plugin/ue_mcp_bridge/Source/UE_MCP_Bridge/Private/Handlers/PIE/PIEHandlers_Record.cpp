// PIE recording handlers: pie_record_arm / _disarm / _stop / _status /
// _list / _read / _delete / pie_mark. Members of FPIEHandlers.

#include "Handlers/PIE/PIEHandlers.h"
#include "HandlerUtils.h"
#include "Handlers/PIE/PIEInputRecorder.h"
#include "Handlers/PIE/PIESequenceFormat.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	using namespace MCPPIE;

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

	void WriteStatusFields(TSharedPtr<FJsonObject> R, const FRecorderStatus& S)
	{
		R->SetStringField(TEXT("state"), StateToString(S.State));
		R->SetStringField(TEXT("id"), S.Id);
		R->SetStringField(TEXT("recording_dir"), S.RecordingDir);
		R->SetNumberField(TEXT("current_frame"), S.CurrentFrame);
		R->SetNumberField(TEXT("elapsed_seconds"), S.ElapsedSeconds);
		R->SetNumberField(TEXT("tracked_action_count"), S.TrackedActionCount);
		if (!S.LastError.IsEmpty())
		{
			R->SetStringField(TEXT("last_error"), S.LastError);
		}
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
		if (F.bTakeRecordAttempted)
		{
			R->SetStringField(TEXT("take_recorder_status"), F.TakeRecorderStatus);
		}

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

TSharedPtr<FJsonValue> FPIEHandlers::PieRecordArm(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	int32 PIEInstance = INDEX_NONE;
	FString PIEInstanceError;
	if (!FPIEHandlers::ParsePIEInstance(Params, PIEInstance, PIEInstanceError))
	{
		return MCPError(PIEInstanceError);
	}
	FRecorderArmConfig Cfg;

	const TArray<TSharedPtr<FJsonValue>>* ActionsArr = nullptr;
	if (TryGetArrayParam(Params, TEXT("actions"), ActionsArr))
	{
		Cfg.ActionPaths = JsonArrayToStringList(ActionsArr);
	}
	const TArray<TSharedPtr<FJsonValue>>* TrackedArr = nullptr;
	if (TryGetArrayParam(Params, TEXT("track_values"), TrackedArr))
	{
		Cfg.TrackedValuePaths = JsonArrayToStringList(TrackedArr);
	}
	const TArray<TSharedPtr<FJsonValue>>* TrackedActorsArr = nullptr;
	if (TryGetArrayParam(Params, TEXT("track_actors"), TrackedActorsArr))
	{
		Cfg.TrackedActorIds = JsonArrayToStringList(TrackedActorsArr);
	}

	double AT = 0.15;
	TryGetNumberParam(Params, TEXT("axis_threshold"), AT);
	Cfg.AxisThreshold = static_cast<float>(AT);

	int32 Hz = 60;
	TryGetNumberParam(Params, TEXT("sample_hz"), Hz);
	Cfg.SampleHz = Hz > 0 ? Hz : 60;

	int32 PinFPS = Cfg.SampleHz;
	if (HasParam(Params, TEXT("pin_fps")))
	{
		TryGetNumberParam(Params, TEXT("pin_fps"), PinFPS);
	}
	Cfg.PinFPS = PinFPS;

	bool BV = true;
	if (TryGetBoolParam(Params, TEXT("capture_pawn_state"), BV)) Cfg.bCapturePawnState = BV;
	if (TryGetBoolParam(Params, TEXT("capture_montage"), BV))    Cfg.bCaptureMontage = BV;
	if (TryGetBoolParam(Params, TEXT("take_record"), BV))        Cfg.bTakeRecord = BV;

	int32 ClientId = 0;
	if (TryGetNumberParam(Params, TEXT("client_id"), ClientId))
	{
		Cfg.ClientId = FMath::Max(0, ClientId);
	}

	Cfg.PIEInstance = PIEInstance;

	if (HasParam(Params, TEXT("rng_seed")))
	{
		double SD = 0;
		if (TryGetNumberParam(Params, TEXT("rng_seed"), SD))
		{
			Cfg.RngSeed = static_cast<int64>(SD);
			Cfg.bUserSuppliedSeed = true;
		}
	}

	int32 GapFrames = 6;
	if (TryGetNumberParam(Params, TEXT("run_gap_frames"), GapFrames)) Cfg.RunGapFrames = GapFrames;

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
	MCPSetCreated(Result);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	MCPSetRollback(Result, TEXT("pie_record_disarm"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieRecordDisarm(const TSharedPtr<FJsonObject>& /*Params*/)
{
	MCP_CHECK_GAME_THREAD();
	FString Err;
	const bool OK = FPIEInputRecorder::Get().Disarm(Err);
	if (!OK) return MCPError(Err);
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("disarmed"), true);
	Result->SetBoolField(TEXT("changed"), true);
	MCPSetNoRollback(Result, TEXT("The disarmed recorder's settings are not kept; arm it again with record_arm"));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieRecordStop(const TSharedPtr<FJsonObject>& /*Params*/)
{
	MCP_CHECK_GAME_THREAD();
	const FRecorderFinishResult F = FPIEInputRecorder::Get().ForceStop();
	if (!F.bSuccess && !F.Error.IsEmpty())
	{
		return MCPError(F.Error);
	}
	auto Result = MCPSuccess();
	WriteFinishFields(Result, F);
	Result->SetBoolField(TEXT("alreadyStopped"), F.Id.IsEmpty());
	MCPSetNoRollback(Result, TEXT("A finalized recording cannot be resumed; record_delete removes it"));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieRecordStatus(const TSharedPtr<FJsonObject>& /*Params*/)
{
	MCP_CHECK_GAME_THREAD();
	const FRecorderStatus S = FPIEInputRecorder::Get().GetStatus();
	auto Result = MCPSuccess();
	WriteStatusFields(Result, S);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieRecordList(const TSharedPtr<FJsonObject>& Params)
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

TSharedPtr<FJsonValue> FPIEHandlers::PieRecordRead(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	// limit and offset apply to file=csv only.
	MCPReadParamsAhead(Params, { TEXT("limit"), TEXT("offset") });
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

TSharedPtr<FJsonValue> FPIEHandlers::PieRecordDelete(const TSharedPtr<FJsonObject>& Params)
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
		MCPSetNoRollback(R, TEXT("Nothing was deleted"));
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
	R->SetBoolField(TEXT("alreadyDeleted"), false);
	MCPSetNoRollback(R, TEXT("The recording folder is deleted from disk and no copy is kept"));
	return MCPResult(R);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieMark(const TSharedPtr<FJsonObject>& Params)
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
	MCPSetIdempotencyUnobservable(Result, TEXT("Each call adds another marker at the current frame"));
	MCPSetNoRollback(Result, TEXT("Markers are written into the recording timeline and none is removed"));
	return MCPResult(Result);
}
