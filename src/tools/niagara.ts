import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const niagaraTools: ToolDef[] = [
  bt("list_niagara_systems", "List Niagara System and Emitter assets in a directory.", {
    directory: z.string().optional(), recursive: z.boolean().optional(),
  }),
  bt("get_niagara_info", "Get information about a Niagara System: emitters, structure.", {
    assetPath: z.string(),
  }),
  bt("spawn_niagara_at_location", "Spawn a Niagara VFX system at a world location.", {
    systemPath: z.string().describe("Path to the Niagara System asset"),
    location: z.object({ x: z.number(), y: z.number(), z: z.number() }),
    rotation: z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() }).optional(),
    label: z.string().optional(),
  }),
  bt("set_niagara_parameter", "Set a parameter on a Niagara component.", {
    actorLabel: z.string(), parameterName: z.string(),
    value: z.unknown().describe("Parameter value"),
    parameterType: z.string().optional().describe("Type: 'float', 'vector', 'bool'"),
  }),
  bt("create_niagara_system", "Create a new empty Niagara System asset.", {
    name: z.string(), packagePath: z.string().optional(),
  }),
];
