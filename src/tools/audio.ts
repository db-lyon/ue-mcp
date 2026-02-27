import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";

export const audioTool: ToolDef = categoryTool(
  "audio",
  "Audio: sound assets, playback, ambient sounds, SoundCues, MetaSounds.",
  {
    list:              bp("list_sound_assets"),
    play_at_location:  bp("play_sound_at_location"),
    spawn_ambient:     bp("spawn_ambient_sound"),
    create_cue:        bp("create_sound_cue"),
    create_metasound:  bp("create_metasound_source"),
  },
  `- list: List sound assets. Params: directory?, recursive?
- play_at_location: Play sound. Params: soundPath, location {x,y,z}, volumeMultiplier?, pitchMultiplier?
- spawn_ambient: Place ambient sound. Params: soundPath, location {x,y,z}, label?
- create_cue: Create SoundCue. Params: name, packagePath?, soundWavePath?
- create_metasound: Create MetaSoundSource. Params: name, packagePath?`,
  {
    directory: z.string().optional(), recursive: z.boolean().optional(),
    soundPath: z.string().optional(),
    location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
    volumeMultiplier: z.number().optional(),
    pitchMultiplier: z.number().optional(),
    label: z.string().optional(),
    name: z.string().optional(),
    packagePath: z.string().optional(),
    soundWavePath: z.string().optional(),
  },
);
