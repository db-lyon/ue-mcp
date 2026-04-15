import { z } from "zod";
import { categoryTool, bp, type ActionSpec, type ToolDef } from "../types.js";
import { Vec3, Rotator } from "../schemas.js";

export const niagaraTool: ToolDef = categoryTool(
  "niagara",
  "Niagara VFX: systems, emitters, spawning, parameters, and graph authoring.",
  {
    list:           bp("List Niagara assets. Params: directory?, recursive?", "list_niagara_systems"),
    get_info:       bp("Inspect system. Params: assetPath", "get_niagara_info"),
    spawn:          bp("Spawn VFX. Params: systemPath, location, rotation?, label?", "spawn_niagara_at_location"),
    set_parameter:  bp("Set parameter. Params: actorLabel, parameterName, value, parameterType?", "set_niagara_parameter"),
    create:         bp("Create system. Params: name, packagePath?", "create_niagara_system"),
    create_emitter: bp("Create Niagara emitter. Params: name, packagePath?, templatePath?", "create_niagara_emitter"),
    add_emitter:    bp("Add emitter to system. Params: systemPath, emitterPath", "add_emitter_to_system"),
    list_emitters:  bp("List emitters in system. Params: systemPath", "list_emitters_in_system"),
    set_emitter_property: bp("Set emitter property. Params: systemPath, emitterName?, propertyName, value", "set_emitter_property"),
    list_modules:   bp("List Niagara modules. Params: directory?", "list_niagara_modules"),
    get_emitter_info: bp("Inspect emitter. Params: assetPath", "get_emitter_info"),
    list_renderers:   bp("List renderers on an emitter. Params: systemPath, emitterName?, emitterIndex?", "list_emitter_renderers"),
    add_renderer:     bp("Add renderer (sprite/mesh/ribbon or full class). Params: systemPath, rendererType, emitterName?, emitterIndex?", "add_emitter_renderer"),
    remove_renderer:  bp("Remove renderer by index. Params: systemPath, rendererIndex, emitterName?, emitterIndex?", "remove_emitter_renderer"),
    set_renderer_property: bp("Set renderer bool/number/string property. Params: systemPath, rendererIndex, propertyName, value, emitterName?, emitterIndex?", "set_renderer_property"),
    inspect_data_interfaces: bp("List user-scope data interfaces. Params: systemPath", "inspect_data_interface"),
    create_system_from_spec: bp("Declaratively create a system + emitters. Params: name, packagePath?, emitters?:[{path}]", "create_niagara_system_from_spec"),
    get_compiled_hlsl: bp("Read GPU compute script info for an emitter. Params: systemPath, emitterName?, emitterIndex?", "get_niagara_compiled_hlsl"),
    list_system_parameters: bp("List user-exposed system parameters. Params: systemPath", "list_niagara_system_parameters"),
    list_module_inputs:  bp("List modules + their input pins for an emitter. Params: systemPath, emitterName?, emitterIndex?, stackContext? (ParticleSpawn|ParticleUpdate|EmitterSpawn|EmitterUpdate|all — default all)", "list_niagara_module_inputs"),
    set_module_input:    bp("Set literal default on a module input pin. Params: systemPath, moduleName, inputName, value, emitterName?, emitterIndex?, stackContext?", "set_niagara_module_input"),
    list_static_switches: bp("List static switch inputs on a module. Params: systemPath, moduleName, emitterName?, emitterIndex?, stackContext?", "list_niagara_static_switches"),
    set_static_switch:   bp("Set static switch value on a module's function call node. Params: systemPath, moduleName, switchName, value, emitterName?, emitterIndex?, stackContext?", "set_niagara_static_switch"),
    create_module_from_hlsl: bp("Create a NiagaraScript module backed by a custom HLSL node. Params: name, hlsl, packagePath?, inputs?:[{name,type}], outputs?:[{name,type}]", "create_niagara_module_from_hlsl"),
    batch: {
      description: "Run a sequence of niagara operations against the bridge in order. Fails fast on the first error (returns results up to that point + error). Params: ops:[{action, params}] where action is any niagara subaction listed above.",
      handler: async (ctx, params) => {
        const opsUnknown = params.ops;
        if (!Array.isArray(opsUnknown)) throw new Error("'ops' must be an array of {action, params}");
        const results: Array<{ action: string; result?: unknown; error?: string }> = [];
        for (let i = 0; i < opsUnknown.length; i++) {
          const op = opsUnknown[i] as { action?: string; params?: Record<string, unknown> } | undefined;
          const action = op?.action;
          if (!action) { results.push({ action: "(missing)", error: `ops[${i}] missing 'action'` }); return { results, stoppedAt: i }; }
          const spec = niagaraTool.actions[action];
          if (!spec) { results.push({ action, error: `Unknown niagara action '${action}'` }); return { results, stoppedAt: i }; }
          if (action === "batch") { results.push({ action, error: "nested batch not allowed" }); return { results, stoppedAt: i }; }
          try {
            const subParams = { ...(op.params ?? {}), action } as Record<string, unknown>;
            const result = await (spec as ActionSpec).handler?.(ctx, subParams)
              ?? (spec.bridge ? await ctx.bridge.call(spec.bridge, spec.mapParams ? spec.mapParams(subParams) : (() => { const { action: _, ...r } = subParams; return r; })(), spec.timeoutMs) : undefined);
            results.push({ action, result });
          } catch (e) {
            results.push({ action, error: (e as Error).message });
            return { results, stoppedAt: i };
          }
        }
        return { results, stoppedAt: null };
      },
    },
  },
  undefined,
  {
    assetPath: z.string().optional(), actorLabel: z.string().optional(),
    directory: z.string().optional(), recursive: z.boolean().optional(),
    systemPath: z.string().optional(), emitterPath: z.string().optional(),
    location: Vec3.optional(),
    rotation: Rotator.optional(),
    label: z.string().optional(),
    parameterName: z.string().optional(),
    value: z.unknown().optional(),
    parameterType: z.string().optional(),
    name: z.string().optional(),
    packagePath: z.string().optional(),
    templatePath: z.string().optional(),
    emitterName: z.string().optional(),
    emitterIndex: z.number().optional(),
    rendererType: z.string().optional().describe("sprite|mesh|ribbon or full class name"),
    rendererIndex: z.number().optional(),
    propertyName: z.string().optional(),
    emitters: z.array(z.record(z.unknown())).optional().describe("Spec: [{path:'/Game/VFX/E_Fire'}]"),
    onConflict: z.string().optional().describe("skip|error when asset exists"),
    moduleName: z.string().optional().describe("For module input / static switch ops: name of the module function call node"),
    inputName: z.string().optional().describe("For set_module_input: module input pin name"),
    switchName: z.string().optional().describe("For set_static_switch: static switch input name"),
    stackContext: z.string().optional().describe("ParticleSpawn|ParticleUpdate|EmitterSpawn|EmitterUpdate|all (default all)"),
    hlsl: z.string().optional().describe("For create_module_from_hlsl: HLSL body"),
    inputs: z.array(z.record(z.unknown())).optional().describe("For create_module_from_hlsl: [{name, type}]"),
    outputs: z.array(z.record(z.unknown())).optional().describe("For create_module_from_hlsl: [{name, type}]"),
    ops: z.array(z.record(z.unknown())).optional().describe("For batch: [{action, params}]"),
  },
);
