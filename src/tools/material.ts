import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";

export const materialTool: ToolDef = categoryTool(
  "material",
  "Materials: create, read, parameters, shading, textures, and graph authoring (expression nodes, connections).",
  {
    read:              bp("read_material", (p) => ({ assetPath: p.assetPath })),
    list_parameters:   bp("list_material_parameters", (p) => ({ assetPath: p.assetPath })),
    set_parameter:     bp("set_material_parameter"),
    create_instance:   bp("create_material_instance"),
    create:            bp("create_material"),
    set_shading_model: bp("set_material_shading_model"),
    set_base_color:    bp("set_material_base_color"),
    connect_texture:   bp("connect_texture_to_material"),
    // Graph authoring
    add_expression:    bp("add_material_expression"),
    connect_expressions: bp("connect_material_expressions"),
    connect_to_property: bp("connect_to_material_property"),
    list_expressions:  bp("list_material_expressions"),
    delete_expression: bp("delete_material_expression"),
    list_expression_types: bp("list_expression_types"),
    recompile:         bp("recompile_material"),
  },
  `- read: Read material structure. Params: assetPath
- list_parameters: List overridable parameters. Params: assetPath
- set_parameter: Set parameter value. Params: assetPath, parameterName, value
- create_instance: Create material instance. Params: parentPath, name?, packagePath?
- create: Create material. Params: name, packagePath?
- set_shading_model: Set shading model. Params: assetPath, shadingModel
- set_base_color: Set base color. Params: assetPath, color {r,g,b,a?}
- connect_texture: Connect texture to property. Params: materialPath, texturePath, property
- add_expression: Add expression node. Params: materialPath, expressionType, x?, y?, properties?
- connect_expressions: Wire two expressions. Params: materialPath, sourceExpression, sourceOutput?, targetExpression, targetInput?
- connect_to_property: Wire expression to material output. Params: materialPath, expressionName, outputName?, property (BaseColor|Normal|Roughness|Metallic|Emissive|...)
- list_expressions: List expression nodes. Params: materialPath
- delete_expression: Remove expression. Params: materialPath, expressionName
- list_expression_types: List available expression types
- recompile: Recompile material. Params: materialPath`,
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
    property: z.string().optional().describe("Material property: BaseColor, Normal, Roughness, Metallic, EmissiveColor, etc."),
    expressionType: z.string().optional().describe("Expression type: Constant, TextureSample, Multiply, Lerp, ScalarParameter, etc."),
    x: z.number().optional(), y: z.number().optional(),
    properties: z.record(z.unknown()).optional(),
    sourceExpression: z.string().optional(),
    sourceOutput: z.string().optional(),
    targetExpression: z.string().optional(),
    targetInput: z.string().optional(),
    expressionName: z.string().optional(),
    outputName: z.string().optional(),
  },
);
