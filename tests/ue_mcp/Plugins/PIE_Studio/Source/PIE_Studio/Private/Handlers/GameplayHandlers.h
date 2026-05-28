#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FGameplayHandlers
{
public:
	// PIE inspection
	static TSharedPtr<FJsonValue> GetPieAnimState(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetPieAnimProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetPieSubsystemState(const TSharedPtr<FJsonObject>& Params);

	// Input injection
	static TSharedPtr<FJsonValue> InjectInput(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> InjectInputStart(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> InjectInputUpdate(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> InjectInputStop(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> InjectInputTape(const TSharedPtr<FJsonObject>& Params);

	// Recording
	static TSharedPtr<FJsonValue> PieRecordArm(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieRecordDisarm(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieRecordStop(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieRecordStatus(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieRecordList(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieRecordRead(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieRecordDelete(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieMark(const TSharedPtr<FJsonObject>& Params);

	// Replay
	static TSharedPtr<FJsonValue> PieReplayArm(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieReplayDisarm(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieReplayStop(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieReplayStatus(const TSharedPtr<FJsonObject>& Params);

	// Diff / Snapshot
	static TSharedPtr<FJsonValue> PieRecordDiff(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieSnapshot(const TSharedPtr<FJsonObject>& Params);

	// Observation profiles
	static TSharedPtr<FJsonValue> PieProfileCreate(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieProfileRead(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieProfileUpdate(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieProfileDelete(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieProfileList(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieObserveArm(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieObserveDisarm(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieObserveStop(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieObserveStatus(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieObserveList(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PieObserveRead(const TSharedPtr<FJsonObject>& Params);
};
