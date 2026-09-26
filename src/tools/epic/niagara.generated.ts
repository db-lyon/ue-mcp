// GENERATED FILE - do not edit.
//
// Written by scripts/generate-epic-actions.mjs from tests/golden/epic-catalog.json
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

const S_epic_add_emitter = {"properties":{"system":{"type":"object","properties":{"refPath":{}}},"templateEmitter":{"type":"object","properties":{"refPath":{}}},"emitterName":{"type":"string"}},"required":["system","templateEmitter","emitterName"]} as const;
const S_epic_add_module = {"properties":{"moduleLocationRef":{"type":"object"},"moduleAsset":{"type":"object","properties":{"refPath":{}}}},"required":["moduleLocationRef","moduleAsset"]} as const;
const S_epic_add_renderer = {"properties":{"newRendererLocation":{"type":"object"},"rendererClass":{"type":"object","properties":{"refPath":{}}}},"required":["newRendererLocation","rendererClass"]} as const;
const S_epic_add_set_parameter_entry = {"properties":{"moduleRef":{"type":"object"},"entry":{"type":"object"}},"required":["moduleRef","entry"]} as const;
const S_epic_add_set_parameters_module = {"properties":{"moduleLocationRef":{"type":"object"},"parameters":{"type":"array"}},"required":["moduleLocationRef","parameters"]} as const;
const S_epic_add_user_variables = {"properties":{"system":{"type":"object","properties":{"refPath":{}}},"variablesToAdd":{"type":"array"}},"required":["system","variablesToAdd"]} as const;
const S_epic_apply_stack_issue_fix = {"properties":{"system":{"type":"object","properties":{"refPath":{}}},"issueId":{"type":"string"},"fixId":{"type":"string"}},"required":["system","issueId","fixId"]} as const;
const S_epic_construct_niagara_bpwrapper_from_component = {"properties":{"newAssetPath":{"type":"string"},"component":{"type":"object","properties":{"refPath":{}}},"parentClass":{"type":"object","properties":{"refPath":{}}}},"required":["newAssetPath","component","parentClass"]} as const;
const S_epic_construct_niagara_bpwrapper_from_system = {"properties":{"newAssetPath":{"type":"string"},"system":{"type":"object","properties":{"refPath":{}}},"parentClass":{"type":"object","properties":{"refPath":{}}}},"required":["newAssetPath","system","parentClass"]} as const;
const S_epic_create_niagara_system = {"properties":{"assetName":{"type":"string"},"assetPath":{"type":"string"},"templateSystem":{"type":"object","properties":{"refPath":{}}}},"required":["assetName","assetPath","templateSystem"]} as const;
const S_epic_find_niagara_scripts = {"properties":{"folderPath":{"type":"string"},"name":{"type":"string"},"usages":{"type":"array"},"visibilities":{"type":"array"},"moduleUsageBitmask":{"type":"integer"},"bRecursive":{"type":"boolean"},"bIncludeDeprecated":{"type":"boolean"}},"required":["folderPath","name","usages","visibilities","moduleUsageBitmask","bRecursive","bIncludeDeprecated"]} as const;
const S_epic_get_asset_discovery_info = {"properties":{}} as const;
const S_epic_get_available_dynamic_inputs = {"properties":{"type":{"type":"object"}},"required":["type"]} as const;
const S_epic_get_data_interface_schema = {"properties":{"dataInterfaceClass":{"type":"object","properties":{"refPath":{}}}},"required":["dataInterfaceClass"]} as const;
const S_epic_get_dynamic_input_chain = {"properties":{"stackInputRef":{"type":"object"}},"required":["stackInputRef"]} as const;
const S_epic_get_dynamic_input_schema = {"properties":{"dynamicInputReference":{"type":"object"}},"required":["dynamicInputReference"]} as const;
const S_epic_get_dynamic_input_schema_from_asset = {"properties":{"dynamicInputAsset":{"type":"object","properties":{"refPath":{}}}},"required":["dynamicInputAsset"]} as const;
const S_epic_get_emitter_data = {"properties":{"emitterRef":{"type":"object"}},"required":["emitterRef"]} as const;
const S_epic_get_emitter_input_values = {"properties":{"emitterRef":{"type":"object"}},"required":["emitterRef"]} as const;
const S_epic_get_emitter_schema = {"properties":{}} as const;
const S_epic_get_emitter_summary = {"properties":{"emitterRef":{"type":"object"}},"required":["emitterRef"]} as const;
const S_epic_get_emitter_topology = {"properties":{"emitterRef":{"type":"object"}},"required":["emitterRef"]} as const;
const S_epic_get_module_input_values = {"properties":{"moduleRef":{"type":"object"}},"required":["moduleRef"]} as const;
const S_epic_get_module_schema = {"properties":{"moduleReference":{"type":"object"}},"required":["moduleReference"]} as const;
const S_epic_get_module_schema_from_asset = {"properties":{"moduleAsset":{"type":"object","properties":{"refPath":{}}}},"required":["moduleAsset"]} as const;
const S_epic_get_module_topology = {"properties":{"moduleRef":{"type":"object"}},"required":["moduleRef"]} as const;
const S_epic_get_niagara_script_digest = {"properties":{"objectPath":{"type":"string"}},"required":["objectPath"]} as const;
const S_epic_get_renderer_data = {"properties":{"rendererRef":{"type":"object"}},"required":["rendererRef"]} as const;
const S_epic_get_renderer_schema = {"properties":{"rendererClass":{"type":"object","properties":{"refPath":{}}}},"required":["rendererClass"]} as const;
const S_epic_get_script_stack_input_values = {"properties":{"scriptRef":{"type":"object"}},"required":["scriptRef"]} as const;
const S_epic_get_script_stack_topology = {"properties":{"scriptRef":{"type":"object"}},"required":["scriptRef"]} as const;
const S_epic_get_stack_input_data = {"properties":{"stackInputRef":{"type":"object"}},"required":["stackInputRef"]} as const;
const S_epic_get_stack_input_schema = {"properties":{"inputReference":{"type":"object"}},"required":["inputReference"]} as const;
const S_epic_get_stack_input_topology = {"properties":{"stackInputRef":{"type":"object"}},"required":["stackInputRef"]} as const;
const S_epic_get_stack_issues = {"properties":{"system":{"type":"object","properties":{"refPath":{}}}},"required":["system"]} as const;
const S_epic_get_system_compile_state = {"properties":{"system":{"type":"object","properties":{"refPath":{}}}},"required":["system"]} as const;
const S_epic_get_system_data = {"properties":{"system":{"type":"object","properties":{"refPath":{}}}},"required":["system"]} as const;
const S_epic_get_system_dependencies = {"properties":{"system":{"type":"object","properties":{"refPath":{}}}},"required":["system"]} as const;
const S_epic_get_system_schema = {"properties":{}} as const;
const S_epic_get_system_summary = {"properties":{"system":{"type":"object","properties":{"refPath":{}}}},"required":["system"]} as const;
const S_epic_get_user_variables = {"properties":{"component":{"type":"object","properties":{"refPath":{}}}},"required":["component"]} as const;
const S_epic_get_user_variables__niagara_toolset_system = {"properties":{"system":{"type":"object","properties":{"refPath":{}}}},"required":["system"]} as const;
const S_epic_get_variable = {"properties":{"component":{"type":"object","properties":{"refPath":{}}},"var":{"type":"object"}},"required":["component","var"]} as const;
const S_epic_remove_emitter = {"properties":{"emitterToRemove":{"type":"object"}},"required":["emitterToRemove"]} as const;
const S_epic_remove_module = {"properties":{"moduleToRemove":{"type":"object"}},"required":["moduleToRemove"]} as const;
const S_epic_remove_renderer = {"properties":{"rendererToRemove":{"type":"object"}},"required":["rendererToRemove"]} as const;
const S_epic_remove_set_parameter_entry = {"properties":{"moduleRef":{"type":"object"},"parameterName":{"type":"string"}},"required":["moduleRef","parameterName"]} as const;
const S_epic_remove_user_variables = {"properties":{"system":{"type":"object","properties":{"refPath":{}}},"variablesToRemove":{"type":"array"}},"required":["system","variablesToRemove"]} as const;
const S_epic_set_emitter_data = {"properties":{"emitter":{"type":"object"},"emitterData":{"type":"object"}},"required":["emitter","emitterData"]} as const;
const S_epic_set_module_enabled = {"properties":{"moduleRef":{"type":"object"},"bEnabled":{"type":"boolean"}},"required":["moduleRef","bEnabled"]} as const;
const S_epic_set_renderer_data = {"properties":{"renderer":{"type":"object"},"rendererData":{"type":"object"}},"required":["renderer","rendererData"]} as const;
const S_epic_set_stack_input_data = {"properties":{"stackInputRef":{"type":"object"},"inputData":{"type":"object"}},"required":["stackInputRef","inputData"]} as const;
const S_epic_set_system = {"properties":{"niagaraComponent":{"type":"object","properties":{"refPath":{}}},"system":{"type":"object","properties":{"refPath":{}}},"bResetExistingOverrideParameters":{"type":"boolean"}},"required":["niagaraComponent","system","bResetExistingOverrideParameters"]} as const;
const S_epic_set_system_data = {"properties":{"system":{"type":"object","properties":{"refPath":{}}},"systemData":{"type":"object"}},"required":["system","systemData"]} as const;
const S_epic_set_variable = {"properties":{"component":{"type":"object","properties":{"refPath":{}}},"variable":{"type":"object"}},"required":["component","variable"]} as const;
const S_epic_uenum_info = {"properties":{"enum":{"type":"object","properties":{"refPath":{}}}},"required":["enum"]} as const;

/** 56 wrapped engine tools routed to the `niagara` category. */
export const actions: Record<string, ActionSpec> = {
  epic_add_emitter: {
    epicSchema: S_epic_add_emitter,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.AddEmitter" },
    ...bp("mutate", "[Epic NiagaraToolsets.NiagaraToolset_System] Adds an emitter to a Niagara System. The new emitter will be based on the template emitter, inheriting its configuration and modules. Returns the full emitter topology (no input values - call GetEmitterInputValues for values). Params: system, templateEmitter, emitterName", "epic_call_tool"),
  },
  epic_add_module: {
    epicSchema: S_epic_add_module,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.AddModule" },
    ...bp("mutate", "[Epic NiagaraToolsets.NiagaraToolset_System] Adds a module to a script stack. The module will be inserted into the specified script's execution stack. Returns the module topology with all inputs walked (no input values - call GetModuleInputValues for values). Params: moduleLocationRef, moduleAsset", "epic_call_tool"),
  },
  epic_add_renderer: {
    epicSchema: S_epic_add_renderer,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.AddRenderer" },
    ...bp("mutate", "[Epic NiagaraToolsets.NiagaraToolset_System] Adds a renderer to an emitter. Creates a new renderer of the specified type and adds it to the emitter's renderer list. Returns an FNiagaraExt_RendererRef with the new renderer's Index and RendererClass, usable directly with SetRendererData / GetRendererData without a follow-up topology call. Params: newRendererLocation, rendererClass", "epic_call_tool"),
  },
  epic_add_set_parameter_entry: {
    epicSchema: S_epic_add_set_parameter_entry,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.AddSetParameterEntry" },
    ...bp("mutate", "[Epic NiagaraToolsets.NiagaraToolset_System] Adds a single parameter to an existing SetParameters module. The module referenced by ModuleRef must be a SetParameters (UNiagaraNodeAssignment) module. Use bIsSetParametersModule in the module topology to confirm before calling. Params: moduleRef, entry", "epic_call_tool"),
  },
  epic_add_set_parameters_module: {
    epicSchema: S_epic_add_set_parameters_module,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.AddSetParametersModule" },
    ...bp("mutate", "[Epic NiagaraToolsets.NiagaraToolset_System] Adds a SetParameters module to a script stack. Unlike AddModule which requires a script asset, a SetParameters module dynamically assigns values to named parameters and generates its own internal script. Use this when you need to set one or more particle/emitter/system parameters directly in the stack. Params: moduleLocationRef, parameters", "epic_call_tool"),
  },
  epic_add_user_variables: {
    epicSchema: S_epic_add_user_variables,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.AddUserVariables" },
    ...bp("mutate", "[Epic NiagaraToolsets.NiagaraToolset_System] Adds or updates user variables on a system. If a variable with the same name already exists, it will be replaced with the new definition. Params: system, variablesToAdd", "epic_call_tool"),
  },
  epic_apply_stack_issue_fix: {
    epicSchema: S_epic_apply_stack_issue_fix,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.ApplyStackIssueFix" },
    ...bp("mutate", "[Epic NiagaraToolsets.NiagaraToolset_System] Applies a Fix-style stack issue fix identified by IssueId and FixId. Link-style fixes are rejected. The fix is undoable via the editor undo stack. Applying a fix may trigger a recompile; the result waits for that compile to complete so post-fix state is valid. Params: system, issueId, fixId", "epic_call_tool"),
  },
  epic_construct_niagara_bpwrapper_from_component: {
    epicSchema: S_epic_construct_niagara_bpwrapper_from_component,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_Blueprint", name: "NiagaraToolsets.NiagaraToolset_Blueprint.ConstructNiagaraBPWrapperFromComponent" },
    ...bp("mutate", "[Epic NiagaraToolsets.NiagaraToolset_Blueprint] Creates a Blueprint actor wrapper from a Niagara Component. This generates a new Blueprint actor and preserves all component property values and user variable overrides. Naming convention: NS_MyEffect -> B_MyEffect Params: newAssetPath, component, parentClass", "epic_call_tool"),
  },
  epic_construct_niagara_bpwrapper_from_system: {
    epicSchema: S_epic_construct_niagara_bpwrapper_from_system,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_Blueprint", name: "NiagaraToolsets.NiagaraToolset_Blueprint.ConstructNiagaraBPWrapperFromSystem" },
    ...bp("mutate", "[Epic NiagaraToolsets.NiagaraToolset_Blueprint] Creates a Blueprint actor wrapper around a Niagara System. This generates a new Blueprint actor with a Niagara component configured to use the specified system. Naming convention: NS_MyEffect -> B_MyEffect Params: newAssetPath, system, parentClass", "epic_call_tool"),
  },
  epic_create_niagara_system: {
    epicSchema: S_epic_create_niagara_system,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.CreateNiagaraSystem" },
    ...bp("mutate", "[Epic NiagaraToolsets.NiagaraToolset_System] Creates a new Niagara System asset. The new system will be based on the template system, inheriting its configuration and emitters. Params: assetName, assetPath, templateSystem", "epic_call_tool"),
  },
  epic_find_niagara_scripts: {
    epicSchema: S_epic_find_niagara_scripts,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_Assets", name: "NiagaraToolsets.NiagaraToolset_Assets.FindNiagaraScripts" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_Assets] Searches for UNiagaraScript assets matching the given filters. Reads filterable metadata from asset registry tags only - no LoadObject required. Tags reflect the exposed (published) version of versioned scripts; this function does not need a version filter and there is no way to discover non-exposed versions through the asset registry. Params: folderPath, name, usages, visibilities, moduleUsageBitmask, bRecursive, bIncludeDeprecated", "epic_call_tool"),
  },
  epic_get_asset_discovery_info: {
    epicSchema: S_epic_get_asset_discovery_info,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_Assets", name: "NiagaraToolsets.NiagaraToolset_Assets.GetAssetDiscoveryInfo" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_Assets] Returns the project's configured asset discovery groups. Each group describes a content directory's purpose and paths. Params: none", "epic_call_tool"),
  },
  epic_get_available_dynamic_inputs: {
    epicSchema: S_epic_get_available_dynamic_inputs,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetAvailableDynamicInputs" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns all available Dynamic Input Module assets compatible with the given type. Dynamic inputs provide procedural value generation for module parameters. Params: type", "epic_call_tool"),
  },
  epic_get_data_interface_schema: {
    epicSchema: S_epic_get_data_interface_schema,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetDataInterfaceSchema" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns property schema for a specific Data Interface class. Describes all available properties and their types for the given data interface type. Params: dataInterfaceClass", "epic_call_tool"),
  },
  epic_get_dynamic_input_chain: {
    epicSchema: S_epic_get_dynamic_input_chain,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetDynamicInputChain" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns the full recursive chain for a dynamic input: topology metadata and resolved values at every level. The starting input must have value mode Dynamic; an error is surfaced otherwise. The schema expands one full level and emits a typed recursion stub at deeper levels; the wire format recurses to arbitrary depth matching the underlying chain. Params: stackInputRef", "epic_call_tool"),
  },
  epic_get_dynamic_input_schema: {
    epicSchema: S_epic_get_dynamic_input_schema,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetDynamicInputSchema" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns schema for a dynamic input module in the stack. Describes the inputs and configuration for a procedural value generator. Params: dynamicInputReference", "epic_call_tool"),
  },
  epic_get_dynamic_input_schema_from_asset: {
    epicSchema: S_epic_get_dynamic_input_schema_from_asset,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetDynamicInputSchemaFromAsset" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns schema for a dynamic input asset. Standalone function that doesn't require a system context - useful for browsing available dynamic inputs. Params: dynamicInputAsset", "epic_call_tool"),
  },
  epic_get_emitter_data: {
    epicSchema: S_epic_get_emitter_data,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetEmitterData" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns emitter property values as a single JSON-string blob in PropertyValues. The blob contains the full FVersionedNiagaraEmitterData (SimTarget, bLocalSpace, RandomSeed, FixedBounds, etc.) - fields use C++ PascalCase, must be parsed to read individual values. For typed access to common metadata (SimTarget, EmitterName, bEnabled, RendererClasses) prefer GetEmitterSummary - it returns those as named fields directly and avoids a JSON parse step. Use this endpoint when you need the full property set or a less-common field. Params: emitterRef", "epic_call_tool"),
  },
  epic_get_emitter_input_values: {
    epicSchema: S_epic_get_emitter_input_values,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetEmitterInputValues" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns all resolved input values for every module across all four emitter script stacks. One FNiagaraExt_ModuleInputValues entry per module, each carrying all its resolved input values. Call this in parallel with GetEmitterTopology to get both structure and values in two passes. Params: emitterRef", "epic_call_tool"),
  },
  epic_get_emitter_schema: {
    epicSchema: S_epic_get_emitter_schema,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetEmitterSchema" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns property schema for Niagara Emitter. Describes all available properties and their types that can be set on a Niagara Emitter. Params: none", "epic_call_tool"),
  },
  epic_get_emitter_summary: {
    epicSchema: S_epic_get_emitter_summary,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetEmitterSummary" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns lightweight emitter metadata: name, enabled state, sim target, renderer classes. Use this when you only need to check metadata without walking the full emitter structure. Params: emitterRef", "epic_call_tool"),
  },
  epic_get_emitter_topology: {
    epicSchema: S_epic_get_emitter_topology,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetEmitterTopology" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns full emitter topology: four script stacks with all modules and inputs, renderer references. All fields always populated. The returned topology carries no input values; call GetEmitterInputValues in parallel. Params: emitterRef", "epic_call_tool"),
  },
  epic_get_module_input_values: {
    epicSchema: S_epic_get_module_input_values,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetModuleInputValues" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns resolved input values for a single module. Use when you need values for one specific module without walking the whole emitter. Params: moduleRef", "epic_call_tool"),
  },
  epic_get_module_schema: {
    epicSchema: S_epic_get_module_schema,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetModuleSchema" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns schema for a module and all its inputs. Call this after seeing a module in topology to understand what inputs it exposes. Params: moduleReference", "epic_call_tool"),
  },
  epic_get_module_schema_from_asset: {
    epicSchema: S_epic_get_module_schema_from_asset,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetModuleSchemaFromAsset" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns schema for a module asset. Standalone function that doesn't require a system context - useful for browsing available modules. Params: moduleAsset", "epic_call_tool"),
  },
  epic_get_module_topology: {
    epicSchema: S_epic_get_module_topology,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetModuleTopology" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns module topology: metadata and all inputs (name/type/visibility only, no values). All fields always populated. Params: moduleRef", "epic_call_tool"),
  },
  epic_get_niagara_script_digest: {
    epicSchema: S_epic_get_niagara_script_digest,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_Assets", name: "NiagaraToolsets.NiagaraToolset_Assets.GetNiagaraScriptDigest" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_Assets] Returns the decoded asset-registry tag metadata for a Niagara script asset. Looks up the asset by object path in the asset registry and reads its tags; no LoadObject is performed. Returned fields reflect the exposed (published) version when the script uses FVersionedNiagaraScriptData versioning - the registry never carries non-exposed-version metadata. Params: objectPath", "epic_call_tool"),
  },
  epic_get_renderer_data: {
    epicSchema: S_epic_get_renderer_data,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetRendererData" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns renderer property values. Retrieves the current values of all configurable renderer properties. Params: rendererRef", "epic_call_tool"),
  },
  epic_get_renderer_schema: {
    epicSchema: S_epic_get_renderer_schema,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetRendererSchema" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns property schema for a specific Renderer class. Describes all available properties and their types for the given renderer type. Params: rendererClass", "epic_call_tool"),
  },
  epic_get_script_stack_input_values: {
    epicSchema: S_epic_get_script_stack_input_values,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetScriptStackInputValues" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns all resolved input values for every module in the given script stack. One FNiagaraExt_ModuleInputValues entry per module. Params: scriptRef", "epic_call_tool"),
  },
  epic_get_script_stack_topology: {
    epicSchema: S_epic_get_script_stack_topology,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetScriptStackTopology" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns script stack topology: all modules and their inputs in execution order. All fields always populated. Params: scriptRef", "epic_call_tool"),
  },
  epic_get_stack_input_data: {
    epicSchema: S_epic_get_stack_input_data,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetStackInputData" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns the value of a stack module input. Retrieves the current value and configuration for a specific module input parameter. Params: stackInputRef", "epic_call_tool"),
  },
  epic_get_stack_input_schema: {
    epicSchema: S_epic_get_stack_input_schema,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetStackInputSchema" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns schema for a single module input in the stack. Describes the type, metadata, and configuration options for a specific input parameter. Params: inputReference", "epic_call_tool"),
  },
  epic_get_stack_input_topology: {
    epicSchema: S_epic_get_stack_input_topology,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetStackInputTopology" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns stack input topology: name, type, visibility, editability. No value payload. For the resolved value call GetStackInputData. For a dynamic-input chain call GetDynamicInputChain. Params: stackInputRef", "epic_call_tool"),
  },
  epic_get_stack_issues: {
    epicSchema: S_epic_get_stack_issues,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetStackIssues" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns all stack issues (errors, warnings, info) from the Niagara module stack, including dismissed ones. Waits for any in-flight compile to complete before collecting. Params: system", "epic_call_tool"),
  },
  epic_get_system_compile_state: {
    epicSchema: S_epic_get_system_compile_state,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetSystemCompileState" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns the current compile state of a Niagara System: aggregate status, per-script compile events, and summary flags. Waits for any in-flight compile to complete before collecting. Params: system", "epic_call_tool"),
  },
  epic_get_system_data: {
    epicSchema: S_epic_get_system_data,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetSystemData" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns system property values. Retrieves the current values of all configurable system-level properties. Params: system", "epic_call_tool"),
  },
  epic_get_system_dependencies: {
    epicSchema: S_epic_get_system_dependencies,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetSystemDependencies" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns the four Used* sets (renderers, data interfaces, modules, dynamic inputs) gathered across all emitters and system scripts. These sets are not included in topology structs; call this endpoint separately when needed. Params: system", "epic_call_tool"),
  },
  epic_get_system_schema: {
    epicSchema: S_epic_get_system_schema,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetSystemSchema" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns property schema for Niagara System. Describes all available properties and their types that can be set on a Niagara System. Params: none", "epic_call_tool"),
  },
  epic_get_system_summary: {
    epicSchema: S_epic_get_system_summary,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetSystemSummary" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns lightweight system metadata: name, user variables, and one summary entry per emitter. Use this for first contact with an unfamiliar system. For full structural detail call GetEmitterTopology per emitter. Params: system", "epic_call_tool"),
  },
  epic_get_user_variables: {
    epicSchema: S_epic_get_user_variables,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_Component", name: "NiagaraToolsets.NiagaraToolset_Component.GetUserVariables" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_Component] Returns all user variable values currently set on the component. This retrieves the current values of all user-exposed parameters that can be overridden at the component level. Params: component", "epic_call_tool"),
  },
  epic_get_user_variables__niagara_toolset_system: {
    epicSchema: S_epic_get_user_variables__niagara_toolset_system,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.GetUserVariables" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_System] Returns all user variables defined on the system. User variables are parameters exposed for external control and can be overridden per component instance. Params: system", "epic_call_tool"),
  },
  epic_get_variable: {
    epicSchema: S_epic_get_variable,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_Component", name: "NiagaraToolsets.NiagaraToolset_Component.GetVariable" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_Component] Gets the current value of a specific user variable on the component. This retrieves the current value of a user-exposed parameter, including any component-level overrides. Params: component, var", "epic_call_tool"),
  },
  epic_remove_emitter: {
    epicSchema: S_epic_remove_emitter,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.RemoveEmitter" },
    ...bp("mutate", "[Epic NiagaraToolsets.NiagaraToolset_System] Removes an emitter from a system. Deletes the specified emitter and all its associated scripts, modules, and renderers. Params: emitterToRemove", "epic_call_tool"),
  },
  epic_remove_module: {
    epicSchema: S_epic_remove_module,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.RemoveModule" },
    ...bp("mutate", "[Epic NiagaraToolsets.NiagaraToolset_System] Removes a module from a script stack. Deletes the specified module and all its inputs from the script's execution stack. Params: moduleToRemove", "epic_call_tool"),
  },
  epic_remove_renderer: {
    epicSchema: S_epic_remove_renderer,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.RemoveRenderer" },
    ...bp("mutate", "[Epic NiagaraToolsets.NiagaraToolset_System] Removes a renderer from an emitter. Deletes the specified renderer from the emitter's renderer list. Params: rendererToRemove", "epic_call_tool"),
  },
  epic_remove_set_parameter_entry: {
    epicSchema: S_epic_remove_set_parameter_entry,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.RemoveSetParameterEntry" },
    ...bp("mutate", "[Epic NiagaraToolsets.NiagaraToolset_System] Removes a parameter from an existing SetParameters module by name. The module referenced by ModuleRef must be a SetParameters (UNiagaraNodeAssignment) module. Use bIsSetParametersModule in the module topology to confirm before calling. Params: moduleRef, parameterName", "epic_call_tool"),
  },
  epic_remove_user_variables: {
    epicSchema: S_epic_remove_user_variables,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.RemoveUserVariables" },
    ...bp("mutate", "[Epic NiagaraToolsets.NiagaraToolset_System] Removes user variables from a system. Deletes the specified user variables from the system's user parameter collection. Params: system, variablesToRemove", "epic_call_tool"),
  },
  epic_set_emitter_data: {
    epicSchema: S_epic_set_emitter_data,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.SetEmitterData" },
    ...bp("mutate", "[Epic NiagaraToolsets.NiagaraToolset_System] Sets property values on a Niagara Emitter. Applies new values to emitter-level properties based on the provided data structure. Params: emitter, emitterData", "epic_call_tool"),
  },
  epic_set_module_enabled: {
    epicSchema: S_epic_set_module_enabled,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.SetModuleEnabled" },
    ...bp("mutate", "[Epic NiagaraToolsets.NiagaraToolset_System] Sets whether a module is enabled. Disabled modules remain in the stack but don't execute. Current state is visible in module topology. Params: moduleRef, bEnabled", "epic_call_tool"),
  },
  epic_set_renderer_data: {
    epicSchema: S_epic_set_renderer_data,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.SetRendererData" },
    ...bp("mutate", "[Epic NiagaraToolsets.NiagaraToolset_System] Sets property values on a Niagara Renderer. Applies new values to renderer properties based on the provided data structure. Payload shape varies with the concrete renderer class; call GetRendererSchema for the renderer's class to inspect valid properties before writing. Params: renderer, rendererData", "epic_call_tool"),
  },
  epic_set_stack_input_data: {
    epicSchema: S_epic_set_stack_input_data,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.SetStackInputData" },
    ...bp("mutate", "[Epic NiagaraToolsets.NiagaraToolset_System] Sets the value of a stack module input and returns the resulting stored value. Updates the value and configuration for a specific module input parameter. Params: stackInputRef, inputData", "epic_call_tool"),
  },
  epic_set_system: {
    epicSchema: S_epic_set_system,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_Component", name: "NiagaraToolsets.NiagaraToolset_Component.SetSystem" },
    ...bp("mutate", "[Epic NiagaraToolsets.NiagaraToolset_Component] Sets the Niagara System for a component. Use this instead of setting the Asset property directly to ensure proper initialization. Params: niagaraComponent, system, bResetExistingOverrideParameters", "epic_call_tool"),
  },
  epic_set_system_data: {
    epicSchema: S_epic_set_system_data,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_System", name: "NiagaraToolsets.NiagaraToolset_System.SetSystemData" },
    ...bp("mutate", "[Epic NiagaraToolsets.NiagaraToolset_System] Sets property values on a Niagara System. Applies new values to system-level properties based on the provided data structure. Params: system, systemData", "epic_call_tool"),
  },
  epic_set_variable: {
    epicSchema: S_epic_set_variable,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_Component", name: "NiagaraToolsets.NiagaraToolset_Component.SetVariable" },
    ...bp("mutate", "[Epic NiagaraToolsets.NiagaraToolset_Component] Sets the value of a user variable on the component. This overrides the default value of a user-exposed parameter on a specific component instance. Params: component, variable", "epic_call_tool"),
  },
  epic_uenum_info: {
    epicSchema: S_epic_uenum_info,
    epicTool: { toolset: "NiagaraToolsets.NiagaraToolset_Info", name: "NiagaraToolsets.NiagaraToolset_Info.UEnum_Info" },
    ...bp("read", "[Epic NiagaraToolsets.NiagaraToolset_Info] Returns information about a UEnum and all its values. ALWAYS call this when working with a UEnum type to see valid values. Params: enum", "epic_call_tool"),
  },
};

/** The parameters those actions accept, declared so the MCP layer stops stripping them. */
export const schema: Record<string, z.ZodType> = {
  assetName: z.string().optional().describe("Name of the new asset (without path or extension)"),
  assetPath: z.string().optional().describe("Directory path where the new asset will be created"),
  bEnabled: z.boolean().optional().describe("True to enable the module, false to disable it"),
  bIncludeDeprecated: z.boolean().optional().describe("If false (default usage), assets whose exposed version has bDeprecated=true are excluded. Pass true only when the caller explicitly wants to surface deprecated…"),
  bRecursive: z.boolean().optional().describe("Whether to search subfolders. Ignored when FolderPath is empty (whole-project scan is always exhaustive)."),
  bResetExistingOverrideParameters: z.boolean().optional().describe("If true, reset all user variables to system defaults; if false, preserve matching overrides"),
  component: z.union([z.string(), z.record(z.unknown())]).optional().describe("The Niagara Component with configured properties and overrides to preserve"),
  dataInterfaceClass: z.union([z.string(), z.record(z.unknown())]).optional().describe("The data interface class to get the schema for"),
  dynamicInputAsset: z.union([z.string(), z.record(z.unknown())]).optional().describe("The dynamic input script asset to get the schema for"),
  dynamicInputReference: z.record(z.unknown()).optional().describe("Reference to the dynamic input to get the schema for"),
  emitter: z.record(z.unknown()).optional().describe("Reference to the emitter to modify"),
  emitterData: z.record(z.unknown()).optional().describe("Data structure containing the property values to set"),
  emitterName: z.string().optional().describe("Name for the new emitter instance"),
  emitterRef: z.record(z.unknown()).optional().describe("Reference to the emitter to retrieve data from"),
  emitterToRemove: z.record(z.unknown()).optional().describe("Reference to the emitter to remove from the system"),
  entry: z.record(z.unknown()).optional().describe("The parameter to add (variable name, type, and optional default value string)"),
  enum: z.union([z.string(), z.record(z.unknown())]).optional().describe("Represents a reference to a UObject or UClass."),
  fixId: z.string().optional().describe("FixId from a prior FNiagaraExt_StackIssueFix."),
  folderPath: z.string().optional().describe("The folder to search within. Pass an empty string to search the entire project."),
  inputData: z.record(z.unknown()).optional().describe("The new value and configuration to apply to the input"),
  inputReference: z.record(z.unknown()).optional().describe("Reference to the stack input to get the schema for"),
  issueId: z.string().optional().describe("IssueId from a prior FNiagaraExt_StackIssue."),
  moduleAsset: z.union([z.string(), z.record(z.unknown())]).optional().describe("The module script asset to add to the stack"),
  moduleLocationRef: z.record(z.unknown()).optional().describe("Reference specifying where to add the module"),
  moduleRef: z.record(z.unknown()).optional().describe("Reference to the existing SetParameters module"),
  moduleReference: z.record(z.unknown()).optional().describe("Reference to the module to get the schema for"),
  moduleToRemove: z.record(z.unknown()).optional().describe("Reference to the module to remove from the stack"),
  moduleUsageBitmask: z.number().optional().describe("If non-zero, restricts results to Module scripts whose ModuleUsageBitmask shares at least one bit with this argument (any-match). Non-Module scripts are exclud…"),
  name: z.string().optional().describe("If non-empty, only return assets whose name contains this substring (case-insensitive)."),
  newAssetPath: z.string().optional().describe("Full path for the new Blueprint asset (prefer same directory as System)"),
  newRendererLocation: z.record(z.unknown()).optional().describe("Reference specifying which emitter to add the renderer to"),
  niagaraComponent: z.union([z.string(), z.record(z.unknown())]).optional().describe("The Niagara component to set the system on"),
  objectPath: z.string().optional().describe("Full object path of the script (e.g. \"/Niagara/Modules/Spawn/Initialize Particle.Initialize Particle\")."),
  parameterName: z.string().optional().describe("Name of the parameter to remove"),
  parameters: z.array(z.unknown()).optional().describe("Array of parameter entries, each with a variable (name + type) and an optional default value string"),
  parentClass: z.union([z.string(), z.record(z.unknown())]).optional().describe("The parent Actor class for the Blueprint (e.g., AActor)"),
  renderer: z.record(z.unknown()).optional().describe("Reference to the renderer to modify"),
  rendererClass: z.union([z.string(), z.record(z.unknown())]).optional().describe("The class of renderer to create (e.g., UNiagaraSpriteRendererProperties)"),
  rendererData: z.record(z.unknown()).optional().describe("Data structure containing the property values to set"),
  rendererRef: z.record(z.unknown()).optional().describe("Reference to the renderer to retrieve data from"),
  rendererToRemove: z.record(z.unknown()).optional().describe("Reference to the renderer to remove"),
  scriptRef: z.record(z.unknown()).optional().describe("Reference to the script stack"),
  stackInputRef: z.record(z.unknown()).optional().describe("Reference to the dynamic input to traverse"),
  system: z.union([z.string(), z.record(z.unknown())]).optional().describe("The Niagara System to wrap in the Blueprint"),
  systemData: z.record(z.unknown()).optional().describe("Data structure containing the property values to set"),
  templateEmitter: z.union([z.string(), z.record(z.unknown())]).optional().describe("The emitter asset to use as a template for the new emitter"),
  templateSystem: z.union([z.string(), z.record(z.unknown())]).optional().describe("Template system to base the new system on (required)"),
  type: z.record(z.unknown()).optional().describe("The Niagara type definition to find compatible dynamic inputs for"),
  usages: z.array(z.unknown()).optional().describe("If non-empty, only return assets whose Usage matches one of the listed values. Empty array = all usages allowed."),
  var: z.record(z.unknown()).optional().describe("The variable definition (name and type) to look up"),
  variable: z.record(z.unknown()).optional().describe("The variable instance containing the name and new value to set"),
  variablesToAdd: z.array(z.unknown()).optional().describe("Array of user variables to add or update"),
  variablesToRemove: z.array(z.unknown()).optional().describe("Array of variable definitions identifying which variables to remove"),
  visibilities: z.array(z.unknown()).optional().describe("If non-empty, only return assets whose LibraryVisibility matches one of the listed values. Empty array defaults to {Library} (the editor's \"Exposed\") - explici…"),
};
