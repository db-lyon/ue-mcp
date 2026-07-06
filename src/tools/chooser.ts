import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";

// #685 — ChooserTable (UChooserTable) row authoring. Chooser tables are the
// data-driven selection layer behind Motion Matching: a chooser maps character
// state (Stance x MovementState x Gait) to which PoseSearchDatabase to search.
// Extending locomotion means adding/editing ROWS (a set of input-column
// conditions plus an output object). The engine stores columns/rows as instanced
// structs with no scripting entry point, so this was previously hand-editing only.
export const chooserTool: ToolDef = categoryTool(
  "chooser",
  "Author ChooserTable assets (the data-driven selection layer behind Motion Matching): introspect columns, list/add/edit/delete rows mapping input-column conditions to an output object.",
  {
    create:     bp("Create an empty ChooserTable asset. Add input columns with add_column, then rows with add_row. Params: name, packagePath? (default /Game), onConflict? (#685)", "chooser_create", (p) => ({ name: p.name, packagePath: p.packagePath, onConflict: p.onConflict })),
    describe:   bp("Introspect a ChooserTable: row count, each input column (index, name, columnType, cellType) and the fallback result. Read this first to learn the cell text format each column expects. Params: table (#685)", "chooser_describe", (p) => ({ table: p.table })),
    add_column: bp("Add an input column to a ChooserTable (so rows have a condition to fill). columnType is a Chooser column struct short name, e.g. EnumColumn, BoolColumn, FloatRangeColumn, GameplayTagColumn, ObjectColumn, or an Output* column. Optionally bind its input: inputStruct (parameter struct e.g. EnumContextProperty/BoolContextProperty), boundProperty (context property name to read), enumPath (for enum columns). Sizes the new column's cells to the current rows. Params: table, columnType, inputStruct?, boundProperty?, enumPath? (#685)", "chooser_add_column", (p) => ({ table: p.table, columnType: p.columnType, inputStruct: p.inputStruct, boundProperty: p.boundProperty, enumPath: p.enumPath })),
    list_rows:  bp("List every row: index, disabled flag, output object (resultType + referenced asset path), and each column's cell value as round-trippable text. Params: table (#685)", "chooser_list_rows", (p) => ({ table: p.table })),
    add_row:    bp("Append a row. Set the output via `output` (asset path) + outputType ('asset' hard ref default | 'soft_asset' | 'evaluate' for a nested ChooserTable). Set input-column conditions via `cells` (array aligned to column order) and/or `inputs` (object keyed by column index or name). Cell values are struct text like '(Value=2)' - partial fields are allowed and unspecified ones keep defaults; a bare number/bool works for scalar columns. Params: table, output?, outputType?, cells?, inputs? (#685)", "chooser_add_row", (p) => ({ table: p.table, output: p.output, outputType: p.outputType, cells: p.cells, inputs: p.inputs })),
    set_row:    bp("Edit an existing row by index: optionally replace the output (output + outputType), toggle disabled, and/or update column cells (cells / inputs, same format as add_row). Params: table, index, output?, outputType?, disabled?, cells?, inputs? (#685)", "chooser_set_row", (p) => ({ table: p.table, index: p.index, output: p.output, outputType: p.outputType, disabled: p.disabled, cells: p.cells, inputs: p.inputs })),
    delete_row: bp("Delete a row by index (removes its output plus the per-row cell from every column). Params: table, index (#685)", "chooser_delete_row", (p) => ({ table: p.table, index: p.index })),
  },
  undefined,
  {
    table: z.string().optional().describe("ChooserTable asset path, e.g. /Game/Path/CT_Locomotion"),
    name: z.string().optional().describe("create: new ChooserTable asset name"),
    packagePath: z.string().optional().describe("create: destination package path (default /Game)"),
    onConflict: z.string().optional().describe("create: conflict policy skip (default) | error | overwrite"),
    index: z.number().optional().describe("Row index for set_row / delete_row"),
    output: z.string().optional().describe("Output asset path for the row (a PoseSearchDatabase, a nested ChooserTable, etc.)"),
    outputType: z.string().optional().describe("Output wrapper: 'asset' (hard ref, default) | 'soft_asset' | 'evaluate' (nested ChooserTable reference)"),
    disabled: z.boolean().optional().describe("set_row: enable/disable the row without deleting it"),
    columnType: z.string().optional().describe("add_column: Chooser column struct short name (EnumColumn, BoolColumn, FloatRangeColumn, GameplayTagColumn, ObjectColumn, Output*Column, ...)"),
    inputStruct: z.string().optional().describe("add_column: parameter struct to bind the column input (e.g. EnumContextProperty, BoolContextProperty)"),
    boundProperty: z.string().optional().describe("add_column: context property name the column reads at evaluation time"),
    enumPath: z.string().optional().describe("add_column: enum asset path for an EnumColumn"),
    cells: z.array(z.any()).optional().describe("Per-column cell values aligned to column order; each is struct text like '(Value=2)', or a scalar. null/omitted leaves a column at its default."),
    inputs: z.record(z.any()).optional().describe("Cell values keyed by column index (as string) or column name; same value format as cells"),
  },
);
