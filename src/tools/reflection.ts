import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";

export const reflectionTool: ToolDef = categoryTool(
  "reflection",
  "UE reflection: classes, structs, enums, gameplay tags.",
  {
    reflect_class:  bp("Reflect UClass. Params: className, includeInherited?", "reflect_class"),
    reflect_struct: bp("Reflect UScriptStruct. Params: structName", "reflect_struct"),
    reflect_enum:   bp("Reflect UEnum. Params: enumName", "reflect_enum"),
    list_classes:   bp("List classes. Params: parentFilter?, limit?", "list_classes"),
    list_tags:      bp("List gameplay tags. Params: filter?", "list_gameplay_tags"),
    create_tag:     bp("Create gameplay tag. Params: tag, comment?", "create_gameplay_tag"),
    create_enum:    bp("Create UUserDefinedEnum asset, optionally seeded with entries. Params: name, packagePath?, entries?: (string|{name, displayName?})[], onConflict? (#274)", "create_enum", (p) => ({ name: p.name, packagePath: p.packagePath, entries: p.entries, onConflict: p.onConflict })),
    set_enum_entries: bp("Replace entries on an existing UUserDefinedEnum. Params: assetPath, entries[] (#274)", "set_enum_entries", (p) => ({ assetPath: p.assetPath, entries: p.entries })),
  },
  undefined,
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
    name: z.string().optional().describe("Enum asset name (create_enum)"),
    packagePath: z.string().optional().describe("Package path (default /Game)"),
    assetPath: z.string().optional().describe("Existing UserDefinedEnum path (set_enum_entries)"),
    entries: z.array(z.union([
      z.string(),
      z.object({ name: z.string(), displayName: z.string().optional() }),
    ])).optional().describe("Enum entries — strings or {name, displayName?}"),
    onConflict: z.string().optional().describe("Asset-creation conflict policy: skip (default) | error | overwrite"),
  },
);
