import { z } from "zod";
import type { ToolContext, ToolDef } from "../types.js";
import { categoryTool } from "../category-tool.js";
import { EDITOR_TARGET_PARAM } from "../routing-params.js";
import { handlerFailure } from "../flow/handler-outcome.js";
import { actions as epicActions, schema as epicSchema } from "./epic/niagara.generated.js";
import { specBp, schema as specSchema } from "./specs/niagara.generated.js";

/** Whether a batch op's `editor` names the session the batch itself runs on. */
function targetsSession(ctx: ToolContext, target: unknown): boolean {
  if (!ctx.sessions || !ctx.session) return false;
  try {
    return ctx.sessions.resolve(target) === ctx.session;
  } catch {
    return false;
  }
}

export const niagaraTool: ToolDef = categoryTool(
  "niagara",
  "Niagara VFX: systems, emitters, spawning, parameters, and graph authoring.",
  {
    list:           specBp("read", "List Niagara assets: every NiagaraSystem and NiagaraEmitter as one row tagged with its `type`, sorted by object path within each type, plus systemCount/emitterCount for the whole listing. Lists the whole project; page through it with cursor/limit.", "list_niagara_systems"),
    get_info:       specBp("read", "Inspect system.", "get_niagara_info"),
    list_dynamic_inputs:   specBp("read", "Report the authored override map per module: which inputs carry a plain value, which are wired to a dynamic-input script, and which hold an inline HLSL expression, with nested dynamic inputs one level down under nestedOverrides. This is a graph walk, so no property read produces it. Pair with list_module_inputs, which shows the inputs that have no override at all.", "list_niagara_dynamic_inputs"),
    set_dynamic_input:     specBp("mutate", "Wire a dynamic-input NiagaraScript into a module input's override pin, creating the pin if needed and replacing whatever was there. A dynamic input is a graph node, not a property, so set_property cannot do this. Returns dynamicInputName, which is the module name to pass to set_module_input when setting the dynamic input's OWN inputs. Errors list the module's real input names and the modules present.", "set_niagara_dynamic_input"),
    remove_dynamic_input:  specBp("mutate", "Unwire a dynamic input, delete the nodes that fed only it, and drop the override pin so the module's own default comes back. An input with no dynamic input returns alreadyRemoved rather than an error, so a rollback replays safely.", "remove_niagara_dynamic_input"),
    add_simulation_stage:  specBp("mutate", "Create a simulation stage on an emitter: the stage object AND the backing NiagaraScript, its output node with a fresh usage id, and a parameter-map input node, so the stage actually compiles. set_property cannot create a graph, which is why this is a handler. Returns simulationStageObjectPath; set IterationSource, NumIterations, ExecuteBehavior and the ElementCount bindings on it with asset(set_property).", "add_niagara_simulation_stage"),
    remove_simulation_stage: specBp("mutate", "Remove a simulation stage, its script, and the graph nodes that fed only its output node, leaving nodes shared with another stack alone. A missing stage returns alreadyRemoved with the stages that do exist. Reports removedModules, which the rollback cannot restore, rather than pretending the undo is complete.", "remove_niagara_simulation_stage"),
    add_event_handler:     specBp("mutate", "Create an event handler on an emitter, with its event script, output node and usage id, so the handler's struct fields point at something real. Returns eventHandlerPropertyPath; set ExecutionMode, SpawnNumber and MaxEventsPerFrame through asset(set_property) on emitterObjectPath with that prefix.", "add_niagara_event_handler"),
    remove_event_handler:  specBp("mutate", "Remove an event handler by its script's usage id and delete the graph chain that fed only it. A missing handler returns alreadyRemoved listing the handlers present. Echoes the ExecutionMode and SpawnNumber the rollback will not restore.", "remove_niagara_event_handler"),
    get_custom_hlsl:       specBp("read", "Read every CustomHLSL node in a graph: the source body, and the pins Niagara parsed out of it. Omit nodeIndex to list them all. This is what makes HLSL iterable rather than write-once, since nothing else can read the body back. stackContext and the emitter apply when addressing a system.", "get_niagara_custom_hlsl"),
    set_custom_hlsl:       specBp("mutate", "Overwrite a CustomHLSL node's body and reconstruct the node, so the returned pins are the ones the new source actually declares. A bare property write would leave stale pins and an uncompiled script, which is why this is a handler. A body identical to the current one returns alreadySet and skips the recompile. stackContext and the emitter apply when addressing a system.", "set_niagara_custom_hlsl"),
    remove_module:         specBp("mutate", "Remove a module from an emitter stack: unwire its node group, close the parameter-map chain over the gap, and delete the module with its override node and dynamic inputs. add_module had no inverse below UE 5.8, where only the Epic toolset covers this. Returns remainingModules in stack order and echoes the removedOverrides the rollback will not restore.", "remove_niagara_module"),
    set_module_enabled:    specBp("mutate", "Enable or disable a module in place. A disabled module keeps its node, its inputs and its stack position and is skipped at compile time, which makes this the reversible way to test whether a module is causing a behaviour. Already in that state returns alreadySet.", "set_niagara_module_enabled"),
    compile:        specBp("mutate", "Force a real compile of a system and report what the translator said, per script: scriptName, usage, status (NCS_UpToDate | NCS_UpToDateWithWarnings | NCS_Error | ...), errorMsg and every compile event with its severity and the node and pin guid that produced it, plus a top-level compiled boolean and a flat errors[]. This is the assertion a graph edit has to survive. validate answers whether the system EMITS, which a malformed script can still pass, and get_compiled_hlsl returns success without compiling anything at all on a CPU-sim emitter. The call blocks until the compile settles, because an asynchronous one hands back the previous compile's status. force (default true) recompiles even when nothing looks dirty, which is the case a graph edit that left change tracking untouched produces.", "compile_niagara_system"),
    validate:       specBp("read", "Verify gate: does this system actually emit? Reports per emitter whether it is enabled and has a spawn module + an enabled renderer. valid=false means empty shell.", "validate_niagara_system"),
    spawn:          specBp("mutate", "Spawn VFX as a transient component (GC's before offscreen capture). For a findable preview use spawn_actor. Scale is three separate keys, scaleX/scaleY/scaleZ, not a vector object; autoDestroy defaults to false.", "spawn_niagara_at_location"),
    spawn_actor:    specBp("mutate", "Spawn a PERSISTENT, labeled NiagaraActor in the editor world (findable, re-activatable, survives capture - unlike spawn). Assigns the system and activates unless activate=false (#537).", "spawn_niagara_actor"),
    reactivate:     specBp("mutate", "Reset + reactivate the NiagaraComponent on a placed actor (replay a burst before capturing) (#537/#983).", "reactivate_niagara"),
    set_parameter:  specBp("mutate", "Set a user parameter on the NiagaraComponent of a placed actor. parameterType selects how the value is read: float, int and bool take `value`; vector takes valueX/valueY/valueZ, which are separate keys rather than a `value` object because that is what the handler reads. Reports previousValue and rolls back to it.", "set_niagara_parameter"),
    create:         specBp("mutate", "Create system. Idempotent by path: an existing system is reported rather than replaced.", "create_niagara_system"),
    create_emitter: specBp("mutate", "Create a Niagara emitter asset. templatePath copies an existing emitter as the starting point (the content browser's create-from-template path); omit it for the default empty emitter with the standard modules and a sprite renderer. inherit=true makes it a child that tracks the template instead, which then refuses local edits to inherited modules.", "create_niagara_emitter"),
    add_emitter:    specBp("mutate", "Add emitter to system.", "add_emitter_to_system"),
    remove_emitter: specBp("mutate", "Remove an emitter from a system (CRUD delete), addressed by emitterName or emitterIndex.", "remove_emitter_from_system"),
    list_emitters:  specBp("read", "List emitters in system.", "list_emitters_in_system"),
    set_emitter_property: specBp("mutate", "Set emitter property.", "set_emitter_property"),
    list_modules:   specBp("read", "List Niagara module scripts, sorted by object path. pathFilter narrows the whole set rather than only the first page, which is what it always claimed to do.", "list_niagara_modules"),
    get_emitter_info: specBp("read", "Inspect emitter.", "get_emitter_info"),
    list_renderers:   specBp("read", "List renderers on an emitter.", "list_emitter_renderers"),
    add_renderer:     specBp("mutate", "Add renderer (sprite/mesh/ribbon or full class).", "add_emitter_renderer"),
    remove_renderer:  specBp("mutate", "Remove renderer by index.", "remove_emitter_renderer"),
    set_renderer_property: specBp("mutate", "Set any renderer property. Bools, numbers and strings are taken directly; object properties (a sprite/mesh renderer's Material, the mesh on a mesh renderer) take an asset path and are class-checked; structs, enums, names and arrays go through the shared JSON property setter, so there is no longer a type whitelist to fall off (#783).", "set_renderer_property"),
    inspect_data_interfaces: specBp("read", "List user-scope data interfaces.", "inspect_data_interface"),
    create_system_from_spec: specBp("mutate", "Declaratively create a system + emitters: emitters=[{path}].", "create_niagara_system_from_spec"),
    get_compiled_hlsl: specBp("read", "Read GPU compute script info for an emitter.", "get_niagara_compiled_hlsl"),
    list_system_parameters: specBp("read", "List user-exposed system parameters.", "list_niagara_system_parameters"),
    list_module_inputs:  specBp("read", "List an emitter's modules with the inputs you can actually SET - Spawn Rate, Lifetime, Colour, Sprite Size - each with its name, qualifiedName, type and a settable flag. Current values are NOT returned: the binder's value reader is not exported from NiagaraEditor, so the names and types are readable but the live value is not. Compile-time switches and enums are reported separately under switchPins; note that 'inputs' now means override-map inputs, NOT the function-call node pins it meant before (those are switchPins) (#784).", "list_niagara_module_inputs"),
    set_module_input:    specBp("mutate", "Set a module input value. Override-map-bound inputs (the numeric/colour values that matter) are written through the stack override map, the same path the Niagara stack editor uses; others fall back to the pin default. Reports writePath ('overrideMap'|'pinDefault'). On the overrideMap path previousValue cannot be read back (NiagaraEditor does not export the binder's reader), so it reports '(unread: override map)' and NO rollback is offered - re-set the value explicitly instead. The pinDefault path reports a real previousValue and is rollback-safe (#769). value accepts a scalar, [x,y,z], {x,y,z[,w]} or {r,g,b[,a]} (alpha defaults to 1); anything the input's type cannot parse is REJECTED rather than written, including gapped component objects and non-finite numbers.", "set_niagara_module_input"),
    add_module:          specBp("mutate", "Add a stock /Niagara/Modules script to an emitter stack (the modules that make an emitter actually do anything). Then set_module_input to tune it.", "add_niagara_module"),
    list_static_switches: specBp("read", "List static switch inputs on a module.", "list_niagara_static_switches"),
    set_static_switch:   specBp("mutate", "Set static switch value on a module's function call node.", "set_niagara_static_switch"),
    create_module_from_hlsl: specBp("mutate", "Create a NiagaraScript module backed by a custom HLSL node. inputs/outputs are [{name,type}].", "create_niagara_module_from_hlsl"),
    create_scratch_module:  specBp("mutate", "Create empty Niagara scratch module. inputs/outputs are [{name,type}] (#185).", "create_scratch_module"),
    batch: {
      kind: "handler",
      effect: "mutate",
      description: "Run a sequence of niagara operations against the bridge in order. Fails fast on the first error (returns results up to that point + error). Params: ops:[{action, params}] where action is any niagara subaction listed above.",
      handler: async (ctx, params) => {
        const opsUnknown = params.ops;
        if (!Array.isArray(opsUnknown)) throw new Error("'ops' must be an array of {action, params}");
        // A session graph may add or remove actions, and the category handler
        // owns every piece of per-call preparation. Dispatch each operation
        // through that active handler instead of duplicating part of it here.
        // Contexts built outside the registry (mainly direct unit calls) have
        // no graph accessor, so only those use the exported base tool.
        // A session with no surface built throws; that is reported per op, like
        // a graph without niagara, rather than failing the whole call.
        let unavailable = "Niagara is not available in the active tool graph";
        let dispatchTool: ToolDef | undefined;
        try {
          const graph = ctx.getToolGraph ? ctx.getToolGraph() : undefined;
          dispatchTool = graph === undefined ? niagaraTool : graph.find((tool) => tool.name === "niagara");
        } catch (e) {
          unavailable = `${unavailable}: ${(e as Error).message}`;
        }
        const results: Array<{ action: string; result?: unknown; error?: string }> = [];
        for (let i = 0; i < opsUnknown.length; i++) {
          const op = opsUnknown[i] as { action?: string; params?: Record<string, unknown> } | undefined;
          const action = op?.action;
          if (!action) { results.push({ action: "(missing)", error: `ops[${i}] missing 'action'` }); return { results, stoppedAt: i }; }
          if (!dispatchTool) { results.push({ action, error: unavailable }); return { results, stoppedAt: i }; }
          const spec = dispatchTool.actions[action];
          if (!spec) { results.push({ action, error: `Unknown niagara action '${action}'` }); return { results, stoppedAt: i }; }
          if (action === "batch") { results.push({ action, error: "nested batch not allowed" }); return { results, stoppedAt: i }; }
          // A batch runs on one editor. An op naming another would be stripped
          // and silently run here, so refuse it unless it names this one.
          const opTarget = dispatchTool.injectedEditorParam ? op.params?.[EDITOR_TARGET_PARAM] : undefined;
          if (opTarget !== undefined && opTarget !== "" && !targetsSession(ctx, opTarget)) {
            results.push({
              action,
              error: `ops[${i}] targets editor '${String(opTarget)}', but a batch runs on one editor`
                + `${ctx.session ? ` ('${ctx.session.name}')` : ""}. Put 'editor' on the batch call, or split the batch per editor.`,
            });
            return { results, stoppedAt: i };
          }
          try {
            const subParams = { ...(op.params ?? {}), action } as Record<string, unknown>;
            // The batch's own budget covers every op that names none of its own.
            if (subParams.timeoutMs === undefined && ctx.callTimeoutMs !== undefined) subParams.timeoutMs = ctx.callTimeoutMs;
            const result = await dispatchTool.handler(ctx, subParams);
            // A handler that failed resolves with success:false rather than
            // throwing, so the verdict is read off the body.
            const failure = handlerFailure(result);
            if (failure !== null) {
              results.push({ action, result, error: failure });
              return { results, stoppedAt: i };
            }
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
  {
    ...epicSchema,
    // #1057: every key a spec'd handler declares, generated from its C++
    // registration. A key listed again below is shared with hand-written
    // actions, and tests/unit/handler-specs.test.ts holds the two to one type.
    ...specSchema,
    ops: z.array(z.record(z.unknown())).optional().describe("For batch: [{action, params}]"),
  },
);
