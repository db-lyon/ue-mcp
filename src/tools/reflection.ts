import { z } from "zod";
import type { ToolDef } from "../core/types.js";
import { categoryTool } from "../surface/category-tool.js";
import { actions as epicActions, schema as epicSchema } from "./epic/reflection.generated.js";
import { specBp, schema as specSchema } from "./specs/reflection.generated.js";

export const reflectionTool: ToolDef = categoryTool(
  "reflection",
  "UE reflection: classes, structs, enums, gameplay tags, and SaveGame instances.",
  {
    reflect_class:  specBp("read", "Reflect UClass. className accepts the C++ spelling with or without the A/U/F/E prefix (UMyConfig and MyConfig both resolve), a /Script/Module.ClassName path, or a Blueprint class path; a failed lookup lists the spellings tried and the closest matches (#823).", "reflect_class"),
    reflect_instance: specBp("read", "Per-instance writable schema: what can be written on THIS asset, CDO or subobject right now. reflect_class answers what a class has; this answers what the object in front of you will accept, which is what removes the write-and-see loop. Per property: type and kind, the same tooltip/category/displayName/clamp/UI-range/units metadata reflect_class reports, the UPROPERTY flag names, the current value and valueText, and the constraints a write has to satisfy - enum values, allowed and disallowed classes for an object reference, array/set element type, map key and value types, struct field layout, container element count. Instance state on top of that: 'editable' with a 'notEditableReason' (EditConst, EditDefaultsOnly read on an instance, EditInstanceOnly read on defaults), the EditCondition and whether it is met on THIS object, 'settable' saying whether asset/editor(set_property) can write it at all, and 'valueObjectPath' for an instanced subobject so the next call can aim at it. objectPath takes an asset path, an object path, a class path or a Blueprint path (its generated-class defaults). propertyPath scopes the read to one nested struct or object reference.", "reflect_instance"),
    reflect_struct: specBp("read", "Reflect UScriptStruct.", "reflect_struct"),
    reflect_enum:   specBp("read", "Reflect UEnum by full path, short name, or short name without the E prefix. Resolves native enums in any loaded module and loads unloaded Blueprint (UserDefinedEnum) assets via the asset registry. Returns enumPath, userDefined, and per-value name/value/displayName/tooltip; a failed lookup lists close matches (#762).", "reflect_enum"),
    list_classes:   specBp("read", "List classes. parentFilter resolves with or without the C++ A/U/F/E prefix (#823). Rows carry name, path and parent, sorted by path so a page boundary is stable.", "list_classes"),
    list_structs:   specBp("read", "List UScriptStructs, native and user-defined. Rows carry name (the registered spelling, without the F prefix), cppName (with it), path, package, parent, native and tableRow (derives from FTableRowBase, so usable as a DataTable row struct), sorted by path so a page boundary is stable. package narrows to one package or content folder: /Script/Engine, a bare module name (Engine), or /Game/Data. filter is a case-insensitive substring of name or cppName (#1088).", "list_structs"),
    list_tags:      specBp("read", "List gameplay tags, optionally only those under a prefix.", "list_gameplay_tags"),
    create_tag:     specBp("mutate", "Create a gameplay tag in Config/DefaultGameplayTags.ini. Registers it live through the Gameplay Tags editor when that module is loaded (method=gameplay_tags_editor, registeredLive=true, no restart); otherwise appends the ini entry and the tag appears after a restart (method=ini_append). Idempotent: a tag the ini already declares reports existed. A failure names the ini path and the cause, including a read-only (checked-in) file.", "create_gameplay_tag"),
    create_enum:    specBp("mutate", "Create UUserDefinedEnum asset, optionally seeded with entries: strings, or {name, displayName?} objects (#274).", "create_enum"),
    set_enum_entries: specBp("mutate", "Replace entries on an existing UUserDefinedEnum. entries takes the create_enum form: strings, or {name, displayName?} (#274).", "set_enum_entries"),
    is_class_loaded: specBp("read", "Report whether a UClass is currently loaded in the editor (loaded), whether it exists/is loadable (exists), and its owning module + that module's load state. Distinguishes 'not loaded yet' from 'does not exist'. className takes a short name, /Script/<Module>.<Class>, or a BP class path (#689).", "is_class_loaded"),
    is_module_loaded: specBp("read", "Report whether a named module is currently loaded (#689).", "is_module_loaded"),
    list_loaded_modules: specBp("read", "Enumerate modules with runtime load state. filter is a case-insensitive substring; loadedOnly defaults to false. Returns modules[{name, loaded, gameModule}] + totalLoaded/totalModules (#689).", "list_loaded_modules"),
    inspect_save_game: specBp("read", "Load a SaveGame slot read-only and return its reflected UPROPERTY(SaveGame) values. Non-serializable properties are listed in skippedProperties instead of failing the call. userIndex defaults to 0.", "inspect_save_game"),
    ...epicActions,
  },
  {
    ...epicSchema,
    // #1057: every key a spec'd handler declares, generated from its C++
    // registration.
    ...specSchema,
    // Spec'd keys declared again only to keep the bounds a spec cannot state.
    // The spec's type is the same, which the handler-specs unit test holds.
    slotName: z.string().min(1).max(128).optional().describe("inspect_save_game: logical save slot name without a path"),
    userIndex: z.number().int().nonnegative().optional().describe("inspect_save_game: platform user index (default 0)"),
  },
);
