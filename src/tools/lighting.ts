import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const lightingTools: ToolDef[] = [
  bt("spawn_light", "Spawn a light actor (point, spot, directional, rect, sky).", {
    lightType: z.string().describe("Light type: 'point', 'spot', 'directional', 'rect', 'sky'"),
    location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
    rotation: z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() }).optional(),
    intensity: z.number().optional(), color: z.object({ r: z.number(), g: z.number(), b: z.number() }).optional(),
    label: z.string().optional(),
  }),
  bt("set_light_properties", "Set properties on a light (intensity, color, temperature, shadows, cone angles).", {
    actorLabel: z.string(), intensity: z.number().optional(),
    color: z.object({ r: z.number(), g: z.number(), b: z.number() }).optional(),
    temperature: z.number().optional(), castShadows: z.boolean().optional(),
    attenuationRadius: z.number().optional(),
    innerConeAngle: z.number().optional(), outerConeAngle: z.number().optional(),
  }),
  bt("build_lighting", "Trigger a lighting build with specified quality.", {
    quality: z.string().optional().describe("Quality: 'preview', 'medium', 'high', 'production'"),
  }),
];
