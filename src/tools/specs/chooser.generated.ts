// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { z } from "zod";
import { makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd chooser handler. */
export const handlerSpecs: HandlerSpecs = {
  "chooser_add_column": {
    "category": "chooser",
    "params": [
      {
        "name": "table",
        "type": "string",
        "required": true,
        "description": "ChooserTable asset path, e.g. /Game/Path/CT_Locomotion",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "columnType",
        "type": "string",
        "required": true,
        "description": "Chooser column struct short name (EnumColumn, BoolColumn, FloatRangeColumn, GameplayTagColumn, ObjectColumn, Output*Column and so on)"
      },
      {
        "name": "inputStruct",
        "type": "string",
        "required": false,
        "description": "Parameter struct to bind the column input (e.g. EnumContextProperty, BoolContextProperty)"
      },
      {
        "name": "boundProperty",
        "type": "string",
        "required": false,
        "description": "Context property name the column reads at evaluation time"
      },
      {
        "name": "enumPath",
        "type": "string",
        "required": false,
        "description": "Enum asset path for an EnumColumn"
      }
    ]
  },
  "chooser_add_row": {
    "category": "chooser",
    "params": [
      {
        "name": "table",
        "type": "string",
        "required": true,
        "description": "ChooserTable asset path, e.g. /Game/Path/CT_Locomotion",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "output",
        "type": "string",
        "required": false,
        "description": "Output asset path for the row (a PoseSearchDatabase, a nested ChooserTable, etc.)"
      },
      {
        "name": "outputType",
        "type": "string",
        "required": false,
        "description": "Output wrapper: 'asset' (hard ref, default) | 'soft_asset' | 'evaluate' (nested ChooserTable reference)"
      },
      {
        "name": "cells",
        "type": "array",
        "required": false,
        "description": "Per-column cell values aligned to column order; each is struct text like '(Value=2)', or a string, number or boolean. null leaves a column at its default"
      },
      {
        "name": "inputs",
        "type": "object",
        "required": false,
        "description": "Cell values keyed by column index (as string) or column name; same value format as cells"
      }
    ]
  },
  "chooser_create": {
    "category": "chooser",
    "params": [
      {
        "name": "name",
        "type": "string",
        "required": true,
        "description": "New ChooserTable asset name"
      },
      {
        "name": "packagePath",
        "type": "string",
        "required": false,
        "description": "Destination folder (default /Game)"
      },
      {
        "name": "onConflict",
        "type": "string",
        "required": false,
        "description": "When the table exists: skip (default, report it) | error"
      }
    ],
    "contractExempt": "Creates and saves a ChooserTable under the contract values; nothing it reads fails first"
  },
  "chooser_delete_row": {
    "category": "chooser",
    "params": [
      {
        "name": "table",
        "type": "string",
        "required": true,
        "description": "ChooserTable asset path, e.g. /Game/Path/CT_Locomotion",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "index",
        "type": "integer",
        "required": true,
        "description": "Row index, 0-based"
      }
    ]
  },
  "chooser_describe": {
    "category": "chooser",
    "params": [
      {
        "name": "table",
        "type": "string",
        "required": true,
        "description": "ChooserTable asset path, e.g. /Game/Path/CT_Locomotion",
        "aliases": [
          "assetPath"
        ]
      }
    ]
  },
  "chooser_list_object_references": {
    "category": "chooser",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "ChooserTable asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "classFilter",
        "type": "string",
        "required": false,
        "description": "Only references whose target class matches (substring)"
      },
      {
        "name": "pathFilter",
        "type": "string",
        "required": false,
        "description": "Substring filter on the referenced object path"
      }
    ]
  },
  "chooser_list_rows": {
    "category": "chooser",
    "params": [
      {
        "name": "table",
        "type": "string",
        "required": true,
        "description": "ChooserTable asset path, e.g. /Game/Path/CT_Locomotion",
        "aliases": [
          "assetPath"
        ]
      }
    ]
  },
  "chooser_remap_object_references": {
    "category": "chooser",
    "params": [
      {
        "name": "assetPath",
        "type": "string",
        "required": true,
        "description": "ChooserTable asset path",
        "aliases": [
          "path"
        ]
      },
      {
        "name": "from",
        "type": "string",
        "required": false,
        "description": "Exact object path to replace (with to)"
      },
      {
        "name": "to",
        "type": "string",
        "required": false,
        "description": "Replacement object path (with from)"
      },
      {
        "name": "fromPrefix",
        "type": "string",
        "required": false,
        "description": "Path prefix to rewrite, e.g. /Game/Vendor/ (with toPrefix)"
      },
      {
        "name": "toPrefix",
        "type": "string",
        "required": false,
        "description": "Replacement prefix, e.g. /Game/MyProject/ (with fromPrefix)"
      },
      {
        "name": "dryRun",
        "type": "boolean",
        "required": false,
        "description": "Preview without writing (default true)"
      },
      {
        "name": "allowMissing",
        "type": "boolean",
        "required": false,
        "description": "Write a soft reference even when the target does not exist yet (default false)"
      }
    ]
  },
  "chooser_set_row": {
    "category": "chooser",
    "params": [
      {
        "name": "table",
        "type": "string",
        "required": true,
        "description": "ChooserTable asset path, e.g. /Game/Path/CT_Locomotion",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "index",
        "type": "integer",
        "required": true,
        "description": "Row index, 0-based"
      },
      {
        "name": "output",
        "type": "string",
        "required": false,
        "description": "Output asset path for the row (a PoseSearchDatabase, a nested ChooserTable, etc.)"
      },
      {
        "name": "outputType",
        "type": "string",
        "required": false,
        "description": "Output wrapper: 'asset' (hard ref, default) | 'soft_asset' | 'evaluate' (nested ChooserTable reference)"
      },
      {
        "name": "disabled",
        "type": "boolean",
        "required": false,
        "description": "Enable or disable the row without deleting it"
      },
      {
        "name": "cells",
        "type": "array",
        "required": false,
        "description": "Per-column cell values aligned to column order; each is struct text like '(Value=2)', or a string, number or boolean. null leaves a column at its default"
      },
      {
        "name": "inputs",
        "type": "object",
        "required": false,
        "description": "Cell values keyed by column index (as string) or column name; same value format as cells"
      }
    ]
  }
};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  chooser_add_column: "Params: table (or assetPath), columnType, inputStruct?, boundProperty?, enumPath?",
  chooser_add_row: "Params: table (or assetPath), output?, outputType?, cells?, inputs?",
  chooser_create: "Params: name, packagePath?, onConflict?",
  chooser_delete_row: "Params: table (or assetPath), index",
  chooser_describe: "Params: table (or assetPath)",
  chooser_list_object_references: "Params: assetPath (or path), classFilter?, pathFilter?",
  chooser_list_rows: "Params: table (or assetPath)",
  chooser_remap_object_references: "Params: assetPath (or path), from?, to?, fromPrefix?, toPrefix?, dryRun?, allowMissing?",
  chooser_set_row: "Params: table (or assetPath), index, output?, outputType?, disabled?, cells?, inputs?",
};

/** Every key the spec'd chooser handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  allowMissing: z.boolean().optional().describe("Write a soft reference even when the target does not exist yet (default false)"),
  assetPath: z.string().optional().describe("Alias for table (chooser_add_column, chooser_add_row, chooser_delete_row, chooser_describe, chooser_list_rows, chooser_set_row). ChooserTable asset path (chooser_list_object_references, chooser_remap_object_references)"),
  boundProperty: z.string().optional().describe("Context property name the column reads at evaluation time"),
  cells: z.array(z.unknown()).optional().describe("Per-column cell values aligned to column order; each is struct text like '(Value=2)', or a string, number or boolean. null leaves a column at its default"),
  classFilter: z.string().optional().describe("Only references whose target class matches (substring)"),
  columnType: z.string().optional().describe("Chooser column struct short name (EnumColumn, BoolColumn, FloatRangeColumn, GameplayTagColumn, ObjectColumn, Output*Column and so on)"),
  disabled: z.boolean().optional().describe("Enable or disable the row without deleting it"),
  dryRun: z.boolean().optional().describe("Preview without writing (default true)"),
  enumPath: z.string().optional().describe("Enum asset path for an EnumColumn"),
  from: z.string().optional().describe("Exact object path to replace (with to)"),
  fromPrefix: z.string().optional().describe("Path prefix to rewrite, e.g. /Game/Vendor/ (with toPrefix)"),
  index: z.number().int().optional().describe("Row index, 0-based"),
  inputs: z.record(z.unknown()).optional().describe("Cell values keyed by column index (as string) or column name; same value format as cells"),
  inputStruct: z.string().optional().describe("Parameter struct to bind the column input (e.g. EnumContextProperty, BoolContextProperty)"),
  name: z.string().optional().describe("New ChooserTable asset name"),
  onConflict: z.string().optional().describe("When the table exists: skip (default, report it) | error"),
  output: z.string().optional().describe("Output asset path for the row (a PoseSearchDatabase, a nested ChooserTable, etc.)"),
  outputType: z.string().optional().describe("Output wrapper: 'asset' (hard ref, default) | 'soft_asset' | 'evaluate' (nested ChooserTable reference)"),
  packagePath: z.string().optional().describe("Destination folder (default /Game)"),
  path: z.string().optional().describe("Alias for assetPath"),
  pathFilter: z.string().optional().describe("Substring filter on the referenced object path"),
  table: z.string().optional().describe("ChooserTable asset path, e.g. /Game/Path/CT_Locomotion"),
  to: z.string().optional().describe("Replacement object path (with from)"),
  toPrefix: z.string().optional().describe("Replacement prefix, e.g. /Game/MyProject/ (with fromPrefix)"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
