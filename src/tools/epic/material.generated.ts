// GENERATED FILE - do not edit.
//
// Written by scripts/generate-epic-actions.mjs from tests/golden/epic-catalog/
// and src/tools/epic/effects.ts. To change what an action DOES, edit the
// effects file and regenerate; to pick up a new engine's toolsets, re-record
// the catalog and regenerate.
//
// These are ordinary actions. They carry a declared effect, real parameters and
// a Params: clause, they are in ALL_TOOLS, and they dispatch through the same
// task factory, guards and locks as every hand-written action in this package.
// Each also carries its input schema, which the compact signatures read (#1172).
import { z } from "zod";
import type { ActionSpec } from "../../core/types.js";
import { bp } from "../../surface/category-tool.js";

const S_epic_add_expression = {"properties":{"material_or_function":{"type":"object","properties":{"refPath":{}}},"expression_class":{"type":"object","properties":{"refPath":{}}},"x":{"type":"integer"},"y":{"type":"integer"}},"required":["material_or_function","expression_class"]} as const;
const S_epic_add_expression__1 = {"properties":{"materialOrFunction":{"type":"object","properties":{"refPath":{}}},"expressionClass":{"type":"object","properties":{"refPath":{}}},"x":{"type":"integer"},"y":{"type":"integer"}},"required":["materialOrFunction","expressionClass"]} as const;
const S_epic_clear_parameters = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}}},"required":["instance"]} as const;
const S_epic_clear_parameters__1 = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}}},"required":["instance"]} as const;
const S_epic_connect_expressions = {"properties":{"from_expression":{"type":"object","properties":{"refPath":{}}},"from_output_name":{"type":"string"},"to_expression":{"type":"object","properties":{"refPath":{}}},"to_input_name":{"type":"string"}},"required":["from_expression","from_output_name","to_expression","to_input_name"]} as const;
const S_epic_connect_expressions__1 = {"properties":{"fromExpression":{"type":"object","properties":{"refPath":{}}},"fromOutputName":{"type":"string"},"toExpression":{"type":"object","properties":{"refPath":{}}},"toInputName":{"type":"string"}},"required":["fromExpression","fromOutputName","toExpression","toInputName"]} as const;
const S_epic_connect_to_output = {"properties":{"expression":{"type":"object","properties":{"refPath":{}}},"output_name":{"type":"string"},"material_property":{"type":"string","enum":["MP_EmissiveColor","MP_Opacity","MP_OpacityMask","MP_DiffuseColor","MP_SpecularColor","MP_BaseColor","MP_Metallic","MP_Specular","MP_Roughness","MP_Anisotropy","MP_Normal","MP_Tangent","MP_WorldPositionOffset","MP_WorldDisplacement_DEPRECATED","MP_TessellationMultiplier_DEPRECATED","MP_SubsurfaceColor","MP_CustomData0","MP_CustomData1","MP_AmbientOcclusion","MP_Refraction","MP_CustomizedUVs0","MP_CustomizedUVs1","MP_CustomizedUVs2","MP_CustomizedUVs3","MP_CustomizedUVs4","MP_CustomizedUVs5","MP_CustomizedUVs6","MP_CustomizedUVs7","MP_PixelDepthOffset","MP_ShadingModel","MP_FrontMaterial","MP_SurfaceThickness","MP_Displacement","MP_MaterialAttributes","MP_CustomOutput","MP_LastCustomizedUVs","MP_NumCustomizedUVs"]}},"required":["expression","output_name","material_property"]} as const;
const S_epic_connect_to_output__1 = {"properties":{"expression":{"type":"object","properties":{"refPath":{}}},"outputName":{"type":"string"},"materialProperty":{"type":"string","enum":["MP_EmissiveColor","MP_Opacity","MP_OpacityMask","MP_BaseColor","MP_Metallic","MP_Specular","MP_Roughness","MP_Anisotropy","MP_Normal","MP_Tangent","MP_WorldPositionOffset","MP_SubsurfaceColor","MP_CustomData0","MP_CustomData1","MP_AmbientOcclusion","MP_Refraction","MP_CustomizedUVs0","MP_CustomizedUVs1","MP_CustomizedUVs2","MP_CustomizedUVs3","MP_CustomizedUVs4","MP_CustomizedUVs5","MP_CustomizedUVs6","MP_CustomizedUVs7","MP_PixelDepthOffset","MP_ShadingModel","MP_FrontMaterial","MP_SurfaceThickness","MP_Displacement","MP_MaterialAttributes","MP_LastCustomizedUVs","MP_NumCustomizedUVs"]}},"required":["expression","outputName","materialProperty"]} as const;
const S_epic_create = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"},"parent":{"type":"object","properties":{"refPath":{}}}},"required":["folder_path","asset_name","parent"]} as const;
const S_epic_create__1 = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"},"parent":{"type":"object","properties":{"refPath":{}}}},"required":["folderPath","assetName","parent"]} as const;
const S_epic_create_function = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"}},"required":["folder_path","asset_name"]} as const;
const S_epic_create_function__1 = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"}},"required":["folderPath","assetName"]} as const;
const S_epic_create_material = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"}},"required":["folder_path","asset_name"]} as const;
const S_epic_create_material__1 = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"}},"required":["folderPath","assetName"]} as const;
const S_epic_create_parameter_collection = {"properties":{"folder_path":{"type":"string"},"asset_name":{"type":"string"}},"required":["folder_path","asset_name"]} as const;
const S_epic_create_parameter_collection__1 = {"properties":{"folderPath":{"type":"string"},"assetName":{"type":"string"}},"required":["folderPath","assetName"]} as const;
const S_epic_delete_expression = {"properties":{"material_or_function":{"type":"object","properties":{"refPath":{}}},"expression":{"type":"object","properties":{"refPath":{}}}},"required":["material_or_function","expression"]} as const;
const S_epic_delete_expression__1 = {"properties":{"materialOrFunction":{"type":"object","properties":{"refPath":{}}},"expression":{"type":"object","properties":{"refPath":{}}}},"required":["materialOrFunction","expression"]} as const;
const S_epic_delete_parameter_group = {"properties":{"material_or_function":{"type":"object","properties":{"refPath":{}}},"group_name":{"type":"string"}},"required":["material_or_function","group_name"]} as const;
const S_epic_delete_parameter_group__1 = {"properties":{"materialOrFunction":{"type":"object","properties":{"refPath":{}}},"groupName":{"type":"string"}},"required":["materialOrFunction","groupName"]} as const;
const S_epic_delete_unused_expressions = {"properties":{"material":{"type":"object","properties":{"refPath":{}}}},"required":["material"]} as const;
const S_epic_delete_unused_expressions__1 = {"properties":{"material":{"type":"object","properties":{"refPath":{}}}},"required":["material"]} as const;
const S_epic_diff = {"properties":{"oldAssetPath":{"type":"string"},"newAssetPath":{"type":"string"}},"required":["oldAssetPath","newAssetPath"]} as const;
const S_epic_diff_function = {"properties":{"oldAssetPath":{"type":"string"},"newAssetPath":{"type":"string"}},"required":["oldAssetPath","newAssetPath"]} as const;
const S_epic_diff_material = {"properties":{"oldAssetPath":{"type":"string"},"newAssetPath":{"type":"string"}},"required":["oldAssetPath","newAssetPath"]} as const;
const S_epic_disconnect_expressions = {"properties":{"to_expression":{"type":"object","properties":{"refPath":{}}},"to_input_name":{"type":"string"}},"required":["to_expression","to_input_name"]} as const;
const S_epic_disconnect_expressions__1 = {"properties":{"toExpression":{"type":"object","properties":{"refPath":{}}},"toInputName":{"type":"string"}},"required":["toExpression","toInputName"]} as const;
const S_epic_disconnect_from_output = {"properties":{"material":{"type":"object","properties":{"refPath":{}}},"material_property":{"type":"string","enum":["MP_EmissiveColor","MP_Opacity","MP_OpacityMask","MP_DiffuseColor","MP_SpecularColor","MP_BaseColor","MP_Metallic","MP_Specular","MP_Roughness","MP_Anisotropy","MP_Normal","MP_Tangent","MP_WorldPositionOffset","MP_WorldDisplacement_DEPRECATED","MP_TessellationMultiplier_DEPRECATED","MP_SubsurfaceColor","MP_CustomData0","MP_CustomData1","MP_AmbientOcclusion","MP_Refraction","MP_CustomizedUVs0","MP_CustomizedUVs1","MP_CustomizedUVs2","MP_CustomizedUVs3","MP_CustomizedUVs4","MP_CustomizedUVs5","MP_CustomizedUVs6","MP_CustomizedUVs7","MP_PixelDepthOffset","MP_ShadingModel","MP_FrontMaterial","MP_SurfaceThickness","MP_Displacement","MP_MaterialAttributes","MP_CustomOutput","MP_LastCustomizedUVs","MP_NumCustomizedUVs"]}},"required":["material","material_property"]} as const;
const S_epic_disconnect_from_output__1 = {"properties":{"material":{"type":"object","properties":{"refPath":{}}},"materialProperty":{"type":"string","enum":["MP_EmissiveColor","MP_Opacity","MP_OpacityMask","MP_BaseColor","MP_Metallic","MP_Specular","MP_Roughness","MP_Anisotropy","MP_Normal","MP_Tangent","MP_WorldPositionOffset","MP_SubsurfaceColor","MP_CustomData0","MP_CustomData1","MP_AmbientOcclusion","MP_Refraction","MP_CustomizedUVs0","MP_CustomizedUVs1","MP_CustomizedUVs2","MP_CustomizedUVs3","MP_CustomizedUVs4","MP_CustomizedUVs5","MP_CustomizedUVs6","MP_CustomizedUVs7","MP_PixelDepthOffset","MP_ShadingModel","MP_FrontMaterial","MP_SurfaceThickness","MP_Displacement","MP_MaterialAttributes","MP_LastCustomizedUVs","MP_NumCustomizedUVs"]}},"required":["material","materialProperty"]} as const;
const S_epic_get_expression_input_names = {"properties":{"expression":{"type":"object","properties":{"refPath":{}}}},"required":["expression"]} as const;
const S_epic_get_expression_input_names__1 = {"properties":{"expression":{"type":"object","properties":{"refPath":{}}}},"required":["expression"]} as const;
const S_epic_get_expression_inputs = {"properties":{"material_or_function":{"type":"object","properties":{"refPath":{}}},"expression":{"type":"object","properties":{"refPath":{}}}},"required":["material_or_function","expression"]} as const;
const S_epic_get_expression_inputs__1 = {"properties":{"materialOrFunction":{"type":"object","properties":{"refPath":{}}},"expression":{"type":"object","properties":{"refPath":{}}}},"required":["materialOrFunction","expression"]} as const;
const S_epic_get_expression_output_names = {"properties":{"expression":{"type":"object","properties":{"refPath":{}}}},"required":["expression"]} as const;
const S_epic_get_expression_output_names__1 = {"properties":{"expression":{"type":"object","properties":{"refPath":{}}}},"required":["expression"]} as const;
const S_epic_get_expressions = {"properties":{"material_or_function":{"type":"object","properties":{"refPath":{}}}},"required":["material_or_function"]} as const;
const S_epic_get_expressions__1 = {"properties":{"materialOrFunction":{"type":"object","properties":{"refPath":{}}}},"required":["materialOrFunction"]} as const;
const S_epic_get_property_input = {"properties":{"material":{"type":"object","properties":{"refPath":{}}},"material_property":{"type":"string","enum":["MP_EmissiveColor","MP_Opacity","MP_OpacityMask","MP_DiffuseColor","MP_SpecularColor","MP_BaseColor","MP_Metallic","MP_Specular","MP_Roughness","MP_Anisotropy","MP_Normal","MP_Tangent","MP_WorldPositionOffset","MP_WorldDisplacement_DEPRECATED","MP_TessellationMultiplier_DEPRECATED","MP_SubsurfaceColor","MP_CustomData0","MP_CustomData1","MP_AmbientOcclusion","MP_Refraction","MP_CustomizedUVs0","MP_CustomizedUVs1","MP_CustomizedUVs2","MP_CustomizedUVs3","MP_CustomizedUVs4","MP_CustomizedUVs5","MP_CustomizedUVs6","MP_CustomizedUVs7","MP_PixelDepthOffset","MP_ShadingModel","MP_FrontMaterial","MP_SurfaceThickness","MP_Displacement","MP_MaterialAttributes","MP_CustomOutput","MP_LastCustomizedUVs","MP_NumCustomizedUVs"]}},"required":["material","material_property"]} as const;
const S_epic_get_property_input__1 = {"properties":{"material":{"type":"object","properties":{"refPath":{}}},"materialProperty":{"type":"string","enum":["MP_EmissiveColor","MP_Opacity","MP_OpacityMask","MP_BaseColor","MP_Metallic","MP_Specular","MP_Roughness","MP_Anisotropy","MP_Normal","MP_Tangent","MP_WorldPositionOffset","MP_SubsurfaceColor","MP_CustomData0","MP_CustomData1","MP_AmbientOcclusion","MP_Refraction","MP_CustomizedUVs0","MP_CustomizedUVs1","MP_CustomizedUVs2","MP_CustomizedUVs3","MP_CustomizedUVs4","MP_CustomizedUVs5","MP_CustomizedUVs6","MP_CustomizedUVs7","MP_PixelDepthOffset","MP_ShadingModel","MP_FrontMaterial","MP_SurfaceThickness","MP_Displacement","MP_MaterialAttributes","MP_LastCustomizedUVs","MP_NumCustomizedUVs"]}},"required":["material","materialProperty"]} as const;
const S_epic_get_referencing_materials = {"properties":{"material_function":{"type":"object","properties":{"refPath":{}}}},"required":["material_function"]} as const;
const S_epic_get_referencing_materials__1 = {"properties":{"materialFunction":{"type":"object","properties":{"refPath":{}}}},"required":["materialFunction"]} as const;
const S_epic_get_scalar_parameter = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["instance","name"]} as const;
const S_epic_get_scalar_parameter__1 = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["instance","name"]} as const;
const S_epic_get_static_switch_parameter = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["instance","name"]} as const;
const S_epic_get_static_switch_parameter__1 = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["instance","name"]} as const;
const S_epic_get_statistics = {"properties":{"material":{"type":"object","properties":{"refPath":{}}}},"required":["material"]} as const;
const S_epic_get_texture_parameter = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["instance","name"]} as const;
const S_epic_get_texture_parameter__1 = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["instance","name"]} as const;
const S_epic_get_vector_parameter = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["instance","name"]} as const;
const S_epic_get_vector_parameter__1 = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"}},"required":["instance","name"]} as const;
const S_epic_layout_expressions = {"properties":{"material_or_function":{"type":"object","properties":{"refPath":{}}}},"required":["material_or_function"]} as const;
const S_epic_layout_expressions__1 = {"properties":{"materialOrFunction":{"type":"object","properties":{"refPath":{}}}},"required":["materialOrFunction"]} as const;
const S_epic_list_expression_classes = {"properties":{"material_or_function":{"type":"object","properties":{"refPath":{}}},"search":{"type":"string"}},"required":["material_or_function","search"]} as const;
const S_epic_list_expression_classes__1 = {"properties":{"materialOrFunction":{"type":"object","properties":{"refPath":{}}},"search":{"type":"string"}},"required":["materialOrFunction"]} as const;
const S_epic_list_parameter_groups = {"properties":{"material_or_function":{"type":"object","properties":{"refPath":{}}}},"required":["material_or_function"]} as const;
const S_epic_list_parameter_groups__1 = {"properties":{"materialOrFunction":{"type":"object","properties":{"refPath":{}}}},"required":["materialOrFunction"]} as const;
const S_epic_list_parameters = {"properties":{"material":{"type":"object","properties":{"refPath":{}}}},"required":["material"]} as const;
const S_epic_list_parameters__1 = {"properties":{"material":{"type":"object","properties":{"refPath":{}}}},"required":["material"]} as const;
const S_epic_recompile = {"properties":{"material_or_function":{"type":"object","properties":{"refPath":{}}}},"required":["material_or_function"]} as const;
const S_epic_recompile__1 = {"properties":{"materialOrFunction":{"type":"object","properties":{"refPath":{}}}},"required":["materialOrFunction"]} as const;
const S_epic_rename_parameter_group = {"properties":{"material_or_function":{"type":"object","properties":{"refPath":{}}},"old_name":{"type":"string"},"new_name":{"type":"string"}},"required":["material_or_function","old_name","new_name"]} as const;
const S_epic_rename_parameter_group__1 = {"properties":{"materialOrFunction":{"type":"object","properties":{"refPath":{}}},"oldName":{"type":"string"},"newName":{"type":"string"}},"required":["materialOrFunction","oldName","newName"]} as const;
const S_epic_set_parameter_override = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"override":{"type":"boolean"}},"required":["instance","name","override"]} as const;
const S_epic_set_parameter_override__1 = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"bOverride":{"type":"boolean"}},"required":["instance","name","bOverride"]} as const;
const S_epic_set_parent = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"parent":{"type":"object","properties":{"refPath":{}}}},"required":["instance","parent"]} as const;
const S_epic_set_parent__1 = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"parent":{"type":"object","properties":{"refPath":{}}}},"required":["instance","parent"]} as const;
const S_epic_set_scalar_parameter = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"value":{"type":"number"}},"required":["instance","name","value"]} as const;
const S_epic_set_scalar_parameter__1 = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"value":{"type":"number"}},"required":["instance","name","value"]} as const;
const S_epic_set_static_switch_parameter = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"value":{"type":"boolean"}},"required":["instance","name","value"]} as const;
const S_epic_set_static_switch_parameter__1 = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"value":{"type":"boolean"}},"required":["instance","name","value"]} as const;
const S_epic_set_texture_parameter = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"value":{"type":"object","properties":{"refPath":{}}}},"required":["instance","name","value"]} as const;
const S_epic_set_texture_parameter__1 = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"value":{"type":"object","properties":{"refPath":{}}}},"required":["instance","name","value"]} as const;
const S_epic_set_vector_parameter = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"value":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}}},"required":["instance","name","value"]} as const;
const S_epic_set_vector_parameter__1 = {"properties":{"instance":{"type":"object","properties":{"refPath":{}}},"name":{"type":"string"},"value":{"type":"object","properties":{"r":{"type":"number"},"g":{"type":"number"},"b":{"type":"number"},"a":{"type":"number"}}}},"required":["instance","name","value"]} as const;

/** 39 wrapped engine tools routed to the `material` category. */
export const actions: Record<string, ActionSpec> = {
  epic_add_expression: {
    epicSchema: S_epic_add_expression,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.add_expression", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialToolset", name: "EditorToolset.MaterialToolset.AddExpression", engines: ["5.9"], schema: S_epic_add_expression__1, rename: {"material_or_function":"materialOrFunction","expression_class":"expressionClass"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Adds a new expression node to a Material or MaterialFunction graph. Use list_expression_classes to discover available types. Params: material_or_function, expression_class, x?, y?. On UE 5.9 it calls EditorToolset.MaterialToolset.AddExpression (Params: materialOrFunction, expressionClass, x?, y?).", "epic_call_tool"),
  },
  epic_clear_parameters: {
    epicSchema: S_epic_clear_parameters,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.clear_parameters", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialInstanceToolset", name: "EditorToolset.MaterialInstanceToolset.ClearParameters", engines: ["5.9"], schema: S_epic_clear_parameters__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Clears all parameter overrides on a material instance, reverting to parent defaults. Params: instance. On UE 5.9 it calls EditorToolset.MaterialInstanceToolset.ClearParameters (Params: instance).", "epic_call_tool"),
  },
  epic_connect_expressions: {
    epicSchema: S_epic_connect_expressions,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.connect_expressions", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialToolset", name: "EditorToolset.MaterialToolset.ConnectExpressions", engines: ["5.9"], schema: S_epic_connect_expressions__1, rename: {"from_expression":"fromExpression","from_output_name":"fromOutputName","to_expression":"toExpression","to_input_name":"toInputName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Connects an expression node's output pin to another expression node's input pin. Params: from_expression, from_output_name, to_expression, to_input_name. On UE 5.9 it calls EditorToolset.MaterialToolset.ConnectExpressions (Params: fromExpression, fromOutputName, toExpression, toInputName).", "epic_call_tool"),
  },
  epic_connect_to_output: {
    epicSchema: S_epic_connect_to_output,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.connect_to_output", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialToolset", name: "EditorToolset.MaterialToolset.ConnectToOutput", engines: ["5.9"], schema: S_epic_connect_to_output__1, rename: {"output_name":"outputName","material_property":"materialProperty"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Connects an expression node's output to one of the material's output properties. Params: expression, output_name, material_property. On UE 5.9 it calls EditorToolset.MaterialToolset.ConnectToOutput (Params: expression, outputName, materialProperty).", "epic_call_tool"),
  },
  epic_create: {
    epicSchema: S_epic_create,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.create", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialInstanceToolset", name: "EditorToolset.MaterialInstanceToolset.Create", engines: ["5.9"], schema: S_epic_create__1, rename: {"folder_path":"folderPath","asset_name":"assetName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Creates a new MaterialInstanceConstant asset derived from a parent material. Material instances expose the parent's parameters without triggering a full shader recompile when parameter values change. Params: folder_path, asset_name, parent. On UE 5.9 it calls EditorToolset.MaterialInstanceToolset.Create (Params: folderPath, assetName, parent).", "epic_call_tool"),
  },
  epic_create_function: {
    epicSchema: S_epic_create_function,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.create_function", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialToolset", name: "EditorToolset.MaterialToolset.CreateFunction", engines: ["5.9"], schema: S_epic_create_function__1, rename: {"folder_path":"folderPath","asset_name":"assetName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Creates a new empty MaterialFunction asset. Params: folder_path, asset_name. On UE 5.9 it calls EditorToolset.MaterialToolset.CreateFunction (Params: folderPath, assetName).", "epic_call_tool"),
  },
  epic_create_material: {
    epicSchema: S_epic_create_material,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.create_material", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialToolset", name: "EditorToolset.MaterialToolset.CreateMaterial", engines: ["5.9"], schema: S_epic_create_material__1, rename: {"folder_path":"folderPath","asset_name":"assetName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Creates a new empty Material asset. Warning: Each new Material increases shader compile times. Prefer creating a MaterialInstance from an existing Material where possible. Params: folder_path, asset_name. On UE 5.9 it calls EditorToolset.MaterialToolset.CreateMaterial (Params: folderPath, assetName).", "epic_call_tool"),
  },
  epic_create_parameter_collection: {
    epicSchema: S_epic_create_parameter_collection,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.create_parameter_collection", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialToolset", name: "EditorToolset.MaterialToolset.CreateParameterCollection", engines: ["5.9"], schema: S_epic_create_parameter_collection__1, rename: {"folder_path":"folderPath","asset_name":"assetName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Creates a new empty MaterialParameterCollection (MPC) asset. An MPC holds named Scalar and Vector parameters with default values that materials can reference at runtime without recompiling shaders. Params: folder_path, asset_name. On UE 5.9 it calls EditorToolset.MaterialToolset.CreateParameterCollection (Params: folderPath, assetName).", "epic_call_tool"),
  },
  epic_delete_expression: {
    epicSchema: S_epic_delete_expression,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.delete_expression", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialToolset", name: "EditorToolset.MaterialToolset.DeleteExpression", engines: ["5.9"], schema: S_epic_delete_expression__1, rename: {"material_or_function":"materialOrFunction"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Removes an expression node from a Material or MaterialFunction graph. Params: material_or_function, expression. On UE 5.9 it calls EditorToolset.MaterialToolset.DeleteExpression (Params: materialOrFunction, expression).", "epic_call_tool"),
  },
  epic_delete_parameter_group: {
    epicSchema: S_epic_delete_parameter_group,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.delete_parameter_group", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialToolset", name: "EditorToolset.MaterialToolset.DeleteParameterGroup", engines: ["5.9"], schema: S_epic_delete_parameter_group__1, rename: {"material_or_function":"materialOrFunction","group_name":"groupName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Removes a parameter group, ungrouping all parameters that belong to it. The parameter expressions themselves are not deleted - only their group assignment is cleared. Params: material_or_function, group_name. On UE 5.9 it calls EditorToolset.MaterialToolset.DeleteParameterGroup (Params: materialOrFunction, groupName).", "epic_call_tool"),
  },
  epic_delete_unused_expressions: {
    epicSchema: S_epic_delete_unused_expressions,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.delete_unused_expressions", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialToolset", name: "EditorToolset.MaterialToolset.DeleteUnusedExpressions", engines: ["5.9"], schema: S_epic_delete_unused_expressions__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Deletes all expression nodes not connected to any material output. Useful for cleaning up a material graph after reorganising or after the AI has added experimental nodes that were later abandoned. Params: material. On UE 5.9 it calls EditorToolset.MaterialToolset.DeleteUnusedExpressions (Params: material).", "epic_call_tool"),
  },
  epic_diff: {
    epicSchema: S_epic_diff,
    epicTool: { toolset: "EditorToolset.MaterialInstanceToolset", name: "EditorToolset.MaterialInstanceToolset.Diff", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.MaterialInstanceToolset] Returns a human-readable text diff between two MaterialInstanceConstant assets. Available on UE 5.9; absent from UE 5.8. Params: oldAssetPath, newAssetPath", "epic_call_tool"),
  },
  epic_diff_function: {
    epicSchema: S_epic_diff_function,
    epicTool: { toolset: "EditorToolset.MaterialToolset", name: "EditorToolset.MaterialToolset.DiffFunction", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.MaterialToolset] Returns a human-readable text diff between two MaterialFunction assets. Available on UE 5.9; absent from UE 5.8. Params: oldAssetPath, newAssetPath", "epic_call_tool"),
  },
  epic_diff_material: {
    epicSchema: S_epic_diff_material,
    epicTool: { toolset: "EditorToolset.MaterialToolset", name: "EditorToolset.MaterialToolset.DiffMaterial", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.MaterialToolset] Returns a human-readable text diff between two Material assets. Available on UE 5.9; absent from UE 5.8. Params: oldAssetPath, newAssetPath", "epic_call_tool"),
  },
  epic_disconnect_expressions: {
    epicSchema: S_epic_disconnect_expressions,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.disconnect_expressions", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialToolset", name: "EditorToolset.MaterialToolset.DisconnectExpressions", engines: ["5.9"], schema: S_epic_disconnect_expressions__1, rename: {"to_expression":"toExpression","to_input_name":"toInputName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Disconnects the input pin of an expression node, removing whatever is connected to it. Params: to_expression, to_input_name. On UE 5.9 it calls EditorToolset.MaterialToolset.DisconnectExpressions (Params: toExpression, toInputName).", "epic_call_tool"),
  },
  epic_disconnect_from_output: {
    epicSchema: S_epic_disconnect_from_output,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.disconnect_from_output", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialToolset", name: "EditorToolset.MaterialToolset.DisconnectFromOutput", engines: ["5.9"], schema: S_epic_disconnect_from_output__1, rename: {"material_property":"materialProperty"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Disconnects the expression currently connected to a material output property. Params: material, material_property. On UE 5.9 it calls EditorToolset.MaterialToolset.DisconnectFromOutput (Params: material, materialProperty).", "epic_call_tool"),
  },
  epic_get_expression_input_names: {
    epicSchema: S_epic_get_expression_input_names,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.get_expression_input_names", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialToolset", name: "EditorToolset.MaterialToolset.GetExpressionInputNames", engines: ["5.9"], schema: S_epic_get_expression_input_names__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.material.MaterialTools] Returns the names of all input pins on a material expression node. Use these names as to_input_name when calling connect_expressions. Params: expression. On UE 5.9 it calls EditorToolset.MaterialToolset.GetExpressionInputNames (Params: expression).", "epic_call_tool"),
  },
  epic_get_expression_inputs: {
    epicSchema: S_epic_get_expression_inputs,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.get_expression_inputs", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialToolset", name: "EditorToolset.MaterialToolset.GetExpressionInputs", engines: ["5.9"], schema: S_epic_get_expression_inputs__1, rename: {"material_or_function":"materialOrFunction"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.material.MaterialTools] Returns the current wiring of each input pin on a material expression. Use after building or modifying a graph to verify the wiring matches expectations. Params: material_or_function, expression. On UE 5.9 it calls EditorToolset.MaterialToolset.GetExpressionInputs (Params: materialOrFunction, expression).", "epic_call_tool"),
  },
  epic_get_expression_output_names: {
    epicSchema: S_epic_get_expression_output_names,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.get_expression_output_names", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialToolset", name: "EditorToolset.MaterialToolset.GetExpressionOutputNames", engines: ["5.9"], schema: S_epic_get_expression_output_names__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.material.MaterialTools] Returns the names of all output pins on a material expression node. Use these names as from_output_name when calling connect_expressions or connect_to_output. The empty string represents the default (first) output of nodes that expose only an unnamed output. Params: expression. On UE 5.9 it calls EditorToolset.MaterialToolset.GetExpressionOutputNames (Params: expression).", "epic_call_tool"),
  },
  epic_get_expressions: {
    epicSchema: S_epic_get_expressions,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.get_expressions", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialToolset", name: "EditorToolset.MaterialToolset.GetExpressions", engines: ["5.9"], schema: S_epic_get_expressions__1, rename: {"material_or_function":"materialOrFunction"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.material.MaterialTools] Returns all expression nodes in a Material or MaterialFunction graph. Params: material_or_function. On UE 5.9 it calls EditorToolset.MaterialToolset.GetExpressions (Params: materialOrFunction).", "epic_call_tool"),
  },
  epic_get_property_input: {
    epicSchema: S_epic_get_property_input,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.get_property_input", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialToolset", name: "EditorToolset.MaterialToolset.GetPropertyInput", engines: ["5.9"], schema: S_epic_get_property_input__1, rename: {"material_property":"materialProperty"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.material.MaterialTools] Returns the expression and output pin feeding a material output property. Use to inspect what drives MP_EmissiveColor, MP_Opacity, MP_BaseColor, etc. Params: material, material_property. On UE 5.9 it calls EditorToolset.MaterialToolset.GetPropertyInput (Params: material, materialProperty).", "epic_call_tool"),
  },
  epic_get_referencing_materials: {
    epicSchema: S_epic_get_referencing_materials,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.get_referencing_materials", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialToolset", name: "EditorToolset.MaterialToolset.GetReferencingMaterials", engines: ["5.9"], schema: S_epic_get_referencing_materials__1, rename: {"material_function":"materialFunction"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.material.MaterialTools] Returns asset data for all Materials that reference this MaterialFunction. Params: material_function. On UE 5.9 it calls EditorToolset.MaterialToolset.GetReferencingMaterials (Params: materialFunction).", "epic_call_tool"),
  },
  epic_get_scalar_parameter: {
    epicSchema: S_epic_get_scalar_parameter,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.get_scalar_parameter", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialInstanceToolset", name: "EditorToolset.MaterialInstanceToolset.GetScalarParameter", engines: ["5.9"], schema: S_epic_get_scalar_parameter__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Gets the current value of a scalar parameter on a material instance. Params: instance, name. On UE 5.9 it calls EditorToolset.MaterialInstanceToolset.GetScalarParameter (Params: instance, name).", "epic_call_tool"),
  },
  epic_get_static_switch_parameter: {
    epicSchema: S_epic_get_static_switch_parameter,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.get_static_switch_parameter", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialInstanceToolset", name: "EditorToolset.MaterialInstanceToolset.GetStaticSwitchParameter", engines: ["5.9"], schema: S_epic_get_static_switch_parameter__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Gets the value of a static switch parameter on a material instance. Params: instance, name. On UE 5.9 it calls EditorToolset.MaterialInstanceToolset.GetStaticSwitchParameter (Params: instance, name).", "epic_call_tool"),
  },
  epic_get_statistics: {
    epicSchema: S_epic_get_statistics,
    epicTool: { toolset: "EditorToolset.MaterialToolset", name: "EditorToolset.MaterialToolset.GetStatistics", engines: ["5.9"] },
    ...bp("read", "[Epic EditorToolset.MaterialToolset] Returns compiled shader statistics for a Material or MaterialInstance on the current shader platform: instruction counts, texture-sample counts, and sampler usage. Available on UE 5.9; absent from UE 5.8. Params: material", "epic_call_tool"),
  },
  epic_get_texture_parameter: {
    epicSchema: S_epic_get_texture_parameter,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.get_texture_parameter", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialInstanceToolset", name: "EditorToolset.MaterialInstanceToolset.GetTextureParameter", engines: ["5.9"], schema: S_epic_get_texture_parameter__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Gets the texture assigned to a texture parameter on a material instance. Params: instance, name. On UE 5.9 it calls EditorToolset.MaterialInstanceToolset.GetTextureParameter (Params: instance, name).", "epic_call_tool"),
  },
  epic_get_vector_parameter: {
    epicSchema: S_epic_get_vector_parameter,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.get_vector_parameter", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialInstanceToolset", name: "EditorToolset.MaterialInstanceToolset.GetVectorParameter", engines: ["5.9"], schema: S_epic_get_vector_parameter__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Gets the current value of a vector parameter on a material instance. Params: instance, name. On UE 5.9 it calls EditorToolset.MaterialInstanceToolset.GetVectorParameter (Params: instance, name).", "epic_call_tool"),
  },
  epic_layout_expressions: {
    epicSchema: S_epic_layout_expressions,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.layout_expressions", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialToolset", name: "EditorToolset.MaterialToolset.LayoutExpressions", engines: ["5.9"], schema: S_epic_layout_expressions__1, rename: {"material_or_function":"materialOrFunction"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Automatically arranges all expression nodes in a Material or MaterialFunction graph. Params: material_or_function. On UE 5.9 it calls EditorToolset.MaterialToolset.LayoutExpressions (Params: materialOrFunction).", "epic_call_tool"),
  },
  epic_list_expression_classes: {
    epicSchema: S_epic_list_expression_classes,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.list_expression_classes", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialToolset", name: "EditorToolset.MaterialToolset.ListExpressionClasses", engines: ["5.9"], schema: S_epic_list_expression_classes__1, rename: {"material_or_function":"materialOrFunction"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.material.MaterialTools] Returns MaterialExpression subclasses valid for the given context. Use the results with add_expression. Pass a search string to filter by name, e.g. 'Multiply' or 'Parameter'. Params: material_or_function, search. On UE 5.9 it calls EditorToolset.MaterialToolset.ListExpressionClasses (Params: materialOrFunction, search?).", "epic_call_tool"),
  },
  epic_list_parameter_groups: {
    epicSchema: S_epic_list_parameter_groups,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.list_parameter_groups", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialToolset", name: "EditorToolset.MaterialToolset.ListParameterGroups", engines: ["5.9"], schema: S_epic_list_parameter_groups__1, rename: {"material_or_function":"materialOrFunction"} }] },
    ...bp("read", "[Epic editor_toolset.toolsets.material.MaterialTools] Returns the unique parameter group names defined in a Material or MaterialFunction. Parameters are organised into groups in the Material Instance editor. This returns the distinct set of group names found across all parameter expressions in the graph. The empty string represents parameters that have not been assigned to a named group. Params: material_or_function. On UE 5.9 it calls EditorToolset.MaterialToolset.ListParameterGroups (Params: materialOrFunction).", "epic_call_tool"),
  },
  epic_list_parameters: {
    epicSchema: S_epic_list_parameters,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.list_parameters", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialInstanceToolset", name: "EditorToolset.MaterialInstanceToolset.ListParameters", engines: ["5.9"], schema: S_epic_list_parameters__1 }] },
    ...bp("read", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Returns all parameters exposed by a material or instance, with their names and types. Params: material. On UE 5.9 it calls EditorToolset.MaterialInstanceToolset.ListParameters (Params: material).", "epic_call_tool"),
  },
  epic_recompile: {
    epicSchema: S_epic_recompile,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.recompile", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialToolset", name: "EditorToolset.MaterialToolset.Recompile", engines: ["5.9"], schema: S_epic_recompile__1, rename: {"material_or_function":"materialOrFunction"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Recompiles a Material or MaterialFunction after edits. For Materials, raises if the shader fails to compile. For MaterialFunctions, also recompiles any Materials that reference the function. Call this once after a set of graph modifications is complete - after adding or deleting expressions, making connections, or changing expression properties such as parameter names or default values. Params: material_or_function. On UE 5.9 it calls EditorToolset.MaterialToolset.Recompile (Params: materialOrFunction).", "epic_call_tool"),
  },
  epic_rename_parameter_group: {
    epicSchema: S_epic_rename_parameter_group,
    epicTool: { toolset: "editor_toolset.toolsets.material.MaterialTools", name: "editor_toolset.toolsets.material.MaterialTools.rename_parameter_group", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialToolset", name: "EditorToolset.MaterialToolset.RenameParameterGroup", engines: ["5.9"], schema: S_epic_rename_parameter_group__1, rename: {"material_or_function":"materialOrFunction","old_name":"oldName","new_name":"newName"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material.MaterialTools] Renames a parameter group across all parameter expressions in a Material or MaterialFunction. All parameters currently in old_name will be moved to new_name. If new_name already exists, the parameters are merged into it. Params: material_or_function, old_name, new_name. On UE 5.9 it calls EditorToolset.MaterialToolset.RenameParameterGroup (Params: materialOrFunction, oldName, newName).", "epic_call_tool"),
  },
  epic_set_parameter_override: {
    epicSchema: S_epic_set_parameter_override,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.set_parameter_override", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialInstanceToolset", name: "EditorToolset.MaterialInstanceToolset.SetParameterOverride", engines: ["5.9"], schema: S_epic_set_parameter_override__1, rename: {"override":"bOverride"} }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Enables or disables a parameter override on a material instance. Enabling sets the override to the current effective value. Disabling reverts to the parent. For non-static parameter types, disabling also discards the prior override value; re-enabling later restores the parent value, not the prior override. Static switches and static component masks preserve their value across toggle. Params: instance, name, override. On UE 5.9 it calls EditorToolset.MaterialInstanceToolset.SetParameterOverride (Params: instance, name, bOverride).", "epic_call_tool"),
  },
  epic_set_parent: {
    epicSchema: S_epic_set_parent,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.set_parent", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialInstanceToolset", name: "EditorToolset.MaterialInstanceToolset.SetParent", engines: ["5.9"], schema: S_epic_set_parent__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Changes the parent of a material instance. Params: instance, parent. On UE 5.9 it calls EditorToolset.MaterialInstanceToolset.SetParent (Params: instance, parent).", "epic_call_tool"),
  },
  epic_set_scalar_parameter: {
    epicSchema: S_epic_set_scalar_parameter,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.set_scalar_parameter", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialInstanceToolset", name: "EditorToolset.MaterialInstanceToolset.SetScalarParameter", engines: ["5.9"], schema: S_epic_set_scalar_parameter__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Sets the value of a scalar parameter on a material instance. Params: instance, name, value. On UE 5.9 it calls EditorToolset.MaterialInstanceToolset.SetScalarParameter (Params: instance, name, value).", "epic_call_tool"),
  },
  epic_set_static_switch_parameter: {
    epicSchema: S_epic_set_static_switch_parameter,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.set_static_switch_parameter", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialInstanceToolset", name: "EditorToolset.MaterialInstanceToolset.SetStaticSwitchParameter", engines: ["5.9"], schema: S_epic_set_static_switch_parameter__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Sets the value of a static switch parameter on a material instance. Params: instance, name, value. On UE 5.9 it calls EditorToolset.MaterialInstanceToolset.SetStaticSwitchParameter (Params: instance, name, value).", "epic_call_tool"),
  },
  epic_set_texture_parameter: {
    epicSchema: S_epic_set_texture_parameter,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.set_texture_parameter", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialInstanceToolset", name: "EditorToolset.MaterialInstanceToolset.SetTextureParameter", engines: ["5.9"], schema: S_epic_set_texture_parameter__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Assigns a texture to a texture parameter on a material instance. Params: instance, name, value. On UE 5.9 it calls EditorToolset.MaterialInstanceToolset.SetTextureParameter (Params: instance, name, value).", "epic_call_tool"),
  },
  epic_set_vector_parameter: {
    epicSchema: S_epic_set_vector_parameter,
    epicTool: { toolset: "editor_toolset.toolsets.material_instance.MaterialInstanceTools", name: "editor_toolset.toolsets.material_instance.MaterialInstanceTools.set_vector_parameter", engines: ["5.8"], successors: [{ toolset: "EditorToolset.MaterialInstanceToolset", name: "EditorToolset.MaterialInstanceToolset.SetVectorParameter", engines: ["5.9"], schema: S_epic_set_vector_parameter__1 }] },
    ...bp("mutate", "[Epic editor_toolset.toolsets.material_instance.MaterialInstanceTools] Sets the value of a vector parameter on a material instance. Params: instance, name, value. On UE 5.9 it calls EditorToolset.MaterialInstanceToolset.SetVectorParameter (Params: instance, name, value).", "epic_call_tool"),
  },
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  asset_name: z.string().optional(),
  assetName: z.string().optional().describe("The name of the new asset."),
  bOverride: z.boolean().optional().describe("True to enable the override, False to clear it."),
  expression: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  expression_class: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  expressionClass: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The type of expression node to create."),
  folder_path: z.string().optional(),
  folderPath: z.string().optional().describe("The content-browser path to the folder for the new asset."),
  from_expression: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  from_output_name: z.string().optional(),
  fromExpression: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The expression providing the output value."),
  fromOutputName: z.string().optional().describe("The output pin name. Pass an empty string to use the default (first) output."),
  group_name: z.string().optional(),
  groupName: z.string().optional().describe("The name of the group to delete."),
  instance: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  material: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  material_function: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  material_or_function: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  material_property: z.string().optional(),
  materialFunction: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The MaterialFunction to query."),
  materialOrFunction: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The Material or MaterialFunction to add the expression to."),
  materialProperty: z.string().optional().describe("The material output property to connect to. Value display names: MP_EmissiveColor = \"Emissive\", MP_Opacity = \"Opacity\", MP_OpacityMask = \"Opacity Mask\", MP_Bas…"),
  name: z.string().optional().describe("The parameter name."),
  new_name: z.string().optional(),
  newAssetPath: z.string().optional().describe("Content path to the new MaterialInstanceConstant."),
  newName: z.string().optional().describe("The new name for the group."),
  old_name: z.string().optional(),
  oldAssetPath: z.string().optional().describe("Content path to the old MaterialInstanceConstant."),
  oldName: z.string().optional().describe("The current name of the group to rename."),
  output_name: z.string().optional(),
  outputName: z.string().optional().describe("The output pin name. Pass an empty string to use the default (first) output."),
  override: z.boolean().optional(),
  parent: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  search: z.string().optional().describe("Optional case-insensitive substring filter on the class path."),
  to_expression: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  to_input_name: z.string().optional(),
  toExpression: z.union([z.string(), z.record(z.string(), z.unknown())]).optional().describe("The expression receiving the input."),
  toInputName: z.string().optional().describe("The input pin name to connect to. Use GetExpressionInputNames to discover valid names."),
  value: z.unknown().optional().describe("Represents a reference to a UObject or UClass."),
  x: z.number().optional().describe("Horizontal position in the graph editor."),
  y: z.number().optional().describe("Vertical position in the graph editor."),
};
