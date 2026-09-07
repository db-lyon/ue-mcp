import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";
import { PAGINATION_SCHEMA, paged } from "../pagination.js";
import { actions as epicActions, schema as epicSchema } from "./epic/reflection.generated.js";

export const reflectionTool: ToolDef = categoryTool(
  "reflection",
  "UE reflection: classes, structs, enums, gameplay tags, and SaveGame instances.",
  {
    reflect_class:  bp("read", "Reflect UClass. className accepts the C++ spelling with or without the A/U/F/E prefix (UMyConfig and MyConfig both resolve), a /Script/Module.ClassName path, or a Blueprint class path; a failed lookup lists the spellings tried and the closest matches (#823). Params: className, includeInherited?", "reflect_class"),
    reflect_instance: bp("read", paged("Per-instance writable schema: what can be written on THIS asset, CDO or subobject right now. reflect_class answers what a class has; this answers what the object in front of you will accept, which is what removes the write-and-see loop. Per property: type and kind, the same tooltip/category/displayName/clamp/UI-range/units metadata reflect_class reports, the UPROPERTY flag names, the current value and valueText, and the constraints a write has to satisfy - enum values, allowed and disallowed classes for an object reference, array/set element type, map key and value types, struct field layout, container element count. Instance state on top of that: 'editable' with a 'notEditableReason' (EditConst, EditDefaultsOnly read on an instance, EditInstanceOnly read on defaults), the EditCondition and whether it is met on THIS object, 'settable' saying whether asset/editor(set_property) can write it at all, and 'valueObjectPath' for an instanced subobject so the next call can aim at it. objectPath takes an asset path, an object path, a class path or a Blueprint path (its generated-class defaults). propertyPath scopes the read to one nested struct or object reference. Params: objectPath, propertyPath?, filter?, includeInherited? (default true), includeValues? (default true), editableOnly? (default false), maxDepth? (0 to 5, default 1)"), "reflect_instance"),
    reflect_struct: bp("read", "Reflect UScriptStruct. Params: structName", "reflect_struct"),
    reflect_enum:   bp("read", "Reflect UEnum by full path, short name, or short name without the E prefix. Resolves native enums in any loaded module and loads unloaded Blueprint (UserDefinedEnum) assets via the asset registry. Returns enumPath, userDefined, and per-value name/value/displayName/tooltip; a failed lookup lists close matches (#762). Params: enumName", "reflect_enum"),
    list_classes:   bp("read", paged("List classes. parentFilter resolves with or without the C++ A/U/F/E prefix (#823). Rows carry name, path and parent, sorted by path so a page boundary is stable. Params: parentFilter?, limit?"), "list_classes"),
    list_tags:      bp("read", paged("List gameplay tags. Params: filter?"), "list_gameplay_tags"),
    create_tag:     bp("mutate", "Create gameplay tag. Params: tag, comment?", "create_gameplay_tag"),
    create_enum:    bp("mutate", "Create UUserDefinedEnum asset, optionally seeded with entries. Params: name, packagePath?, entries?: (string|{name, displayName?})[], onConflict? (#274)", "create_enum", (p) => ({ name: p.name, packagePath: p.packagePath, entries: p.entries, onConflict: p.onConflict })),
    set_enum_entries: bp("mutate", "Replace entries on an existing UUserDefinedEnum. Params: assetPath, entries[] (#274)", "set_enum_entries", (p) => ({ assetPath: p.assetPath, entries: p.entries })),
    is_class_loaded: bp("read", "Report whether a UClass is currently loaded in the editor (loaded), whether it exists/is loadable (exists), and its owning module + that module's load state. Distinguishes 'not loaded yet' from 'does not exist'. Params: className (short name, /Script/<Module>.<Class>, or BP class path) (#689)", "is_class_loaded", (p) => ({ className: p.className })),
    is_module_loaded: bp("read", "Report whether a named module is currently loaded. Params: moduleName (#689)", "is_module_loaded", (p) => ({ moduleName: p.moduleName })),
    list_loaded_modules: bp("read", paged("Enumerate modules with runtime load state. Params: filter? (case-insensitive substring), loadedOnly? (default false). Returns modules[{name, loaded, gameModule}] + totalLoaded/totalModules (#689)"), "list_loaded_modules", (p) => ({ filter: p.filter, loadedOnly: p.loadedOnly, cursor: p.cursor, limit: p.limit })),
    inspect_save_game: bp("read", "Load a SaveGame slot read-only and return its reflected UPROPERTY(SaveGame) values. Non-serializable properties are listed in skippedProperties instead of failing the call. Params: slotName, userIndex? (default 0)", "inspect_save_game", (p) => ({ slotName: p.slotName, userIndex: p.userIndex })),
    ...epicActions,
  },
  undefined,
  {
    ...epicSchema,
    className: z.string().optional(),
    moduleName: z.string().optional().describe("is_module_loaded: module name (#689)"),
    loadedOnly: z.boolean().optional().describe("list_loaded_modules: only loaded modules (#689)"),
    slotName: z.string().min(1).max(128).optional().describe("inspect_save_game: logical save slot name without a path"),
    userIndex: z.number().int().nonnegative().optional().describe("inspect_save_game: platform user index (default 0)"),
    includeInherited: z.boolean().optional().describe("reflect_class (default false) / reflect_instance (default true): walk superclass properties too"),
    structName: z.string().optional(),
    enumName: z.string().optional(),
    parentFilter: z.string().optional(),
    filter: z.string().optional(),
    tag: z.string().optional(),
    comment: z.string().optional(),
    name: z.string().optional().describe("Enum asset name (create_enum)"),
    packagePath: z.string().optional().describe("Package path (default /Game)"),
    assetPath: z.string().optional().describe("Existing UserDefinedEnum path (set_enum_entries)"),
    objectPath: z.string().optional().describe("reflect_instance: the asset, CDO, actor or subobject to describe. An asset path, a full object path, a class path, or a Blueprint path (described as its generated-class defaults)"),
    propertyPath: z.string().optional().describe("reflect_instance: scope the read to one nested struct or object reference, dotted and indexable (Config.Traits[1].Params). Omit to describe the object itself"),
    includeValues: z.boolean().optional().describe("reflect_instance: include each property's current value and valueText (default true)"),
    editableOnly: z.boolean().optional().describe("reflect_instance: return only the properties the details panel would let you edit on this object (default false, which returns every property with its reason)"),
    maxDepth: z.number().int().optional().describe("reflect_instance: how far to expand struct and container types, 0 to 5 (default 1). 0 names the types without expanding them"),
    entries: z.array(z.union([
      z.string(),
      z.object({ name: z.string(), displayName: z.string().optional() }),
    ])).optional().describe("Enum entries - strings or {name, displayName?}"),
    onConflict: z.string().optional().describe("Asset-creation conflict policy: skip (default) | error | overwrite"),
    // Declared for the whole category because the MCP layer strips undeclared
    // keys: an action that documents `cursor` but whose category does not
    // declare it receives `undefined` and silently returns page one forever.
    ...PAGINATION_SCHEMA,
  },
);
