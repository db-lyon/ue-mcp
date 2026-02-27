import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";

export const foliageTool: ToolDef = categoryTool(
  "foliage",
  "Foliage painting, types, sampling, and settings.",
  {
    list_types:    bp("list_foliage_types"),
    get_settings:  bp("get_foliage_type_settings"),
    sample:        bp("sample_foliage"),
    paint:         bp("paint_foliage"),
    erase:         bp("erase_foliage"),
    create_type:   bp("create_foliage_type"),
    set_settings:  bp("set_foliage_type_settings"),
  },
  `- list_types: List foliage types in level
- get_settings: Read foliage type settings. Params: foliageTypeName
- sample: Query instances in region. Params: center {x,y,z}, radius, foliageType?
- paint: Add foliage. Params: foliageType, center {x,y,z}, radius, count?, density?
- erase: Remove foliage. Params: center {x,y,z}, radius, foliageType?
- create_type: Create from mesh. Params: meshPath, name?, packagePath?
- set_settings: Modify type settings. Params: foliageTypeName, settings`,
  {
    foliageTypeName: z.string().optional(),
    foliageType: z.string().optional(),
    center: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
    radius: z.number().optional(),
    count: z.number().optional(),
    density: z.number().optional(),
    meshPath: z.string().optional(),
    name: z.string().optional(),
    packagePath: z.string().optional(),
    settings: z.record(z.unknown()).optional(),
  },
);
