import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const landscapeTools: ToolDef[] = [
  bt("get_landscape_info", "Read the level's landscape setup: size, components, material, layers.", {}),
  bt("list_landscape_layers", "List all paint/weight layers on the landscape.", {}),
  bt("sample_landscape", "Sample landscape at world coordinates: height, normal, layer weights.", {
    x: z.number().describe("World X"),
    y: z.number().describe("World Y"),
  }),
  bt("list_landscape_splines", "Read landscape spline data.", {}),
  bt("get_landscape_component", "Inspect a specific landscape component.", {
    componentIndex: z.number().describe("Component index"),
  }),
  bt("sculpt_landscape", "Modify the landscape heightmap.", {
    x: z.number(), y: z.number(),
    radius: z.number().describe("Brush radius"),
    strength: z.number().describe("Brush strength (-1 to 1)"),
    falloff: z.number().optional().describe("Falloff (0–1). Default: 0.5"),
  }),
  bt("paint_landscape_layer", "Paint a weight layer on the landscape.", {
    layerName: z.string(), x: z.number(), y: z.number(),
    radius: z.number(), strength: z.number().optional(),
  }),
  bt("set_landscape_material", "Set the landscape material.", {
    materialPath: z.string().describe("Path to the material asset"),
  }),
  bt("add_landscape_layer_info", "Register a new paint layer on the landscape.", {
    layerName: z.string().describe("Layer name"),
  }),
  bt("import_landscape_heightmap", "Import a heightmap from a file.", {
    filePath: z.string().describe("Absolute path to heightmap file"),
  }),
];
