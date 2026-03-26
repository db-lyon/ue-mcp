import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";

export const animationTool: ToolDef = categoryTool(
  "animation",
  "Animation assets, skeletons, montages, blendspaces, anim blueprints, physics assets.",
  {
    read_anim_blueprint:  bp("read_anim_blueprint"),
    read_montage:         bp("read_anim_montage", (p) => ({ assetPath: p.assetPath })),
    read_sequence:        bp("read_anim_sequence", (p) => ({ assetPath: p.assetPath })),
    read_blendspace:      bp("read_blendspace", (p) => ({ assetPath: p.assetPath })),
    list:                 bp("list_anim_assets"),
    create_montage:       bp("create_anim_montage"),
    create_anim_blueprint: bp("create_anim_blueprint"),
    create_blendspace:    bp("create_blendspace"),
    add_notify:           bp("add_anim_notify"),
    get_skeleton_info:    bp("get_skeleton_info"),
    list_sockets:         bp("list_sockets"),
    list_skeletal_meshes: bp("list_skeletal_meshes"),
    get_physics_asset:    bp("get_physics_asset_info"),
    create_sequence:      bp("create_sequence"),
    set_bone_keyframes:   bp("set_bone_keyframes"),
    get_bone_transforms:  bp("get_bone_transforms"),
    set_montage_sequence: bp("set_montage_sequence"),
    set_montage_properties: bp("set_montage_properties"),

    // State machine authoring
    create_state_machine: bp("create_state_machine"),
    add_state:            bp("add_state"),
    add_transition:       bp("add_transition"),
    set_state_animation:  bp("set_state_animation"),
    set_transition_blend: bp("set_transition_blend"),
    read_state_machine:   bp("read_state_machine"),
  },
  `- read_anim_blueprint: Read AnimBP structure. Params: assetPath
- read_montage: Read montage. Params: assetPath
- read_sequence: Read anim sequence. Params: assetPath
- read_blendspace: Read blendspace. Params: assetPath
- list: List anim assets. Params: directory?, recursive?
- create_montage: Create montage. Params: animSequencePath, name?, packagePath?
- create_anim_blueprint: Create AnimBP. Params: skeletonPath, name?, packagePath?, parentClass? (e.g. "MyAnimInstance")
- create_blendspace: Create blendspace. Params: skeletonPath, name?, packagePath?, axisHorizontal?, axisVertical?
- add_notify: Add notify. Params: assetPath, notifyName, triggerTime, notifyClass?
- get_skeleton_info: Read skeleton. Params: assetPath
- list_sockets: List sockets. Params: assetPath
- list_skeletal_meshes: List skeletal meshes. Params: directory?, recursive?
- get_physics_asset: Read physics asset. Params: assetPath
- create_sequence: Create blank AnimSequence. Params: name, skeletonPath, packagePath?, numFrames?, frameRate?
- set_bone_keyframes: Set bone transform keyframes. Params: assetPath, boneName, keyframes (array of {frame, location?, rotation?, scale?})
- get_bone_transforms: Read reference pose transforms. Params: skeletonPath, boneNames? (array filter)
- set_montage_sequence: Replace animation sequence in a montage (auto-updates montage duration). Params: assetPath, animSequencePath, slotIndex?
- set_montage_properties: Set montage properties directly. Params: assetPath, sequenceLength?, rateScale?, blendIn?, blendOut?
- create_state_machine: Create state machine in AnimBP AnimGraph. Params: assetPath, name?, graphName?
- add_state: Add state to a state machine. Params: assetPath, stateMachineName, stateName
- add_transition: Add directed transition between states. Params: assetPath, stateMachineName, fromState, toState
- set_state_animation: Assign anim asset to state. Params: assetPath, stateMachineName, stateName, animAssetPath
- set_transition_blend: Set blend type/duration on transition. Params: assetPath, stateMachineName, fromState, toState, blendDuration?, blendLogic? (Standard|Inertialization)
- read_state_machine: Read state machine topology. Params: assetPath, stateMachineName`,
  {
    assetPath: z.string().optional(),
    directory: z.string().optional(),
    recursive: z.boolean().optional(),
    animSequencePath: z.string().optional(),
    skeletonPath: z.string().optional(),
    name: z.string().optional(),
    packagePath: z.string().optional(),
    axisHorizontal: z.string().optional(),
    axisVertical: z.string().optional(),
    notifyName: z.string().optional(),
    triggerTime: z.number().optional(),
    notifyClass: z.string().optional(),
    slotIndex: z.number().optional(),
    sequenceLength: z.number().optional(),
    rateScale: z.number().optional(),
    blendIn: z.number().optional(),
    blendOut: z.number().optional(),
    numFrames: z.number().optional(),
    frameRate: z.number().optional(),
    boneName: z.string().optional(),
    boneNames: z.array(z.string()).optional(),
    parentClass: z.string().optional().describe("Parent AnimInstance class name for create_anim_blueprint"),
    // State machine params
    stateMachineName: z.string().optional(),
    stateName: z.string().optional(),
    fromState: z.string().optional(),
    toState: z.string().optional(),
    animAssetPath: z.string().optional().describe("Path to animation sequence or blendspace"),
    blendDuration: z.number().optional(),
    blendLogic: z.string().optional().describe("Standard or Inertialization"),
    graphName: z.string().optional().describe("Target graph name (default: AnimGraph)"),
    keyframes: z.array(z.object({
      frame: z.number(),
      location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
      rotation: z.object({ x: z.number(), y: z.number(), z: z.number(), w: z.number() }).optional(),
      scale: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
    })).optional(),
  },
);
