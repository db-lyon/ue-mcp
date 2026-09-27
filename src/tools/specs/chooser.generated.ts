// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { categorySchema, makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

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
        ],
        "role": "editTarget"
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
        ],
        "role": "editTarget"
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
        ],
        "role": "editTarget"
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
        ],
        "role": "editTarget"
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
        ],
        "role": "editTarget"
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

/** Every key the spec'd chooser handlers declare, aliases included. */
export const schema = categorySchema(handlerSpecs);

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(handlerSpecs);
