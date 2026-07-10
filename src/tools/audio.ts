import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";
import { Vec3 } from "../schemas.js";

export const audioTool: ToolDef = categoryTool(
  "audio",
  "Audio: sound assets, playback, ambient sounds, SoundCues, MetaSounds.",
  {
    list:              bp("List sound assets. Params: directory?, recursive?", "list_sound_assets"),
    import_audio:      bp("Import a WAV/OGG/FLAC file as a USoundWave. Returns durationSeconds, numChannels, looping. Params: filePath, name?, packagePath? (default /Game/Audio), looping?, replaceExisting? (default true) (#664)", "import_audio", (p) => ({ filePath: p.filePath, name: p.name, packagePath: p.packagePath, looping: p.looping, replaceExisting: p.replaceExisting })),
    play_at_location:  bp("Play sound. Params: soundPath, location, volumeMultiplier?, pitchMultiplier?", "play_sound_at_location"),
    spawn_ambient:     bp("Place ambient sound. Params: soundPath, location, label?", "spawn_ambient_sound"),
    create_cue:        bp("Create SoundCue. Params: name, packagePath?, soundWavePath?", "create_sound_cue"),
    create_metasound:  bp("Create MetaSoundSource. Params: name, packagePath?", "create_metasound_source"),
  },
  undefined,
  {
    directory: z.string().optional(), recursive: z.boolean().optional(),
    soundPath: z.string().optional(),
    location: Vec3.optional(),
    volumeMultiplier: z.number().optional(),
    pitchMultiplier: z.number().optional(),
    label: z.string().optional(),
    name: z.string().optional(),
    packagePath: z.string().optional(),
    soundWavePath: z.string().optional(),
    filePath: z.string().optional().describe("import_audio: path to a WAV/OGG/FLAC file (#664)"),
    looping: z.boolean().optional().describe("import_audio: set SoundWave bLooping (#664)"),
    replaceExisting: z.boolean().optional().describe("import_audio: replace an existing asset (default true) (#664)"),
  },
);
