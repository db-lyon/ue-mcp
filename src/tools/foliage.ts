import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const foliageTools: ToolDef[] = [
  bt("list_foliage_types", "List all foliage types used in the level.", {}),
  bt("get_foliage_type_settings", "Read full settings for a foliage type.", { foliageTypeName: z.string() }),
  bt("sample_foliage", "Query foliage instances in a region.", {
    center: z.object({ x: z.number(), y: z.number(), z: z.number() }),
    radius: z.number(),
    foliageType: z.string().optional(),
  }),
  bt("paint_foliage", "Add foliage instances in a radius.", {
    foliageType: z.string(), center: z.object({ x: z.number(), y: z.number(), z: z.number() }),
    radius: z.number(), count: z.number().optional(), density: z.number().optional(),
  }),
  bt("erase_foliage", "Remove foliage instances in a radius.", {
    center: z.object({ x: z.number(), y: z.number(), z: z.number() }),
    radius: z.number(), foliageType: z.string().optional(),
  }),
  bt("create_foliage_type", "Create a new FoliageType asset from a StaticMesh.", {
    meshPath: z.string().describe("Path to the StaticMesh"),
    name: z.string().optional(), packagePath: z.string().optional(),
  }),
  bt("set_foliage_type_settings", "Modify settings on a foliage type (partial update).", {
    foliageTypeName: z.string(), settings: z.record(z.unknown()),
  }),
];
