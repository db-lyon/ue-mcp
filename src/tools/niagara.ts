import { z } from "zod";
import { categoryTool, bp, type ActionSpec, type ToolDef } from "../types.js";
import { Vec3, Rotator } from "../schemas.js";
import { PAGINATION_SCHEMA, paged } from "../pagination.js";
import { actions as epicActions, schema as epicSchema } from "./epic/niagara.generated.js";

/**
 * set_module_input takes a string on the bridge, so scalars are stringified
 * here. Objects and arrays are rejected rather than stringified: String({...})
 * is "[object Object]", which the handler cannot parse, and an unparsable
 * value used to fall through to a raw pin-default write that reported success.
 * The shared `value` key stays z.unknown() because set_renderer_property
 * genuinely takes structs and arrays (#783); the narrowing belongs here.
 */
function coerceModuleInputValue(value: unknown): string | undefined {
  if (value === undefined || value === null) return undefined;

  const scalar = (v: unknown): string => {
    if (typeof v === "number" && !Number.isFinite(v)) {
      // Atof parses "NaN"/"Infinity", so these reach the pin as real values
      // and report success. Nothing downstream can use them.
      throw new Error("set_module_input: value must be a finite number");
    }
    return String(v);
  };

  if (Array.isArray(value)) {
    // A vector given as [x, y, z] is exactly the comma-separated form the
    // handler's float parser expects, so this one is a real convenience.
    if (value.length === 0) {
      // Joins to "", which the handler reports as a MISSING value - an error
      // about a parameter the caller did pass.
      throw new Error("set_module_input: value must not be an empty array");
    }
    if (!value.every((v) => typeof v === "number" || typeof v === "boolean" || typeof v === "string")) {
      throw new Error("set_module_input: array values must contain only numbers, booleans or strings");
    }
    return value.map(scalar).join(",");
  }

  if (typeof value === "object") {
    const o = value as Record<string, unknown>;
    // {x,y,z[,w]} and {r,g,b[,a]} are the shapes callers reach for.
    for (const keys of [["x", "y", "z", "w"], ["r", "g", "b", "a"]]) {
      const numeric = keys.filter((k) => typeof o[k] === "number");
      if (numeric.length < 2 || numeric.length !== Object.keys(o).length) continue;
      // The present keys must be a PREFIX of the canonical order. Filtering
      // alone accepted {x,y,w} and quietly wrote w into z - a success response
      // with the wrong value in the pin.
      const expected = keys.slice(0, numeric.length);
      if (numeric.join() !== expected.join()) {
        throw new Error(
          `set_module_input: object value has a gap (${numeric.join(",")}); components must be contiguous from ${keys[0]}`,
        );
      }
      const parts = numeric.map((k) => scalar(o[k]));
      // Colour and Vec4 inputs need four components, and the handler's arity
      // error does not say which one is missing. Alpha defaults to opaque
      // because {r,g,b} is the shape callers actually type.
      if (keys[0] === "r" && parts.length === 3) parts.push("1");
      return parts.join(",");
    }
    throw new Error(
      "set_module_input: object values are only accepted as {x,y,z[,w]} or {r,g,b[,a]}; pass other types as a string the input's type can parse",
    );
  }

  return scalar(value);
}

export const niagaraTool: ToolDef = categoryTool(
  "niagara",
  "Niagara VFX: systems, emitters, spawning, parameters, and graph authoring.",
  {
    list:           bp("read", paged("List Niagara assets: every NiagaraSystem and NiagaraEmitter as one row tagged with its `type`, sorted by object path within each type, plus systemCount/emitterCount for the whole listing. Lists the whole project; page through it with cursor/limit. Params: none"), "list_niagara_systems"),
    get_info:       bp("read", "Inspect system. Params: assetPath", "get_niagara_info"),
    list_dynamic_inputs:   bp("read", "Report the authored override map per module: which inputs carry a plain value, which are wired to a dynamic-input script, and which hold an inline HLSL expression, with nested dynamic inputs one level down under nestedOverrides. This is a graph walk, so no property read produces it. Pair with list_module_inputs, which shows the inputs that have no override at all. Params: systemPath, emitterName?, emitterIndex?, stackContext? (ParticleSpawn|ParticleUpdate|EmitterSpawn|EmitterUpdate|all), moduleName?", "list_niagara_dynamic_inputs", (p) => ({ systemPath: p.systemPath, emitterName: p.emitterName, emitterIndex: p.emitterIndex, stackContext: p.stackContext, moduleName: p.moduleName })),
    set_dynamic_input:     bp("mutate", "Wire a dynamic-input NiagaraScript into a module input's override pin, creating the pin if needed and replacing whatever was there. A dynamic input is a graph node, not a property, so set_property cannot do this. Returns dynamicInputName, which is the module name to pass to set_module_input when setting the dynamic input's OWN inputs. Errors list the module's real input names and the modules present. Params: systemPath, stackContext, moduleName, inputName, dynamicInputScript, emitterName?, emitterIndex?", "set_niagara_dynamic_input", (p) => ({ systemPath: p.systemPath, stackContext: p.stackContext, moduleName: p.moduleName, inputName: p.inputName, dynamicInputScript: p.dynamicInputScript, emitterName: p.emitterName, emitterIndex: p.emitterIndex })),
    remove_dynamic_input:  bp("mutate", "Unwire a dynamic input, delete the nodes that fed only it, and drop the override pin so the module's own default comes back. An input with no dynamic input returns alreadyRemoved rather than an error, so a rollback replays safely. Params: systemPath, stackContext, moduleName, inputName, emitterName?, emitterIndex?", "remove_niagara_dynamic_input", (p) => ({ systemPath: p.systemPath, stackContext: p.stackContext, moduleName: p.moduleName, inputName: p.inputName, emitterName: p.emitterName, emitterIndex: p.emitterIndex })),
    add_simulation_stage:  bp("mutate", "Create a simulation stage on an emitter: the stage object AND the backing NiagaraScript, its output node with a fresh usage id, and a parameter-map input node, so the stage actually compiles. set_property cannot create a graph, which is why this is a handler. Returns simulationStageObjectPath; set IterationSource, NumIterations, ExecuteBehavior and the ElementCount bindings on it with asset(set_property). Params: systemPath, stageName, emitterName?, emitterIndex?, enabled?", "add_niagara_simulation_stage", (p) => ({ systemPath: p.systemPath, stageName: p.stageName, emitterName: p.emitterName, emitterIndex: p.emitterIndex, enabled: p.enabled })),
    remove_simulation_stage: bp("mutate", "Remove a simulation stage, its script, and the graph nodes that fed only its output node, leaving nodes shared with another stack alone. A missing stage returns alreadyRemoved with the stages that do exist. Reports removedModules, which the rollback cannot restore, rather than pretending the undo is complete. Params: systemPath, stageName, emitterName?, emitterIndex?", "remove_niagara_simulation_stage", (p) => ({ systemPath: p.systemPath, stageName: p.stageName, emitterName: p.emitterName, emitterIndex: p.emitterIndex })),
    add_event_handler:     bp("mutate", "Create an event handler on an emitter, with its event script, output node and usage id, so the handler's struct fields point at something real. Returns eventHandlerPropertyPath; set ExecutionMode, SpawnNumber and MaxEventsPerFrame through asset(set_property) on emitterObjectPath with that prefix. Params: systemPath, eventName, emitterName?, emitterIndex?, sourceEmitterId?", "add_niagara_event_handler", (p) => ({ systemPath: p.systemPath, eventName: p.eventName, emitterName: p.emitterName, emitterIndex: p.emitterIndex, sourceEmitterId: p.sourceEmitterId })),
    remove_event_handler:  bp("mutate", "Remove an event handler by its script's usage id and delete the graph chain that fed only it. A missing handler returns alreadyRemoved listing the handlers present. Echoes the ExecutionMode and SpawnNumber the rollback will not restore. Params: systemPath, eventName, emitterName?, emitterIndex?", "remove_niagara_event_handler", (p) => ({ systemPath: p.systemPath, eventName: p.eventName, emitterName: p.emitterName, emitterIndex: p.emitterIndex })),
    get_custom_hlsl:       bp("read", "Read every CustomHLSL node in a graph: the source body, and the pins Niagara parsed out of it. Omit nodeIndex to list them all. This is what makes HLSL iterable rather than write-once, since nothing else can read the body back. Params: scriptPath (a NiagaraScript) OR systemPath + stackContext? + emitterName?/emitterIndex?, nodeIndex?", "get_niagara_custom_hlsl", (p) => ({ scriptPath: p.scriptPath, systemPath: p.systemPath, stackContext: p.stackContext, emitterName: p.emitterName, emitterIndex: p.emitterIndex, nodeIndex: p.nodeIndex })),
    set_custom_hlsl:       bp("mutate", "Overwrite a CustomHLSL node's body and reconstruct the node, so the returned pins are the ones the new source actually declares. A bare property write would leave stale pins and an uncompiled script, which is why this is a handler. A body identical to the current one returns alreadySet and skips the recompile. Params: hlsl, scriptPath OR systemPath + stackContext? + emitterName?/emitterIndex?, nodeIndex?", "set_niagara_custom_hlsl", (p) => ({ hlsl: p.hlsl, scriptPath: p.scriptPath, systemPath: p.systemPath, stackContext: p.stackContext, emitterName: p.emitterName, emitterIndex: p.emitterIndex, nodeIndex: p.nodeIndex })),
    remove_module:         bp("mutate", "Remove a module from an emitter stack: unwire its node group, close the parameter-map chain over the gap, and delete the module with its override node and dynamic inputs. add_module had no inverse below UE 5.8, where only the Epic toolset covers this. Returns remainingModules in stack order and echoes the removedOverrides the rollback will not restore. Params: systemPath, stackContext, moduleName, emitterName?, emitterIndex?", "remove_niagara_module", (p) => ({ systemPath: p.systemPath, stackContext: p.stackContext, moduleName: p.moduleName, emitterName: p.emitterName, emitterIndex: p.emitterIndex })),
    set_module_enabled:    bp("mutate", "Enable or disable a module in place. A disabled module keeps its node, its inputs and its stack position and is skipped at compile time, which makes this the reversible way to test whether a module is causing a behaviour. Already in that state returns alreadySet. Params: systemPath, stackContext, moduleName, enabled, emitterName?, emitterIndex?", "set_niagara_module_enabled", (p) => ({ systemPath: p.systemPath, stackContext: p.stackContext, moduleName: p.moduleName, enabled: p.enabled, emitterName: p.emitterName, emitterIndex: p.emitterIndex })),
    compile:        bp("mutate", "Force a real compile of a system and report what the translator said, per script: scriptName, usage, status (NCS_UpToDate | NCS_UpToDateWithWarnings | NCS_Error | ...), errorMsg and every compile event with its severity and the node and pin guid that produced it, plus a top-level compiled boolean and a flat errors[]. This is the assertion a graph edit has to survive. validate answers whether the system EMITS, which a malformed script can still pass, and get_compiled_hlsl returns success without compiling anything at all on a CPU-sim emitter. The call blocks until the compile settles, because an asynchronous one hands back the previous compile's status. force (default true) recompiles even when nothing looks dirty, which is the case a graph edit that left change tracking untouched produces. Params: systemPath, force?, includeGpuShaders?", "compile_niagara_system", (p) => ({ systemPath: p.systemPath, force: p.force, includeGpuShaders: p.includeGpuShaders })),
    validate:       bp("read", "Verify gate: does this system actually emit? Reports per emitter whether it is enabled and has a spawn module + an enabled renderer. valid=false means empty shell. Params: systemPath", "validate_niagara_system", (p) => ({ systemPath: p.systemPath })),
    spawn:          bp("mutate", "Spawn VFX as a transient component (GC's before offscreen capture). For a findable preview use spawn_actor. Params: systemPath, location, rotation?, label?, scaleX?/scaleY?/scaleZ? (separate keys, not a vector object), autoDestroy? (default false)", "spawn_niagara_at_location"),
    spawn_actor:    bp("mutate", "Spawn a PERSISTENT, labeled NiagaraActor in the editor world (findable, re-activatable, survives capture - unlike spawn). Assigns the system and activates. Params: systemPath, location?, rotation?, label?, activate? (default true) (#537)", "spawn_niagara_actor", (p) => ({ systemPath: p.systemPath, location: p.location, rotation: p.rotation, label: p.label, activate: p.activate })),
    reactivate:     bp("mutate", "Reset + reactivate the NiagaraComponent on a placed actor (replay a burst before capturing). Params: actorLabel OR actorPath (#537/#983)", "reactivate_niagara", (p) => ({ actorLabel: p.actorLabel, actorPath: p.actorPath })),
    set_parameter:  bp("mutate", "Set a user parameter on the NiagaraComponent of a placed actor. parameterType selects how the value is read: float, int and bool take `value`; vector takes valueX/valueY/valueZ, which are separate keys rather than a `value` object because that is what the handler reads. Reports previousValue and rolls back to it. Params: actorLabel OR actorPath, parameterName, parameterType? (float|vector|bool|int, default float), value (float/int/bool) OR valueX+valueY+valueZ (vector)", "set_niagara_parameter"),
    create:         bp("mutate", "Create system. Params: name, packagePath?", "create_niagara_system"),
    create_emitter: bp("mutate", "Create a Niagara emitter asset. templatePath copies an existing emitter as the starting point (the content browser's create-from-template path); omit it for the default empty emitter with the standard modules and a sprite renderer. inherit=true makes it a child that tracks the template instead, which then refuses local edits to inherited modules. Params: name, packagePath?, templatePath?, inherit? (default false), onConflict?", "create_niagara_emitter"),
    add_emitter:    bp("mutate", "Add emitter to system. Params: systemPath, emitterPath", "add_emitter_to_system"),
    remove_emitter: bp("mutate", "Remove an emitter from a system (CRUD delete). Params: systemPath, emitterName? or emitterIndex?", "remove_emitter_from_system", (p) => ({ systemPath: p.systemPath, emitterName: p.emitterName, emitterIndex: p.emitterIndex })),
    list_emitters:  bp("read", "List emitters in system. Params: systemPath", "list_emitters_in_system"),
    set_emitter_property: bp("mutate", "Set emitter property. Params: systemPath, emitterName?, propertyName, value", "set_emitter_property"),
    list_modules:   bp("read", paged("List Niagara module scripts, sorted by object path. pathFilter narrows the whole set rather than only the first page, which is what it always claimed to do. Params: pathFilter?"), "list_niagara_modules"),
    get_emitter_info: bp("read", "Inspect emitter. Params: assetPath", "get_emitter_info"),
    list_renderers:   bp("read", "List renderers on an emitter. Params: systemPath, emitterName?, emitterIndex?", "list_emitter_renderers"),
    add_renderer:     bp("mutate", "Add renderer (sprite/mesh/ribbon or full class). Params: systemPath, rendererType, emitterName?, emitterIndex?", "add_emitter_renderer"),
    remove_renderer:  bp("mutate", "Remove renderer by index. Params: systemPath, rendererIndex, emitterName?, emitterIndex?", "remove_emitter_renderer"),
    set_renderer_property: bp("mutate", "Set any renderer property. Bools, numbers and strings are taken directly; object properties (a sprite/mesh renderer's Material, the mesh on a mesh renderer) take an asset path and are class-checked; structs, enums, names and arrays go through the shared JSON property setter, so there is no longer a type whitelist to fall off (#783). Params: systemPath, rendererIndex, propertyName, value, emitterName?, emitterIndex?", "set_renderer_property", (p) => ({ systemPath: p.systemPath, rendererIndex: p.rendererIndex, propertyName: p.propertyName, value: p.value, emitterName: p.emitterName, emitterIndex: p.emitterIndex })),
    inspect_data_interfaces: bp("read", "List user-scope data interfaces. Params: systemPath", "inspect_data_interface"),
    create_system_from_spec: bp("mutate", "Declaratively create a system + emitters. Params: name, packagePath?, emitters?:[{path}]", "create_niagara_system_from_spec"),
    get_compiled_hlsl: bp("read", "Read GPU compute script info for an emitter. Params: systemPath, emitterName?, emitterIndex?", "get_niagara_compiled_hlsl"),
    list_system_parameters: bp("read", "List user-exposed system parameters. Params: systemPath", "list_niagara_system_parameters"),
    list_module_inputs:  bp("read", "List an emitter's modules with the inputs you can actually SET - Spawn Rate, Lifetime, Colour, Sprite Size - each with its name, qualifiedName, type and a settable flag. Current values are NOT returned: the binder's value reader is not exported from NiagaraEditor, so the names and types are readable but the live value is not. Compile-time switches and enums are reported separately under switchPins; note that 'inputs' now means override-map inputs, NOT the function-call node pins it meant before (those are switchPins) (#784). Params: systemPath, emitterName?, emitterIndex?, stackContext? (ParticleSpawn|ParticleUpdate|EmitterSpawn|EmitterUpdate|all - default all), moduleName?", "list_niagara_module_inputs", (p) => ({ systemPath: p.systemPath, emitterName: p.emitterName, emitterIndex: p.emitterIndex, stackContext: p.stackContext, moduleName: p.moduleName })),
    set_module_input:    bp("mutate", "Set a module input value. Override-map-bound inputs (the numeric/colour values that matter) are written through the stack override map, the same path the Niagara stack editor uses; others fall back to the pin default. Reports writePath ('overrideMap'|'pinDefault'). On the overrideMap path previousValue cannot be read back (NiagaraEditor does not export the binder's reader), so it reports '(unread: override map)' and NO rollback is offered - re-set the value explicitly instead. The pinDefault path reports a real previousValue and is rollback-safe (#769). value accepts a scalar, [x,y,z], {x,y,z[,w]} or {r,g,b[,a]} (alpha defaults to 1); anything the input's type cannot parse is REJECTED rather than written, including gapped component objects and non-finite numbers. Params: systemPath, moduleName, inputName, value, emitterName?, emitterIndex?, stackContext?", "set_niagara_module_input", (p) => ({ systemPath: p.systemPath, moduleName: p.moduleName, inputName: p.inputName, value: coerceModuleInputValue(p.value), emitterName: p.emitterName, emitterIndex: p.emitterIndex, stackContext: p.stackContext })),
    add_module:          bp("mutate", "Add a stock /Niagara/Modules script to an emitter stack (the modules that make an emitter actually do anything). Params: systemPath, moduleScript (e.g. /Niagara/Modules/Emitter/SpawnRate), stackContext (ParticleSpawn|ParticleUpdate|EmitterSpawn|EmitterUpdate), emitterName?, emitterIndex?, targetIndex? (-1 appends). Then set_module_input to tune it.", "add_niagara_module", (p) => ({ systemPath: p.systemPath, moduleScript: p.moduleScript, stackContext: p.stackContext, emitterName: p.emitterName, emitterIndex: p.emitterIndex, targetIndex: p.targetIndex })),
    list_static_switches: bp("read", "List static switch inputs on a module. Params: systemPath, moduleName, emitterName?, emitterIndex?, stackContext?", "list_niagara_static_switches"),
    set_static_switch:   bp("mutate", "Set static switch value on a module's function call node. Params: systemPath, moduleName, switchName, value, emitterName?, emitterIndex?, stackContext?", "set_niagara_static_switch"),
    create_module_from_hlsl: bp("mutate", "Create a NiagaraScript module backed by a custom HLSL node. Params: name, hlsl, packagePath?, inputs?:[{name,type}], outputs?:[{name,type}]", "create_niagara_module_from_hlsl"),
    create_scratch_module:  bp("mutate", "Create empty Niagara scratch module. Params: name, packagePath?, inputs?:[{name,type}], outputs?:[{name,type}] (#185)", "create_scratch_module"),
    batch: {
      kind: "handler",
      effect: "mutate",
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
    ...epicActions,
  },
  undefined,
  {
    ...epicSchema,
    dynamicInputScript: z.string().optional().describe("set_dynamic_input: the dynamic-input NiagaraScript to wire in"),
    enabled: z.boolean().optional().describe("add_simulation_stage: create the stage enabled (default true). set_module_enabled: the state to set"),
    stageName: z.string().optional().describe("add/remove_simulation_stage: the simulation stage name"),
    eventName: z.string().optional().describe("add/remove_event_handler: the event handler name"),
    sourceEmitterId: z.string().optional().describe("add_event_handler: the emitter whose events this handler listens to"),
    nodeIndex: z.number().optional().describe("get/set_custom_hlsl: which CustomHLSL node, when the graph has several"),
    scriptPath: z.string().optional().describe("get/set_custom_hlsl: address a NiagaraScript asset directly instead of a system"),
    assetPath: z.string().optional(), actorLabel: z.string().optional(),
    actorPath: z.string().optional().describe("Full actor object path. The unambiguous selector, and it wins over actorLabel when both are given. Editor labels are NOT unique, and a label matching several actors is refused with the candidates rather than resolved at random (#983)"),
    systemPath: z.string().optional(), emitterPath: z.string().optional(),
    location: Vec3.optional(),
    activate: z.boolean().optional().describe("spawn_actor: activate the system on spawn (default true) (#537)"),
    // spawn reads these four by name. Undeclared, the MCP layer stripped them
    // before the bridge saw them, so a scaled or auto-destroying spawn reported
    // success and spawned at unit scale that never went away.
    scaleX: z.number().optional().describe("spawn: X scale of the spawned component (default 1)"),
    scaleY: z.number().optional().describe("spawn: Y scale of the spawned component (default 1)"),
    scaleZ: z.number().optional().describe("spawn: Z scale of the spawned component (default 1)"),
    autoDestroy: z.boolean().optional().describe("spawn: destroy the component once the system finishes (default false)"),
    rotation: Rotator.optional(),
    label: z.string().optional(),
    parameterName: z.string().optional(),
    // Shared across set_renderer_property / set_parameter / set_module_input.
    // Must stay unknown: the renderer/parameter paths accept structs and arrays
    // via the JSON property setter (#783). set_module_input stringifies in its
    // own mapper because its handler takes a string.
    value: z.unknown().optional(),
    parameterType: z.string().optional().describe("set_parameter: float (default) | vector | bool | int"),
    valueX: z.number().optional().describe("set_parameter with parameterType=vector: X component"),
    valueY: z.number().optional().describe("set_parameter with parameterType=vector: Y component"),
    valueZ: z.number().optional().describe("set_parameter with parameterType=vector: Z component"),
    name: z.string().optional(),
    packagePath: z.string().optional(),
    templatePath: z.string().optional().describe("create_emitter: emitter asset to copy as a starting point"),
    inherit: z.boolean().optional().describe("create_emitter: inherit from templatePath instead of copying it (default false)"),
    emitterName: z.string().optional(),
    emitterIndex: z.number().optional(),
    rendererType: z.string().optional().describe("sprite|mesh|ribbon or full class name"),
    rendererIndex: z.number().optional(),
    propertyName: z.string().optional(),
    emitters: z.array(z.record(z.unknown())).optional().describe("Spec: [{path:'/Game/VFX/E_Fire'}]"),
    onConflict: z.string().optional().describe("skip|error when asset exists"),
    moduleName: z.string().optional().describe("For module input / static switch ops: name of the module function call node"),
    moduleScript: z.string().optional().describe("For add_module: stock module script path, e.g. /Niagara/Modules/Emitter/SpawnRate"),
    targetIndex: z.number().optional().describe("For add_module: stack insert position; -1 (default) appends"),
    inputName: z.string().optional().describe("set_module_input / set_dynamic_input / remove_dynamic_input: module input pin name"),
    switchName: z.string().optional().describe("For set_static_switch: static switch input name"),
    stackContext: z.string().optional().describe("ParticleSpawn|ParticleUpdate|EmitterSpawn|EmitterUpdate|all (default all)"),
    force: z.boolean().optional().describe("compile: recompile even when nothing looks dirty (default true)"),
    includeGpuShaders: z.boolean().optional().describe("compile: also wait for GPU shader compilation to finish (default false)"),
    hlsl: z.string().optional().describe("create_module_from_hlsl / set_custom_hlsl: HLSL body"),
    inputs: z.array(z.record(z.unknown())).optional().describe("For create_module_from_hlsl: [{name, type}]"),
    outputs: z.array(z.record(z.unknown())).optional().describe("For create_module_from_hlsl: [{name, type}]"),
    ops: z.array(z.record(z.unknown())).optional().describe("For batch: [{action, params}]"),
    pathFilter: z.string().optional().describe("list_modules: case-sensitive substring of the module script object path, applied to the whole set before paging"),
    // cursor + limit for the paged list actions. Declared once: the MCP layer
    // strips a key the category never declares, so a paged action whose
    // category omits these silently returns page one forever.
    ...PAGINATION_SCHEMA,
  },
);
