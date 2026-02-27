import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const animationTools: ToolDef[] = [
  bt("read_anim_blueprint",
    "Read an Animation Blueprint's structure: target skeleton, parent class, state machines, " +
    "variables, graphs, and anim layers. The entry point for understanding any AnimBP.",
    { assetPath: z.string().describe("Asset path to the Anim Blueprint") }),

  bt("read_anim_montage",
    "Read an Animation Montage: sections, notifies, slot anim tracks, blend in/out, rate scale.",
    { assetPath: z.string().describe("Asset path to the Anim Montage") }),

  bt("read_anim_sequence",
    "Read an Animation Sequence: length, frame count, rate, skeleton, notifies, curve names.",
    { assetPath: z.string().describe("Asset path to the Anim Sequence") }),

  bt("read_blendspace",
    "Read a BlendSpace: axis names, sample points with animations and coordinates, skeleton.",
    { assetPath: z.string().describe("Asset path to the BlendSpace") }),

  bt("list_anim_assets",
    "List animation assets in a directory: montages, sequences, blendspaces, and anim Blueprints.",
    {
      directory: z.string().optional().describe("Content directory to search. Default: '/Game/'"),
      recursive: z.boolean().optional().describe("Search subdirectories. Default: true"),
    }),

  bt("create_anim_montage",
    "Create an Animation Montage from an existing AnimSequence.",
    {
      animSequencePath: z.string().describe("Path to the source AnimSequence"),
      name: z.string().optional().describe("Asset name (e.g. 'AM_Attack')"),
      packagePath: z.string().optional().describe("Package path"),
    }),

  bt("create_anim_blueprint",
    "Create an Animation Blueprint for a given skeleton.",
    {
      skeletonPath: z.string().describe("Path to the Skeleton asset"),
      name: z.string().optional().describe("Asset name"),
      packagePath: z.string().optional().describe("Package path"),
    }),

  bt("create_blendspace",
    "Create a BlendSpace for a given skeleton.",
    {
      skeletonPath: z.string().describe("Path to the Skeleton asset"),
      name: z.string().optional().describe("Asset name"),
      packagePath: z.string().optional().describe("Package path"),
      axisHorizontal: z.string().optional().describe("Horizontal axis name. Default: 'Speed'"),
      axisVertical: z.string().optional().describe("Vertical axis name. Default: 'Direction'"),
    }),

  bt("add_anim_notify",
    "Add a notify event to an animation at a specific time.",
    {
      assetPath: z.string().describe("Asset path to the animation"),
      notifyName: z.string().describe("Notify name"),
      triggerTime: z.number().describe("Time in seconds"),
      notifyClass: z.string().optional().describe("Notify class name"),
    }),
];
