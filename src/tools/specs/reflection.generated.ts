// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { z } from "zod";
import { makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd reflection handler. */
export const handlerSpecs: HandlerSpecs = {
  "create_enum": {
    "category": "reflection",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Enum asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Folder for the new enum (default /Game)"
      },
      {
        "name": "entries",
        "type": "array",
        "required": false,
        "description": "Entries to seed: strings, or {name, displayName?} objects"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the enum exists: skip (default, report it) | error"
      }
    ],
    "contractExempt": "Creates and saves an enum asset under the contract values; nothing it reads fails first"
  },
  "create_gameplay_tag": {
    "category": "reflection",
    "params": [
      {
        "name": "tag",
        "type": "string",
        "required": true,
        "description": "Gameplay tag to create, e.g. Ability.Fire"
      },
      {
        "name": "comment",
        "type": "string",
        "required": false,
        "description": "Developer comment stored with the tag"
      }
    ],
    "contractExempt": "Writes the tag into Config/DefaultGameplayTags.ini under the contract values unless the tag check refuses it first"
  },
  "inspect_save_game": {
    "category": "reflection",
    "params": [
      {
        "name": "slotName",
        "type": "string",
        "required": true,
        "description": "Logical save slot name, a plain filename without a path"
      },
      {
        "name": "userIndex",
        "type": "integer",
        "required": false,
        "description": "Platform user index (default 0)"
      }
    ]
  },
  "is_class_loaded": {
    "category": "reflection",
    "params": [
      {
        "name": "className",
        "type": "string",
        "required": true,
        "description": "Class to probe: short name, /Script/Module.Class, or a Blueprint class path",
        "aliases": [
          "class"
        ]
      }
    ]
  },
  "is_module_loaded": {
    "category": "reflection",
    "params": [
      {
        "name": "moduleName",
        "type": "string",
        "required": true,
        "description": "Module name to probe",
        "aliases": [
          "module"
        ]
      }
    ]
  },
  "list_classes": {
    "category": "reflection",
    "params": [
      {
        "name": "parentFilter",
        "type": "string",
        "required": false,
        "description": "List classes deriving from this one; with or without the C++ A/U/F/E prefix"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the nextCursor from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows to return on this page (default 100, max 1000)"
      }
    ]
  },
  "list_gameplay_tags": {
    "category": "reflection",
    "params": [
      {
        "name": "filter",
        "type": "string",
        "required": false,
        "description": "Only tags starting with this prefix"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the nextCursor from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows to return on this page (default 500, max 5000)"
      }
    ]
  },
  "list_loaded_modules": {
    "category": "reflection",
    "params": [
      {
        "name": "filter",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring of the module name"
      },
      {
        "name": "loadedOnly",
        "type": "boolean",
        "required": false,
        "description": "Only modules that are loaded (default false)"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the nextCursor from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows to return on this page (default 500, max 5000)"
      }
    ]
  },
  "list_structs": {
    "category": "reflection",
    "params": [
      {
        "name": "package",
        "type": "string",
        "required": false,
        "description": "Narrow to one package or content folder: /Script/Engine, a bare module name (Engine), or /Game/Data"
      },
      {
        "name": "filter",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring of the struct name or cppName"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the nextCursor from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows to return on this page (default 200, max 5000)"
      }
    ]
  },
  "reflect_class": {
    "category": "reflection",
    "params": [
      {
        "name": "className",
        "type": "string",
        "required": true,
        "description": "Class to reflect: C++ spelling with or without the A/U/F/E prefix, /Script/Module.ClassName, or a Blueprint class path"
      },
      {
        "name": "includeInherited",
        "type": "boolean",
        "required": false,
        "description": "Walk superclass properties and functions too (default false)"
      }
    ]
  },
  "reflect_enum": {
    "category": "reflection",
    "params": [
      {
        "name": "enumName",
        "type": "string",
        "required": true,
        "description": "UEnum by full path, short name, or short name without the E prefix"
      }
    ]
  },
  "reflect_instance": {
    "category": "reflection",
    "params": [
      {
        "name": "objectPath",
        "type": "string",
        "required": true,
        "description": "The asset, CDO, actor or subobject to describe: an asset path, a full object path, a class path, or a Blueprint path (described as its generated-class defaults)"
      },
      {
        "name": "propertyPath",
        "type": "string",
        "required": false,
        "description": "Scope the read to one nested struct or object reference, dotted and indexable (Config.Traits[1].Params). Omit to describe the object itself"
      },
      {
        "name": "filter",
        "type": "string",
        "required": false,
        "description": "Case-insensitive substring of the property name"
      },
      {
        "name": "includeInherited",
        "type": "boolean",
        "required": false,
        "description": "Walk superclass properties too (default true)"
      },
      {
        "name": "includeValues",
        "type": "boolean",
        "required": false,
        "description": "Include each property's current value and valueText (default true)"
      },
      {
        "name": "editableOnly",
        "type": "boolean",
        "required": false,
        "description": "Return only the properties the details panel would let you edit on this object (default false, which returns every property with its reason)"
      },
      {
        "name": "maxDepth",
        "type": "integer",
        "required": false,
        "description": "How far to expand struct and container types, 0 to 5 (default 1). 0 names the types without expanding them"
      },
      {
        "name": "cursor",
        "type": "string",
        "required": false,
        "description": "Resume a paged read: pass back the nextCursor from the previous page, unmodified"
      },
      {
        "name": "limit",
        "type": "integer",
        "required": false,
        "description": "Rows to return on this page (default 100, max 500)"
      }
    ]
  },
  "reflect_struct": {
    "category": "reflection",
    "params": [
      {
        "name": "structName",
        "type": "string",
        "required": true,
        "description": "UScriptStruct to reflect, by name or path"
      }
    ]
  },
  "set_enum_entries": {
    "category": "reflection",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "Existing UserDefinedEnum asset path"
      },
      {
        "name": "entries",
        "type": "array",
        "required": true,
        "description": "The complete new entry list: strings, or {name, displayName?} objects"
      }
    ]
  }
};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  create_enum: "Params: name, packagePath?, entries?, onConflict?",
  create_gameplay_tag: "Params: tag, comment?",
  inspect_save_game: "Params: slotName, userIndex?",
  is_class_loaded: "Params: className (or class)",
  is_module_loaded: "Params: moduleName (or module)",
  list_classes: "Params: parentFilter?, cursor?, limit?",
  list_gameplay_tags: "Params: filter?, cursor?, limit?",
  list_loaded_modules: "Params: filter?, loadedOnly?, cursor?, limit?",
  list_structs: "Params: package?, filter?, cursor?, limit?",
  reflect_class: "Params: className, includeInherited?",
  reflect_enum: "Params: enumName",
  reflect_instance: "Params: objectPath, propertyPath?, filter?, includeInherited?, includeValues?, editableOnly?, maxDepth?, cursor?, limit?",
  reflect_struct: "Params: structName",
  set_enum_entries: "Params: assetPath, entries",
};

/** Every key the spec'd reflection handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  assetPath: z.string().optional().describe("Existing UserDefinedEnum asset path"),
  class: z.string().optional().describe("Alias for className"),
  className: z.string().optional().describe("Class to probe: short name, /Script/Module.Class, or a Blueprint class path (is_class_loaded). Class to reflect: C++ spelling with or without the A/U/F/E prefix, /Script/Module.ClassName, or a Blueprint class path (reflect_class)"),
  comment: z.string().optional().describe("Developer comment stored with the tag"),
  cursor: z.string().optional().describe("Resume a paged read: pass back the nextCursor from the previous page, unmodified"),
  editableOnly: z.boolean().optional().describe("Return only the properties the details panel would let you edit on this object (default false, which returns every property with its reason)"),
  entries: z.array(z.unknown()).optional().describe("Entries to seed: strings, or {name, displayName?} objects (create_enum). The complete new entry list: strings, or {name, displayName?} objects (set_enum_entries)"),
  enumName: z.string().optional().describe("UEnum by full path, short name, or short name without the E prefix"),
  filter: z.string().optional().describe("Only tags starting with this prefix (list_gameplay_tags). Case-insensitive substring of the module name (list_loaded_modules). Case-insensitive substring of the struct name or cppName (list_structs). Case-insensitive substring of the property name (reflect_instance)"),
  includeInherited: z.boolean().optional().describe("Walk superclass properties and functions too (default false) (reflect_class). Walk superclass properties too (default true) (reflect_instance)"),
  includeValues: z.boolean().optional().describe("Include each property's current value and valueText (default true)"),
  limit: z.number().int().optional().describe("Rows to return on this page (default 100, max 1000) (list_classes). Rows to return on this page (default 500, max 5000) (list_gameplay_tags, list_loaded_modules). Rows to return on this page (default 200, max 5000) (list_structs). Rows to return on this page (default 100, max 500) (reflect_instance)"),
  loadedOnly: z.boolean().optional().describe("Only modules that are loaded (default false)"),
  maxDepth: z.number().int().optional().describe("How far to expand struct and container types, 0 to 5 (default 1). 0 names the types without expanding them"),
  module: z.string().optional().describe("Alias for moduleName"),
  moduleName: z.string().optional().describe("Module name to probe"),
  name: z.string().optional().describe("Enum asset name"),
  objectPath: z.string().optional().describe("The asset, CDO, actor or subobject to describe: an asset path, a full object path, a class path, or a Blueprint path (described as its generated-class defaults)"),
  onConflict: z.string().optional().describe("When the enum exists: skip (default, report it) | error"),
  package: z.string().optional().describe("Narrow to one package or content folder: /Script/Engine, a bare module name (Engine), or /Game/Data"),
  packagePath: z.string().optional().describe("Folder for the new enum (default /Game)"),
  parentFilter: z.string().optional().describe("List classes deriving from this one; with or without the C++ A/U/F/E prefix"),
  propertyPath: z.string().optional().describe("Scope the read to one nested struct or object reference, dotted and indexable (Config.Traits[1].Params). Omit to describe the object itself"),
  slotName: z.string().optional().describe("Logical save slot name, a plain filename without a path"),
  structName: z.string().optional().describe("UScriptStruct to reflect, by name or path"),
  tag: z.string().optional().describe("Gameplay tag to create, e.g. Ability.Fire"),
  userIndex: z.number().int().optional().describe("Platform user index (default 0)"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
