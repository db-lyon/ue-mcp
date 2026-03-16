#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FSequencerHandlers
{
public:
	// Register all sequencer handlers
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	// Handler implementations
	static TSharedPtr<FJsonValue> CreateLevelSequence(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadSequenceInfo(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddTrack(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SequenceControl(const TSharedPtr<FJsonObject>& Params);
};
