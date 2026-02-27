import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";

export const levelTool: ToolDef = categoryTool(
  "level",
  "Level actors, selection, components, level management, volumes, lights, and splines.",
  {
    get_outliner:       bp("get_world_outliner"),
    place_actor:        bp("place_actor"),
    delete_actor:       bp("delete_actor"),
    get_actor_details:  bp("get_actor_details"),
    move_actor:         bp("move_actor"),
    select:             bp("select_actors"),
    get_selected:       bp("get_selected_actors"),
    add_component:      bp("add_component_to_actor"),
    set_component_property: bp("set_component_property"),
    get_current:        bp("get_current_level"),
    load:               bp("load_level"),
    save:               bp("save_current_level"),
    list:               bp("list_levels"),
    create:             bp("create_new_level"),
    spawn_volume:       bp("spawn_volume"),
    list_volumes:       bp("list_volumes"),
    set_volume_properties: bp("set_volume_properties"),
    spawn_light:        bp("spawn_light"),
    set_light_properties: bp("set_light_properties"),
    build_lighting:     bp("build_lighting"),
    get_spline_info:    bp("get_spline_info"),
    set_spline_points:  bp("set_spline_points"),
  },
  `- get_outliner: List actors. Params: classFilter?, nameFilter?
- place_actor: Spawn actor. Params: actorClass, label?, location?, rotation?, scale?, properties?
- delete_actor: Remove actor. Params: actorLabel
- get_actor_details: Inspect actor. Params: actorLabel
- move_actor: Transform actor. Params: actorLabel, location?, rotation?, scale?
- select: Select actors. Params: actorLabels[]
- get_selected: Get selection
- add_component: Add component to actor. Params: actorLabel, componentClass, componentName?
- set_component_property: Set component prop. Params: actorLabel, componentName, propertyName, value
- get_current/load/save/list/create: Level management. Params: levelPath?, templateLevel?
- spawn_volume: Place volume. Params: volumeType, location?, extent?, label?
- list_volumes: List volumes. Params: volumeType?
- set_volume_properties: Edit volume. Params: actorLabel, properties
- spawn_light: Place light. Params: lightType, location?, rotation?, intensity?, color?, label?
- set_light_properties: Edit light. Params: actorLabel, intensity?, color?, temperature?, castShadows?, attenuationRadius?
- build_lighting: Build lights. Params: quality?
- get_spline_info: Read spline. Params: actorLabel
- set_spline_points: Set spline points. Params: actorLabel, points[], closedLoop?`,
  {
    actorLabel: z.string().optional(), actorLabels: z.array(z.string()).optional(),
    actorClass: z.string().optional(), label: z.string().optional(),
    location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
    rotation: z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() }).optional(),
    scale: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
    properties: z.record(z.unknown()).optional(),
    classFilter: z.string().optional(), nameFilter: z.string().optional(),
    componentClass: z.string().optional(), componentName: z.string().optional(),
    propertyName: z.string().optional(), value: z.unknown().optional(),
    levelPath: z.string().optional(), templateLevel: z.string().optional(),
    directory: z.string().optional(), recursive: z.boolean().optional(),
    volumeType: z.string().optional(),
    extent: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
    lightType: z.string().optional(), intensity: z.number().optional(),
    color: z.object({ r: z.number(), g: z.number(), b: z.number() }).optional(),
    temperature: z.number().optional(), castShadows: z.boolean().optional(),
    attenuationRadius: z.number().optional(),
    innerConeAngle: z.number().optional(), outerConeAngle: z.number().optional(),
    quality: z.string().optional(),
    points: z.array(z.object({ x: z.number(), y: z.number(), z: z.number() })).optional(),
    closedLoop: z.boolean().optional(),
  },
);
