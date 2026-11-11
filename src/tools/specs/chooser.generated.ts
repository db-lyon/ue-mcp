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
      },
      {
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      },
      {
        "name": "insertIndex",
        "type": "integer",
        "required": false,
        "description": "Column position (default where the editor places a new column: last, but before a trailing randomize column)"
      }
    ]
  },
  "chooser_add_parameter": {
    "category": "chooser",
    "params": [
      {
        "name": "table",
        "type": "string",
        "required": true,
        "description": "ChooserTable or ChooserSignature asset path. A nested chooser uses its root table's signature",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      },
      {
        "name": "parameterType",
        "type": "string",
        "required": true,
        "description": "Parameter struct type, e.g. ContextObjectTypeClass or ContextObjectTypeStruct (chooser(list_types, kind='parameter') lists them)"
      },
      {
        "name": "properties",
        "type": "object",
        "required": false,
        "description": "Property path to value text to set on the new parameter, e.g. {\"Class\": \"/Script/Engine.Character\", \"Direction\": \"ReadWrite\"}"
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
      },
      {
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      },
      {
        "name": "resultType",
        "type": "string",
        "required": false,
        "description": "Result struct type for a default result (AssetChooser, SoftAssetChooser, EvaluateChooser, ClassChooser and so on; chooser(list_types, kind='result') lists them). Fill it in with chooser(set_property)"
      },
      {
        "name": "insertIndex",
        "type": "integer",
        "required": false,
        "description": "Insert the row before this row index (default append)"
      }
    ]
  },
  "chooser_auto_populate": {
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
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      },
      {
        "name": "column",
        "type": "integer",
        "required": false,
        "description": "Fill only this column, for every row and the fallback"
      },
      {
        "name": "rows",
        "type": "array",
        "required": false,
        "description": "Fill only these rows, in every auto populating column",
        "items": "integer"
      },
      {
        "name": "includeFallback",
        "type": "boolean",
        "required": false,
        "description": "With rows, also fill the fallback"
      }
    ]
  },
  "chooser_copy_column": {
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
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      },
      {
        "name": "column",
        "type": "integer",
        "required": true,
        "description": "Column index, 0-based"
      },
      {
        "name": "targetTable",
        "type": "string",
        "required": false,
        "description": "Destination ChooserTable asset path (default the source table)"
      },
      {
        "name": "targetNested",
        "type": "string",
        "required": false,
        "description": "Nested chooser inside targetTable's asset to copy into"
      },
      {
        "name": "insertIndex",
        "type": "integer",
        "required": false,
        "description": "Insert the copy before this column of the target (default last, before a randomize column)"
      }
    ]
  },
  "chooser_copy_rows": {
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
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      },
      {
        "name": "rows",
        "type": "array",
        "required": true,
        "description": "Row indices, 0-based; duplicates are ignored",
        "items": "integer"
      },
      {
        "name": "targetTable",
        "type": "string",
        "required": false,
        "description": "Destination ChooserTable asset path (default the source table)"
      },
      {
        "name": "targetNested",
        "type": "string",
        "required": false,
        "description": "Nested chooser inside targetTable's asset to copy into"
      },
      {
        "name": "insertIndex",
        "type": "integer",
        "required": false,
        "description": "Insert the copies before this row of the target (default append)"
      },
      {
        "name": "includeFallback",
        "type": "boolean",
        "required": false,
        "description": "Also copy the fallback result and fallback cells, replacing the target's (default false)"
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
  "chooser_create_nested": {
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
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      },
      {
        "name": "row",
        "type": "integer",
        "required": false,
        "description": "Row whose result becomes the new nested chooser (default: add a new row for it)"
      },
      {
        "name": "name",
        "type": "string",
        "required": false,
        "description": "Name for the nested chooser (default NestedChooser, made unique)"
      }
    ]
  },
  "chooser_delete_column": {
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
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      },
      {
        "name": "column",
        "type": "integer",
        "required": true,
        "description": "Column index, 0-based"
      }
    ]
  },
  "chooser_delete_nested": {
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
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Nested object name or object path (chooser(list_nested) lists them)"
      }
    ]
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
      },
      {
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      }
    ]
  },
  "chooser_delete_rows": {
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
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      },
      {
        "name": "rows",
        "type": "array",
        "required": true,
        "description": "Row indices, 0-based; duplicates are ignored",
        "items": "integer"
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
      },
      {
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      }
    ]
  },
  "chooser_duplicate_column": {
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
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      },
      {
        "name": "column",
        "type": "integer",
        "required": true,
        "description": "Column index, 0-based"
      }
    ]
  },
  "chooser_duplicate_rows": {
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
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      },
      {
        "name": "rows",
        "type": "array",
        "required": true,
        "description": "Row indices, 0-based; duplicates are ignored",
        "items": "integer"
      }
    ]
  },
  "chooser_get_cell": {
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
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      },
      {
        "name": "column",
        "type": "integer",
        "required": true,
        "description": "Column index, 0-based"
      },
      {
        "name": "row",
        "type": "integer",
        "required": false,
        "description": "Row index, 0-based (omit with fallback=true)"
      },
      {
        "name": "fallback",
        "type": "boolean",
        "required": false,
        "description": "Address the column's fallback cell instead of a row (output columns only)"
      }
    ]
  },
  "chooser_get_property": {
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
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      },
      {
        "name": "target",
        "type": "string",
        "required": true,
        "description": "What the path is read from: a column, a row's result, the fallback result, or a parameter",
        "enum": [
          "column",
          "row",
          "fallback",
          "parameter"
        ]
      },
      {
        "name": "index",
        "type": "integer",
        "required": false,
        "description": "Column, row or parameter index (not used for fallback)"
      },
      {
        "name": "propertyPath",
        "type": "string",
        "required": false,
        "description": "'.' separated property names with [index] for array elements, stepping into instanced structs, e.g. 'InputValue.Binding.DisplayName', 'Asset', 'DefaultRowValue'. Empty addresses the whole struct"
      }
    ]
  },
  "chooser_get_signature": {
    "category": "chooser",
    "params": [
      {
        "name": "table",
        "type": "string",
        "required": true,
        "description": "ChooserTable or ChooserSignature asset path. A nested chooser uses its root table's signature",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      }
    ]
  },
  "chooser_list_nested": {
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
      },
      {
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      }
    ]
  },
  "chooser_list_types": {
    "category": "chooser",
    "params": [
      {
        "name": "kind",
        "type": "string",
        "required": true,
        "description": "Which types: column, result (row result), parameter, or input (the input types one column accepts; needs table and column)",
        "enum": [
          "column",
          "result",
          "parameter",
          "input"
        ]
      },
      {
        "name": "table",
        "type": "string",
        "required": false,
        "description": "ChooserTable asset path, for kind 'input'"
      },
      {
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      },
      {
        "name": "column",
        "type": "integer",
        "required": false,
        "description": "Column index, for kind 'input'"
      }
    ]
  },
  "chooser_move_column": {
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
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      },
      {
        "name": "column",
        "type": "integer",
        "required": true,
        "description": "Column index, 0-based"
      },
      {
        "name": "newIndex",
        "type": "integer",
        "required": true,
        "description": "Index the column ends up at. A randomize column stays last and cannot be moved"
      }
    ]
  },
  "chooser_move_rows": {
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
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      },
      {
        "name": "rows",
        "type": "array",
        "required": true,
        "description": "Row indices, 0-based; duplicates are ignored",
        "items": "integer"
      },
      {
        "name": "targetIndex",
        "type": "integer",
        "required": true,
        "description": "Insert the rows, in their current order, before this row index as it is before the move; rowCount moves them to the end. To move row r to final index n, pass n+1 when n > r, else n"
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
  "chooser_remove_disabled": {
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
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      }
    ]
  },
  "chooser_remove_parameter": {
    "category": "chooser",
    "params": [
      {
        "name": "table",
        "type": "string",
        "required": true,
        "description": "ChooserTable or ChooserSignature asset path. A nested chooser uses its root table's signature",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      },
      {
        "name": "index",
        "type": "integer",
        "required": true,
        "description": "Parameter index, 0-based"
      }
    ]
  },
  "chooser_rename_nested": {
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
        "name": "name",
        "type": "string",
        "required": true,
        "description": "Nested object name or object path (chooser(list_nested) lists them)"
      },
      {
        "name": "newName",
        "type": "string",
        "required": true,
        "description": "New object name, unique among the asset's nested objects"
      }
    ]
  },
  "chooser_restore_snapshot": {
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
        "name": "snapshot",
        "type": "object",
        "required": true,
        "description": "Snapshot of the asset's chooser tables, as the rollback of delete_column, delete_rows, move_rows, auto_populate, delete_nested and remove_disabled carries it"
      }
    ]
  },
  "chooser_set_cell": {
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
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      },
      {
        "name": "column",
        "type": "integer",
        "required": true,
        "description": "Column index, 0-based"
      },
      {
        "name": "row",
        "type": "integer",
        "required": false,
        "description": "Row index, 0-based (omit with fallback=true)"
      },
      {
        "name": "fallback",
        "type": "boolean",
        "required": false,
        "description": "Address the column's fallback cell instead of a row (output columns only)"
      },
      {
        "name": "value",
        "type": "string",
        "required": true,
        "description": "Cell value as struct text, e.g. '(Min=0,Max=100)', or a bare number or boolean for a scalar cell"
      }
    ]
  },
  "chooser_set_column": {
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
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      },
      {
        "name": "column",
        "type": "integer",
        "required": true,
        "description": "Column index, 0-based"
      },
      {
        "name": "disabled",
        "type": "boolean",
        "required": false,
        "description": "Disable or enable the column"
      },
      {
        "name": "inputType",
        "type": "string",
        "required": false,
        "description": "Change the column's input type (chooser(list_types, kind='input') lists the ones it accepts)"
      },
      {
        "name": "parameterIndex",
        "type": "integer",
        "required": false,
        "description": "Bind the input to this class or struct parameter (with propertyChain)"
      },
      {
        "name": "propertyChain",
        "type": "array",
        "required": false,
        "description": "Property names to follow from the parameter, e.g. [\"Speed\"] or [\"MyStruct\", \"MyFloat\"]; empty binds the parameter itself",
        "items": "string"
      }
    ]
  },
  "chooser_set_fallback": {
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
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      },
      {
        "name": "resultType",
        "type": "string",
        "required": false,
        "description": "Result struct type for a default result (AssetChooser, SoftAssetChooser, EvaluateChooser, ClassChooser and so on; chooser(list_types, kind='result') lists them). Fill it in with chooser(set_property)"
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
        "name": "remove",
        "type": "boolean",
        "required": false,
        "description": "Remove the fallback result; output columns keep their fallback cells"
      }
    ]
  },
  "chooser_set_property": {
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
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      },
      {
        "name": "target",
        "type": "string",
        "required": true,
        "description": "What the path is read from: a column, a row's result, the fallback result, or a parameter",
        "enum": [
          "column",
          "row",
          "fallback",
          "parameter"
        ]
      },
      {
        "name": "index",
        "type": "integer",
        "required": false,
        "description": "Column, row or parameter index (not used for fallback)"
      },
      {
        "name": "propertyPath",
        "type": "string",
        "required": false,
        "description": "'.' separated property names with [index] for array elements, stepping into instanced structs, e.g. 'InputValue.Binding.DisplayName', 'Asset', 'DefaultRowValue'. Empty addresses the whole struct"
      },
      {
        "name": "value",
        "type": "string",
        "required": true,
        "description": "Value in Unreal text format, the form copy and paste use in the details panel, e.g. '0.5', '/Game/Anims/Run.Run', '(Min=0,Max=100)'"
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
      },
      {
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      },
      {
        "name": "resultType",
        "type": "string",
        "required": false,
        "description": "Result struct type for a default result (AssetChooser, SoftAssetChooser, EvaluateChooser, ClassChooser and so on; chooser(list_types, kind='result') lists them). Fill it in with chooser(set_property)"
      }
    ]
  },
  "chooser_set_signature": {
    "category": "chooser",
    "params": [
      {
        "name": "table",
        "type": "string",
        "required": true,
        "description": "ChooserTable or ChooserSignature asset path. A nested chooser uses its root table's signature",
        "aliases": [
          "assetPath"
        ]
      },
      {
        "name": "nested",
        "type": "string",
        "required": false,
        "description": "Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"
      },
      {
        "name": "resultType",
        "type": "string",
        "required": false,
        "description": "What the chooser returns",
        "enum": [
          "ObjectResult",
          "ClassResult",
          "NoPrimaryResult"
        ]
      },
      {
        "name": "resultClass",
        "type": "string",
        "required": false,
        "description": "Result Class: the class of object returned, or the parent class of the classes returned. Empty clears it"
      },
      {
        "name": "signatureAsset",
        "type": "string",
        "required": false,
        "description": "ChooserSignature asset to inherit the result type, result class and parameters from (UE 5.9). Empty clears the reference and restores the local values"
      }
    ]
  }
};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  chooser_add_column: "Params: table (or assetPath), columnType, inputStruct?, boundProperty?, enumPath?, nested?, insertIndex?",
  chooser_add_parameter: "Params: table (or assetPath), nested?, parameterType, properties?",
  chooser_add_row: "Params: table (or assetPath), output?, outputType?, cells?, inputs?, nested?, resultType?, insertIndex?",
  chooser_auto_populate: "Params: table (or assetPath), nested?, column?, rows?, includeFallback?",
  chooser_copy_column: "Params: table (or assetPath), nested?, column, targetTable?, targetNested?, insertIndex?",
  chooser_copy_rows: "Params: table (or assetPath), nested?, rows, targetTable?, targetNested?, insertIndex?, includeFallback?",
  chooser_create: "Params: name, packagePath?, onConflict?",
  chooser_create_nested: "Params: table (or assetPath), nested?, row?, name?",
  chooser_delete_column: "Params: table (or assetPath), nested?, column",
  chooser_delete_nested: "Params: table (or assetPath), name",
  chooser_delete_row: "Params: table (or assetPath), index, nested?",
  chooser_delete_rows: "Params: table (or assetPath), nested?, rows",
  chooser_describe: "Params: table (or assetPath), nested?",
  chooser_duplicate_column: "Params: table (or assetPath), nested?, column",
  chooser_duplicate_rows: "Params: table (or assetPath), nested?, rows",
  chooser_get_cell: "Params: table (or assetPath), nested?, column, row?, fallback?",
  chooser_get_property: "Params: table (or assetPath), nested?, target, index?, propertyPath?",
  chooser_get_signature: "Params: table (or assetPath), nested?",
  chooser_list_nested: "Params: table (or assetPath)",
  chooser_list_object_references: "Params: assetPath (or path), classFilter?, pathFilter?",
  chooser_list_rows: "Params: table (or assetPath), nested?",
  chooser_list_types: "Params: kind, table?, nested?, column?",
  chooser_move_column: "Params: table (or assetPath), nested?, column, newIndex",
  chooser_move_rows: "Params: table (or assetPath), nested?, rows, targetIndex",
  chooser_remap_object_references: "Params: assetPath (or path), from?, to?, fromPrefix?, toPrefix?, dryRun?, allowMissing?",
  chooser_remove_disabled: "Params: table (or assetPath), nested?",
  chooser_remove_parameter: "Params: table (or assetPath), nested?, index",
  chooser_rename_nested: "Params: table (or assetPath), name, newName",
  chooser_restore_snapshot: "Params: table (or assetPath), snapshot",
  chooser_set_cell: "Params: table (or assetPath), nested?, column, row?, fallback?, value",
  chooser_set_column: "Params: table (or assetPath), nested?, column, disabled?, inputType?, parameterIndex?, propertyChain?",
  chooser_set_fallback: "Params: table (or assetPath), nested?, resultType?, output?, outputType?, remove?",
  chooser_set_property: "Params: table (or assetPath), nested?, target, index?, propertyPath?, value",
  chooser_set_row: "Params: table (or assetPath), index, output?, outputType?, disabled?, cells?, inputs?, nested?, resultType?",
  chooser_set_signature: "Params: table (or assetPath), nested?, resultType?, resultClass?, signatureAsset?",
};

/** Every key the spec'd chooser handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  allowMissing: z.boolean().optional().describe("Write a soft reference even when the target does not exist yet (default false)"),
  assetPath: z.string().optional().describe("Alias for table (chooser_add_column, chooser_add_parameter, chooser_add_row, chooser_auto_populate, chooser_copy_column, chooser_copy_rows, chooser_create_nested, chooser_delete_column, chooser_delete_nested, chooser_delete_row, chooser_delete_rows, chooser_describe, chooser_duplicate_column, chooser_duplicate_rows, chooser_get_cell, chooser_get_property, chooser_get_signature, chooser_list_nested, chooser_list_rows, chooser_move_column, chooser_move_rows, chooser_remove_disabled, chooser_remove_parameter, chooser_rename_nested, chooser_restore_snapshot, chooser_set_cell, chooser_set_column, chooser_set_fallback, chooser_set_property, chooser_set_row, chooser_set_signature). ChooserTable asset path (chooser_list_object_references, chooser_remap_object_references)"),
  boundProperty: z.string().optional().describe("Context property name the column reads at evaluation time"),
  cells: z.array(z.unknown()).optional().describe("Per-column cell values aligned to column order; each is struct text like '(Value=2)', or a string, number or boolean. null leaves a column at its default"),
  classFilter: z.string().optional().describe("Only references whose target class matches (substring)"),
  column: z.number().int().optional().describe("Fill only this column, for every row and the fallback (chooser_auto_populate). Column index, 0-based (chooser_copy_column, chooser_delete_column, chooser_duplicate_column, chooser_get_cell, chooser_move_column, chooser_set_cell, chooser_set_column). Column index, for kind 'input' (chooser_list_types)"),
  columnType: z.string().optional().describe("Chooser column struct short name (EnumColumn, BoolColumn, FloatRangeColumn, GameplayTagColumn, ObjectColumn, Output*Column and so on)"),
  disabled: z.boolean().optional().describe("Disable or enable the column (chooser_set_column). Enable or disable the row without deleting it (chooser_set_row)"),
  dryRun: z.boolean().optional().describe("Preview without writing (default true)"),
  enumPath: z.string().optional().describe("Enum asset path for an EnumColumn"),
  fallback: z.boolean().optional().describe("Address the column's fallback cell instead of a row (output columns only)"),
  from: z.string().optional().describe("Exact object path to replace (with to)"),
  fromPrefix: z.string().optional().describe("Path prefix to rewrite, e.g. /Game/Vendor/ (with toPrefix)"),
  includeFallback: z.boolean().optional().describe("With rows, also fill the fallback (chooser_auto_populate). Also copy the fallback result and fallback cells, replacing the target's (default false) (chooser_copy_rows)"),
  index: z.number().int().optional().describe("Row index, 0-based (chooser_delete_row, chooser_set_row). Column, row or parameter index (not used for fallback) (chooser_get_property, chooser_set_property). Parameter index, 0-based (chooser_remove_parameter)"),
  inputs: z.record(z.string(), z.unknown()).optional().describe("Cell values keyed by column index (as string) or column name; same value format as cells"),
  inputStruct: z.string().optional().describe("Parameter struct to bind the column input (e.g. EnumContextProperty, BoolContextProperty)"),
  inputType: z.string().optional().describe("Change the column's input type (chooser(list_types, kind='input') lists the ones it accepts)"),
  insertIndex: z.number().int().optional().describe("Column position (default where the editor places a new column: last, but before a trailing randomize column) (chooser_add_column). Insert the row before this row index (default append) (chooser_add_row). Insert the copy before this column of the target (default last, before a randomize column) (chooser_copy_column). Insert the copies before this row of the target (default append) (chooser_copy_rows)"),
  kind: z.enum(["column","result","parameter","input"]).optional().describe("Which types: column, result (row result), parameter, or input (the input types one column accepts; needs table and column)"),
  name: z.string().optional().describe("New ChooserTable asset name (chooser_create). Name for the nested chooser (default NestedChooser, made unique) (chooser_create_nested). Nested object name or object path (chooser(list_nested) lists them) (chooser_delete_nested, chooser_rename_nested)"),
  nested: z.string().optional().describe("Name or object path of a nested chooser inside the table's asset to work on instead of the table itself (chooser(list_nested) lists them)"),
  newIndex: z.number().int().optional().describe("Index the column ends up at. A randomize column stays last and cannot be moved"),
  newName: z.string().optional().describe("New object name, unique among the asset's nested objects"),
  onConflict: z.string().optional().describe("When the table exists: skip (default, report it) | error"),
  output: z.string().optional().describe("Output asset path for the row (a PoseSearchDatabase, a nested ChooserTable, etc.)"),
  outputType: z.string().optional().describe("Output wrapper: 'asset' (hard ref, default) | 'soft_asset' | 'evaluate' (nested ChooserTable reference)"),
  packagePath: z.string().optional().describe("Destination folder (default /Game)"),
  parameterIndex: z.number().int().optional().describe("Bind the input to this class or struct parameter (with propertyChain)"),
  parameterType: z.string().optional().describe("Parameter struct type, e.g. ContextObjectTypeClass or ContextObjectTypeStruct (chooser(list_types, kind='parameter') lists them)"),
  path: z.string().optional().describe("Alias for assetPath"),
  pathFilter: z.string().optional().describe("Substring filter on the referenced object path"),
  properties: z.record(z.string(), z.unknown()).optional().describe("Property path to value text to set on the new parameter, e.g. {\"Class\": \"/Script/Engine.Character\", \"Direction\": \"ReadWrite\"}"),
  propertyChain: z.array(z.string()).optional().describe("Property names to follow from the parameter, e.g. [\"Speed\"] or [\"MyStruct\", \"MyFloat\"]; empty binds the parameter itself"),
  propertyPath: z.string().optional().describe("'.' separated property names with [index] for array elements, stepping into instanced structs, e.g. 'InputValue.Binding.DisplayName', 'Asset', 'DefaultRowValue'. Empty addresses the whole struct"),
  remove: z.boolean().optional().describe("Remove the fallback result; output columns keep their fallback cells"),
  resultClass: z.string().optional().describe("Result Class: the class of object returned, or the parent class of the classes returned. Empty clears it"),
  resultType: z.string().optional().describe("Result struct type for a default result (AssetChooser, SoftAssetChooser, EvaluateChooser, ClassChooser and so on; chooser(list_types, kind='result') lists them). Fill it in with chooser(set_property) (chooser_add_row, chooser_set_fallback, chooser_set_row). What the chooser returns (chooser_set_signature)"),
  row: z.number().int().optional().describe("Row whose result becomes the new nested chooser (default: add a new row for it) (chooser_create_nested). Row index, 0-based (omit with fallback=true) (chooser_get_cell, chooser_set_cell)"),
  rows: z.array(z.number().int()).optional().describe("Fill only these rows, in every auto populating column (chooser_auto_populate). Row indices, 0-based; duplicates are ignored (chooser_copy_rows, chooser_delete_rows, chooser_duplicate_rows, chooser_move_rows)"),
  signatureAsset: z.string().optional().describe("ChooserSignature asset to inherit the result type, result class and parameters from (UE 5.9). Empty clears the reference and restores the local values"),
  snapshot: z.record(z.string(), z.unknown()).optional().describe("Snapshot of the asset's chooser tables, as the rollback of delete_column, delete_rows, move_rows, auto_populate, delete_nested and remove_disabled carries it"),
  table: z.string().optional().describe("ChooserTable asset path, e.g. /Game/Path/CT_Locomotion (chooser_add_column, chooser_add_row, chooser_auto_populate, chooser_copy_column, chooser_copy_rows, chooser_create_nested, chooser_delete_column, chooser_delete_nested, chooser_delete_row, chooser_delete_rows, chooser_describe, chooser_duplicate_column, chooser_duplicate_rows, chooser_get_cell, chooser_get_property, chooser_list_nested, chooser_list_rows, chooser_move_column, chooser_move_rows, chooser_remove_disabled, chooser_rename_nested, chooser_restore_snapshot, chooser_set_cell, chooser_set_column, chooser_set_fallback, chooser_set_property, chooser_set_row). ChooserTable or ChooserSignature asset path. A nested chooser uses its root table's signature (chooser_add_parameter, chooser_get_signature, chooser_remove_parameter, chooser_set_signature). ChooserTable asset path, for kind 'input' (chooser_list_types)"),
  target: z.enum(["column","row","fallback","parameter"]).optional().describe("What the path is read from: a column, a row's result, the fallback result, or a parameter"),
  targetIndex: z.number().int().optional().describe("Insert the rows, in their current order, before this row index as it is before the move; rowCount moves them to the end. To move row r to final index n, pass n+1 when n > r, else n"),
  targetNested: z.string().optional().describe("Nested chooser inside targetTable's asset to copy into"),
  targetTable: z.string().optional().describe("Destination ChooserTable asset path (default the source table)"),
  to: z.string().optional().describe("Replacement object path (with from)"),
  toPrefix: z.string().optional().describe("Replacement prefix, e.g. /Game/MyProject/ (with fromPrefix)"),
  value: z.string().optional().describe("Cell value as struct text, e.g. '(Min=0,Max=100)', or a bare number or boolean for a scalar cell (chooser_set_cell). Value in Unreal text format, the form copy and paste use in the details panel, e.g. '0.5', '/Game/Anims/Run.Run', '(Min=0,Max=100)' (chooser_set_property)"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
