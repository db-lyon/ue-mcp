import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const volumeTools: ToolDef[] = [
  bt("spawn_volume", "Spawn a volume actor (trigger, blocking, post-process, nav modifier, etc.).", {
    volumeType: z.string().describe("Volume type: 'trigger', 'blocking', 'postprocess', 'navmodifier', etc."),
    location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
    extent: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
    label: z.string().optional(),
  }),
  bt("list_volumes", "List all volume actors in the level.", {
    volumeType: z.string().optional().describe("Filter by volume type"),
  }),
  bt("set_volume_properties", "Set properties on a volume actor.", {
    actorLabel: z.string(), properties: z.record(z.unknown()),
  }),
];
