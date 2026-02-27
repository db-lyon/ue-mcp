import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const inputTools: ToolDef[] = [
  bt("create_input_action", "Create a new Enhanced Input Action asset.", {
    name: z.string(), packagePath: z.string().optional(),
    valueType: z.string().optional().describe("Value type: 'bool', 'float', 'Vector2D', 'Vector3D'"),
  }),
  bt("create_input_mapping_context", "Create a new Enhanced Input Mapping Context asset.", {
    name: z.string(), packagePath: z.string().optional(),
  }),
  bt("list_input_assets", "List Enhanced Input assets (InputAction, InputMappingContext) in a directory.", {
    directory: z.string().optional(), recursive: z.boolean().optional(),
  }),
];
