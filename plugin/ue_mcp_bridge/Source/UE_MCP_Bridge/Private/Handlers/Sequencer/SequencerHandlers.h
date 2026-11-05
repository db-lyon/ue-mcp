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
	// #881: move the playhead to an exact time and evaluate there.
	static TSharedPtr<FJsonValue> ScrubSequence(const TSharedPtr<FJsonObject>& Params);
	// #1098: scrub + capture over a frame range (SequencerHandlers_Render.cpp).
	static TSharedPtr<FJsonValue> RenderSequenceFrames(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetPlaybackRange(const TSharedPtr<FJsonObject>& Params);
	// #1641: the sequence's display rate.
	static TSharedPtr<FJsonValue> SetDisplayRate(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddSection(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetKeyframes(const TSharedPtr<FJsonObject>& Params);
	// #1407/#1232: channel keys on any section, and section removal (SequencerHandlers_Channels.cpp).
	static TSharedPtr<FJsonValue> GetChannelKeys(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetChannelKeys(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveSection(const TSharedPtr<FJsonObject>& Params);
	// #1356/#1232: binding resolution, retargeting and runtime players (SequencerHandlers_Bindings.cpp).
	static TSharedPtr<FJsonValue> ResolveBindings(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RebindPossessable(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetRuntimeBindings(const TSharedPtr<FJsonObject>& Params);
	// #1640: components and property values of each binding's template or bound actor.
	static TSharedPtr<FJsonValue> GetBindingComponents(const TSharedPtr<FJsonObject>& Params);
};
