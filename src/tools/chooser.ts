import type { ToolDef } from "../core/types.js";
import { categoryTool } from "../surface/category-tool.js";
import { specBp, schema as specSchema } from "./specs/chooser.generated.js";

// #685 - ChooserTable (UChooserTable) row authoring. Chooser tables are the
// data-driven selection layer behind Motion Matching: a chooser maps character
// state (Stance x MovementState x Gait) to which PoseSearchDatabase to search.
// Extending locomotion means adding/editing ROWS (a set of input-column
// conditions plus an output object). The engine stores columns/rows as instanced
// structs with no scripting entry point, so this was previously hand-editing only.
//
// #1828 - the rest of what the Chooser Table editor does: type discovery, the
// signature (result type, result class, parameters), property paths, column and
// row restructuring, cells, the fallback, auto populate and nested choosers.
// Every table action takes `nested` to work on a nested chooser in the asset.
export const chooserTool: ToolDef = categoryTool(
  "chooser",
  "Author ChooserTable assets (the data-driven selection layer behind Motion Matching): signature and parameters, columns and their input bindings, rows and their results, cells, the fallback result, auto populate, and nested choosers.",
  {
    create:     specBp("mutate", "Create an empty ChooserTable asset. Add input columns with add_column, then rows with add_row (#685).", "chooser_create"),
    describe:   specBp("read", "Introspect a ChooserTable: row count, result type, whether it is nested, each column (index, name, columnType, cellType, disabled, randomize, autoPopulates, hasFallbackValue, inputType and its binding) and the fallback result. Read this first to learn the cell text format each column expects (#685).", "chooser_describe"),
    add_column: specBp("mutate", "Add an input column to a ChooserTable (so rows have a condition to fill). columnType is a Chooser column struct short name, e.g. EnumColumn, BoolColumn, FloatRangeColumn, GameplayTagColumn, ObjectColumn, or an Output* column. Optionally bind its input: inputStruct (parameter struct e.g. EnumContextProperty/BoolContextProperty), boundProperty (context property name to read), enumPath (for enum columns). Placed where the editor places it (before a trailing randomize column) unless insertIndex says otherwise. Sizes the new column's cells to the current rows (#685).", "chooser_add_column"),
    list_rows:  specBp("read", "List every row: index, disabled flag, output object (resultType + referenced asset path), and each column's cell value as round-trippable text (#685).", "chooser_list_rows"),
    add_row:    specBp("mutate", "Add a row (appended, or before insertIndex). Set the output via `output` (asset path) + outputType ('asset' hard ref default | 'soft_asset' | 'evaluate' for a nested ChooserTable), or a default result of any type via resultType. Set input-column conditions via `cells` (array aligned to column order) and/or `inputs` (object keyed by column index or name). Cell values are struct text like '(Value=2)' - partial fields are allowed and unspecified ones keep defaults; a bare number/bool works for scalar columns (#685).", "chooser_add_row"),
    set_row:    specBp("mutate", "Edit an existing row by index: optionally replace the output (output + outputType) or the result type (resultType, keeping the cells), toggle disabled, and/or update column cells (cells / inputs, same format as add_row) (#685).", "chooser_set_row"),
    delete_row: specBp("mutate", "Delete a row by index (removes its output plus the per-row cell from every column). The rollback puts it back at the same index (#685).", "chooser_delete_row"),
    list_object_references: specBp("read", "List every leaf object reference reachable from a chooser, descending through nested chooser tables. list_rows renders those as an opaque resultType:NestedChooser with an empty output, so the actual PoseSearchDatabase/asset paths were invisible. Each entry reports the owning table, the exact location (e.g. ResultsStructs[3].Asset), the struct type and the current object path. classFilter matches the referenced object's class, pathFilter is a substring on the path (#754).", "chooser_list_object_references"),
    remap_object_references: specBp("mutate", "Repoint object references throughout a chooser's nested structure. Either an exact swap (from + to) or a folder rewrite (fromPrefix + toPrefix), which is the 'adopt vendor choosers into our namespace' case. DRY RUN BY DEFAULT - pass dryRun=false to apply. Object-typed targets are class-checked before assignment; the chooser is recompiled and left dirty rather than saved (#754).", "chooser_remap_object_references"),

    // Types and signature (#1828).
    list_types:       specBp("read", "List the type names an authoring call accepts: kind 'column' (add_column), 'result' (resultType on add_row/set_row/set_fallback), 'parameter' (add_parameter), or 'input' (the input types one column accepts for set_column inputType; needs table + column). Full paths, sorted; short names work too.", "chooser_list_types"),
    get_signature:    specBp("read", "Read a chooser's signature: result type (ObjectResult | ClassResult | NoPrimaryResult), result class, the ChooserSignature asset it inherits from (UE 5.9), and every parameter with its type, class or struct, and full value text. Works on a ChooserTable or a ChooserSignature asset; a nested chooser reports its root's.", "chooser_get_signature"),
    set_signature:    specBp("mutate", "Set a chooser's result type and/or result class, or point it at a ChooserSignature asset to inherit them and the parameters from (UE 5.9; empty clears the reference). Refused while the signature is inherited, except for clearing the reference. Bindings recompile against the new signature.", "chooser_set_signature"),
    add_parameter:    specBp("mutate", "Add a parameter (the object or struct a chooser reads from or writes to) of parameterType, e.g. ContextObjectTypeClass with properties {Class: '/Script/Engine.Character'} or ContextObjectTypeStruct with {Struct: ...}. Returns its index, which column bindings use.", "chooser_add_parameter"),
    remove_parameter: specBp("mutate", "Remove a parameter by index. Column bindings name parameters by index and are not renumbered, so bindings to later parameters need set_column afterwards.", "chooser_remove_parameter"),
    get_property:     specBp("read", "Read any property by path from a column (target 'column'), a row's result ('row'), the fallback result ('fallback') or a parameter ('parameter'), in Unreal text format. Paths are '.' separated with [index], stepping into instanced structs, e.g. 'InputValue.Binding.DisplayName', 'DefaultRowValue', 'Asset'. An empty path reads the whole struct.", "chooser_get_property"),
    set_property:     specBp("mutate", "Write any property by path on a column, a row's result, the fallback result or a parameter (same targets and paths as get_property), in Unreal text format. The value is parsed in full before anything changes. A result change refreshes auto populating columns, as the editor does.", "chooser_set_property"),

    // Columns (#1828).
    delete_column:    specBp("mutate", "Delete a column and its cell in every row.", "chooser_delete_column"),
    move_column:      specBp("mutate", "Move a column so it ends up at newIndex. A randomize column stays last and cannot be moved, and nothing moves after it.", "chooser_move_column"),
    duplicate_column: specBp("mutate", "Duplicate a column with its cells, inserting the copy right after it.", "chooser_duplicate_column"),
    copy_column:      specBp("mutate", "Copy a column with its cells (matched to rows by index) into another table, a nested chooser, or the same table, before insertIndex. The copy's binding keeps its parameter index, so the target needs compatible parameters.", "chooser_copy_column"),
    set_column:       specBp("mutate", "Change a column: disable or enable it, change its input type (list_types kind 'input'), and/or bind its input to a parameter (parameterIndex) through a property chain, e.g. ['Speed'] or ['MyStruct','MyFloat']; an empty chain binds the parameter itself. The chain is checked under the editor's binding rules before anything changes.", "chooser_set_column"),

    // Rows (#1828).
    delete_rows:      specBp("mutate", "Delete several rows at once, with their cells.", "chooser_delete_rows"),
    move_rows:        specBp("mutate", "Move rows, keeping their order, so they sit before targetIndex (a row index from before the move; rowCount moves them to the end), like dragging rows in the editor. Returns the new index of the first moved row.", "chooser_move_rows"),
    duplicate_rows:   specBp("mutate", "Duplicate rows (results, disabled flags and cells), inserting the copies after the last of them. A nested chooser result is deep copied.", "chooser_duplicate_rows"),
    copy_rows:        specBp("mutate", "Copy rows into another table, a nested chooser, or the same table, like copy and paste in the editor: cells go into columns matching by type and input name, and a column is added for cells with no match. Nested choosers are deep copied. includeFallback also copies the fallback result and fallback cells.", "chooser_copy_rows"),

    // Cells and the fallback (#1828).
    get_cell:         specBp("read", "Read one cell (column + row) or a column's fallback cell (fallback=true, output columns only), with its cell type.", "chooser_get_cell"),
    set_cell:         specBp("mutate", "Write one cell (column + row) or a column's fallback cell (fallback=true). The value is struct text like '(Min=0,Max=100)' or a bare scalar, parsed in full before anything changes.", "chooser_set_cell"),
    set_fallback:     specBp("mutate", "Set the fallback result (used when no row passes every filter) from an asset (output + outputType) or a default result of resultType, or remove it (remove=true; output columns keep their fallback cells).", "chooser_set_fallback"),
    auto_populate:    specBp("mutate", "Fill auto populating columns' cells from each row's result: one column (column), some rows (rows, optionally includeFallback), or everything. UE 5.7 and newer.", "chooser_auto_populate"),

    // Nested choosers (#1828).
    create_nested:    specBp("mutate", "Create a nested chooser inside the table's asset and make it a row's result (row), or add a new row for it. Address it afterwards with `nested` on any table action.", "chooser_create_nested"),
    list_nested:      specBp("read", "List the nested objects of a chooser's asset (nested choosers and column-owned objects) with name, path, class and parent.", "chooser_list_nested"),
    delete_nested:    specBp("mutate", "Delete a nested chooser or other nested object from the asset, clearing every row's reference to it.", "chooser_delete_nested"),
    rename_nested:    specBp("mutate", "Rename a nested chooser or other nested object; the name must be unique among the asset's nested objects.", "chooser_rename_nested"),
    restore_snapshot: specBp("mutate", "Put a chooser asset back as a snapshot records it: every table's columns, rows, disabled flags and fallback, recreating nested choosers that were deleted. The snapshot is the rollback payload of the edits that drop data no other action restores (delete_column, delete_rows, a scattered move_rows, auto_populate, delete_nested, remove_disabled).", "chooser_restore_snapshot"),
    remove_disabled:  specBp("mutate", "Delete every disabled row and column (and any column whose type is not loaded), as the editor's Remove Disabled Data does.", "chooser_remove_disabled"),
  },
  {
    // #1057: every key a spec'd handler declares, generated from its C++
    // registration.
    ...specSchema,
  },
);
