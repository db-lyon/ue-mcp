import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const importTools: ToolDef[] = [
  bt("import_static_mesh", "Import a StaticMesh from FBX/OBJ file.", {
    filePath: z.string().describe("Absolute path to the FBX/OBJ file"),
    name: z.string().optional(), packagePath: z.string().optional(),
  }),
  bt("import_skeletal_mesh", "Import a SkeletalMesh from FBX file.", {
    filePath: z.string().describe("Absolute path to the FBX file"),
    name: z.string().optional(), packagePath: z.string().optional(),
    skeletonPath: z.string().optional().describe("Existing skeleton to retarget to"),
  }),
  bt("import_animation", "Import an animation from FBX, optionally targeting an existing skeleton.", {
    filePath: z.string().describe("Absolute path to the FBX file"),
    name: z.string().optional(), packagePath: z.string().optional(),
    skeletonPath: z.string().optional().describe("Skeleton to import against"),
  }),
];
