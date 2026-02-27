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
  },
  `- read_anim_blueprint: Read AnimBP structure. Params: assetPath
- read_montage: Read montage. Params: assetPath
- read_sequence: Read anim sequence. Params: assetPath
- read_blendspace: Read blendspace. Params: assetPath
- list: List anim assets. Params: directory?, recursive?
- create_montage: Create montage. Params: animSequencePath, name?, packagePath?
- create_anim_blueprint: Create AnimBP. Params: skeletonPath, name?, packagePath?
- create_blendspace: Create blendspace. Params: skeletonPath, name?, packagePath?, axisHorizontal?, axisVertical?
- add_notify: Add notify. Params: assetPath, notifyName, triggerTime, notifyClass?
- get_skeleton_info: Read skeleton. Params: assetPath
- list_sockets: List sockets. Params: assetPath
- list_skeletal_meshes: List skeletal meshes. Params: directory?, recursive?
- get_physics_asset: Read physics asset. Params: assetPath`,
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
  },
);
