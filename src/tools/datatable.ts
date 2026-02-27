import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const dataTableTools: ToolDef[] = [
  bt("read_datatable", "Read a DataTable: row names and data.", {
    assetPath: z.string().describe("Path to the DataTable asset"),
    rowFilter: z.string().optional().describe("Filter rows by name substring"),
  }, "read_datatable", (p) => ({ path: p.assetPath, rowFilter: p.rowFilter })),

  bt("create_datatable", "Create a new DataTable asset with a given row struct.", {
    name: z.string(), packagePath: z.string().optional(),
    rowStruct: z.string().describe("Row struct name (e.g. 'InventoryItem')"),
  }),

  bt("reimport_datatable", "Reimport a DataTable from a JSON file or JSON string.", {
    assetPath: z.string().describe("Path to the DataTable asset"),
    jsonPath: z.string().optional().describe("Absolute path to JSON file"),
    jsonString: z.string().optional().describe("JSON data as string"),
  }, "reimport_datatable", (p) => ({ path: p.assetPath, jsonPath: p.jsonPath, jsonString: p.jsonString })),
];
