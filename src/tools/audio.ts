import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const audioTools: ToolDef[] = [
  bt("list_sound_assets", "List sound assets in a directory.", {
    directory: z.string().optional(), recursive: z.boolean().optional(),
  }),
  bt("play_sound_at_location", "Play a sound at a world location.", {
    soundPath: z.string(), location: z.object({ x: z.number(), y: z.number(), z: z.number() }),
    volumeMultiplier: z.number().optional(), pitchMultiplier: z.number().optional(),
  }),
  bt("spawn_ambient_sound", "Place an AmbientSound actor with a sound asset.", {
    soundPath: z.string(),
    location: z.object({ x: z.number(), y: z.number(), z: z.number() }),
    label: z.string().optional(),
  }),
  bt("create_sound_cue", "Create a new SoundCue asset, optionally wiring in a SoundWave.", {
    name: z.string(), packagePath: z.string().optional(),
    soundWavePath: z.string().optional(),
  }),
  bt("create_metasound_source", "Create a new MetaSoundSource asset.", {
    name: z.string(), packagePath: z.string().optional(),
  }),
];
