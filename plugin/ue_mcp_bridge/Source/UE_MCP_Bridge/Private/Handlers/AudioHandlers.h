#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FAudioHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	// ── Assets + playback ──────────────────────────────────────────────
	static TSharedPtr<FJsonValue> ListSoundAssets(const TSharedPtr<FJsonObject>& Params);
	// #664: import a WAV/OGG file as a USoundWave asset.
	static TSharedPtr<FJsonValue> ImportAudio(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateSoundCue(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateMetaSoundSource(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PlaySoundAtLocation(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SpawnAmbientSound(const TSharedPtr<FJsonObject>& Params);

	// ── MetaSound graph authoring (AudioHandlers_MetaSound.cpp) ─────────
	static TSharedPtr<FJsonValue> MetaSoundListNodeClasses(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MetaSoundGetGraph(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MetaSoundAddNode(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MetaSoundAddGraphInput(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MetaSoundAddGraphOutput(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MetaSoundConnect(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MetaSoundConnectGraphInput(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MetaSoundConnectGraphOutput(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MetaSoundConnectAudioOut(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MetaSoundSetInputDefault(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MetaSoundBuild(const TSharedPtr<FJsonObject>& Params);

	// ── SoundCue graph authoring (AudioHandlers_SoundCue.cpp) ───────────
	static TSharedPtr<FJsonValue> SoundCueAddNode(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SoundCueConnect(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SoundCueGetGraph(const TSharedPtr<FJsonObject>& Params);

	// ── Mixing + routing + spatialization (AudioHandlers_Mixing.cpp) ────
	static TSharedPtr<FJsonValue> CreateSubmix(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetSubmixParent(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddSubmixEffect(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateSoundClass(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateSoundMix(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateConcurrency(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateAttenuation(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetSoundSubmix(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddSoundSubmixSend(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetSoundClass(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetSoundAttenuation(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetSoundConcurrency(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetAudioProperty(const TSharedPtr<FJsonObject>& Params);
};
