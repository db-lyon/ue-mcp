import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";
import { Vec3 } from "../schemas.js";

/**
 * Audio: the full UE5 audio stack, authored end-to-end through the bridge.
 *
 *  - Assets + playback (import, cue/metasound creation, play, ambient).
 *  - MetaSound graph authoring (nodes, inputs/outputs, connections, defaults) via
 *    the MetaSound Builder subsystem - the modern UE5 audio graph.
 *  - SoundCue graph authoring (wave player, mixer, random, modulator, ...).
 *  - Mixing + routing: submixes and submix effect chains, sound classes, sound
 *    mixes, concurrency.
 *  - Spatialization: attenuation assets, and assigning submix/class/attenuation/
 *    concurrency onto sounds.
 *
 * Nothing here creates an empty placeholder: every asset is authorable to a
 * working state through these actions.
 */
export const audioTool: ToolDef = categoryTool(
  "audio",
  "Audio: sound assets, playback, MetaSound + SoundCue graph authoring, submixes/effects, sound classes/mixes, attenuation, concurrency, spatialization.",
  {
    // ── Assets + playback ──────────────────────────────────────────────
    list:              bp("List sound assets (SoundWave, SoundCue, MetaSoundSource). Params: directory?, recursive?", "list_sound_assets"),
    import_audio:      bp("Import a WAV/OGG/FLAC file as a USoundWave. Returns durationSeconds, numChannels, looping. Params: filePath, name?, packagePath? (default /Game/Audio), looping?, replaceExisting? (default true)", "import_audio", (p) => ({ filePath: p.filePath, name: p.name, packagePath: p.packagePath, looping: p.looping, replaceExisting: p.replaceExisting })),
    play_at_location:  bp("Play a sound in the editor world. Params: soundPath, location, volumeMultiplier?, pitchMultiplier?", "play_sound_at_location"),
    spawn_ambient:     bp("Place an AmbientSound actor. Params: soundPath, location, label?", "spawn_ambient_sound"),

    // ── MetaSound ──────────────────────────────────────────────────────
    create_metasound:  bp("Create a MetaSoundSource and open a builder session for authoring. Params: name, packagePath? (default /Game/Audio/MetaSounds), format? ('mono'|'stereo', default mono), oneShot? (default true). Returns assetPath. Author with metasound_* then metasound_build.", "create_metasound_source", (p) => ({ name: p.name, packagePath: p.packagePath, format: p.format, oneShot: p.oneShot, onConflict: p.onConflict })),
    metasound_list_node_classes: bp("List common MetaSound node classes to add (name, namespace, variant, notes). Params: filter? (substring).", "metasound_list_node_classes", (p) => ({ filter: p.filter })),
    metasound_get_graph:         bp("Report a MetaSound's builder-session state (active builder, audio outputs, oneShot). Params: assetPath.", "metasound_get_graph", (p) => ({ assetPath: p.assetPath ?? p.metasoundPath })),
    metasound_add_node:          bp("Add a node to a MetaSound graph by registered class name. Returns nodeId (+ input/output counts). Params: assetPath, nodeClassName (e.g. 'Sine'), nodeNamespace? (default 'UE'), nodeVariant? (e.g. 'Audio'), majorVersion? (default 1).", "metasound_add_node", (p) => ({ assetPath: p.assetPath ?? p.metasoundPath, nodeClassName: p.nodeClassName, nodeNamespace: p.nodeNamespace, nodeVariant: p.nodeVariant, majorVersion: p.majorVersion })),
    metasound_add_input:         bp("Add a graph input to a MetaSound. Params: assetPath, name, dataType ('Float'|'Int32'|'Bool'|'String'|'Trigger'|'Audio'|'Time'|...), defaultValue?.", "metasound_add_graph_input", (p) => ({ assetPath: p.assetPath ?? p.metasoundPath, name: p.name, dataType: p.dataType, defaultValue: p.defaultValue })),
    metasound_add_output:        bp("Add a graph output to a MetaSound. Params: assetPath, name, dataType.", "metasound_add_graph_output", (p) => ({ assetPath: p.assetPath ?? p.metasoundPath, name: p.name, dataType: p.dataType })),
    metasound_connect:           bp("Connect one node's output vertex to another node's input vertex. Params: assetPath, fromNodeId, fromOutput (vertex name), toNodeId, toInput (vertex name).", "metasound_connect", (p) => ({ assetPath: p.assetPath ?? p.metasoundPath, fromNodeId: p.fromNodeId, fromOutput: p.fromOutput, toNodeId: p.toNodeId, toInput: p.toInput })),
    metasound_connect_input:     bp("Connect a graph input to a node input vertex. Params: assetPath, graphInput (name), toNodeId, toInput (vertex name).", "metasound_connect_graph_input", (p) => ({ assetPath: p.assetPath ?? p.metasoundPath, graphInput: p.graphInput, toNodeId: p.toNodeId, toInput: p.toInput })),
    metasound_connect_output:    bp("Connect a node output vertex to a graph output. Params: assetPath, fromNodeId, fromOutput (vertex name), graphOutput (name).", "metasound_connect_graph_output", (p) => ({ assetPath: p.assetPath ?? p.metasoundPath, fromNodeId: p.fromNodeId, fromOutput: p.fromOutput, graphOutput: p.graphOutput })),
    metasound_connect_audio_out: bp("Connect a node output vertex to the source's audio output. Params: assetPath, fromNodeId, fromOutput (vertex name, must be Audio type), channel? (0=left/mono, 1=right; default 0).", "metasound_connect_audio_out", (p) => ({ assetPath: p.assetPath ?? p.metasoundPath, fromNodeId: p.fromNodeId, fromOutput: p.fromOutput, channel: p.channel })),
    metasound_set_default:       bp("Set a default value on a node input vertex, or on a graph input. Params: assetPath, value (required), dataType? (Float|Int32|Bool|String hint), then EITHER (nodeId + inputName) OR graphInput.", "metasound_set_input_default", (p) => ({ assetPath: p.assetPath ?? p.metasoundPath, value: p.value, dataType: p.dataType, nodeId: p.nodeId, inputName: p.inputName, graphInput: p.graphInput })),
    metasound_build:             bp("Write the builder document to the MetaSound asset and save. Call after authoring. Params: assetPath.", "metasound_build", (p) => ({ assetPath: p.assetPath ?? p.metasoundPath })),

    // ── SoundCue graph ─────────────────────────────────────────────────
    create_cue:        bp("Create a SoundCue, optionally seeded from a wave. Params: name, packagePath?, soundWavePath?.", "create_sound_cue"),
    cue_add_node:      bp("Add a node to a SoundCue graph. Returns nodeId. Params: cuePath, nodeType ('wave_player'|'mixer'|'random'|'modulator'|'attenuation'|'looping'|'concatenator'|'delay'|'switch'), soundWavePath? (wave_player), properties? (node-specific fields).", "soundcue_add_node", (p) => ({ cuePath: p.cuePath ?? p.assetPath, nodeType: p.nodeType, soundWavePath: p.soundWavePath, properties: p.properties })),
    cue_connect:       bp("Connect a SoundCue node as a child of another (or as the cue root). Params: cuePath, parentNodeId (omit for root), childNodeId, childIndex? (default append).", "soundcue_connect", (p) => ({ cuePath: p.cuePath ?? p.assetPath, parentNodeId: p.parentNodeId, childNodeId: p.childNodeId, childIndex: p.childIndex })),
    cue_get_graph:     bp("Read a SoundCue node graph: nodes (id, type, children) and root. Params: cuePath.", "soundcue_get_graph", (p) => ({ cuePath: p.cuePath ?? p.assetPath })),

    // ── Mixing + routing ───────────────────────────────────────────────
    create_submix:     bp("Create a USoundSubmix, optionally parented. Params: name, packagePath? (default /Game/Audio/Submixes), parentPath?, outputVolume?, wetLevel?, dryLevel?.", "create_submix", (p) => ({ name: p.name, packagePath: p.packagePath, parentPath: p.parentPath, outputVolume: p.outputVolume, wetLevel: p.wetLevel, dryLevel: p.dryLevel, onConflict: p.onConflict })),
    set_submix_parent: bp("Reparent a submix (sets ParentSubmix, updating both ends). Params: submixPath, parentPath (empty detaches to root).", "set_submix_parent", (p) => ({ submixPath: p.submixPath, parentPath: p.parentPath })),
    add_submix_effect: bp("Append a submix effect preset to a submix's effect chain (creates the preset asset). Params: submixPath, effectType ('reverb'|'eq'|'dynamics'|'filter'|'delay'), name?, packagePath?, settings? (effect Settings struct as JSON).", "add_submix_effect", (p) => ({ submixPath: p.submixPath, effectType: p.effectType, name: p.name, packagePath: p.packagePath, settings: p.settings })),
    create_sound_class: bp("Create a USoundClass, optionally parented, with properties. Params: name, packagePath? (default /Game/Audio/SoundClasses), parentPath?, properties? (FSoundClassProperties JSON: Volume, Pitch, bIsUISound, ...).", "create_sound_class", (p) => ({ name: p.name, packagePath: p.packagePath, parentPath: p.parentPath, properties: p.properties, onConflict: p.onConflict })),
    create_sound_mix:   bp("Create a USoundMix with sound-class adjusters. Params: name, packagePath? (default /Game/Audio/SoundMixes), adjusters? ([{soundClassPath, volumeAdjuster?, pitchAdjuster?, applyToChildren?}]), fadeInTime?, fadeOutTime?.", "create_sound_mix", (p) => ({ name: p.name, packagePath: p.packagePath, adjusters: p.adjusters, fadeInTime: p.fadeInTime, fadeOutTime: p.fadeOutTime, onConflict: p.onConflict })),
    create_concurrency: bp("Create a USoundConcurrency asset. Params: name, packagePath? (default /Game/Audio/Concurrency), maxCount?, limitToOwner?, resolutionRule? (e.g. 'StopFarthestThenOldest'), volumeScale?.", "create_concurrency", (p) => ({ name: p.name, packagePath: p.packagePath, maxCount: p.maxCount, limitToOwner: p.limitToOwner, resolutionRule: p.resolutionRule, volumeScale: p.volumeScale, onConflict: p.onConflict })),

    // ── Spatialization ─────────────────────────────────────────────────
    create_attenuation: bp("Create a USoundAttenuation asset. Params: name, packagePath? (default /Game/Audio/Attenuation), settings? (FSoundAttenuationSettings JSON), plus shortcuts: falloffDistance?, spatialize?, enableOcclusion?.", "create_attenuation", (p) => ({ name: p.name, packagePath: p.packagePath, settings: p.settings, falloffDistance: p.falloffDistance, spatialize: p.spatialize, enableOcclusion: p.enableOcclusion, onConflict: p.onConflict })),

    // ── Assign routing onto a sound ────────────────────────────────────
    set_sound_submix:      bp("Set a sound's base submix (routing target). Params: soundPath, submixPath (empty detaches).", "set_sound_submix", (p) => ({ soundPath: p.soundPath ?? p.assetPath, submixPath: p.submixPath })),
    add_sound_submix_send: bp("Add a submix send to a sound. Params: soundPath, submixPath, sendLevel? (default 1.0).", "add_sound_submix_send", (p) => ({ soundPath: p.soundPath ?? p.assetPath, submixPath: p.submixPath, sendLevel: p.sendLevel })),
    set_sound_class:       bp("Assign a sound class to a sound. Params: soundPath, soundClassPath.", "set_sound_class", (p) => ({ soundPath: p.soundPath ?? p.assetPath, soundClassPath: p.soundClassPath })),
    set_sound_attenuation: bp("Attach an attenuation asset to a sound. Params: soundPath, attenuationPath (empty clears).", "set_sound_attenuation", (p) => ({ soundPath: p.soundPath ?? p.assetPath, attenuationPath: p.attenuationPath })),
    set_sound_concurrency: bp("Attach a concurrency asset to a sound. Params: soundPath, concurrencyPath (empty clears).", "set_sound_concurrency", (p) => ({ soundPath: p.soundPath ?? p.assetPath, concurrencyPath: p.concurrencyPath })),

    // ── Generic property set (any audio asset) ─────────────────────────
    set_property:      bp("Set any UPROPERTY on an audio asset by (dotted) name, value as JSON. Handles nested structs, arrays, object refs. Params: assetPath, propertyName, value.", "set_audio_property", (p) => ({ assetPath: p.assetPath, propertyName: p.propertyName, value: p.value })),
  },
  undefined,
  {
    // shared / assets
    directory: z.string().optional(), recursive: z.boolean().optional(),
    soundPath: z.string().optional(),
    location: Vec3.optional(),
    volumeMultiplier: z.number().optional(),
    pitchMultiplier: z.number().optional(),
    label: z.string().optional(),
    name: z.string().optional(),
    packagePath: z.string().optional(),
    onConflict: z.string().optional().describe("skip|replace|rename when the asset name exists"),
    soundWavePath: z.string().optional(),
    filePath: z.string().optional().describe("import_audio: path to a WAV/OGG/FLAC file"),
    looping: z.boolean().optional().describe("import_audio: set SoundWave bLooping"),
    replaceExisting: z.boolean().optional().describe("import_audio: replace an existing asset (default true)"),
    assetPath: z.string().optional(),
    propertyName: z.string().optional(),
    value: z.any().optional().describe("JSON value for set_default / set_property"),

    // metasound
    metasoundPath: z.string().optional(),
    format: z.string().optional().describe("create_metasound: 'mono' | 'stereo'"),
    oneShot: z.boolean().optional(),
    filter: z.string().optional(),
    nodeClassName: z.string().optional(),
    nodeNamespace: z.string().optional(),
    nodeVariant: z.string().optional(),
    majorVersion: z.number().optional(),
    dataType: z.string().optional().describe("MetaSound data type: Float, Int32, Bool, String, Trigger, Audio, Time, ..."),
    defaultValue: z.any().optional(),
    fromNodeId: z.string().optional(),
    fromOutput: z.string().optional(),
    toNodeId: z.string().optional(),
    toInput: z.string().optional(),
    graphInput: z.string().optional(),
    graphOutput: z.string().optional(),
    channel: z.number().optional(),
    nodeId: z.string().optional(),
    inputName: z.string().optional(),

    // soundcue graph
    cuePath: z.string().optional(),
    nodeType: z.string().optional(),
    properties: z.record(z.any()).optional(),
    parentNodeId: z.string().optional(),
    childNodeId: z.string().optional(),
    childIndex: z.number().optional(),

    // mixing / routing
    parentPath: z.string().optional(),
    outputVolume: z.number().optional(),
    wetLevel: z.number().optional(),
    dryLevel: z.number().optional(),
    submixPath: z.string().optional(),
    effectType: z.string().optional(),
    settings: z.record(z.any()).optional(),
    adjusters: z.array(z.any()).optional(),
    fadeInTime: z.number().optional(),
    fadeOutTime: z.number().optional(),
    maxCount: z.number().optional(),
    limitToOwner: z.boolean().optional(),
    resolutionRule: z.string().optional(),
    volumeScale: z.number().optional(),

    // spatialization
    falloffDistance: z.number().optional(),
    spatialize: z.boolean().optional(),
    enableOcclusion: z.boolean().optional(),

    // assignment
    soundClassPath: z.string().optional(),
    attenuationPath: z.string().optional(),
    concurrencyPath: z.string().optional(),
    sendLevel: z.number().optional(),
  },
);
