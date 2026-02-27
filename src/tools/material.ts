import { z } from "zod";
import { bt, type ToolDef } from "../types.js";

export const materialTools: ToolDef[] = [
  bt("read_material", "Read a material or material instance's parameters and structure.", {
    assetPath: z.string().describe("Path to the material asset"),
  }),
  bt("list_material_parameters", "List all overridable parameters on a material or material instance.", {
    assetPath: z.string().describe("Path to the material asset"),
  }),
  bt("set_material_parameter", "Set a scalar, vector, or texture parameter on a material instance.", {
    assetPath: z.string().describe("Path to the material instance"),
    parameterName: z.string().describe("Parameter name"),
    value: z.unknown().describe("Parameter value (number, {r,g,b,a}, or texture path)"),
  }),
  bt("create_material_instance", "Create a new material instance from a parent material.", {
    parentPath: z.string().describe("Path to the parent material"),
    name: z.string().optional().describe("Asset name"),
    packagePath: z.string().optional().describe("Package path"),
  }),
  bt("create_material", "Create a new Material asset from scratch.", {
    name: z.string().describe("Asset name"),
    packagePath: z.string().optional().describe("Package path (e.g. '/Game/Materials')"),
  }),
  bt("set_material_shading_model", "Set the shading model on a material.", {
    assetPath: z.string().describe("Path to the material"),
    shadingModel: z.string().describe("Shading model (e.g. 'DefaultLit', 'Unlit', 'Subsurface')"),
  }),
  bt("set_material_base_color", "Set the base color of a material to a constant value.", {
    assetPath: z.string().describe("Path to the material"),
    color: z.object({ r: z.number(), g: z.number(), b: z.number(), a: z.number().optional() }).describe("Color value"),
  }),
  bt("connect_texture_to_material", "Connect a texture to a material property.", {
    materialPath: z.string().describe("Path to the material"),
    texturePath: z.string().describe("Path to the texture"),
    property: z.string().describe("Material property (e.g. 'BaseColor', 'Normal', 'Roughness')"),
  }),
];
