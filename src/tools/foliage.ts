import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";
import { Vec3 } from "../schemas.js";

export const foliageTool: ToolDef = categoryTool(
  "foliage",
  "Foliage painting, types, sampling, and settings.",
  {
    list_types:    bp("List foliage types in level", "list_foliage_types"),
    get_settings:  bp("Read foliage type settings. Params: foliageTypeName", "get_foliage_type_settings"),
    sample:        bp("Query instances in region. Params: center, radius, foliageType?", "sample_foliage"),
    create_type:   bp("Create foliage type from mesh. Params: meshPath, name?, packagePath?", "create_foliage_type"),
    set_settings:  bp("Modify type settings. Params: foliageTypeName, settings", "set_foliage_type_settings"),
  },
  undefined,
  {
    foliageTypeName: z.string().optional(),
    foliageType: z.string().optional(),
    center: Vec3.optional(),
    radius: z.number().optional(),
    count: z.number().optional(),
    density: z.number().optional(),
    meshPath: z.string().optional(),
    name: z.string().optional(),
    packagePath: z.string().optional(),
    settings: z.record(z.unknown()).optional(),
  },
);
