import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const sequencerTools: ToolDef[] = [
  bt("create_level_sequence", "Create a new Level Sequence asset.", {
    name: z.string(), packagePath: z.string().optional(),
  }),
  bt("get_sequence_info", "Get information about a Level Sequence: bindings, tracks, display rate.", {
    assetPath: z.string(),
  }),
  bt("add_sequence_track", "Add a track to a Level Sequence.", {
    assetPath: z.string(), trackType: z.string(), actorLabel: z.string().optional(),
  }),
  bt("play_sequence", "Play, stop, or pause a sequence in Sequencer.", {
    assetPath: z.string(), action: z.enum(["play", "stop", "pause"]),
  }),
];
