import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";

export const materialTool: ToolDef = categoryTool(
  "material",
  "Materials: create, read, set parameters, shading model, base color, textures.",
  {
    read:              bp("read_material", (p) => ({ assetPath: p.assetPath })),
    list_parameters:   bp("list_material_parameters", (p) => ({ assetPath: p.assetPath })),
    set_parameter:     bp("set_material_parameter"),
    create_instance:   bp("create_material_instance"),
    create:            bp("create_material"),
    set_shading_model: bp("set_material_shading_model"),
    set_base_color:    bp("set_material_base_color"),
    connect_texture:   bp("connect_texture_to_material"),
  },
  `- read: Read material structure. Params: assetPath
- list_parameters: List overridable parameters. Params: assetPath
- set_parameter: Set parameter value. Params: assetPath, parameterName, value
- create_instance: Create material instance. Params: parentPath, name?, packagePath?
- create: Create material. Params: name, packagePath?
- set_shading_model: Set shading model. Params: assetPath, shadingModel
- set_base_color: Set base color. Params: assetPath, color {r,g,b,a?}
- connect_texture: Connect texture to property. Params: materialPath, texturePath, property`,
  {
    assetPath: z.string().optional(),
    parameterName: z.string().optional(),
    value: z.unknown().optional(),
    parentPath: z.string().optional(),
    name: z.string().optional(),
    packagePath: z.string().optional(),
    shadingModel: z.string().optional(),
    color: z.object({ r: z.number(), g: z.number(), b: z.number(), a: z.number().optional() }).optional(),
    materialPath: z.string().optional(),
    texturePath: z.string().optional(),
    property: z.string().optional().describe("Material property: BaseColor, Normal, Roughness, etc."),
  },
);
