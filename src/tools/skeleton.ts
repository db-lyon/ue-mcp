import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const skeletonTools: ToolDef[] = [
  bt("get_skeleton_info", "Get skeleton information: sockets, bones, associated assets.", {
    assetPath: z.string().describe("Path to the Skeleton or SkeletalMesh asset"),
  }),
  bt("list_sockets", "List all sockets on a skeleton with their bone attachments and transforms.", {
    assetPath: z.string(),
  }),
  bt("list_skeletal_meshes", "List SkeletalMesh, Skeleton, and PhysicsAsset assets in a directory.", {
    directory: z.string().optional(), recursive: z.boolean().optional(),
  }),
  bt("get_physics_asset_info", "Get information about a Physics Asset: body setups, bone names.", {
    assetPath: z.string(),
  }),
];
