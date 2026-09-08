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
	// #729: decode a USoundWave's imported audio to in-memory PCM for semantic search.
	static TSharedPtr<FJsonValue> ExtractSoundWavePCM(const TSharedPtr<FJsonObject>& Params);
	// #664: import a WAV/OGG file as a USoundWave asset.
	static TSharedPtr<FJsonValue> ImportAudio(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateSoundCue(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateMetaSoundSource(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> PlaySoundAtLocation(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SpawnAmbientSound(const TSharedPtr<FJsonObject>& Params);

	// ── MetaSound graph authoring (AudioHandlers_MetaSound.cpp) ─────────
	// One-shot: stamp a whole graph (nodes/connections/inputs/outputs) in one call.
	static TSharedPtr<FJsonValue> MetaSoundAuthor(const TSharedPtr<FJsonObject>& Params);
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

	// MetaSound graph introspection, in AudioHandlers_MetaSoundRead.cpp. The
	// bridge could BUILD a graph and not read it back, so it could write but
	// not verify or iterate, which is worse than not writing at all. Node ids
	// match what the authoring actions already accept.
	static TSharedPtr<FJsonValue> MetaSoundReadDocument(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MetaSoundListConnections(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MetaSoundListVariables(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MetaSoundSearchNodes(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MetaSoundInspectNode(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MetaSoundListNodePins(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MetaSoundValidate(const TSharedPtr<FJsonObject>& Params);

	// ── SoundCue graph authoring (AudioHandlers_SoundCue.cpp) ───────────
	// One-shot: stamp a whole cue tree (nodes + connections + root) in one call.
	static TSharedPtr<FJsonValue> SoundCueAuthor(const TSharedPtr<FJsonObject>& Params);
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

	// ── Authoring depth (AudioHandlers_Depth.cpp) ───────────────────────
	// The editing half. Every action above that adds a MetaSound node, graph
	// input, graph output or connection had no inverse, and every MetaSound
	// write action refuses unless create_metasound opened a builder session in
	// THIS editor run, so a MetaSound on disk could not be edited at all. These
	// resolve a live session builder when one exists and otherwise attach a
	// builder to the asset's own document, the way the MetaSound editor does.
	static TSharedPtr<FJsonValue> MetaSoundRemoveNode(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MetaSoundDisconnect(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MetaSoundRemoveMember(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MetaSoundRenameMember(const TSharedPtr<FJsonObject>& Params);
	// SoundCue: the inverse of soundcue_add_node and soundcue_connect.
	static TSharedPtr<FJsonValue> SoundCueRemoveNode(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SoundCueDisconnect(const TSharedPtr<FJsonObject>& Params);
	// Reparenting is two-sided (ParentClass, the new parent's ChildClasses, and
	// removal from the old parent's), which is why it is not a property write.
	static TSharedPtr<FJsonValue> SetSoundClassParent(const TSharedPtr<FJsonObject>& Params);
	// The verification half of the six routing assignment actions.
	static TSharedPtr<FJsonValue> ReadSoundRouting(const TSharedPtr<FJsonObject>& Params);
};
