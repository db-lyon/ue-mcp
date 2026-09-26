import type { ToolDef } from "../types.js";
import { categoryTool } from "../category-tool.js";
import { specBp, schema as specSchema } from "./specs/chooser.generated.js";

// #685 - ChooserTable (UChooserTable) row authoring. Chooser tables are the
// data-driven selection layer behind Motion Matching: a chooser maps character
// state (Stance x MovementState x Gait) to which PoseSearchDatabase to search.
// Extending locomotion means adding/editing ROWS (a set of input-column
// conditions plus an output object). The engine stores columns/rows as instanced
// structs with no scripting entry point, so this was previously hand-editing only.
export const chooserTool: ToolDef = categoryTool(
  "chooser",
  "Author ChooserTable assets (the data-driven selection layer behind Motion Matching): introspect columns, list/add/edit/delete rows mapping input-column conditions to an output object.",
  {
    create:     specBp("mutate", "Create an empty ChooserTable asset. Add input columns with add_column, then rows with add_row (#685).", "chooser_create"),
    describe:   specBp("read", "Introspect a ChooserTable: row count, each input column (index, name, columnType, cellType) and the fallback result. Read this first to learn the cell text format each column expects (#685).", "chooser_describe"),
    add_column: specBp("mutate", "Add an input column to a ChooserTable (so rows have a condition to fill). columnType is a Chooser column struct short name, e.g. EnumColumn, BoolColumn, FloatRangeColumn, GameplayTagColumn, ObjectColumn, or an Output* column. Optionally bind its input: inputStruct (parameter struct e.g. EnumContextProperty/BoolContextProperty), boundProperty (context property name to read), enumPath (for enum columns). Sizes the new column's cells to the current rows (#685).", "chooser_add_column"),
    list_rows:  specBp("read", "List every row: index, disabled flag, output object (resultType + referenced asset path), and each column's cell value as round-trippable text (#685).", "chooser_list_rows"),
    add_row:    specBp("mutate", "Append a row. Set the output via `output` (asset path) + outputType ('asset' hard ref default | 'soft_asset' | 'evaluate' for a nested ChooserTable). Set input-column conditions via `cells` (array aligned to column order) and/or `inputs` (object keyed by column index or name). Cell values are struct text like '(Value=2)' - partial fields are allowed and unspecified ones keep defaults; a bare number/bool works for scalar columns (#685).", "chooser_add_row"),
    set_row:    specBp("mutate", "Edit an existing row by index: optionally replace the output (output + outputType), toggle disabled, and/or update column cells (cells / inputs, same format as add_row) (#685).", "chooser_set_row"),
    delete_row: specBp("mutate", "Delete a row by index (removes its output plus the per-row cell from every column) (#685).", "chooser_delete_row"),
    list_object_references: specBp("read", "List every leaf object reference reachable from a chooser, descending through nested chooser tables. list_rows renders those as an opaque resultType:NestedChooser with an empty output, so the actual PoseSearchDatabase/asset paths were invisible. Each entry reports the owning table, the exact location (e.g. ResultsStructs[3].Asset), the struct type and the current object path. classFilter matches the referenced object's class, pathFilter is a substring on the path (#754).", "chooser_list_object_references"),
    remap_object_references: specBp("mutate", "Repoint object references throughout a chooser's nested structure. Either an exact swap (from + to) or a folder rewrite (fromPrefix + toPrefix), which is the 'adopt vendor choosers into our namespace' case. DRY RUN BY DEFAULT - pass dryRun=false to apply. Object-typed targets are class-checked before assignment; the chooser is recompiled and left dirty rather than saved (#754).", "chooser_remap_object_references"),
  },
  {
    // #1057: every key a spec'd handler declares, generated from its C++
    // registration.
    ...specSchema,
  },
);
