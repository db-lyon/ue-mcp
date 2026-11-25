// Playtest clip handlers (#2097): create, read, edit and check clips, play them, record into them. Members of FPIEHandlers.

#include "Handlers/PIE/PIEHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include "HandlerAssetDelete.h"
#include "HandlerPackageSave.h"
#include "HandlerWorld.h"
#include "Handlers/PIE/MCPPIEPlaytestClip.h"
#include "Handlers/PIE/MCPPIEObservationProfile.h"
#include "Handlers/PIE/PIEClip.h"
#include "Handlers/PIE/PIEClipPlayer.h"
#include "Handlers/PIE/PIEScriptRunner.h"
#include "Handlers/PIE/PIEScriptValidation.h"
#include "Handlers/PIE/PIESequenceFormat.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "InputAction.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	using namespace MCPPIE;

	const TCHAR* const ClipDefaultPackage = TEXT("/Game/PIEStudio/Clips");

	UMCPPIEPlaytestClip* LoadClipAsset(const FString& Path)
	{
		UMCPPIEPlaytestClip* Asset = LoadObject<UMCPPIEPlaytestClip>(nullptr, *Path);
		if (!Asset && !Path.Contains(TEXT(".")))
		{
			Asset = LoadObject<UMCPPIEPlaytestClip>(nullptr, *(Path + TEXT(".") + FPackageName::GetShortName(Path)));
		}
		return Asset;
	}

	FString ClipJsonString(const FClip& Clip)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(ClipToJson(Clip), W);
		return Out;
	}

	bool ClipFromAsset(const UMCPPIEPlaytestClip* Asset, FClip& Out, TArray<FClipIssue>& Issues)
	{
		const TSharedPtr<FJsonObject> Obj = [&Asset]() -> TSharedPtr<FJsonObject>
		{
			TSharedPtr<FJsonObject> O;
			TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Asset->ClipJson);
			FJsonSerializer::Deserialize(R, O);
			return O;
		}();
		if (!Obj.IsValid())
		{
			Out = FClip();
			Out.Description = Asset->Description;
			Out.Map = Asset->Map;
			return Asset->ClipJson.TrimStartAndEnd().IsEmpty();
		}
		return ClipFromJson(Obj, Out, Issues);
	}

	void ClipToAsset(const FClip& Clip, UMCPPIEPlaytestClip* Asset)
	{
		Asset->Description = Clip.Description;
		Asset->Map = Clip.Map;
		Asset->ClipJson = ClipJsonString(Clip);
	}

	// The clip a call gives: clip (the JSON object) or text (the compact form), exactly one. Parse problems go to Issues.
	TSharedPtr<FJsonValue> ReadClipParam(const TSharedPtr<FJsonObject>& Params, FClip& Out, TArray<FClipIssue>& Issues, bool bRequired)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		const bool bHasClip = TryGetObjectParam(Params, TEXT("clip"), Obj) && Obj;
		FString Text;
		const bool bHasText = TryGetStringParam(Params, TEXT("text"), Text);
		if (bHasClip && bHasText) return MCPError(TEXT("Give clip (JSON) or text (the compact form), not both"));
		if (!bHasClip && !bHasText)
		{
			return bRequired ? MCPError(TEXT("Give the clip as clip (JSON) or text (the compact form)")) : nullptr;
		}
		if (bHasClip) ClipFromJson(*Obj, Out, Issues);
		else ClipFromText(Text, Out, Issues);
		return nullptr;
	}

	// Every check clip_create, clip_update, clip_edit and clip_validate run: the structure, then what the clip names
	// against the level open in the editor and the clip's profile.
	void CheckClip(const FClip& Clip, TArray<FClipIssue>& Issues)
	{
		ValidateClipStructure(Clip, Issues);
		if (!Clip.Map.IsEmpty())
		{
			const FString MapError = MCPCheckMapExists(Clip.Map);
			if (!MapError.IsEmpty())
			{
				FClipIssue I;
				I.Field = TEXT("map");
				I.Message = FString::Printf(TEXT("Map not found: %s is not a level asset"), *MCPMapPackageName(Clip.Map));
				I.bBlocksSave = true;
				Issues.Add(I);
				return;
			}
		}
		if (!Clip.Profile.IsEmpty() && !LoadObject<UMCPPIEObservationProfile>(nullptr, *Clip.Profile))
		{
			FClipIssue I;
			I.Field = TEXT("profile");
			I.Message = FString::Printf(TEXT("Profile not found: %s"), *Clip.Profile);
			Issues.Add(I);
		}
		FScriptValidationContext Ctx = MakeEditorScriptValidationContext(LoadObject<UMCPPIEObservationProfile>(nullptr, *Clip.Profile) ? Clip.Profile : FString());
		const UWorld* Open = GetEditorWorld();
		if (!Clip.Map.IsEmpty() && (!Open || Open->GetOutermost()->GetName() != MCPMapPackageName(Clip.Map)))
		{
			// Actors live in the clip's level, which is not the one open, so they resolve when the run opens it.
			Ctx.ResolveActor = [](const FString&) { FScriptLookup L; L.bFound = true; return L; };
		}
		auto ActionType = [](const FString& Action, EClipValueType& OutType)
		{
			const UInputAction* A = LoadObject<UInputAction>(nullptr, *Action);
			if (!A) return false;
			switch (A->ValueType)
			{
			case EInputActionValueType::Axis1D: OutType = EClipValueType::Axis1D; break;
			case EInputActionValueType::Axis2D: OutType = EClipValueType::Axis2D; break;
			case EInputActionValueType::Axis3D: OutType = EClipValueType::Axis3D; break;
			default: OutType = EClipValueType::Bool; break;
			}
			return true;
		};
		ValidateClipReferences(Clip, Ctx, ActionType, Issues);
	}

	TArray<FClipIssue> BlockingClipIssues(const TArray<FClipIssue>& Issues, TArray<FClipIssue>& OutWarnings)
	{
		TArray<FClipIssue> Blocking;
		for (const FClipIssue& I : Issues) (I.bBlocksSave ? Blocking : OutWarnings).Add(I);
		return Blocking;
	}

	TSharedPtr<FJsonValue> ClipRefusal(const TCHAR* Lead, const TArray<FClipIssue>& Blocking, const TArray<FClipIssue>& Warnings)
	{
		TArray<FString> Messages;
		for (const FClipIssue& I : Blocking) Messages.Add(I.Message);
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("success"), false);
		O->SetStringField(TEXT("errorCode"), TEXT("INVALID_CLIP"));
		O->SetStringField(TEXT("error"), FString::Printf(TEXT("%s (%d problem%s): %s"), Lead, Blocking.Num(), Blocking.Num() == 1 ? TEXT("") : TEXT("s"), *FString::Join(Messages, TEXT("; "))));
		O->SetArrayField(TEXT("errors"), ClipIssuesToJson(Blocking));
		O->SetArrayField(TEXT("warnings"), ClipIssuesToJson(Warnings));
		return MakeShared<FJsonValueObject>(O);
	}

	void ClipFieldsToJson(const FClip& Clip, const FString& Path, const FString& Name, TSharedPtr<FJsonObject> R)
	{
		if (!Path.IsEmpty()) R->SetStringField(TEXT("path"), Path);
		if (!Name.IsEmpty()) R->SetStringField(TEXT("name"), Name);
		R->SetObjectField(TEXT("clip"), ClipToJson(Clip));
		R->SetStringField(TEXT("text"), ClipToText(Clip));
		int32 Notes = 0, Keys = 0;
		for (const FClipTrack& T : Clip.Tracks) { Notes += T.Notes.Num(); Keys += T.Keys.Num(); }
		TSharedRef<FJsonObject> Counts = MakeShared<FJsonObject>();
		Counts->SetNumberField(TEXT("tracks"), Clip.Tracks.Num());
		Counts->SetNumberField(TEXT("notes"), Notes);
		Counts->SetNumberField(TEXT("keys"), Keys);
		Counts->SetNumberField(TEXT("events"), Clip.Events.Num());
		Counts->SetNumberField(TEXT("checks"), Clip.Checks.Num());
		Counts->SetNumberField(TEXT("regions"), Clip.Regions.Num());
		Counts->SetNumberField(TEXT("takes"), Clip.Takes.Num());
		Counts->SetNumberField(TEXT("length"), Clip.EffectiveLength());
		R->SetObjectField(TEXT("counts"), Counts);
	}

	TSharedPtr<FJsonObject> ClipUpdatePayload(const FClip& Clip, const FString& Path)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("path"), Path);
		P->SetObjectField(TEXT("clip"), ClipToJson(Clip));
		return P;
	}

	bool ReadClipRunTarget(const TSharedPtr<FJsonObject>& Params, FClipPlayConfig& Cfg, FString& OutError)
	{
		int32 PIEInstance = INDEX_NONE;
		if (!FPIEHandlers::ParsePIEInstance(Params, PIEInstance, OutError)) return false;
		Cfg.PIEInstance = PIEInstance;
		Cfg.ClientId = OptionalInt(Params, TEXT("client_id"), 0);
		if (Cfg.ClientId < 0)
		{
			OutError = TEXT("client_id must be 0 or more");
			return false;
		}
		return true;
	}

	TSharedPtr<FJsonObject> ClipRunAnswer(const FString& Message, bool bStartedPIE)
	{
		const FClipRun* Run = FPIEClipPlayer::Get().GetRun();
		auto Result = MCPSuccess();
		if (Run)
		{
			// Held in a local: iterating the temporary's map would outlive the object.
			const TSharedRef<FJsonObject> Fields = FPIEClipPlayer::Get().RunToJson(true);
			for (const auto& F : Fields->Values) Result->SetField(F.Key, F.Value);
		}
		Result->SetBoolField(TEXT("pie_started"), bStartedPIE);
		Result->SetStringField(TEXT("message"), Message);
		return Result;
	}

	// A run that started: clip_stop ends it.
	TSharedPtr<FJsonValue> ClipRunStarted(const FString& Message, bool bStartedPIE)
	{
		TSharedPtr<FJsonObject> Result = ClipRunAnswer(Message, bStartedPIE);
		MCPSetCreated(Result);
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("reason"), TEXT("rolled back"));
		MCPSetRollback(Result, TEXT("pie_clip_stop"), Payload);
		return MCPResult(Result);
	}
}

void FPIEHandlers::RegisterClipHandlers(FMCPHandlerRegistry& Registry)
{
	using EType = EMCPParamType;
	const TCHAR* const PathDoc = TEXT("Playtest clip asset path");
	const TCHAR* const ClipDoc = TEXT("The clip as JSON: {rate (frames per second, default 60), length (frames, 0 ends at the last thing on it), grid (frame | 1/N | free), map, profile, description, fixed_timestep (default true), seed, hold_timeout (frames), regions: [{id?, name, start, end}], tracks: [{action (Input Action path), type: bool|axis1d|axis2d|axis3d, notes: [{id?, start, length, value? (axis tracks)}], keys: [{id?, t, v, interp?: linear|constant|cubic}] (axis tracks)}], events: [{id?, at, do (an act entry: place, console, capture, key, press, hold, tape, inject; or an arrange entry: {kind: spawn|set|call, ...})}], checks: [{id?, name?, start, length, mode: by_end|throughout|hold, expect: {value, op, target?} | manual: true with instruction}]}. Times are frames at rate. A track holds the raw value that reaches the Input Action, before its own modifiers and triggers");
	const TCHAR* const TextDoc = TEXT("The clip in its compact text form, one thing per line: clip rate=60 length=600 | region <id> <start> <end> \"name\" | track <action> <type> followed by indented note <id> <start> <length> [value] and key <id> <t> <value> [interp] lines | event <id> <at> {json} | check <id> <start> <length> <by_end|throughout|hold> <value> <op> [target] | check <id> <start> <length> manual \"question\". Use - for an id to have one assigned");
	const TCHAR* const RunIdDoc = TEXT("Clip run id (default the current or last run)");

	Registry.RegisterHandler(TEXT("pie_clip_create"), &PieClipCreate, {
		MCPParam::Required(TEXT("name"), EType::String, TEXT("Asset name")),
		MCPParam::Optional(TEXT("package_path"), EType::String, TEXT("Package folder (default /Game/PIEStudio/Clips)")),
		MCPParam::Optional(TEXT("clip"), EType::Object, ClipDoc),
		MCPParam::Optional(TEXT("text"), EType::String, TextDoc),
		MCPSharedParam::OnConflict().Describe(TEXT("When the asset exists: skip (default) returns it untouched, replace overwrites it, error refuses")).Enum({
			TEXT("skip"), TEXT("replace"), TEXT("error") }),
	}, MCPSpec::ExactlyOne({ { TEXT("clip") }, { TEXT("text") } }).ContractExempt(TEXT("Creates and saves a clip asset")));
	Registry.RegisterHandler(TEXT("pie_clip_read"), &PieClipRead, {
		MCPParam::Required(TEXT("path"), EType::String, PathDoc),
	});
	Registry.RegisterHandler(TEXT("pie_clip_update"), &PieClipUpdate, {
		MCPParam::Required(TEXT("path"), EType::String, PathDoc),
		MCPParam::Optional(TEXT("clip"), EType::Object, TEXT("The whole clip as JSON, replacing the stored one")),
		MCPParam::Optional(TEXT("text"), EType::String, TEXT("The whole clip in its compact text form, replacing the stored one")),
	}, MCPSpec::ExactlyOne({ { TEXT("clip") }, { TEXT("text") } }));
	Registry.RegisterHandler(TEXT("pie_clip_edit"), &PieClipEdit, {
		MCPParam::Required(TEXT("path"), EType::String, PathDoc),
		MCPParam::Required(TEXT("ops"), EType::Array, TEXT("Edits applied in order, all or none: {op: add, kind: note|key|track|check|event|region, ...the thing's fields (track names the Input Action a note or key goes on)} | {op: move, id, to | by} | {op: trim, id, start?, length? | end?} | {op: set, id, ...fields} | {op: delete, id} (a track by its action) | {op: quantize, grid?, track? | ids?} | {op: set_clip, ...clip fields}. Times snap to the clip's grid")).Items(EType::Object),
	});
	Registry.RegisterHandler(TEXT("pie_clip_validate"), &PieClipValidate, {
		MCPParam::Optional(TEXT("path"), EType::String, TEXT("A stored clip to check")),
		MCPParam::Optional(TEXT("clip"), EType::Object, ClipDoc),
		MCPParam::Optional(TEXT("text"), EType::String, TextDoc),
	}, MCPSpec::ExactlyOne({ { TEXT("path") }, { TEXT("clip") }, { TEXT("text") } }));
	Registry.RegisterHandler(TEXT("pie_clip_list"), &PieClipList, {
		MCPParam::Optional(TEXT("directory"), EType::String, TEXT("Content folder to list under (default /Game)")),
	});
	Registry.RegisterHandler(TEXT("pie_clip_delete"), &PieClipDelete, {
		MCPParam::Required(TEXT("path"), EType::String, PathDoc),
		MCPParam::Optional(TEXT("confirm"), EType::Boolean, TEXT("Must be true to delete")),
	});
	Registry.RegisterHandler(TEXT("pie_clip_run"), &PieClipRun, {
		MCPParam::Optional(TEXT("path"), EType::String, PathDoc),
		MCPParam::Optional(TEXT("clip"), EType::Object, TEXT("An inline clip instead of an asset, as clip_create takes it")),
		MCPParam::Optional(TEXT("text"), EType::String, TEXT("An inline clip in its compact text form")),
		MCPParam::Optional(TEXT("name"), EType::String, TEXT("The run's name (default the asset name, or inline)")),
		MCPParam::Optional(TEXT("region"), EType::String, TEXT("Region id: judge only its checks and stop at its end. Playback still starts at frame 0, since the state at a region's start comes from playing what comes before it")),
		MCPParam::Optional(TEXT("start_pie"), EType::Boolean, TEXT("Start PIE when it is not running (default false). The run ends PIE it started when it ends")),
		MCPParam::Optional(TEXT("keep_pie"), EType::Boolean, TEXT("Leave PIE the run started running when the run ends (default false)")),
		MCPParam::Optional(TEXT("fixed_timestep"), EType::Boolean, TEXT("Fixed frame deltas: to the next recorded frame where the clip has frame_times, else 1/rate (default the clip's fixed_timestep, true unless set)")),
		MCPParam::Optional(TEXT("lock_input"), EType::Boolean, TEXT("Keep the person's keyboard, mouse and gamepad out of PIE while the clip plays (default true)")),
		MCPParam::Optional(TEXT("pie_instance"), EType::Integer, TEXT("FWorldContext PIEInstance whose player the clip drives and observes (default the editor's own player)")).Min(0),
		MCPParam::Optional(TEXT("client_id"), EType::Integer, TEXT("Local-player index inside that PIE world (default 0)")).Min(0),
		MCPParam::Optional(TEXT("wait"), EType::Boolean, TEXT("Answer once the run finishes, stops or awaits a verdict (default false)")),
		MCPParam::Optional(TEXT("timeout_seconds"), EType::Number, TEXT("Most seconds wait holds the answer (default 600)")).Min(0),
	}, MCPSpec::ExactlyOne({ { TEXT("path") }, { TEXT("clip") }, { TEXT("text") } }).ContractExempt(TEXT("Arms the observer and can start PIE")));
	Registry.RegisterHandler(TEXT("pie_clip_record"), &PieClipRecord, {
		MCPParam::Required(TEXT("path"), EType::String, TEXT("Playtest clip asset path the take goes into; create an empty clip first to record a new one")),
		MCPParam::Optional(TEXT("region"), EType::String, TEXT("Punch in at this region's start and out at its end. The clip plays up to the punch-in with the person's input locked out")),
		MCPParam::Optional(TEXT("from_check"), EType::String, TEXT("Punch in at this hold check's frame instead, and record until clip_record_stop")),
		MCPParam::Optional(TEXT("armed"), EType::Array, TEXT("Input Actions the take records (default every action the player has a mapping for). The take replaces these tracks over the range, and keeps what they held there as history")).Items(EType::String),
		MCPParam::Optional(TEXT("pie_instance"), EType::Integer, TEXT("FWorldContext PIEInstance of the player being recorded (default the editor's own player)")).Min(0),
		MCPParam::Optional(TEXT("client_id"), EType::Integer, TEXT("Local-player index inside that PIE world (default 0)")).Min(0),
	}, MCPSpec::ContractExempt(TEXT("Starts PIE and records the person's input into the clip")));
	Registry.RegisterHandler(TEXT("pie_clip_record_stop"), &PieClipRecordStop, {});
	Registry.RegisterHandler(TEXT("pie_clip_status"), &PieClipStatus, {
		MCPParam::Optional(TEXT("run_id"), EType::String, RunIdDoc),
		MCPParam::Optional(TEXT("wait"), EType::Boolean, TEXT("Answer once the run finishes, stops or awaits a verdict")),
		MCPParam::Optional(TEXT("timeout_seconds"), EType::Number, TEXT("Most seconds wait holds the answer (default 600)")).Min(0),
	});
	Registry.RegisterHandler(TEXT("pie_clip_stop"), &PieClipStop, {
		MCPParam::Optional(TEXT("reason"), EType::String, TEXT("Recorded as the run's stop reason")),
	});
	Registry.RegisterHandler(TEXT("pie_clip_verdict"), &PieClipVerdict, {
		MCPParam::Required(TEXT("check"), EType::String, TEXT("Id of the manual check awaiting its verdict")),
		MCPParam::Required(TEXT("verdict"), EType::String, TEXT("Verdict")).Enum({ TEXT("pass"), TEXT("fail"), TEXT("skip") }),
		MCPParam::Optional(TEXT("note"), EType::String, TEXT("Tester's note")),
		MCPParam::Optional(TEXT("source"), EType::String, TEXT("Who judged it: human (default) or agent, which needs evidence and is refused on a check with agent_judgeable false")).Enum({
			TEXT("human"), TEXT("agent") }),
		MCPParam::Optional(TEXT("evidence"), EType::Object, TEXT("What the verdict rests on; required when source is agent")).WithFields({
			MCPParam::RequiredField(TEXT("note"), EType::String, TEXT("What was observed")),
			MCPParam::OptionalField(TEXT("captures"), EType::Array, TEXT("Capture files that show it: absolute, or relative to the run's output_dir; each must exist")).Items(EType::String),
		}),
	});
}

TSharedPtr<FJsonValue> FPIEHandlers::PieClipCreate(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Name;
	TSharedPtr<FJsonValue> NameErr = RequireString(Params, TEXT("name"), Name);
	const FString PackagePath = OptionalString(Params, TEXT("package_path"), ClipDefaultPackage);
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip")).ToLower();
	FClip Clip;
	TArray<FClipIssue> Issues;
	TSharedPtr<FJsonValue> ClipErr = ReadClipParam(Params, Clip, Issues, true);
	if (NameErr) return NameErr;
	if (ClipErr) return ClipErr;
	if (OnConflict != TEXT("skip") && OnConflict != TEXT("replace") && OnConflict != TEXT("error"))
	{
		return MCPError(FString::Printf(TEXT("onConflict must be skip, replace or error, got '%s'"), *OnConflict));
	}
	CheckClip(Clip, Issues);
	TArray<FClipIssue> Warnings;
	const TArray<FClipIssue> Blocking = BlockingClipIssues(Issues, Warnings);
	if (Blocking.Num() > 0) return ClipRefusal(TEXT("The clip cannot be saved as written"), Blocking, Warnings);

	const FString AssetPath = PackagePath / Name;
	if (OnConflict == TEXT("replace"))
	{
		if (UMCPPIEPlaytestClip* Existing = LoadClipAsset(AssetPath))
		{
			FClip Old;
			TArray<FClipIssue> Ignored;
			ClipFromAsset(Existing, Old, Ignored);
			ClipToAsset(Clip, Existing);
			if (!SaveAssetPackage(Existing)) return MCPError(FString::Printf(TEXT("Failed to save %s"), *AssetPath));
			auto Result = MCPSuccess();
			MCPSetUpdated(Result);
			MCPSetRollback(Result, TEXT("pie_clip_update"), ClipUpdatePayload(Old, AssetPath));
			ClipFieldsToJson(Clip, AssetPath, Name, Result);
			Result->SetArrayField(TEXT("warnings"), ClipIssuesToJson(Warnings));
			return MCPResult(Result);
		}
	}

	auto Create = MCPCreateAssetIdempotentNewObject<UMCPPIEPlaytestClip>(Name, PackagePath, OnConflict == TEXT("error") ? OnConflict : FString(TEXT("skip")), TEXT("PlaytestClip"));
	if (Create.EarlyReturn) return Create.EarlyReturn;
	ClipToAsset(Clip, Create.Asset);
	FString SaveError;
	if (!SaveAssetPackageChecked(Create.Asset, SaveError)) return MCPError(FString::Printf(TEXT("Saving the clip failed: %s"), *SaveError));

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	ClipFieldsToJson(Clip, AssetPath, Name, Result);
	Result->SetArrayField(TEXT("warnings"), ClipIssuesToJson(Warnings));
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("path"), AssetPath);
	Payload->SetBoolField(TEXT("confirm"), true);
	MCPSetRollback(Result, TEXT("pie_clip_delete"), Payload);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieClipRead(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Path;
	if (auto Err = RequireString(Params, TEXT("path"), Path)) return Err;
	UMCPPIEPlaytestClip* Asset = LoadClipAsset(Path);
	if (!Asset) return MCPError(FString::Printf(TEXT("Playtest clip not found: %s"), *Path));
	FClip Clip;
	TArray<FClipIssue> Issues;
	ClipFromAsset(Asset, Clip, Issues);
	auto Result = MCPSuccess();
	ClipFieldsToJson(Clip, Path, Asset->GetName(), Result);
	if (Issues.Num() > 0) Result->SetArrayField(TEXT("warnings"), ClipIssuesToJson(Issues));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieClipUpdate(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Path;
	TSharedPtr<FJsonValue> PathErr = RequireString(Params, TEXT("path"), Path);
	FClip Clip;
	TArray<FClipIssue> Issues;
	TSharedPtr<FJsonValue> ClipErr = ReadClipParam(Params, Clip, Issues, true);
	if (PathErr) return PathErr;
	if (ClipErr) return ClipErr;
	UMCPPIEPlaytestClip* Asset = LoadClipAsset(Path);
	if (!Asset) return MCPError(FString::Printf(TEXT("Playtest clip not found: %s"), *Path));
	CheckClip(Clip, Issues);
	TArray<FClipIssue> Warnings;
	const TArray<FClipIssue> Blocking = BlockingClipIssues(Issues, Warnings);
	if (Blocking.Num() > 0) return ClipRefusal(TEXT("The clip cannot be saved as written"), Blocking, Warnings);

	FClip Old;
	TArray<FClipIssue> Ignored;
	ClipFromAsset(Asset, Old, Ignored);
	// Takes are history the clip keeps; an update that does not carry them leaves them as they were.
	if (Clip.Takes.Num() == 0) Clip.Takes = Old.Takes;
	auto Result = MCPSuccess();
	if (ClipJsonString(Old) == ClipJsonString(Clip))
	{
		Result->SetBoolField(TEXT("unchanged"), true);
	}
	else
	{
		ClipToAsset(Clip, Asset);
		if (!SaveAssetPackage(Asset)) return MCPError(FString::Printf(TEXT("Failed to save %s"), *Path));
		MCPSetUpdated(Result);
		MCPSetRollback(Result, TEXT("pie_clip_update"), ClipUpdatePayload(Old, Path));
	}
	ClipFieldsToJson(Clip, Path, Asset->GetName(), Result);
	Result->SetArrayField(TEXT("warnings"), ClipIssuesToJson(Warnings));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieClipEdit(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Path;
	TSharedPtr<FJsonValue> PathErr = RequireString(Params, TEXT("path"), Path);
	const TArray<TSharedPtr<FJsonValue>>* Ops = nullptr;
	TSharedPtr<FJsonValue> OpsErr = RequireArray(Params, TEXT("ops"), Ops);
	if (PathErr) return PathErr;
	if (OpsErr) return OpsErr;
	UMCPPIEPlaytestClip* Asset = LoadClipAsset(Path);
	if (!Asset) return MCPError(FString::Printf(TEXT("Playtest clip not found: %s"), *Path));
	FClip Clip;
	TArray<FClipIssue> Ignored;
	ClipFromAsset(Asset, Clip, Ignored);
	const FClip Old = Clip;

	TArray<FString> Changed;
	TArray<FClipIssue> EditIssues;
	if (!ApplyClipEdits(Clip, *Ops, Changed, EditIssues))
	{
		return ClipRefusal(TEXT("The edit was not applied"), EditIssues, TArray<FClipIssue>());
	}
	TArray<FClipIssue> Issues;
	CheckClip(Clip, Issues);
	TArray<FClipIssue> Warnings;
	const TArray<FClipIssue> Blocking = BlockingClipIssues(Issues, Warnings);
	if (Blocking.Num() > 0) return ClipRefusal(TEXT("The edit was not applied"), Blocking, Warnings);

	ClipToAsset(Clip, Asset);
	if (!SaveAssetPackage(Asset)) return MCPError(FString::Printf(TEXT("Failed to save %s"), *Path));
	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	MCPSetRollback(Result, TEXT("pie_clip_update"), ClipUpdatePayload(Old, Path));
	TArray<TSharedPtr<FJsonValue>> ChangedJson;
	for (const FString& Id : Changed) ChangedJson.Add(MakeShared<FJsonValueString>(Id));
	Result->SetArrayField(TEXT("changed"), ChangedJson);
	ClipFieldsToJson(Clip, Path, Asset->GetName(), Result);
	Result->SetArrayField(TEXT("warnings"), ClipIssuesToJson(Warnings));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieClipValidate(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FClip Clip;
	TArray<FClipIssue> Issues;
	const FString Path = OptionalString(Params, TEXT("path"));
	if (!Path.IsEmpty())
	{
		UMCPPIEPlaytestClip* Asset = LoadClipAsset(Path);
		if (!Asset) return MCPError(FString::Printf(TEXT("Playtest clip not found: %s"), *Path));
		ClipFromAsset(Asset, Clip, Issues);
	}
	else if (TSharedPtr<FJsonValue> Err = ReadClipParam(Params, Clip, Issues, true))
	{
		return Err;
	}
	CheckClip(Clip, Issues);
	// Every finding is reported, so a caller can draft in a loop.
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("valid"), Issues.Num() == 0);
	Result->SetBoolField(TEXT("saveable"), !ClipIssuesBlockSave(Issues));
	Result->SetArrayField(TEXT("errors"), ClipIssuesToJson(Issues));
	ClipFieldsToJson(Clip, Path, FString(), Result);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieClipList(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	const FString Directory = OptionalString(Params, TEXT("directory"), TEXT("/Game"));
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	ARM.Get().GetAssetsByClass(UMCPPIEPlaytestClip::StaticClass()->GetClassPathName(), Assets, true);
	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FAssetData& A : Assets)
	{
		const FString PackageName = A.PackageName.ToString();
		if (!PackageName.StartsWith(Directory)) continue;
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("path"), PackageName);
		Item->SetStringField(TEXT("object_path"), A.GetObjectPathString());
		Item->SetStringField(TEXT("name"), A.AssetName.ToString());
		FString Map;
		A.GetTagValue(GET_MEMBER_NAME_CHECKED(UMCPPIEPlaytestClip, Map), Map);
		Item->SetStringField(TEXT("map"), Map);
		Items.Add(MakeShared<FJsonValueObject>(Item));
	}
	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("clips"), Items);
	Result->SetNumberField(TEXT("count"), Items.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieClipDelete(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Path;
	TSharedPtr<FJsonValue> PathErr = RequireString(Params, TEXT("path"), Path);
	const bool bConfirm = OptionalBool(Params, TEXT("confirm"), false);
	if (PathErr) return PathErr;
	if (!bConfirm) return MCPError(TEXT("confirm=true required to delete a playtest clip"));
	if (MCPIsProtectedAssetPath(Path)) return MCPError(FString::Printf(TEXT("Refusing to delete a protected path: %s. This cannot be overridden."), *Path));
	if (MCPIsPlayInEditorActive()) return MCPDeleteDuringPlayError();
	if (!UEditorAssetLibrary::DoesAssetExist(Path))
	{
		auto Result = MCPSuccess();
		Result->SetBoolField(TEXT("already_deleted"), true);
		Result->SetBoolField(TEXT("changed"), false);
		Result->SetStringField(TEXT("path"), Path);
		return MCPResult(Result);
	}
	UMCPPIEPlaytestClip* Asset = LoadClipAsset(Path);
	if (!Asset) return MCPError(FString::Printf(TEXT("%s is not a playtest clip"), *Path));
	FClip Clip;
	TArray<FClipIssue> Ignored;
	ClipFromAsset(Asset, Clip, Ignored);
	TSharedPtr<FJsonObject> Recreate = MakeShared<FJsonObject>();
	Recreate->SetStringField(TEXT("name"), Asset->GetName());
	Recreate->SetStringField(TEXT("package_path"), FPackageName::GetLongPackagePath(Asset->GetOutermost()->GetName()));
	Recreate->SetObjectField(TEXT("clip"), ClipToJson(Clip));
	Asset = nullptr;

	bool bRemovedOrphanFile = false;
	if (!MCPDeleteAssetFromDisk(Path, bRemovedOrphanFile))
	{
		TSharedPtr<FJsonObject> Failed = MakeShared<FJsonObject>();
		Failed->SetBoolField(TEXT("success"), false);
		Failed->SetBoolField(TEXT("deleted"), false);
		Failed->SetBoolField(TEXT("changed"), false);
		Failed->SetStringField(TEXT("path"), Path);
		Failed->SetStringField(TEXT("error"), FString::Printf(TEXT("'%s' was not deleted; the clip is still at this path. reason and the fields beside it say why."), *Path));
		MCPApplyDeleteDiagnosticsToJson(Failed, MCPDiagnoseDeleteFailure(Path));
		return MCPResult(Failed);
	}
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("deleted"), true);
	Result->SetBoolField(TEXT("changed"), true);
	Result->SetStringField(TEXT("path"), Path);
	MCPSetRollback(Result, TEXT("pie_clip_create"), Recreate);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieClipRun(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	MCPReadParamsAhead(Params, { TEXT("wait"), TEXT("timeout_seconds") });
	FClipPlayConfig Cfg;
	FString TargetErr;
	if (!ReadClipRunTarget(Params, Cfg, TargetErr)) return MCPError(TargetErr);
	const FString Path = OptionalString(Params, TEXT("path"));
	TArray<FClipIssue> Issues;
	if (!Path.IsEmpty())
	{
		UMCPPIEPlaytestClip* Asset = LoadClipAsset(Path);
		if (!Asset) return MCPError(FString::Printf(TEXT("Playtest clip not found: %s"), *Path));
		ClipFromAsset(Asset, Cfg.Clip, Issues);
		Cfg.ClipPath = Path;
		Cfg.Name = Asset->GetName();
	}
	else if (TSharedPtr<FJsonValue> Err = ReadClipParam(Params, Cfg.Clip, Issues, true))
	{
		return Err;
	}
	ValidateClipStructure(Cfg.Clip, Issues);
	TArray<FClipIssue> Warnings;
	const TArray<FClipIssue> Blocking = BlockingClipIssues(Issues, Warnings);
	if (Blocking.Num() > 0) return ClipRefusal(TEXT("The clip cannot play as written"), Blocking, Warnings);
	if (FPIEScriptRunner::Get().IsActive()) return MCPError(TEXT("A script run is active and would feed input too; pie_script_stop it first"));

	const FString Name = OptionalString(Params, TEXT("name"));
	if (!Name.IsEmpty()) Cfg.Name = Name;
	Cfg.RegionId = OptionalString(Params, TEXT("region"));
	Cfg.bStartPIE = OptionalBool(Params, TEXT("start_pie"), false);
	Cfg.bKeepPIE = OptionalBool(Params, TEXT("keep_pie"), false);
	bool bFixed = true;
	if (TryGetBoolParam(Params, TEXT("fixed_timestep"), bFixed)) Cfg.FixedTimestep = bFixed ? 1 : 0;
	Cfg.bLockInput = OptionalBool(Params, TEXT("lock_input"), true);
	FString Err, Msg;
	if (!FPIEClipPlayer::Get().Start(Cfg, Err, Msg)) return MCPError(Err);
	const FClipRun* Run = FPIEClipPlayer::Get().GetRun();
	return ClipRunStarted(Msg, Run && Run->bStartedPIE);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieClipRecord(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FClipPlayConfig Cfg;
	FString TargetErr;
	if (!ReadClipRunTarget(Params, Cfg, TargetErr)) return MCPError(TargetErr);
	FString Path;
	if (auto Err = RequireString(Params, TEXT("path"), Path)) return Err;
	UMCPPIEPlaytestClip* Asset = LoadClipAsset(Path);
	if (!Asset) return MCPError(FString::Printf(TEXT("Playtest clip not found: %s. Create an empty clip with clip_create to record a new one."), *Path));
	TArray<FClipIssue> Issues;
	ClipFromAsset(Asset, Cfg.Clip, Issues);
	Cfg.ClipPath = Path;
	Cfg.Name = Asset->GetName();
	Cfg.bRecord = true;
	if (FPIEScriptRunner::Get().IsActive()) return MCPError(TEXT("A script run is active; pie_script_stop it first"));

	const FString Region = OptionalString(Params, TEXT("region"));
	const FString FromCheck = OptionalString(Params, TEXT("from_check"));
	if (!Region.IsEmpty() && !FromCheck.IsEmpty()) return MCPError(TEXT("Punch in at a region or at a hold check, not both"));
	if (!Region.IsEmpty())
	{
		const FClipRegion* R = Cfg.Clip.Regions.FindByPredicate([&Region](const FClipRegion& X) { return X.Id == Region; });
		if (!R)
		{
			TArray<FString> Ids;
			for (const FClipRegion& X : Cfg.Clip.Regions) Ids.Add(X.Id);
			return MCPError(FString::Printf(TEXT("The clip has no region '%s' (regions: %s)"), *Region, Ids.Num() ? *FString::Join(Ids, TEXT(", ")) : TEXT("none")));
		}
		Cfg.PunchIn = R->Start;
		Cfg.PunchOut = R->End;
	}
	else if (!FromCheck.IsEmpty())
	{
		// Punch-in starts at a region or a hold check: input replayed as pre-roll does not reproduce physics, AI or
		// random state, so mid-region punch-in is not offered.
		const FClipCheck* C = Cfg.Clip.Checks.FindByPredicate([&FromCheck](const FClipCheck& X) { return X.Id == FromCheck; });
		if (!C || C->Mode != EClipCheckMode::Hold || C->bManual)
		{
			TArray<FString> Ids;
			for (const FClipCheck& X : Cfg.Clip.Checks) if (X.Mode == EClipCheckMode::Hold && !X.bManual) Ids.Add(X.Id);
			return MCPError(FString::Printf(TEXT("'%s' is not a hold check on the clip; punch-in starts at a region or a hold check (hold checks: %s)"),
				*FromCheck, Ids.Num() ? *FString::Join(Ids, TEXT(", ")) : TEXT("none")));
		}
		Cfg.PunchIn = C->Recorded.bValid ? FMath::Max(C->Start, C->Recorded.Frame) : C->Start;
	}
	const TArray<TSharedPtr<FJsonValue>>* Armed = nullptr;
	if (TryGetArrayParam(Params, TEXT("armed"), Armed) && Armed)
	{
		for (const TSharedPtr<FJsonValue>& V : *Armed)
		{
			const FString A = NormalizeScriptInputActionPath(V->AsString());
			if (!LoadObject<UInputAction>(nullptr, *A)) return MCPError(FString::Printf(TEXT("armed: no Input Action loads from %s"), *A));
			Cfg.Armed.Add(A);
		}
	}
	FString Err, Msg;
	if (!FPIEClipPlayer::Get().Start(Cfg, Err, Msg)) return MCPError(Err);
	return ClipRunStarted(Msg, true);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieClipRecordStop(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Err;
	if (!FPIEClipPlayer::Get().StopRecording(Err)) return MCPError(Err);
	TSharedPtr<FJsonObject> Result = ClipRunAnswer(TEXT("recording stopped and the take saved"), false);
	MCPSetUpdated(Result);
	MCPSetNoRollback(Result, TEXT("The take is saved into the clip; what it replaced is in the clip's takes, which clip_update can put back."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieClipStatus(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	MCPReadParamsAhead(Params, { TEXT("wait"), TEXT("timeout_seconds") });
	const FString RunId = OptionalString(Params, TEXT("run_id"));
	const FClipRun* Run = FPIEClipPlayer::Get().GetRun();
	auto Result = MCPSuccess();
	if (Run && (RunId.IsEmpty() || Run->RunId == RunId))
	{
		const TSharedRef<FJsonObject> Fields = FPIEClipPlayer::Get().RunToJson(FPIEClipPlayer::Get().IsActive());
		for (const auto& F : Fields->Values) Result->SetField(F.Key, F.Value);
		Result->SetBoolField(TEXT("active"), FPIEClipPlayer::Get().IsActive());
		return MCPResult(Result);
	}
	if (!RunId.IsEmpty())
	{
		const TSharedPtr<FJsonObject> Saved = LoadJsonFile(DefaultScriptObservationsRoot() / RunId / TEXT("clip_results.json"));
		if (Saved.IsValid())
		{
			for (const auto& F : Saved->Values) Result->SetField(F.Key, F.Value);
			Result->SetBoolField(TEXT("active"), false);
			return MCPResult(Result);
		}
	}
	Result->SetStringField(TEXT("state"), TEXT("idle"));
	if (!RunId.IsEmpty()) Result->SetStringField(TEXT("run_id"), RunId);
	Result->SetBoolField(TEXT("active"), false);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieClipStop(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Err;
	if (!FPIEClipPlayer::Get().Stop(OptionalString(Params, TEXT("reason")), Err))
	{
		auto Result = MCPSuccess();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetBoolField(TEXT("alreadyStopped"), true);
		Result->SetStringField(TEXT("error"), Err);
		return MCPResult(Result);
	}
	TSharedPtr<FJsonObject> Result = ClipRunAnswer(TEXT("stopped"), false);
	MCPSetUpdated(Result);
	MCPSetNoRollback(Result, TEXT("A stopped run cannot be resumed; start a new run with pie_clip_run."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieClipVerdict(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Check, Verdict;
	TSharedPtr<FJsonValue> CheckErr = RequireString(Params, TEXT("check"), Check);
	TSharedPtr<FJsonValue> VerdictErr = RequireString(Params, TEXT("verdict"), Verdict);
	if (CheckErr) return CheckErr;
	if (VerdictErr) return VerdictErr;
	FString EvidenceNote;
	TArray<FString> Captures;
	const TSharedPtr<FJsonObject>* Evidence = nullptr;
	if (TryGetObjectParam(Params, TEXT("evidence"), Evidence) && Evidence)
	{
		(*Evidence)->TryGetStringField(TEXT("note"), EvidenceNote);
		const TArray<TSharedPtr<FJsonValue>>* Caps = nullptr;
		const FClipRun* Run = FPIEClipPlayer::Get().GetRun();
		if ((*Evidence)->TryGetArrayField(TEXT("captures"), Caps) && Caps)
		{
			for (const TSharedPtr<FJsonValue>& C : *Caps)
			{
				FString File = C->AsString();
				if (FPaths::IsRelative(File) && Run) File = Run->OutputDir / File;
				if (!FPaths::FileExists(File)) return MCPError(FString::Printf(TEXT("evidence.captures: no file at %s"), *File));
				Captures.Add(FPaths::ConvertRelativePathToFull(File));
			}
		}
	}
	FString Err;
	if (!FPIEClipPlayer::Get().Verdict(Check, Verdict, OptionalString(Params, TEXT("note")), OptionalString(Params, TEXT("source"), TEXT("human")), EvidenceNote, Captures, Err))
	{
		return MCPError(Err);
	}
	TSharedPtr<FJsonObject> Result = ClipRunAnswer(FString::Printf(TEXT("verdict on %s: %s"), *Check, *Verdict), false);
	MCPSetUpdated(Result);
	MCPSetNoRollback(Result, TEXT("The check was awaiting its verdict and the run has moved on from it; a verdict cannot be withdrawn."));
	return MCPResult(Result);
}
