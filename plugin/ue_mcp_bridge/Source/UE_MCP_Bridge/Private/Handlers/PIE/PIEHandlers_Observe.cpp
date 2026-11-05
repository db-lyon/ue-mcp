// Observation profile CRUD + observer lifecycle handlers.
// Members of FPIEHandlers.

#include "Handlers/PIE/PIEHandlers.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include "Handlers/PIE/PIEObserver.h"
#include "Handlers/PIE/MCPPIEObservationProfile.h"
#include "Handlers/PIE/PIESequenceFormat.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"

namespace
{
	using namespace MCPPIE;

	FString ObserverStateToString(EObserverState S)
	{
		switch (S)
		{
		case EObserverState::Idle:           return TEXT("idle");
		case EObserverState::Armed:          return TEXT("armed");
		case EObserverState::WaitingForPawn: return TEXT("waiting_for_pawn");
		case EObserverState::Observing:      return TEXT("observing");
		case EObserverState::Completed:      return TEXT("completed");
		}
		return TEXT("idle");
	}

	void WriteObserverStatusFields(TSharedPtr<FJsonObject> R, const FObserverStatus& S)
	{
		R->SetStringField(TEXT("state"), ObserverStateToString(S.State));
		R->SetStringField(TEXT("run_id"), S.RunId);
		R->SetStringField(TEXT("profile"), S.ProfilePath);
		R->SetNumberField(TEXT("frames_sampled"), S.FramesSampled);
		R->SetNumberField(TEXT("elapsed_seconds"), S.ElapsedSeconds);
		if (!S.LastError.IsEmpty())
		{
			R->SetStringField(TEXT("last_error"), S.LastError);
		}
	}

	void ProfileToJson(const UMCPPIEObservationProfile* P, TSharedPtr<FJsonObject> R)
	{
		TArray<TSharedPtr<FJsonValue>> Vals;
		for (const FMCPPIETrackedValueEntry& E : P->TrackedValues)
		{
			TSharedRef<FJsonObject> V = MakeShared<FJsonObject>();
			V->SetStringField(TEXT("path"), E.Path);
			V->SetNumberField(TEXT("drift_threshold"), E.DriftThreshold);
			Vals.Add(MakeShared<FJsonValueObject>(V));
		}
		R->SetArrayField(TEXT("tracked_values"), Vals);

		TArray<TSharedPtr<FJsonValue>> Acts;
		for (const FMCPPIETrackedActorEntry& E : P->TrackedActors)
		{
			Acts.Add(MakeShared<FJsonValueString>(E.ActorId));
		}
		R->SetArrayField(TEXT("tracked_actors"), Acts);

		TArray<TSharedPtr<FJsonValue>> Wids;
		for (const FMCPPIETrackedWidgetEntry& E : P->TrackedWidgets)
		{
			Wids.Add(MakeShared<FJsonValueString>(E.Widget));
		}
		R->SetArrayField(TEXT("tracked_widgets"), Wids);

		R->SetBoolField(TEXT("capture_pawn_state"), P->bCapturePawnState);
		R->SetBoolField(TEXT("capture_montage"), P->bCaptureMontage);
		R->SetNumberField(TEXT("position_threshold_cm"), P->PositionThresholdCm);
		R->SetNumberField(TEXT("rotation_threshold_deg"), P->RotationThresholdDeg);
		R->SetNumberField(TEXT("velocity_threshold_cms"), P->VelocityThresholdCms);
		R->SetNumberField(TEXT("tracked_value_default_threshold"), P->TrackedValueDefaultThreshold);
	}
}

TSharedPtr<FJsonValue> FPIEHandlers::PieProfileCreate(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	const FString PackagePath = OptionalString(Params, TEXT("package_path"), TEXT("/Game/Observation"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	auto Create = MCPCreateAssetIdempotentNewObject<UMCPPIEObservationProfile>(
		Name, PackagePath, OnConflict, TEXT("ObservationProfile"));
	if (Create.EarlyReturn) return Create.EarlyReturn;

	UMCPPIEObservationProfile* Profile = Create.Asset;

	const TArray<TSharedPtr<FJsonValue>>* TVArr = nullptr;
	if (TryGetArrayParam(Params, TEXT("tracked_values"), TVArr) && TVArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *TVArr)
		{
			if (!V.IsValid()) continue;
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			FString Str;
			if (V->TryGetObject(Obj) && Obj)
			{
				FMCPPIETrackedValueEntry E;
				(*Obj)->TryGetStringField(TEXT("path"), E.Path);
				double Thr = 0;
				if ((*Obj)->TryGetNumberField(TEXT("drift_threshold"), Thr))
					E.DriftThreshold = static_cast<float>(Thr);
				if (!E.Path.IsEmpty()) Profile->TrackedValues.Add(E);
			}
			else if (V->TryGetString(Str))
			{
				FMCPPIETrackedValueEntry E;
				E.Path = Str;
				Profile->TrackedValues.Add(E);
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* TAArr = nullptr;
	if (TryGetArrayParam(Params, TEXT("tracked_actors"), TAArr) && TAArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *TAArr)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S))
			{
				FMCPPIETrackedActorEntry E;
				E.ActorId = S;
				Profile->TrackedActors.Add(E);
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* TWArr = nullptr;
	if (TryGetArrayParam(Params, TEXT("tracked_widgets"), TWArr) && TWArr)
	{
		Profile->TrackedWidgets.Reset();
		for (const TSharedPtr<FJsonValue>& V : *TWArr)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty())
			{
				FMCPPIETrackedWidgetEntry E;
				E.Widget = S;
				Profile->TrackedWidgets.Add(E);
			}
		}
	}

	bool BV;
	if (TryGetBoolParam(Params, TEXT("capture_pawn_state"), BV)) Profile->bCapturePawnState = BV;
	if (TryGetBoolParam(Params, TEXT("capture_montage"), BV))    Profile->bCaptureMontage = BV;

	double D;
	if (TryGetNumberParam(Params, TEXT("position_threshold_cm"), D))
		Profile->PositionThresholdCm = static_cast<float>(D);
	if (TryGetNumberParam(Params, TEXT("rotation_threshold_deg"), D))
		Profile->RotationThresholdDeg = static_cast<float>(D);
	if (TryGetNumberParam(Params, TEXT("velocity_threshold_cms"), D))
		Profile->VelocityThresholdCms = static_cast<float>(D);
	if (TryGetNumberParam(Params, TEXT("tracked_value_default_threshold"), D))
		Profile->TrackedValueDefaultThreshold = static_cast<float>(D);

	FString SaveError;
	if (!SaveAssetPackageChecked(Profile, SaveError))
	{
		return MCPError(FString::Printf(TEXT("Saving the profile failed: %s"), *SaveError));
	}

	const FString AssetPath = PackagePath / Name;
	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), AssetPath);
	ProfileToJson(Profile, Result);
	MCPSetDeleteAssetRollback(Result, AssetPath);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieProfileRead(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Path;
	if (auto Err = RequireString(Params, TEXT("path"), Path)) return Err;

	UMCPPIEObservationProfile* Profile = LoadObject<UMCPPIEObservationProfile>(nullptr, *Path);
	if (!Profile) return MCPError(FString::Printf(TEXT("Profile not found: %s"), *Path));

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("path"), Path);
	ProfileToJson(Profile, Result);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieProfileUpdate(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Path;
	if (auto Err = RequireString(Params, TEXT("path"), Path)) return Err;

	UMCPPIEObservationProfile* Profile = LoadObject<UMCPPIEObservationProfile>(nullptr, *Path);
	if (!Profile) return MCPError(FString::Printf(TEXT("Profile not found: %s"), *Path));

	const TArray<TSharedPtr<FJsonValue>>* TVArr = nullptr;
	if (TryGetArrayParam(Params, TEXT("tracked_values"), TVArr) && TVArr)
	{
		Profile->TrackedValues.Reset();
		for (const TSharedPtr<FJsonValue>& V : *TVArr)
		{
			if (!V.IsValid()) continue;
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			FString Str;
			if (V->TryGetObject(Obj) && Obj)
			{
				FMCPPIETrackedValueEntry E;
				(*Obj)->TryGetStringField(TEXT("path"), E.Path);
				double Thr = 0;
				if ((*Obj)->TryGetNumberField(TEXT("drift_threshold"), Thr))
					E.DriftThreshold = static_cast<float>(Thr);
				if (!E.Path.IsEmpty()) Profile->TrackedValues.Add(E);
			}
			else if (V->TryGetString(Str))
			{
				FMCPPIETrackedValueEntry E;
				E.Path = Str;
				Profile->TrackedValues.Add(E);
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* TAArr = nullptr;
	if (TryGetArrayParam(Params, TEXT("tracked_actors"), TAArr) && TAArr)
	{
		Profile->TrackedActors.Reset();
		for (const TSharedPtr<FJsonValue>& V : *TAArr)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S))
			{
				FMCPPIETrackedActorEntry E;
				E.ActorId = S;
				Profile->TrackedActors.Add(E);
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* TWArr = nullptr;
	if (TryGetArrayParam(Params, TEXT("tracked_widgets"), TWArr) && TWArr)
	{
		Profile->TrackedWidgets.Reset();
		for (const TSharedPtr<FJsonValue>& V : *TWArr)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty())
			{
				FMCPPIETrackedWidgetEntry E;
				E.Widget = S;
				Profile->TrackedWidgets.Add(E);
			}
		}
	}

	bool BV;
	if (TryGetBoolParam(Params, TEXT("capture_pawn_state"), BV)) Profile->bCapturePawnState = BV;
	if (TryGetBoolParam(Params, TEXT("capture_montage"), BV))    Profile->bCaptureMontage = BV;

	double D;
	if (TryGetNumberParam(Params, TEXT("position_threshold_cm"), D))
		Profile->PositionThresholdCm = static_cast<float>(D);
	if (TryGetNumberParam(Params, TEXT("rotation_threshold_deg"), D))
		Profile->RotationThresholdDeg = static_cast<float>(D);
	if (TryGetNumberParam(Params, TEXT("velocity_threshold_cms"), D))
		Profile->VelocityThresholdCms = static_cast<float>(D);
	if (TryGetNumberParam(Params, TEXT("tracked_value_default_threshold"), D))
		Profile->TrackedValueDefaultThreshold = static_cast<float>(D);

	FString SaveError;
	if (!SaveAssetPackageChecked(Profile, SaveError))
	{
		return MCPError(FString::Printf(TEXT("Saving the profile failed: %s"), *SaveError));
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("path"), Path);
	ProfileToJson(Profile, Result);
	MCPSetNoRollback(Result, TEXT("The previous profile settings are not kept; profile_read before an update to restore them with profile_update"));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieProfileDelete(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Path;
	if (auto Err = RequireString(Params, TEXT("path"), Path)) return Err;

	if (!OptionalBool(Params, TEXT("confirm"), false))
	{
		return MCPError(TEXT("confirm=true required to delete a profile"));
	}

	if (!UEditorAssetLibrary::DoesAssetExist(Path))
	{
		auto Result = MCPSuccess();
		Result->SetBoolField(TEXT("alreadyDeleted"), true);
		MCPSetNoRollback(Result, TEXT("Nothing was deleted"));
		return MCPResult(Result);
	}

	const bool bDeleted = UEditorAssetLibrary::DeleteAsset(Path);
	if (!bDeleted) return MCPError(FString::Printf(TEXT("Failed to delete: %s"), *Path));

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("deleted"), true);
	Result->SetBoolField(TEXT("alreadyDeleted"), false);
	Result->SetStringField(TEXT("path"), Path);
	MCPSetNoRollback(Result, TEXT("The profile asset is deleted and no copy is kept; profile_create recreates it"));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieProfileList(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	const FString Directory = OptionalString(Params, TEXT("directory"), TEXT("/Game"));

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	ARM.Get().GetAssetsByClass(UMCPPIEObservationProfile::StaticClass()->GetClassPathName(), Assets, true);

	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FAssetData& A : Assets)
	{
		const FString AP = A.GetObjectPathString();
		if (!AP.StartsWith(Directory)) continue;
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("path"), AP);
		Item->SetStringField(TEXT("name"), A.AssetName.ToString());
		Items.Add(MakeShared<FJsonValueObject>(Item));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("profiles"), Items);
	Result->SetNumberField(TEXT("count"), Items.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieObserveArm(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	int32 PIEInstance = INDEX_NONE;
	FString PIEInstanceError;
	if (!FPIEHandlers::ParsePIEInstance(Params, PIEInstance, PIEInstanceError))
	{
		return MCPError(PIEInstanceError);
	}
	FString ProfilePath;
	if (auto Err = RequireString(Params, TEXT("profile"), ProfilePath)) return Err;

	FObserverArmConfig Cfg;
	Cfg.ProfilePath = ProfilePath;
	Cfg.OutputDir = OptionalString(Params, TEXT("output_dir"));
	Cfg.SampleHz = OptionalInt(Params, TEXT("sample_hz"), 60);
	Cfg.ClientId = OptionalInt(Params, TEXT("client_id"), 0);
	Cfg.PIEInstance = PIEInstance;


	if (HasParam(Params, TEXT("pin_fps")))
	{
		int32 Hz = 60;
		TryGetNumberParam(Params, TEXT("pin_fps"), Hz);
		Cfg.PinFPS = Hz;
	}

	FString Err, Msg;
	if (!FPIEObserver::Get().Arm(Cfg, Err, Msg))
	{
		return MCPError(Err);
	}

	const FObserverStatus S = FPIEObserver::Get().GetStatus();
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("armed"), true);
	Result->SetStringField(TEXT("message"), Msg);
	WriteObserverStatusFields(Result, S);
	MCPSetCreated(Result);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	MCPSetRollback(Result, TEXT("pie_observe_disarm"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieObserveDisarm(const TSharedPtr<FJsonObject>& /*Params*/)
{
	MCP_CHECK_GAME_THREAD();
	FString Err;
	if (!FPIEObserver::Get().Disarm(Err)) return MCPError(Err);
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("disarmed"), true);
	Result->SetBoolField(TEXT("changed"), true);
	MCPSetNoRollback(Result, TEXT("The disarmed observation's settings are not kept; arm it again with observe_arm"));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieObserveStop(const TSharedPtr<FJsonObject>& /*Params*/)
{
	MCP_CHECK_GAME_THREAD();
	const FObserverFinishResult F = FPIEObserver::Get().ForceStop();
	if (!F.bSuccess && !F.Error.IsEmpty()) return MCPError(F.Error);
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("stopped"), true);
	Result->SetStringField(TEXT("run_id"), F.RunId);
	Result->SetStringField(TEXT("output_dir"), F.OutputDir);
	Result->SetNumberField(TEXT("frames_sampled"), F.FramesSampled);
	if (!F.CSVPath.IsEmpty()) Result->SetStringField(TEXT("csv_path"), F.CSVPath);
	if (!F.TrackedActorsPath.IsEmpty()) Result->SetStringField(TEXT("tracked_actors_path"), F.TrackedActorsPath);
	if (!F.TrackedWidgetsPath.IsEmpty()) Result->SetStringField(TEXT("tracked_widgets_path"), F.TrackedWidgetsPath);
	Result->SetBoolField(TEXT("alreadyStopped"), F.RunId.IsEmpty());
	MCPSetNoRollback(Result, TEXT("A finalized observation cannot be resumed; observe_arm starts a new one"));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieObserveStatus(const TSharedPtr<FJsonObject>& /*Params*/)
{
	MCP_CHECK_GAME_THREAD();
	const FObserverStatus S = FPIEObserver::Get().GetStatus();
	auto Result = MCPSuccess();
	WriteObserverStatusFields(Result, S);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieObserveList(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	const FString Root = OptionalString(Params, TEXT("output_dir"),
		FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("MCPObservations"));
	const int32 Limit = OptionalInt(Params, TEXT("limit"), 200);

	TArray<FString> Dirs;
	IFileManager::Get().FindFiles(Dirs, *(Root / TEXT("*")), false, true);

	Dirs.Sort([](const FString& A, const FString& B) { return A > B; });
	if (Dirs.Num() > Limit) Dirs.SetNum(Limit);

	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FString& D : Dirs)
	{
		const FString ManifestPath = Root / D / TEXT("manifest.json");
		if (!FPaths::FileExists(ManifestPath)) continue;

		FString JsonStr;
		if (!FFileHelper::LoadFileToString(JsonStr, *ManifestPath)) continue;

		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
		if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid()) continue;

		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("run_id"), D);
		FString Profile;
		if (Obj->TryGetStringField(TEXT("profile"), Profile))
			Item->SetStringField(TEXT("profile"), Profile);
		int32 Frames = 0;
		if (Obj->TryGetNumberField(TEXT("frames_sampled"), Frames))
			Item->SetNumberField(TEXT("frames_sampled"), Frames);
		FString StartedAt;
		if (Obj->TryGetStringField(TEXT("started_at"), StartedAt))
			Item->SetStringField(TEXT("started_at"), StartedAt);

		Items.Add(MakeShared<FJsonValueObject>(Item));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("observations"), Items);
	Result->SetNumberField(TEXT("count"), Items.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieObserveRead(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	// Each file kind reads its own subset; all of them are taken as read here.
	MCPReadParamsAhead(Params, { TEXT("limit"), TEXT("offset"), TEXT("channels"), TEXT("threshold"), TEXT("include_values"), TEXT("max_points") });
	FString RunId;
	if (auto Err = RequireString(Params, TEXT("run_id"), RunId)) return Err;

	const FString Root = OptionalString(Params, TEXT("output_dir"),
		FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("MCPObservations"));
	const FString File = OptionalString(Params, TEXT("file"), TEXT("manifest"));
	const int32 Limit = OptionalInt(Params, TEXT("limit"), 1000);
	const int32 Offset = OptionalInt(Params, TEXT("offset"), 0);

	const FString RunDir = Root / RunId;

	if (File == TEXT("manifest"))
	{
		FString JsonStr;
		if (!FFileHelper::LoadFileToString(JsonStr, *(RunDir / TEXT("manifest.json"))))
			return MCPError(FString::Printf(TEXT("manifest.json not found in %s"), *RunDir));

		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
		if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
			return MCPError(TEXT("Failed to parse manifest.json"));

		return MCPResult(Obj.ToSharedRef());
	}

	if (File == TEXT("csv"))
	{
		FString CSV;
		if (!FFileHelper::LoadFileToString(CSV, *(RunDir / TEXT("observation.csv"))))
			return MCPError(TEXT("observation.csv not found"));

		TArray<FString> Lines;
		CSV.ParseIntoArrayLines(Lines);
		const int32 Start = FMath::Max(0, Offset);
		const int32 End = FMath::Min(Lines.Num(), Start + Limit);

		TArray<TSharedPtr<FJsonValue>> Rows;
		for (int32 i = Start; i < End; ++i)
		{
			Rows.Add(MakeShared<FJsonValueString>(Lines[i]));
		}
		auto Result = MCPSuccess();
		Result->SetArrayField(TEXT("rows"), Rows);
		Result->SetNumberField(TEXT("total_lines"), Lines.Num());
		Result->SetNumberField(TEXT("offset"), Start);
		Result->SetNumberField(TEXT("count"), Rows.Num());
		return MCPResult(Result);
	}

	if (File == TEXT("tracked") || File == TEXT("widgets"))
	{
		const FString JsonlName = File == TEXT("widgets") ? TEXT("widgets.jsonl") : TEXT("tracked.jsonl");
		FString JSONL;
		if (!FFileHelper::LoadFileToString(JSONL, *(RunDir / JsonlName)))
			return MCPError(FString::Printf(TEXT("%s not found"), *JsonlName));

		TArray<FString> Lines;
		JSONL.ParseIntoArrayLines(Lines);
		const int32 Start = FMath::Max(0, Offset);
		const int32 End = FMath::Min(Lines.Num(), Start + Limit);

		TArray<TSharedPtr<FJsonValue>> Rows;
		for (int32 i = Start; i < End; ++i)
		{
			TSharedPtr<FJsonObject> Row;
			TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Lines[i]);
			if (FJsonSerializer::Deserialize(R, Row) && Row.IsValid())
			{
				Rows.Add(MakeShared<FJsonValueObject>(Row.ToSharedRef()));
			}
		}
		auto Result = MCPSuccess();
		Result->SetArrayField(TEXT("rows"), Rows);
		Result->SetNumberField(TEXT("total_lines"), Lines.Num());
		Result->SetNumberField(TEXT("offset"), Start);
		Result->SetNumberField(TEXT("count"), Rows.Num());
		return MCPResult(Result);
	}

	if (File == TEXT("series"))
	{
		// Item 2b: turn observation.csv into per-channel time series with
		// min/max/first/last and the first frame that crosses a threshold, so an
		// agent can ask "how did channel X change" and get a curve, not one value.
		FString CSV;
		if (!FFileHelper::LoadFileToString(CSV, *(RunDir / TEXT("observation.csv"))))
			return MCPError(TEXT("observation.csv not found"));

		TArray<FString> Lines;
		CSV.ParseIntoArrayLines(Lines);
		int32 HeaderIdx = INDEX_NONE;
		for (int32 i = 0; i < Lines.Num(); ++i)
		{
			if (!Lines[i].StartsWith(TEXT("#"))) { HeaderIdx = i; break; }
		}
		if (HeaderIdx == INDEX_NONE) return MCPError(TEXT("observation.csv has no header"));

		const TArray<FString> Cols = MCPPIE::SplitCSVLine(Lines[HeaderIdx]);
		auto ColIndex = [&Cols](const FString& Name) -> int32
		{
			for (int32 i = 0; i < Cols.Num(); ++i) { if (Cols[i] == Name) return i; }
			return INDEX_NONE;
		};
		const int32 CFrame = ColIndex(TEXT("frame"));
		const int32 CTime = ColIndex(TEXT("time"));

		// Requested channels, or every tracked-value / speed column by default.
		TArray<FString> Channels;
		const FString ChParam = OptionalString(Params, TEXT("channels"));
		if (!ChParam.IsEmpty())
		{
			ChParam.ParseIntoArray(Channels, TEXT(","));
			for (FString& C : Channels) { C.TrimStartAndEndInline(); }
		}
		else
		{
			for (const FString& C : Cols)
			{
				if (C.StartsWith(TEXT("t:")) || C == TEXT("speed2d") ||
					C == TEXT("gpu_ms") || C == TEXT("game_ms"))
				{
					Channels.Add(C);
				}
			}
		}

		const bool bHasThreshold = HasParam(Params, TEXT("threshold"));
		const double Threshold = OptionalNumber(Params, TEXT("threshold"), 0.0);
		const bool bIncludeValues = OptionalBool(Params, TEXT("include_values"), false);
		const int32 MaxPoints = OptionalInt(Params, TEXT("max_points"), 500);

		struct FChan { int32 Col; TArray<double> Vals; TArray<uint64> Frames; };
		TMap<FString, FChan> Series;
		for (const FString& Ch : Channels)
		{
			const int32 Ci = ColIndex(Ch);
			if (Ci != INDEX_NONE) { FChan& S = Series.Add(Ch); S.Col = Ci; }
		}

		for (int32 i = HeaderIdx + 1; i < Lines.Num(); ++i)
		{
			if (Lines[i].IsEmpty() || Lines[i].StartsWith(TEXT("#"))) continue;
			const TArray<FString> F = MCPPIE::SplitCSVLine(Lines[i]);
			const uint64 Fr = (CFrame != INDEX_NONE && CFrame < F.Num())
				? static_cast<uint64>(FCString::Atoi64(*F[CFrame])) : 0;
			for (TPair<FString, FChan>& KV : Series)
			{
				if (KV.Value.Col < F.Num())
				{
					KV.Value.Vals.Add(FCString::Atod(*F[KV.Value.Col]));
					KV.Value.Frames.Add(Fr);
				}
			}
		}

		TArray<TSharedPtr<FJsonValue>> Out;
		for (const TPair<FString, FChan>& KV : Series)
		{
			const FChan& S = KV.Value;
			if (S.Vals.Num() == 0) continue;
			double MinV = S.Vals[0], MaxV = S.Vals[0];
			int64 FirstOverFrame = -1;
			for (int32 i = 0; i < S.Vals.Num(); ++i)
			{
				MinV = FMath::Min(MinV, S.Vals[i]);
				MaxV = FMath::Max(MaxV, S.Vals[i]);
				if (bHasThreshold && FirstOverFrame < 0 && FMath::Abs(S.Vals[i]) > Threshold)
				{
					FirstOverFrame = static_cast<int64>(S.Frames[i]);
				}
			}
			TSharedRef<FJsonObject> CO = MakeShared<FJsonObject>();
			CO->SetStringField(TEXT("channel"), KV.Key);
			CO->SetNumberField(TEXT("count"), S.Vals.Num());
			CO->SetNumberField(TEXT("min"), MinV);
			CO->SetNumberField(TEXT("max"), MaxV);
			CO->SetNumberField(TEXT("first"), S.Vals[0]);
			CO->SetNumberField(TEXT("last"), S.Vals.Last());
			if (bHasThreshold)
			{
				CO->SetNumberField(TEXT("threshold"), Threshold);
				if (FirstOverFrame >= 0) CO->SetNumberField(TEXT("first_frame_over"), static_cast<double>(FirstOverFrame));
			}
			if (bIncludeValues)
			{
				// Even downsample to MaxPoints so the payload stays bounded.
				TArray<TSharedPtr<FJsonValue>> Vs;
				const int32 N = S.Vals.Num();
				const int32 Step = FMath::Max(1, N / FMath::Max(1, MaxPoints));
				for (int32 i = 0; i < N; i += Step) { Vs.Add(MakeShared<FJsonValueNumber>(S.Vals[i])); }
				CO->SetArrayField(TEXT("values"), Vs);
			}
			Out.Add(MakeShared<FJsonValueObject>(CO));
		}

		auto Result = MCPSuccess();
		Result->SetArrayField(TEXT("channels"), Out);
		Result->SetNumberField(TEXT("channel_count"), Out.Num());
		return MCPResult(Result);
	}

	return MCPError(FString::Printf(TEXT("Unknown file type: %s (use manifest, csv, tracked, widgets, or series)"), *File));
}
