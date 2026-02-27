import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";

export const niagaraTool: ToolDef = categoryTool(
  "niagara",
  "Niagara VFX: systems, emitters, spawning, parameters, and graph authoring.",
  {
    list:           bp("list_niagara_systems"),
    get_info:       bp("get_niagara_info"),
    spawn:          bp("spawn_niagara_at_location"),
    set_parameter:  bp("set_niagara_parameter"),
    create:         bp("create_niagara_system"),
    // Graph authoring
    create_emitter: bp("create_niagara_emitter"),
    add_emitter:    bp("add_emitter_to_system"),
    list_emitters:  bp("list_emitters_in_system"),
    set_emitter_property: bp("set_emitter_property"),
    list_modules:   bp("list_niagara_modules"),
    get_emitter_info: bp("get_emitter_info"),
  },
  `- list: List Niagara assets. Params: directory?, recursive?
- get_info: Inspect system. Params: assetPath
- spawn: Spawn VFX. Params: systemPath, location {x,y,z}, rotation?, label?
- set_parameter: Set parameter. Params: actorLabel, parameterName, value, parameterType?
- create: Create system. Params: name, packagePath?
- create_emitter: Create Niagara emitter. Params: name, packagePath?, templatePath?
- add_emitter: Add emitter to system. Params: systemPath, emitterPath
- list_emitters: List emitters in system. Params: systemPath
- set_emitter_property: Set emitter property. Params: systemPath, emitterName?, propertyName, value
- list_modules: List Niagara modules. Params: directory?
- get_emitter_info: Inspect emitter. Params: assetPath`,
  {
    assetPath: z.string().optional(), actorLabel: z.string().optional(),
    directory: z.string().optional(), recursive: z.boolean().optional(),
    systemPath: z.string().optional(), emitterPath: z.string().optional(),
    location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
    rotation: z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() }).optional(),
    label: z.string().optional(),
    parameterName: z.string().optional(),
    value: z.unknown().optional(),
    parameterType: z.string().optional(),
    name: z.string().optional(),
    packagePath: z.string().optional(),
    templatePath: z.string().optional(),
    emitterName: z.string().optional(),
    propertyName: z.string().optional(),
  },
);
