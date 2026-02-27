import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";

export const niagaraTool: ToolDef = categoryTool(
  "niagara",
  "Niagara VFX: systems, emitters, spawning, parameters.",
  {
    list:           bp("list_niagara_systems"),
    get_info:       bp("get_niagara_info"),
    spawn:          bp("spawn_niagara_at_location"),
    set_parameter:  bp("set_niagara_parameter"),
    create:         bp("create_niagara_system"),
  },
  `- list: List Niagara assets. Params: directory?, recursive?
- get_info: Inspect system. Params: assetPath
- spawn: Spawn VFX. Params: systemPath, location {x,y,z}, rotation?, label?
- set_parameter: Set parameter. Params: actorLabel, parameterName, value, parameterType?
- create: Create system. Params: name, packagePath?`,
  {
    assetPath: z.string().optional(), actorLabel: z.string().optional(),
    directory: z.string().optional(), recursive: z.boolean().optional(),
    systemPath: z.string().optional(),
    location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
    rotation: z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() }).optional(),
    label: z.string().optional(),
    parameterName: z.string().optional(),
    value: z.unknown().optional(),
    parameterType: z.string().optional(),
    name: z.string().optional(),
    packagePath: z.string().optional(),
  },
);
