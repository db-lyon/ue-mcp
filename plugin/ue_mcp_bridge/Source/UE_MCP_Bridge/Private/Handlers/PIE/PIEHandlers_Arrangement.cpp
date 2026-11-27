// Playtest Hub handlers (#2146): play an arrangement or one region of it, and record a performance into a clip.
// The daemon owns the hub's files; these take what they play as JSON. Members of FPIEHandlers.

#include "Handlers/PIE/PIEHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/PIE/PIEArrangement.h"
#include "Handlers/PIE/PIEArrangementPlayer.h"

namespace
{
	using namespace MCPPIE;

	const TCHAR* const RunIdDoc = TEXT("The hub's id for the run");
}

void FPIEHandlers::RegisterArrangementHandlers(FMCPHandlerRegistry& Registry)
{
	using EType = EMCPParamType;
	Registry.RegisterHandler(TEXT("pie_arrangement_play"), &PieArrangementPlay, {
		MCPParam::Required(TEXT("runId"), EType::String, RunIdDoc),
		MCPParam::Required(TEXT("arrangement"), EType::Object, TEXT("The arrangement as the hub stores it: {id, level, rate, tracks: [{id, performer, mute, solo, instances: [{id, clip, at}]}], checks: [{id, at, length, mode: by_end|throughout|hold|manual, expect?: {value, op, target?, subject?}, instruction?, timeout?}], regions: [{id, start, end}]}")),
		MCPParam::Required(TEXT("clips"), EType::Object, TEXT("Every clip the tracks place, by id: {rate, length, performer, input, lanes}")),
		MCPParam::Optional(TEXT("region"), EType::String, TEXT("Play from frame 0 to this region's end and judge only the checks that start inside it")),
	}, MCPSpec::ContractExempt(TEXT("Loads the arrangement's level, starts PIE and plays it")));
	Registry.RegisterHandler(TEXT("pie_arrangement_status"), &PieArrangementStatus, {
		MCPParam::Required(TEXT("runId"), EType::String, RunIdDoc),
	});
	Registry.RegisterHandler(TEXT("pie_arrangement_stop"), &PieArrangementStop, {
		MCPParam::Required(TEXT("runId"), EType::String, RunIdDoc),
		MCPParam::Optional(TEXT("reason"), EType::String, TEXT("Why it stopped, for the unsettled checks")),
	}, MCPSpec::ContractExempt(TEXT("Ends the run and the PIE session it started")));
	Registry.RegisterHandler(TEXT("pie_arrangement_verdict"), &PieArrangementVerdict, {
		MCPParam::Required(TEXT("runId"), EType::String, RunIdDoc),
		MCPParam::Required(TEXT("check"), EType::String, TEXT("Id of the manual check awaiting its verdict")),
		MCPParam::Required(TEXT("verdict"), EType::String, TEXT("The verdict")).Enum({ TEXT("pass"), TEXT("fail") }),
		MCPParam::Optional(TEXT("note"), EType::String, TEXT("What the verdict rests on")),
	}, MCPSpec::ContractExempt(TEXT("Settles a check in a running play and resumes PIE")));
	Registry.RegisterHandler(TEXT("pie_clip_record_start"), &PieClipRecordStart, {
		MCPParam::Required(TEXT("performer"), EType::Object, TEXT("Who to record: {kind: player, client: 0} or {kind: actor, actor}")),
		MCPParam::Required(TEXT("kind"), EType::String, TEXT("The performer's kind")).Enum({ TEXT("player"), TEXT("actor") }),
		MCPParam::Optional(TEXT("rate"), EType::Number, TEXT("Frames per second of a new clip (default 60)")).Min(1),
		MCPParam::Optional(TEXT("punch"), EType::Object, TEXT("Punch in: play clip from frame 0 with the person's input locked out, record from `from` to `to` (the clip's frames), and replace that range")).WithFields({
			MCPParam::RequiredField(TEXT("clip"), EType::Object, TEXT("The clip as the hub stores it")),
			MCPParam::RequiredField(TEXT("from"), EType::Number, TEXT("The frame the take starts on")),
			MCPParam::RequiredField(TEXT("to"), EType::Number, TEXT("The frame the take ends on")),
		}),
	}, MCPSpec::ContractExempt(TEXT("Starts PIE and records a performance")));
	Registry.RegisterHandler(TEXT("pie_clip_record_stop"), &PieClipRecordStop, {}, MCPSpec::ContractExempt(TEXT("Ends the recording and the PIE session it started")));
}

TSharedPtr<FJsonValue> FPIEHandlers::PieArrangementPlay(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString RunId;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("runId"), RunId)) return Err;
	const TSharedPtr<FJsonObject>* Arrangement = nullptr;
	const TSharedPtr<FJsonObject>* Clips = nullptr;
	if (!Params->TryGetObjectField(TEXT("arrangement"), Arrangement) || !Arrangement) return MCPError(TEXT("arrangement is the arrangement to play, as an object"));
	if (!Params->TryGetObjectField(TEXT("clips"), Clips) || !Clips) return MCPError(TEXT("clips holds every clip the arrangement places, by id"));
	FArrangementPlayConfig Cfg;
	Cfg.RunId = RunId;
	FString Error;
	if (!CompileArrangement(*Arrangement, *Clips, OptionalString(Params, TEXT("region")), Cfg.Plan, Error)) return MCPError(Error);
	bool bStartedPIE = false;
	if (!FPIEArrangementPlayer::Get().Play(Cfg, bStartedPIE, Error)) return MCPError(Error);
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("runId"), RunId);
	if (bStartedPIE) Result->SetBoolField(TEXT("pieStarted"), true);
	MCPSetNoRollback(Result, TEXT("Stop the run with arrangement_stop; it ends the PIE session it started."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieArrangementStatus(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString RunId;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("runId"), RunId)) return Err;
	TSharedPtr<FJsonObject> Status = FPIEArrangementPlayer::Get().Status(RunId);
	if (!Status.IsValid()) return MCPError(FString::Printf(TEXT("no run %s: the editor holds only the last run it played"), *RunId));
	auto Result = MCPSuccess();
	for (const TPair<FString, TSharedPtr<FJsonValue>>& F : Status->Values) Result->SetField(F.Key, F.Value);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieArrangementStop(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString RunId, Error;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("runId"), RunId)) return Err;
	if (!FPIEArrangementPlayer::Get().Stop(RunId, OptionalString(Params, TEXT("reason")), Error)) return MCPError(Error);
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("runId"), RunId);
	MCPSetNoRollback(Result, TEXT("A stopped run cannot be resumed; play it again."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieArrangementVerdict(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString RunId, Check, Verdict, Error;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("runId"), RunId)) return Err;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("check"), Check)) return Err;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("verdict"), Verdict)) return Err;
	if (!FPIEArrangementPlayer::Get().Verdict(RunId, Check, Verdict, OptionalString(Params, TEXT("note")), Error)) return MCPError(Error);
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("runId"), RunId);
	Result->SetStringField(TEXT("check"), Check);
	MCPSetNoRollback(Result, TEXT("A verdict is final for the run; play it again to judge again."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieClipRecordStart(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FClipRecordConfig Cfg;
	FString Error, Kind;
	if (!ClipPerformerFromJson(Params->TryGetField(TEXT("performer")), Cfg.Performer, Error)) return MCPError(Error);
	if (Params->TryGetStringField(TEXT("kind"), Kind) && Kind != ClipPerformerKindToString(Cfg.Performer.Kind))
	{
		return MCPError(FString::Printf(TEXT("kind is %s, and the performer is a %s"), *Kind, *ClipPerformerKindToString(Cfg.Performer.Kind)));
	}
	Params->TryGetNumberField(TEXT("rate"), Cfg.Rate);
	if (Cfg.Rate <= 0.0 || Cfg.Rate > 1000.0) return MCPError(TEXT("rate is frames per second, more than 0 and at most 1000"));
	const TSharedPtr<FJsonObject>* Punch = nullptr;
	if (Params->TryGetObjectField(TEXT("punch"), Punch) && Punch)
	{
		const TSharedPtr<FJsonObject>* Clip = nullptr;
		if (!(*Punch)->TryGetObjectField(TEXT("clip"), Clip) || !Clip || !(*Punch)->TryGetNumberField(TEXT("from"), Cfg.PunchFrom) || !(*Punch)->TryGetNumberField(TEXT("to"), Cfg.PunchTo))
		{
			return MCPError(TEXT("punch is {clip, from, to}"));
		}
		if (Cfg.PunchFrom < 0.0 || Cfg.PunchTo <= Cfg.PunchFrom) return MCPError(TEXT("a punch-in records from `from` to a later `to`, in the clip's frames"));
		if (!ClipFromJson(*Clip, Cfg.Clip, Error)) return MCPError(TEXT("punch.clip: ") + Error);
		Cfg.bPunch = true;
	}
	if (!FPIEArrangementPlayer::Get().StartRecording(Cfg, Error)) return MCPError(Error);
	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("recording"), true);
	MCPSetNoRollback(Result, TEXT("End the recording with clip_record_stop."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieClipRecordStop(const TSharedPtr<FJsonObject>& /*Params*/)
{
	MCP_CHECK_GAME_THREAD();
	FClip Clip;
	FString Error;
	if (!FPIEArrangementPlayer::Get().StopRecording(Clip, Error)) return MCPError(Error);
	auto Result = MCPSuccess();
	Result->SetObjectField(TEXT("clip"), ClipToJson(Clip));
	MCPSetNoRollback(Result, TEXT("The take is answered, not saved; the hub saves it into the clip."));
	return MCPResult(Result);
}
