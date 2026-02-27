import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";

export const reflectionTool: ToolDef = categoryTool(
  "reflection",
  "UE reflection: classes, structs, enums, gameplay tags.",
  {
    reflect_class:  bp("reflect_class"),
    reflect_struct: bp("reflect_struct"),
    reflect_enum:   bp("reflect_enum"),
    list_classes:   bp("list_classes"),
    list_tags:      bp("list_gameplay_tags"),
    create_tag:     bp("create_gameplay_tag"),
  },
  `- reflect_class: Reflect UClass. Params: className, includeInherited?
- reflect_struct: Reflect UScriptStruct. Params: structName
- reflect_enum: Reflect UEnum. Params: enumName
- list_classes: List classes. Params: parentFilter?, limit?
- list_tags: List gameplay tags. Params: filter?
- create_tag: Create gameplay tag. Params: tag, comment?`,
  {
    className: z.string().optional(),
    includeInherited: z.boolean().optional(),
    structName: z.string().optional(),
    enumName: z.string().optional(),
    parentFilter: z.string().optional(),
    limit: z.number().optional(),
    filter: z.string().optional(),
    tag: z.string().optional(),
    comment: z.string().optional(),
  },
);
