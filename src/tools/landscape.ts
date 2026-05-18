import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";
import { Vec3 } from "../schemas.js";

export const landscapeTool: ToolDef = categoryTool(
  "landscape",
  "Landscape terrain: info, layers, sculpting, painting, materials, heightmap import.",
  {
    get_info:          bp("Get landscape setup", "get_landscape_info"),
    list_layers:       bp("List paint layers", "list_landscape_layers"),
    sample:            bp("Sample height/layers. Params: x, y", "sample_landscape"),
    list_splines:      bp("Read landscape splines", "list_landscape_splines"),
    get_component:     bp("Inspect component. Params: componentIndex", "get_landscape_component"),
    set_material:      bp("Set landscape material. Params: materialPath", "set_landscape_material"),
    add_layer_info:    bp("Register paint layer (creates LayerInfo asset + binds to active landscape). Params: layerName, packagePath?, weightBlended?", "add_landscape_layer_info"),
    create_layer_info: bp("Standalone LayerInfo asset creation - no landscape required. Params: layerName, name? (default LI_<layerName>), packagePath? (default /Game/Landscape/LayerInfos), physMaterial? (asset path), hardness? (#251)", "create_landscape_layer_info", (p) => ({ layerName: p.layerName, name: p.name, packagePath: p.packagePath, physMaterial: p.physMaterial, hardness: p.hardness, onConflict: p.onConflict })),
    create:            bp("Spawn a new ALandscape with a flat heightmap. Defaults match the Editor's Landscape Mode 'create new' (8x8 components, 63 quads/subsection, 2 subsections/component = 1016x1016 quads). Params: location? (Vec3), scale? (Vec3, default 100,100,100), componentCountX? (default 8), componentCountY? (default 8), subsectionSizeQuads? (one of 7|15|31|63|127|255, default 63), numSubsections? (1|2, default 2), heightOffset? (uint16, default 32768 = mid-elevation), label? (#303)", "create_landscape", (p) => ({ location: p.location, scale: p.scale, componentCountX: p.componentCountX, componentCountY: p.componentCountY, subsectionSizeQuads: p.subsectionSizeQuads, numSubsections: p.numSubsections, heightOffset: p.heightOffset, label: p.label })),
    get_material_usage_summary: bp("Per-proxy summary: landscape/hole material paths + component/grass/nanite counts (#150)", "get_landscape_material_usage_summary"),
  },
  undefined,
  {
    x: z.number().optional(), y: z.number().optional(),
    radius: z.number().optional(), strength: z.number().optional(),
    falloff: z.number().optional(),
    layerName: z.string().optional(),
    materialPath: z.string().optional(),
    filePath: z.string().optional(),
    componentIndex: z.number().optional(),
    name: z.string().optional(),
    packagePath: z.string().optional(),
    physMaterial: z.string().optional(),
    hardness: z.number().optional(),
    onConflict: z.string().optional(),
    location: Vec3.optional(),
    scale: Vec3.optional(),
    componentCountX: z.number().optional(),
    componentCountY: z.number().optional(),
    subsectionSizeQuads: z.number().optional(),
    numSubsections: z.number().optional(),
    heightOffset: z.number().optional(),
    label: z.string().optional(),
  },
);
