import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";

export const landscapeTool: ToolDef = categoryTool(
  "landscape",
  "Landscape terrain: info, layers, sculpting, painting, materials, heightmap import.",
  {
    get_info:          bp("get_landscape_info"),
    list_layers:       bp("list_landscape_layers"),
    sample:            bp("sample_landscape"),
    list_splines:      bp("list_landscape_splines"),
    get_component:     bp("get_landscape_component"),
    sculpt:            bp("sculpt_landscape"),
    paint_layer:       bp("paint_landscape_layer"),
    set_material:      bp("set_landscape_material"),
    add_layer_info:    bp("add_landscape_layer_info"),
    import_heightmap:  bp("import_landscape_heightmap"),
  },
  `- get_info: Get landscape setup
- list_layers: List paint layers
- sample: Sample height/layers. Params: x, y
- list_splines: Read landscape splines
- get_component: Inspect component. Params: componentIndex
- sculpt: Sculpt heightmap. Params: x, y, radius, strength, falloff?
- paint_layer: Paint weight layer. Params: layerName, x, y, radius, strength?
- set_material: Set landscape material. Params: materialPath
- add_layer_info: Register paint layer. Params: layerName
- import_heightmap: Import heightmap file. Params: filePath`,
  {
    x: z.number().optional(), y: z.number().optional(),
    radius: z.number().optional(), strength: z.number().optional(),
    falloff: z.number().optional(),
    layerName: z.string().optional(),
    materialPath: z.string().optional(),
    filePath: z.string().optional(),
    componentIndex: z.number().optional(),
  },
);
