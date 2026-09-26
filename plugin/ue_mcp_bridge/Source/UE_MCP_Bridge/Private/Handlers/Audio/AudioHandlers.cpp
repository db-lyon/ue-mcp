#include "AudioHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "AudioHandlers_Internal.h"
#include "HandlerPagination.h"
#include "HandlerAssetCreate.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundWave.h"
#include "Factories/SoundCueFactoryNew.h"
#include "AssetImportTask.h"
#include "Misc/Paths.h"
#include "Misc/Base64.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/AmbientSound.h"
#include "Components/AudioComponent.h"
#include "EngineUtils.h"

void FAudioHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	// Reports parameters its handlers never read (#1057).
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("audio"));

	// #1057: a spec'd handler declares its parameters here and nowhere else; the
	// TS surface is generated from a recording of these. The asset creators are
	// contract-exempt, because the contract values would create an asset.
	using EType = EMCPParamType;
	Registry.RegisterHandler(TEXT("list_sound_assets"), &ListSoundAssets, {
		MCPParam::Optional(TEXT("directory"), EType::String, TEXT("Content directory to list (default /Game)")),
		MCPParam::Optional(TEXT("recursive"), EType::Boolean, TEXT("Include subdirectories (default true)")),
		MCPParam::Optional(TEXT("offset"), EType::Number, TEXT("Refused. The row offset was replaced by cursor paging (#730); pass cursor and limit instead")),
		MCPParam::Optional(TEXT("cursor"), EType::String, TEXT("Resume a paged read: pass back the nextCursor from the previous page, unmodified")),
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("Rows per page, 1 to 5000 (default 1000)")).Alias(TEXT("maxResults")),
	});
	Registry.RegisterHandler(TEXT("extract_sound_wave_pcm"), &ExtractSoundWavePCM, {
		MCPParam::Required(TEXT("soundPath"), EType::String, TEXT("SoundWave asset path")).Alias(TEXT("assetPath")),
		MCPParam::Optional(TEXT("maxSeconds"), EType::Number, TEXT("Cap the decoded window in seconds (default the full asset)")),
		MCPParam::Optional(TEXT("downmixMono"), EType::Boolean, TEXT("Average the channels to mono (default false)")),
	});
	Registry.RegisterHandler(TEXT("import_audio"), &ImportAudio, {
		MCPParam::Required(TEXT("filePath"), EType::String, TEXT("WAV, OGG or FLAC file on disk")).Alias(TEXT("filename")),
		MCPParam::Optional(TEXT("name"), EType::String, TEXT("Asset name (default the file's base name)")).Alias(TEXT("assetName")),
		MCPParam::Optional(TEXT("packagePath"), EType::String, TEXT("Destination folder (default /Game/Audio)")).Alias(TEXT("destinationPath")),
		MCPParam::Optional(TEXT("looping"), EType::Boolean, TEXT("Set bLooping on the imported SoundWave (omit to keep the importer's value)")),
		MCPParam::Optional(TEXT("replaceExisting"), EType::Boolean, TEXT("Replace an existing asset of that name (default true)")),
	});
	Registry.RegisterHandler(TEXT("create_sound_cue"), &CreateSoundCue, {
		MCPParam::Required(TEXT("name"), EType::String, TEXT("SoundCue asset name")),
		MCPParam::Optional(TEXT("packagePath"), EType::String, TEXT("Destination folder (default /Game/Audio/SoundCues)")),
		MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("skip (default) returns the existing asset, error refuses; it never overwrites")),
	}, MCPSpec::ContractExempt(TEXT("Creates and saves a SoundCue asset named by the contract values")));
	Registry.RegisterHandler(TEXT("create_metasound_source"), &CreateMetaSoundSource, {
		MCPParam::Required(TEXT("name"), EType::String, TEXT("MetaSoundSource asset name")),
		MCPParam::Optional(TEXT("packagePath"), EType::String, TEXT("Destination folder (default /Game/Audio/MetaSounds)")),
		MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("skip (default) returns the existing asset, error refuses; it never overwrites")),
		MCPParam::Optional(TEXT("format"), EType::String, TEXT("Output format: mono | stereo (default mono)")),
		MCPParam::Optional(TEXT("oneShot"), EType::Boolean, TEXT("Declare the one-shot interface (default true)")),
	}, MCPSpec::ContractExempt(TEXT("Creates and saves a MetaSoundSource asset named by the contract values")));
	Registry.RegisterHandler(TEXT("play_sound_at_location"), &PlaySoundAtLocation, {
		MCPParam::Required(TEXT("soundPath"), EType::String, TEXT("Sound asset to play (SoundWave, SoundCue or MetaSoundSource)")).Alias(TEXT("assetPath")).Alias(TEXT("path")),
		MCPParam::Optional(TEXT("location"), EType::Vec3, TEXT("World location to play at (default origin)")),
		MCPParam::Optional(TEXT("volumeMultiplier"), EType::Number, TEXT("Volume multiplier (default 1)")).Alias(TEXT("volume")),
		MCPParam::Optional(TEXT("pitchMultiplier"), EType::Number, TEXT("Pitch multiplier (default 1)")).Alias(TEXT("pitch")),
	});
	Registry.RegisterHandler(TEXT("spawn_ambient_sound"), &SpawnAmbientSound, {
		MCPParam::Required(TEXT("soundPath"), EType::String, TEXT("Sound asset the AmbientSound plays")).Alias(TEXT("assetPath")).Alias(TEXT("path")),
		MCPParam::Optional(TEXT("location"), EType::Vec3, TEXT("World location of the actor (default origin)")),
		MCPParam::Optional(TEXT("label"), EType::String, TEXT("Actor label")),
		MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("skip | error | rename when an actor with that label exists (default skip)")),
		MCPParam::Optional(TEXT("volumeMultiplier"), EType::Number, TEXT("Volume multiplier on the audio component (default 1)")).Alias(TEXT("volume")),
	});

	// MetaSound graph authoring (AudioHandlers_MetaSound.cpp)
	Registry.RegisterHandler(TEXT("metasound_author"), &MetaSoundAuthor, {
		MCPParam::Required(TEXT("name"), EType::String, TEXT("MetaSoundSource asset name")),
		MCPParam::Optional(TEXT("packagePath"), EType::String, TEXT("Destination folder (default /Game/Audio/MetaSounds)")),
		MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("skip (default) returns the existing asset, error refuses; it never overwrites")),
		MCPParam::Optional(TEXT("format"), EType::String, TEXT("Output format: mono | stereo (default mono)")),
		MCPParam::Optional(TEXT("oneShot"), EType::Boolean, TEXT("Declare the one-shot interface (default true)")),
		MCPParam::Optional(TEXT("inputs"), EType::Array, TEXT("Graph inputs to add")).Items(EType::Object).WithFields({
			MCPParam::RequiredField(TEXT("name"), EType::String, TEXT("Graph input name")),
			MCPParam::RequiredField(TEXT("dataType"), EType::String, TEXT("MetaSound data type: Float, Int32, Bool, String, Trigger, Audio, Time, ...")),
			MCPParam::OptionalField(TEXT("default"), EType::Any, TEXT("Literal default for the input")),
		}),
		MCPParam::Optional(TEXT("outputs"), EType::Array, TEXT("Graph outputs to add")).Items(EType::Object).WithFields({
			MCPParam::RequiredField(TEXT("name"), EType::String, TEXT("Graph output name")),
			MCPParam::RequiredField(TEXT("dataType"), EType::String, TEXT("MetaSound data type")),
		}),
		MCPParam::Optional(TEXT("nodes"), EType::Array, TEXT("Nodes to add, each {id, class, namespace?, variant?, majorVersion?, inputs?: {vertex: value}}; id is the local name connections use, namespace defaults to UE and majorVersion to 1")).Items(EType::Object),
		MCPParam::Optional(TEXT("connections"), EType::Array, TEXT("Edges, each {from, to}. Endpoints are 'nodeId:vertex', or the heads 'input:<name>', 'output:<name>' and 'audioOut:<channel>'")).Items(EType::Object),
	}, MCPSpec::ContractExempt(TEXT("Creates and saves a MetaSoundSource asset named by the contract values")));
	Registry.RegisterHandler(TEXT("metasound_list_node_classes"), &MetaSoundListNodeClasses, {
		MCPParam::Optional(TEXT("filter"), EType::String, TEXT("Case-insensitive substring over the node class name")),
	});
	Registry.RegisterHandler(TEXT("metasound_get_graph"), &MetaSoundGetGraph, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("MetaSoundSource asset path")).Alias(TEXT("metasoundPath")),
	});
	Registry.RegisterHandler(TEXT("metasound_add_node"), &MetaSoundAddNode, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("MetaSoundSource asset path")).Alias(TEXT("metasoundPath")),
		MCPParam::Required(TEXT("nodeClassName"), EType::String, TEXT("Registered node class name, e.g. Sine")),
		MCPParam::Optional(TEXT("nodeNamespace"), EType::String, TEXT("Node class namespace (default UE)")),
		MCPParam::Optional(TEXT("nodeVariant"), EType::String, TEXT("Node class variant, e.g. Audio")),
		MCPParam::Optional(TEXT("majorVersion"), EType::Integer, TEXT("Node class major version (default 1)")),
	});
	Registry.RegisterHandler(TEXT("metasound_add_graph_input"), &MetaSoundAddGraphInput, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("MetaSoundSource asset path")).Alias(TEXT("metasoundPath")),
		MCPParam::Required(TEXT("name"), EType::String, TEXT("Graph input name")),
		MCPParam::Required(TEXT("dataType"), EType::String, TEXT("MetaSound data type: Float, Int32, Bool, String, Trigger, Audio, Time, ...")),
		MCPParam::Optional(TEXT("defaultValue"), EType::Any, TEXT("Literal default for the input")),
	});
	Registry.RegisterHandler(TEXT("metasound_add_graph_output"), &MetaSoundAddGraphOutput, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("MetaSoundSource asset path")).Alias(TEXT("metasoundPath")),
		MCPParam::Required(TEXT("name"), EType::String, TEXT("Graph output name")),
		MCPParam::Required(TEXT("dataType"), EType::String, TEXT("MetaSound data type: Float, Int32, Bool, String, Trigger, Audio, Time, ...")),
	});
	Registry.RegisterHandler(TEXT("metasound_connect"), &MetaSoundConnect, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("MetaSoundSource asset path")).Alias(TEXT("metasoundPath")),
		MCPParam::Required(TEXT("fromNodeId"), EType::String, TEXT("Source node id")),
		MCPParam::Required(TEXT("fromOutput"), EType::String, TEXT("Output vertex name on the source node")),
		MCPParam::Required(TEXT("toNodeId"), EType::String, TEXT("Destination node id")),
		MCPParam::Required(TEXT("toInput"), EType::String, TEXT("Input vertex name on the destination node")),
	});
	Registry.RegisterHandler(TEXT("metasound_connect_graph_input"), &MetaSoundConnectGraphInput, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("MetaSoundSource asset path")).Alias(TEXT("metasoundPath")),
		MCPParam::Required(TEXT("graphInput"), EType::String, TEXT("Graph input name")),
		MCPParam::Required(TEXT("toNodeId"), EType::String, TEXT("Destination node id")),
		MCPParam::Required(TEXT("toInput"), EType::String, TEXT("Input vertex name on the destination node")),
	});
	Registry.RegisterHandler(TEXT("metasound_connect_graph_output"), &MetaSoundConnectGraphOutput, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("MetaSoundSource asset path")).Alias(TEXT("metasoundPath")),
		MCPParam::Required(TEXT("fromNodeId"), EType::String, TEXT("Source node id")),
		MCPParam::Required(TEXT("fromOutput"), EType::String, TEXT("Output vertex name on the source node")),
		MCPParam::Required(TEXT("graphOutput"), EType::String, TEXT("Graph output name")),
	});
	Registry.RegisterHandler(TEXT("metasound_connect_audio_out"), &MetaSoundConnectAudioOut, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("MetaSoundSource asset path")).Alias(TEXT("metasoundPath")),
		MCPParam::Required(TEXT("fromNodeId"), EType::String, TEXT("Source node id")),
		MCPParam::Required(TEXT("fromOutput"), EType::String, TEXT("Output vertex name on the source node, of Audio type")),
		MCPParam::Optional(TEXT("channel"), EType::Integer, TEXT("Audio output channel: 0 left or mono, 1 right (default 0)")),
	});
	Registry.RegisterHandler(TEXT("metasound_set_input_default"), &MetaSoundSetInputDefault, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("MetaSoundSource asset path")).Alias(TEXT("metasoundPath")),
		MCPParam::Required(TEXT("value"), EType::Any, TEXT("Default value to set")),
		MCPParam::Optional(TEXT("dataType"), EType::String, TEXT("Literal type hint: Float | Int32 | Bool | String")),
		MCPParam::Optional(TEXT("nodeId"), EType::String, TEXT("Node whose input to set, with inputName. Pass either nodeId and inputName, or graphInput")),
		MCPParam::Optional(TEXT("inputName"), EType::String, TEXT("Input vertex name on nodeId")),
		MCPParam::Optional(TEXT("graphInput"), EType::String, TEXT("Graph input whose default to set, instead of nodeId and inputName")),
	});
	Registry.RegisterHandler(TEXT("metasound_build"), &MetaSoundBuild, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("MetaSoundSource asset path")).Alias(TEXT("metasoundPath")),
	});
	Registry.RegisterHandler(TEXT("metasound_read_document"), &MetaSoundReadDocument, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("MetaSound asset path")).Alias(TEXT("metasoundPath")),
		MCPParam::Optional(TEXT("pageId"), EType::String, TEXT("Graph page to read, for assets that declare more than one (default the default page)")),
		MCPParam::Optional(TEXT("includeNodes"), EType::Boolean, TEXT("Include the node list (default true)")),
		MCPParam::Optional(TEXT("includeConnections"), EType::Boolean, TEXT("Include the edge list (default true)")),
	});
	Registry.RegisterHandler(TEXT("metasound_list_connections"), &MetaSoundListConnections, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("MetaSound asset path")).Alias(TEXT("metasoundPath")),
		MCPParam::Optional(TEXT("pageId"), EType::String, TEXT("Graph page to read, for assets that declare more than one (default the default page)")),
		MCPParam::Optional(TEXT("nodeId"), EType::String, TEXT("Narrow to edges touching this node")),
		MCPParam::Optional(TEXT("direction"), EType::String, TEXT("With nodeId: in | out | both (default both)")),
		MCPParam::Optional(TEXT("dataType"), EType::String, TEXT("Only edges carrying this data type")),
	});
	Registry.RegisterHandler(TEXT("metasound_list_variables"), &MetaSoundListVariables, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("MetaSound asset path")).Alias(TEXT("metasoundPath")),
		MCPParam::Optional(TEXT("pageId"), EType::String, TEXT("Graph page to read, for assets that declare more than one (default the default page)")),
		MCPParam::Optional(TEXT("filter"), EType::String, TEXT("Case-insensitive substring over the variable name")),
	});
	Registry.RegisterHandler(TEXT("metasound_search_nodes"), &MetaSoundSearchNodes, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("MetaSound asset path")).Alias(TEXT("metasoundPath")),
		MCPParam::Optional(TEXT("pageId"), EType::String, TEXT("Graph page to read, for assets that declare more than one (default the default page)")),
		MCPParam::Optional(TEXT("query"), EType::String, TEXT("Substring over node name, class name, namespace or variant")),
		MCPParam::Optional(TEXT("dataType"), EType::String, TEXT("Only nodes with a vertex of this data type")),
		MCPParam::Optional(TEXT("classType"), EType::String, TEXT("External | Input | Output | Variable | ...")),
		MCPParam::Optional(TEXT("limit"), EType::Integer, TEXT("Most matches to return, 1 to 1000 (default 100)")),
	});
	Registry.RegisterHandler(TEXT("metasound_inspect_node"), &MetaSoundInspectNode, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("MetaSound asset path")).Alias(TEXT("metasoundPath")),
		MCPParam::Required(TEXT("nodeId"), EType::String, TEXT("Node id to inspect")),
		MCPParam::Optional(TEXT("pageId"), EType::String, TEXT("Graph page to read, for assets that declare more than one (default the default page)")),
	});
	Registry.RegisterHandler(TEXT("metasound_list_node_pins"), &MetaSoundListNodePins, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("MetaSound asset path")).Alias(TEXT("metasoundPath")),
		MCPParam::Required(TEXT("nodeId"), EType::String, TEXT("Node id whose vertices to list")),
		MCPParam::Optional(TEXT("pageId"), EType::String, TEXT("Graph page to read, for assets that declare more than one (default the default page)")),
		MCPParam::Optional(TEXT("direction"), EType::String, TEXT("inputs | outputs | both (default both)")),
		MCPParam::Optional(TEXT("dataType"), EType::String, TEXT("Only vertices of this data type")),
	});
	Registry.RegisterHandler(TEXT("metasound_validate"), &MetaSoundValidate, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("MetaSound asset path")).Alias(TEXT("metasoundPath")),
		MCPParam::Optional(TEXT("pageId"), EType::String, TEXT("Graph page to read, for assets that declare more than one (default the default page)")),
	});

	// SoundCue graph authoring (AudioHandlers_SoundCue.cpp)
	Registry.RegisterHandler(TEXT("soundcue_author"), &SoundCueAuthor, {
		MCPParam::Required(TEXT("name"), EType::String, TEXT("SoundCue asset name")),
		MCPParam::Optional(TEXT("packagePath"), EType::String, TEXT("Destination folder (default /Game/Audio/SoundCues)")),
		MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("skip (default) returns the existing asset, error refuses; it never overwrites")),
		MCPParam::Optional(TEXT("nodes"), EType::Array, TEXT("Nodes to add, each {id, type, soundWavePath?, properties?: {field: value}}; id is the local name connections use, type is a soundcue_add_node nodeType")).Items(EType::Object),
		MCPParam::Optional(TEXT("connections"), EType::Array, TEXT("Links, each {child, parent?, index?}; an omitted or 'root' parent makes the child the cue root, and index defaults to append")).Items(EType::Object),
		MCPParam::Optional(TEXT("root"), EType::String, TEXT("Local id of the node to make the cue root, overriding the connections")),
	}, MCPSpec::ContractExempt(TEXT("Creates and saves a SoundCue asset named by the contract values")));
	Registry.RegisterHandler(TEXT("soundcue_add_node"), &SoundCueAddNode, {
		MCPParam::Required(TEXT("cuePath"), EType::String, TEXT("SoundCue asset path")).Alias(TEXT("assetPath")),
		MCPParam::Required(TEXT("nodeType"), EType::String, TEXT("wave_player | mixer | random | modulator | attenuation | looping | concatenator | delay | switch")),
		MCPParam::Optional(TEXT("soundWavePath"), EType::String, TEXT("SoundWave for a wave_player node")),
		MCPParam::Optional(TEXT("properties"), EType::Object, TEXT("Node-specific fields to set, as {property: value}")),
	});
	Registry.RegisterHandler(TEXT("soundcue_connect"), &SoundCueConnect, {
		MCPParam::Required(TEXT("cuePath"), EType::String, TEXT("SoundCue asset path")).Alias(TEXT("assetPath")),
		MCPParam::Required(TEXT("childNodeId"), EType::String, TEXT("Node to attach")),
		MCPParam::Optional(TEXT("parentNodeId"), EType::String, TEXT("Parent node (omit to make the child the cue root)")),
		MCPParam::Optional(TEXT("childIndex"), EType::Integer, TEXT("Slot under the parent (default append)")),
	});
	Registry.RegisterHandler(TEXT("soundcue_get_graph"), &SoundCueGetGraph, {
		MCPParam::Required(TEXT("cuePath"), EType::String, TEXT("SoundCue asset path")).Alias(TEXT("assetPath")),
	});

	// Mixing + routing + spatialization (AudioHandlers_Mixing.cpp)
	Registry.RegisterHandler(TEXT("create_submix"), &CreateSubmix, {
		MCPParam::Required(TEXT("name"), EType::String, TEXT("SoundSubmix asset name")),
		MCPParam::Optional(TEXT("packagePath"), EType::String, TEXT("Destination folder (default /Game/Audio/Submixes)")),
		MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("skip (default) returns the existing asset, error refuses; it never overwrites")),
		MCPParam::Optional(TEXT("parentPath"), EType::String, TEXT("Parent submix (default none)")),
		MCPParam::Optional(TEXT("outputVolume"), EType::Number, TEXT("Output volume")),
		MCPParam::Optional(TEXT("wetLevel"), EType::Number, TEXT("Wet level")),
		MCPParam::Optional(TEXT("dryLevel"), EType::Number, TEXT("Dry level")),
	}, MCPSpec::ContractExempt(TEXT("Creates and saves a SoundSubmix asset named by the contract values")));
	Registry.RegisterHandler(TEXT("set_submix_parent"), &SetSubmixParent, {
		MCPParam::Required(TEXT("submixPath"), EType::String, TEXT("SoundSubmix to reparent")),
		MCPParam::Optional(TEXT("parentPath"), EType::String, TEXT("New parent submix (empty detaches to root)")),
	});
	Registry.RegisterHandler(TEXT("add_submix_effect"), &AddSubmixEffect, {
		MCPParam::Required(TEXT("submixPath"), EType::String, TEXT("SoundSubmix whose effect chain to append to")),
		MCPParam::Required(TEXT("effectType"), EType::String, TEXT("reverb | eq | dynamics | filter | delay")),
		MCPParam::Optional(TEXT("name"), EType::String, TEXT("Preset asset name (default <submix>_<effectType>)")),
		MCPParam::Optional(TEXT("packagePath"), EType::String, TEXT("Preset folder (default /Game/Audio/SubmixEffects)")),
		MCPParam::Optional(TEXT("settings"), EType::Object, TEXT("Effect Settings struct as JSON")),
	});
	Registry.RegisterHandler(TEXT("create_sound_class"), &CreateSoundClass, {
		MCPParam::Required(TEXT("name"), EType::String, TEXT("SoundClass asset name")),
		MCPParam::Optional(TEXT("packagePath"), EType::String, TEXT("Destination folder (default /Game/Audio/SoundClasses)")),
		MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("skip (default) returns the existing asset, error refuses; it never overwrites")),
		MCPParam::Optional(TEXT("properties"), EType::Object, TEXT("FSoundClassProperties as JSON: Volume, Pitch, bIsUISound, ...")),
		MCPParam::Optional(TEXT("parentPath"), EType::String, TEXT("Parent SoundClass (default none)")),
	}, MCPSpec::ContractExempt(TEXT("Creates and saves a SoundClass asset named by the contract values")));
	Registry.RegisterHandler(TEXT("create_sound_mix"), &CreateSoundMix, {
		MCPParam::Required(TEXT("name"), EType::String, TEXT("SoundMix asset name")),
		MCPParam::Optional(TEXT("packagePath"), EType::String, TEXT("Destination folder (default /Game/Audio/SoundMixes)")),
		MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("skip (default) returns the existing asset, error refuses; it never overwrites")),
		MCPParam::Optional(TEXT("fadeInTime"), EType::Number, TEXT("Fade-in time in seconds")),
		MCPParam::Optional(TEXT("fadeOutTime"), EType::Number, TEXT("Fade-out time in seconds")),
		MCPParam::Optional(TEXT("adjusters"), EType::Array, TEXT("Per-SoundClass adjustments; an entry whose class does not load is skipped")).Items(EType::Object).WithFields({
			MCPParam::RequiredField(TEXT("soundClassPath"), EType::String, TEXT("SoundClass asset the adjustment applies to")),
			MCPParam::OptionalField(TEXT("volumeAdjuster"), EType::Number, TEXT("Volume multiplier (default 1)")),
			MCPParam::OptionalField(TEXT("pitchAdjuster"), EType::Number, TEXT("Pitch multiplier (default 1)")),
			MCPParam::OptionalField(TEXT("applyToChildren"), EType::Boolean, TEXT("Apply to child sound classes (default false)")),
		}),
	}, MCPSpec::ContractExempt(TEXT("Creates and saves a SoundMix asset named by the contract values")));
	Registry.RegisterHandler(TEXT("create_concurrency"), &CreateConcurrency, {
		MCPParam::Required(TEXT("name"), EType::String, TEXT("SoundConcurrency asset name")),
		MCPParam::Optional(TEXT("packagePath"), EType::String, TEXT("Destination folder (default /Game/Audio/Concurrency)")),
		MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("skip (default) returns the existing asset, error refuses; it never overwrites")),
		MCPParam::Optional(TEXT("maxCount"), EType::Integer, TEXT("Most concurrent voices")),
		MCPParam::Optional(TEXT("limitToOwner"), EType::Boolean, TEXT("Count voices per owning actor")),
		MCPParam::Optional(TEXT("volumeScale"), EType::Number, TEXT("Volume scale applied to each new voice")),
		MCPParam::Optional(TEXT("resolutionRule"), EType::String, TEXT("EMaxConcurrentResolutionRule name, e.g. StopFarthestThenOldest")),
	}, MCPSpec::ContractExempt(TEXT("Creates and saves a SoundConcurrency asset named by the contract values")));
	Registry.RegisterHandler(TEXT("create_attenuation"), &CreateAttenuation, {
		MCPParam::Required(TEXT("name"), EType::String, TEXT("SoundAttenuation asset name")),
		MCPParam::Optional(TEXT("packagePath"), EType::String, TEXT("Destination folder (default /Game/Audio/Attenuation)")),
		MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("skip (default) returns the existing asset, error refuses; it never overwrites")),
		MCPParam::Optional(TEXT("settings"), EType::Object, TEXT("FSoundAttenuationSettings as JSON, applied before the shortcuts")),
		MCPParam::Optional(TEXT("falloffDistance"), EType::Number, TEXT("Falloff distance; also turns volume attenuation on")),
		MCPParam::Optional(TEXT("spatialize"), EType::Boolean, TEXT("bSpatialize")),
		MCPParam::Optional(TEXT("enableOcclusion"), EType::Boolean, TEXT("bEnableOcclusion")),
	}, MCPSpec::ContractExempt(TEXT("Creates and saves a SoundAttenuation asset named by the contract values")));
	Registry.RegisterHandler(TEXT("set_sound_submix"), &SetSoundSubmix, {
		MCPParam::Required(TEXT("soundPath"), EType::String, TEXT("Sound asset path")).Alias(TEXT("assetPath")),
		MCPParam::Optional(TEXT("submixPath"), EType::String, TEXT("Base submix to route to (empty detaches)")),
	});
	Registry.RegisterHandler(TEXT("add_sound_submix_send"), &AddSoundSubmixSend, {
		MCPParam::Required(TEXT("soundPath"), EType::String, TEXT("Sound asset path")).Alias(TEXT("assetPath")),
		MCPParam::Required(TEXT("submixPath"), EType::String, TEXT("Submix to send to")),
		MCPParam::Optional(TEXT("sendLevel"), EType::Number, TEXT("Send level (default 1.0)")),
	});
	Registry.RegisterHandler(TEXT("set_sound_class"), &SetSoundClass, {
		MCPParam::Required(TEXT("soundPath"), EType::String, TEXT("Sound asset path")).Alias(TEXT("assetPath")),
		MCPParam::Required(TEXT("soundClassPath"), EType::String, TEXT("SoundClass to assign")),
	});
	Registry.RegisterHandler(TEXT("set_sound_attenuation"), &SetSoundAttenuation, {
		MCPParam::Required(TEXT("soundPath"), EType::String, TEXT("Sound asset path")).Alias(TEXT("assetPath")),
		MCPParam::Optional(TEXT("attenuationPath"), EType::String, TEXT("SoundAttenuation to attach (empty clears)")),
	});
	Registry.RegisterHandler(TEXT("set_sound_concurrency"), &SetSoundConcurrency, {
		MCPParam::Required(TEXT("soundPath"), EType::String, TEXT("Sound asset path")).Alias(TEXT("assetPath")),
		MCPParam::Optional(TEXT("concurrencyPath"), EType::String, TEXT("SoundConcurrency to attach (empty clears)")),
	});
	Registry.RegisterHandler(TEXT("set_audio_property"), &SetAudioProperty, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("Audio asset path")),
		MCPParam::Required(TEXT("propertyName"), EType::String, TEXT("UPROPERTY name, dotted for nested structs")),
		MCPParam::Required(TEXT("value"), EType::Any, TEXT("Value as JSON: scalars, structs, arrays, object paths, or UE export text")),
	});

	// Authoring depth (AudioHandlers_GraphEdit.cpp)
	Registry.RegisterHandler(TEXT("metasound_remove_node"), &MetaSoundRemoveNode, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("MetaSound asset path")).Alias(TEXT("metasoundPath")),
		MCPParam::Required(TEXT("nodeId"), EType::String, TEXT("Node id to remove")),
		MCPParam::Optional(TEXT("removeUnusedDependencies"), EType::Boolean, TEXT("Also drop node classes the graph no longer references (default true)")),
	});
	Registry.RegisterHandler(TEXT("metasound_disconnect"), &MetaSoundDisconnect, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("MetaSound asset path")).Alias(TEXT("metasoundPath")),
		MCPParam::Optional(TEXT("fromNodeId"), EType::String, TEXT("Source node id, with fromOutput")),
		MCPParam::Optional(TEXT("fromOutput"), EType::String, TEXT("Output vertex name on fromNodeId")),
		MCPParam::Optional(TEXT("toNodeId"), EType::String, TEXT("Destination node id, with toInput")),
		MCPParam::Optional(TEXT("toInput"), EType::String, TEXT("Input vertex name on toNodeId")),
		MCPParam::Optional(TEXT("graphOutput"), EType::String, TEXT("Graph output to clear, audio outputs included (Out Mono, Out Left, Out Right)")),
	});
	Registry.RegisterHandler(TEXT("metasound_remove_member"), &MetaSoundRemoveMember, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("MetaSound asset path")).Alias(TEXT("metasoundPath")),
		MCPParam::Required(TEXT("memberKind"), EType::String, TEXT("input | output | variable")),
		MCPParam::Required(TEXT("name"), EType::String, TEXT("Member name to remove")),
	});
	Registry.RegisterHandler(TEXT("metasound_rename_member"), &MetaSoundRenameMember, {
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("MetaSound asset path")).Alias(TEXT("metasoundPath")),
		MCPParam::Required(TEXT("memberKind"), EType::String, TEXT("input | output")),
		MCPParam::Required(TEXT("name"), EType::String, TEXT("Current graph input or output name")),
		MCPParam::Required(TEXT("newName"), EType::String, TEXT("Name to rename it to")),
	});
	Registry.RegisterHandler(TEXT("soundcue_remove_node"), &SoundCueRemoveNode, {
		MCPParam::Required(TEXT("cuePath"), EType::String, TEXT("SoundCue asset path")).Alias(TEXT("assetPath")),
		MCPParam::Required(TEXT("nodeId"), EType::String, TEXT("Node to remove")),
	});
	Registry.RegisterHandler(TEXT("soundcue_disconnect"), &SoundCueDisconnect, {
		MCPParam::Required(TEXT("cuePath"), EType::String, TEXT("SoundCue asset path")).Alias(TEXT("assetPath")),
		MCPParam::Optional(TEXT("childNodeId"), EType::String, TEXT("Child to detach")),
		MCPParam::Optional(TEXT("parentNodeId"), EType::String, TEXT("Detach from this parent only (default every parent)")),
		MCPParam::Optional(TEXT("clearRoot"), EType::Boolean, TEXT("Unset the cue root instead of detaching a parent link")),
	});
	Registry.RegisterHandler(TEXT("set_sound_class_parent"), &SetSoundClassParent, {
		MCPParam::Required(TEXT("soundClassPath"), EType::String, TEXT("SoundClass to reparent")).Alias(TEXT("assetPath")),
		MCPParam::Optional(TEXT("parentPath"), EType::String, TEXT("New parent SoundClass (empty detaches to the root)")),
	});
	Registry.RegisterHandler(TEXT("read_sound_routing"), &ReadSoundRouting, {
		MCPParam::Required(TEXT("soundPath"), EType::String, TEXT("Sound asset path")).Alias(TEXT("assetPath")),
	});
}

// #664: import a WAV/OGG/FLAC file as a USoundWave. Passing a null factory lets
// AssetTools auto-select the sound-import factory from the file extension.
TSharedPtr<FJsonValue> FAudioHandlers::ImportAudio(const TSharedPtr<FJsonObject>& Params)
{
	// Every parameter is read before the file check can fail; filename,
	// assetName and destinationPath are aliases the registry resolves (#1057).
	FString FileName;
	if (auto Err = RequireString(Params, TEXT("filePath"), FileName)) return Err;
	const FString AssetName = OptionalString(Params, TEXT("name"));
	FString DestinationPath = OptionalString(Params, TEXT("packagePath"));
	if (DestinationPath.IsEmpty()) DestinationPath = TEXT("/Game/Audio");
	const bool bHasLooping = HasParam(Params, TEXT("looping"));
	const bool bLooping = OptionalBool(Params, TEXT("looping"), false);
	const bool bReplaceExisting = OptionalBool(Params, TEXT("replaceExisting"), true);

	if (!FPaths::FileExists(FileName))
	{
		return MCPError(FString::Printf(TEXT("File not found: %s"), *FileName));
	}

	UAssetImportTask* Task = NewObject<UAssetImportTask>();
	FGCRootScope TaskRoot(Task);
	Task->bAutomated = true;
	Task->bReplaceExisting = bReplaceExisting;
	Task->bSave = false;
	Task->Filename = FileName;
	Task->DestinationPath = DestinationPath;
	// Factory left null: AssetTools resolves USoundFactory for wav/ogg/flac.
	if (!AssetName.IsEmpty()) Task->DestinationName = AssetName;

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	TArray<UAssetImportTask*> Tasks;
	Tasks.Add(Task);
	AssetToolsModule.Get().ImportAssetTasks(Tasks);

	TArray<TSharedPtr<FJsonValue>> ImportedPaths;
	USoundWave* ImportedWave = nullptr;
	for (UObject* Obj : Task->GetObjects())
	{
		if (!Obj) continue;
		ImportedPaths.Add(MakeShared<FJsonValueString>(Obj->GetPathName()));
		if (!ImportedWave) ImportedWave = Cast<USoundWave>(Obj);
	}

	// Optional looping toggle on the resulting SoundWave.
	if (ImportedWave && bHasLooping)
	{
		ImportedWave->bLooping = bLooping;
	}

	auto Result = MCPSuccess();
	if (ImportedPaths.Num() > 0) MCPSetCreated(Result);
	Result->SetStringField(TEXT("filename"), FileName);
	Result->SetStringField(TEXT("destinationPath"), DestinationPath);
	Result->SetArrayField(TEXT("importedAssets"), ImportedPaths);
	Result->SetNumberField(TEXT("importedCount"), ImportedPaths.Num());
	Result->SetBoolField(TEXT("success"), ImportedPaths.Num() > 0);
	if (ImportedWave)
	{
		Result->SetNumberField(TEXT("durationSeconds"), ImportedWave->GetDuration());
		Result->SetNumberField(TEXT("numChannels"), ImportedWave->NumChannels);
		Result->SetBoolField(TEXT("looping"), ImportedWave->bLooping);
	}
	if (ImportedPaths.Num() == 0)
	{
		Result->SetStringField(TEXT("error"), TEXT("Import task completed but no SoundWave was produced (unsupported format?)"));
	}
	else if (ImportedPaths.Num() == 1)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), ImportedPaths[0]->AsString());
		MCPSetRollback(Result, TEXT("delete_asset"), Payload);
	}
	if (ImportedWave && bHasLooping)
	{
		MCPAudio::SaveAndNote(Result, { ImportedWave });
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAudioHandlers::ListSoundAssets(const TSharedPtr<FJsonObject>& Params)
{
	auto Result = MCPSuccess();

	// #730: the old implementation ignored `directory`, had no result cap, and
	// serialized SoundWave + SoundCue + MetaSoundSource for the whole project in
	// one response. On projects with hundreds of SoundWaves that response could
	// exceed the WebSocket framing threshold and drop the bridge. Honor the
	// directory, filter recursively via a single FARFilter query, and paginate.
	const FString Directory = OptionalString(Params, TEXT("directory"), TEXT("/Game"));
	const bool bRecursive = OptionalBool(Params, TEXT("recursive"), true);
	// Noted before the offset refusal can return, so every declared parameter
	// is read (#1057). ReadPageRequest reads both for real below.
	HasParam(Params, TEXT("cursor"));
	HasParam(Params, TEXT("limit"));

	// T3: the row offset #730 introduced is replaced by the shared cursor. An
	// offset re-read a moved library at a row number and could not tell that it
	// had moved; a cursor names the row it resumes after and says so when that
	// row shifted or was deleted.
	if (Params.IsValid() && HasParam(Params, TEXT("offset")))
	{
		return MCPError(TEXT(
			"'offset' is no longer how list_sound_assets pages, because a row number cannot tell you "
			"the library changed underneath it. Pass the 'nextCursor' this action returned as 'cursor', "
			"and size the page with 'limit' (1 to 5000, default 1000). Omit both for the first page."));
	}
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_sound_assets|directory=%s|recursive=%d"), *Directory, bRecursive ? 1 : 0),
			/*DefaultLimit*/ 1000, /*MaxLimit*/ 5000, Page))
	{
		return Err;
	}

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("SoundWave")));
	Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("SoundCue")));
	Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/MetasoundEngine"), TEXT("MetaSoundSource")));
	Filter.bRecursiveClasses = true;
	Filter.PackagePaths.Add(FName(*Directory));
	Filter.bRecursivePaths = bRecursive;

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);

	// Stable ordering so paging is deterministic across calls: the asset
	// registry does not promise an enumeration order, and a cursor over an
	// unordered enumeration cannot find its anchor again.
	AssetDataList.Sort([](const FAssetData& A, const FAssetData& B)
	{
		return A.GetObjectPathString() < B.GetObjectPathString();
	});

	TArray<MCPPagination::FPageRow> Rows;
	Rows.Reserve(AssetDataList.Num());
	for (const FAssetData& AssetData : AssetDataList)
	{
		const FString ObjectPath = AssetData.GetObjectPathString();
		TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
		AssetObj->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
		AssetObj->SetStringField(TEXT("path"), ObjectPath);
		AssetObj->SetStringField(TEXT("class"), AssetData.AssetClassPath.GetAssetName().ToString());
		AssetObj->SetStringField(TEXT("packagePath"), AssetData.PackagePath.ToString());
		// The object path is the anchor: unique across the project, and the
		// same string the next enumeration produces for that sound.
		Rows.Add({ ObjectPath, MakeShared<FJsonValueObject>(AssetObj) });
	}

	Result->SetStringField(TEXT("directory"), Directory);
	MCPPagination::EmitPage(Page, Rows, TEXT("assets"), Result);

	return MCPResult(Result);
}

// #729: decode a USoundWave's imported audio to in-memory PCM. UE Python does
// not expose USoundWave::GetImportedSoundWaveData, so a semantic-search pipeline
// (CLAP etc.) previously had no way to reach the samples without relying on the
// original import file, which may have moved. This returns interleaved signed
// 16-bit PCM, base64-encoded, plus the format metadata needed to feed a model.
TSharedPtr<FJsonValue> FAudioHandlers::ExtractSoundWavePCM(const TSharedPtr<FJsonObject>& Params)
{
	FString SoundPath;
	if (auto Err = RequireString(Params, TEXT("soundPath"), SoundPath)) return Err;
	// Every parameter is read before anything can fail (#1057).
	const double MaxSeconds = OptionalNumber(Params, TEXT("maxSeconds"), 0.0);
	const bool bDownmix = OptionalBool(Params, TEXT("downmixMono"), false);

	USoundWave* Wave = LoadAssetByPath<USoundWave>(SoundPath);
	if (!Wave)
	{
		return MCPAssetLoadError(SoundPath, TEXT("SoundWave"));
	}

#if WITH_EDITOR
	TArray<uint8> RawPCM;
	uint32 SampleRate = 0;
	uint16 NumChannels = 0;
	if (!Wave->GetImportedSoundWaveData(RawPCM, SampleRate, NumChannels)
		|| RawPCM.Num() == 0 || SampleRate == 0 || NumChannels == 0)
	{
		return MCPError(TEXT("Failed to decode imported SoundWave data (no editor source data available for this asset)"));
	}

	// RawPCM is interleaved signed 16-bit little-endian across NumChannels.
	int32 TotalFrames = (RawPCM.Num() / (int32)sizeof(int16)) / NumChannels;

	// Optional decode window so callers can bound the response size (CLAP-style
	// pipelines only need a few seconds). Default is the whole asset.
	if (MaxSeconds > 0.0)
	{
		const int32 FrameCap = FMath::Clamp(FMath::FloorToInt(MaxSeconds * (double)SampleRate), 0, TotalFrames);
		TotalFrames = FrameCap;
	}

	const int16* Samples = reinterpret_cast<const int16*>(RawPCM.GetData());

	TArray<uint8> OutBytes;
	int32 OutChannels = NumChannels;
	if (bDownmix && NumChannels > 1)
	{
		OutChannels = 1;
		OutBytes.SetNumUninitialized(TotalFrames * (int32)sizeof(int16));
		int16* Dst = reinterpret_cast<int16*>(OutBytes.GetData());
		for (int32 Frame = 0; Frame < TotalFrames; ++Frame)
		{
			int32 Acc = 0;
			for (int32 Ch = 0; Ch < NumChannels; ++Ch)
			{
				Acc += Samples[Frame * NumChannels + Ch];
			}
			Dst[Frame] = static_cast<int16>(Acc / NumChannels);
		}
	}
	else
	{
		const int32 ByteCount = TotalFrames * NumChannels * (int32)sizeof(int16);
		OutBytes.Append(RawPCM.GetData(), ByteCount);
	}

	const FString Base64 = FBase64::Encode(OutBytes);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("soundPath"), SoundPath);
	Result->SetNumberField(TEXT("sampleRate"), static_cast<double>(SampleRate));
	Result->SetNumberField(TEXT("numChannels"), static_cast<double>(OutChannels));
	Result->SetNumberField(TEXT("numFrames"), static_cast<double>(TotalFrames));
	Result->SetNumberField(TEXT("durationSeconds"), SampleRate > 0 ? static_cast<double>(TotalFrames) / static_cast<double>(SampleRate) : 0.0);
	Result->SetStringField(TEXT("format"), TEXT("pcm_s16le"));
	Result->SetStringField(TEXT("pcmBase64"), Base64);
	return MCPResult(Result);
#else
	return MCPError(TEXT("extract_sound_wave_pcm requires an editor build"));
#endif
}

TSharedPtr<FJsonValue> FAudioHandlers::CreateSoundCue(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Audio/SoundCues"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	USoundCueFactoryNew* SoundCueFactory = NewObject<USoundCueFactoryNew>();
	auto Created = MCPCreateAssetIdempotent<USoundCue>(Name, PackagePath, OnConflict, TEXT("SoundCue"), SoundCueFactory);
	if (Created.EarlyReturn) return Created.EarlyReturn;

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	MCPAudio::SaveAndNote(Result, { Created.Asset });
	Result->SetStringField(TEXT("path"), Created.Asset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	MCPSetDeleteAssetRollback(Result, Created.Asset->GetPathName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAudioHandlers::PlaySoundAtLocation(const TSharedPtr<FJsonObject>& Params)
{
	// Every parameter is read before anything can fail; volume and pitch are
	// aliases the registry resolves (#1057).
	FString SoundPath;
	if (auto Err = RequireString(Params, TEXT("soundPath"), SoundPath)) return Err;
	const FVector Location = OptionalVec3(Params, TEXT("location"));
	const double Volume = OptionalNumber(Params, TEXT("volumeMultiplier"), 1.0);
	const double Pitch = OptionalNumber(Params, TEXT("pitchMultiplier"), 1.0);

	USoundBase* Sound = LoadAssetByPath<USoundBase>(SoundPath);
	if (!Sound)
	{
		return MCPAssetLoadError(SoundPath, TEXT("SoundBase"));
	}

	REQUIRE_EDITOR_WORLD(World);

	// No rollback: destructive/external - playing a one-shot sound has no inverse.
	// Replays produce a new audible event; not natural-key idempotent.
	UGameplayStatics::PlaySoundAtLocation(World, Sound, Location, static_cast<float>(Volume), static_cast<float>(Pitch));

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), SoundPath);
	// Stated in the result, not just in the comment above, so a flow can read
	// the answer rather than infer it from a missing field.
	MCPSetNoRollback(Result,
		TEXT("A one-shot sound is an event, not a state. It is already audible by the time this returns and nothing changed on disk or ")
		TEXT("in the level, so there is nothing to undo and no action that would undo it. Calling again plays it again."));

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FAudioHandlers::SpawnAmbientSound(const TSharedPtr<FJsonObject>& Params)
{
	// Every parameter is read, and the sound loaded, before anything is spawned:
	// an AmbientSound with no sound plays nothing (#1057).
	FString SoundPath;
	if (auto Err = RequireString(Params, TEXT("soundPath"), SoundPath)) return Err;
	const FVector Location = OptionalVec3(Params, TEXT("location"));
	const FString Label = OptionalString(Params, TEXT("label"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	double Volume = 1.0;
	const bool bHasVolume = TryGetNumberParam(Params, TEXT("volumeMultiplier"), Volume);

	USoundBase* Sound = LoadAssetByPath<USoundBase>(SoundPath);
	if (!Sound)
	{
		return MCPAssetLoadError(SoundPath, TEXT("SoundBase"));
	}

	REQUIRE_EDITOR_WORLD(World);

	if (auto Existing = MCPCheckActorLabelExists(World, Label, OnConflict, TEXT("AmbientSound")))
	{
		return Existing;
	}

	FTransform SpawnTransform(FRotator::ZeroRotator, Location);
	AAmbientSound* AmbientSoundActor = World->SpawnActor<AAmbientSound>(AAmbientSound::StaticClass(), SpawnTransform);
	if (!AmbientSoundActor)
	{
		return MCPError(TEXT("Failed to spawn AmbientSound actor"));
	}

	if (!Label.IsEmpty())
	{
		AmbientSoundActor->SetActorLabel(Label);
	}

	if (UAudioComponent* AudioComp = AmbientSoundActor->GetAudioComponent())
	{
		AudioComp->SetSound(Sound);
		if (bHasVolume)
		{
			AudioComp->VolumeMultiplier = static_cast<float>(Volume);
		}
	}

	const FString FinalLabel = AmbientSoundActor->GetActorLabel();

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), SoundPath);
	Result->SetStringField(TEXT("label"), FinalLabel);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), FinalLabel);
	MCPSetRollback(Result, TEXT("delete_actor"), Payload);

	return MCPResult(Result);
}
