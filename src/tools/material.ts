import { z } from "zod";
import { categoryTool, bp, type ToolDef } from "../types.js";
import { Color } from "../schemas.js";

export const materialTool: ToolDef = categoryTool(
  "material",
  "Materials: create, read, parameters, shading, textures, and graph authoring (expression nodes, connections).",
  {
    read:              bp("read_material", (p) => ({ assetPath: p.assetPath })),
    list_parameters:   bp("list_material_parameters", (p) => ({ assetPath: p.assetPath })),
    set_parameter:     bp("set_material_parameter"),
    set_expression_value: bp("set_expression_value"),
    disconnect_property: bp("disconnect_material_property"),
    create_instance:   bp("create_material_instance"),
    create:            bp("create_material"),
    set_shading_model: bp("set_material_shading_model"),
    set_blend_mode:    bp("set_material_blend_mode"),
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
- set_parameter: Set parameter on MaterialInstance. Params: assetPath, parameterName, parameterType (scalar|vector|texture), value
- set_expression_value: Set value on expression node (Constant, Constant3Vector, etc.). Params: materialPath, expressionIndex, value (number or {r,g,b,a})
- disconnect_property: Disconnect a material property input. Params: materialPath, property (BaseColor|Normal|Roughness|...)
- create_instance: Create material instance. Params: parentPath, name?, packagePath?
- create: Create material. Params: name, packagePath?
- set_shading_model: Set shading model. Params: assetPath, shadingModel
- set_blend_mode: Set blend mode. Params: assetPath, blendMode (Opaque|Masked|Translucent|Additive|Modulate|AlphaComposite|AlphaHoldout)
- set_base_color: Set base color. Params: assetPath, color {r,g,b,a?}
- connect_texture: Connect texture to property. Params: materialPath, texturePath, property
- add_expression: Add expression node. Returns nodeId (use as sourceExpression/targetExpression). Params: materialPath, expressionType, name?, parameterName?, positionX?, positionY?
- connect_expressions: Wire two expressions. Use nodeId from add_expression, or expression name/index/class. Params: materialPath, sourceExpression, sourceOutput?, targetExpression, targetInput?
- connect_to_property: Wire expression to material output. Params: materialPath, expressionName, outputName?, property (BaseColor|Normal|Roughness|Metallic|Emissive|...)
- list_expressions: List expression nodes. Params: materialPath
- delete_expression: Remove expression. Params: materialPath, expressionName
- list_expression_types: List available expression types
- recompile: Recompile material. Params: materialPath`,
  {
    assetPath: z.string().optional(),
    parameterName: z.string().optional(),
    parameterType: z.string().optional().describe("Parameter type for set_parameter: scalar, vector, texture"),
    expressionIndex: z.number().optional(),
    value: z.unknown().optional(),
    parentPath: z.string().optional(),
    name: z.string().optional(),
    packagePath: z.string().optional(),
    shadingModel: z.string().optional(),
    blendMode: z.string().optional().describe("Blend mode: Opaque, Masked, Translucent, Additive, Modulate, AlphaComposite, AlphaHoldout"),
    color: Color.optional(),
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
