import { TaskRegistry, ShellTask, mergeOptionSpecs } from "@db-lyon/flowkit";
import type {
  OptionSpecs,
  ReferenceContext,
  TaskConstructor,
  TaskContext,
  TaskContextInput,
  TaskDefinition,
  TaskDescription,
  TaskResult,
} from "@db-lyon/flowkit";
import type { ToolDef } from "../core/types.js";
import type { FlowContext } from "./context.js";
import { BridgeTask, ReconnectTask } from "./bridge-task.js";
import { bridgeTaskClass, compositeTaskClass, handlerTaskClass, readTaskClass } from "./task-factory.js";
import { STATUS_PART_PREFIX, STATUS_PARTS } from "../tools/project/status-parts.js";
import { actionPreparation } from "./run-action.js";
import { MICRO_GATEWAY_TOOL, MICRO_GATEWAY_CALL, microGatewayTargets, resolveMicroCall } from "../surface/context/micro-context.js";
import { McpError, ErrorCode } from "../core/errors.js";
import { paramMapperOf } from "../surface/epic-input.js";
import { builtinClassPath, LIVE_REFERENCES_KEY, resolveConfiguredTask } from "./task-call.js";
import { actionOptionSpecs } from "../surface/option-specs.js";
import { internalTasks } from "./internal-tasks.js";
import { isContextCommand } from "../runtime/context-commands.js";
import { UeMcpTask } from "../task.js";
import { RECORDED_HANDLER_SPECS } from "../tools/specs/index.js";
import type { ParamSpec } from "../surface/handler-spec.js";

/**
 * A registry that describes every action's options, from the same reading
 * describe_action reports: a handler's declared options, a bridge action's
 * recorded C++ spec or Epic schema, a plugin action's manifest.
 *
 * Describing is not enforcing. A handler task enforces only the types of its
 * options (see buildFlowRegistry), and a bridge task none, because the bridge
 * coerces values and renames aliases after the point where the runner checks.
 */
export class DescribedTaskRegistry extends TaskRegistry {
  private readonly declared = new Map<string, { read: () => OptionSpecs | undefined; overClass: boolean }>();

  /**
   * Record where a class path's option schema comes from, read on first
   * describe. `overClass` lets it stand in for the class's own declaration,
   * which is the case for the built-ins whose class enforces a subset of it.
   */
  declareOptions(classPath: string, source: () => OptionSpecs | undefined, overClass = true): this {
    let cached: { value: OptionSpecs | undefined } | undefined;
    this.declared.set(classPath, { read: () => (cached ??= { value: source() }).value, overClass });
    return this;
  }

  override async describe(name: string, taskDefinitions?: Record<string, TaskDefinition>): Promise<TaskDescription> {
    const out = await super.describe(name, taskDefinitions);
    const entry = this.declared.get(out.class_path);
    if (!entry || (out.options_schema && !entry.overClass)) return out;
    const merged = mergeOptionSpecs(entry.read(), taskDefinitions?.[name]?.options_schema);
    if (merged && Object.keys(merged).length > 0) out.options_schema = merged;
    return out;
  }
}

/** A recorded parameter as an option spec: its JSON type, and whether it is required. */
function paramOptionSpec(param: ParamSpec): OptionSpecs[string] {
  const type = param.type === "vec3" || param.type === "rotator" || param.type === "color" ? "object"
    : param.type === "any" ? undefined
    : param.type;
  return {
    ...(type ? { type } : {}),
    description: param.description,
    ...(param.required ? { required: true } : {}),
  };
}

/** Types and allowed values only: a declared handler's required flags are advisory. */
function enforcedSpecs(specs: OptionSpecs): OptionSpecs {
  return Object.fromEntries(Object.entries(specs).map(([name, spec]) => {
    const { required: _required, ...rest } = spec;
    return [name, rest];
  }));
}

/** A gateway call is an alias for the target task, not a handler that executes
 *  another action and repackages its result. Both MCP and FlowRunner create
 *  tasks here, so plugin dispatch, failures and rollback keep the same path. */
class MicroTaskRegistry extends DescribedTaskRegistry {
  /** `targets` is what the gateway itself reaches: the enabled categories. */
  constructor(private readonly targets: ToolDef[]) {
    super();
  }

  override async create(name: string, ctx: TaskContextInput, options: Record<string, unknown>) {
    if (name === `${MICRO_GATEWAY_TOOL}.${MICRO_GATEWAY_CALL}`) {
      const graph = (ctx as FlowContext).getToolGraph?.() ?? this.targets;
      const call = resolveMicroCall(graph, options);
      if (!this.listRegistered().includes(call.taskName)) {
        throw new McpError(ErrorCode.NO_HANDLER, `Action ${call.taskName} has no registered task.`);
      }
      // The target resolves through the same `tasks:` definitions as a direct call.
      const c = ctx as TaskContext;
      const references = c.taskReferenceContext ?? (c[LIVE_REFERENCES_KEY] as ReferenceContext | undefined);
      const target = resolveConfiguredTask(call.taskName, c.taskDefinitions, call.params, references);
      return super.create(target.classPath, ctx, target.options);
    }
    return super.create(name, ctx, options);
  }
}

/** A task that refuses to run a context command, saying what to do instead. */
function contextCommandRefusal(taskName: string): TaskConstructor {
  class ContextCommandRefusal extends UeMcpTask {
    get taskName() { return taskName; }
    async execute(): Promise<TaskResult> {
      return {
        success: false,
        error: new McpError(
          ErrorCode.INVALID_PARAMS,
          `${taskName} changes which editor calls run in, so it is not a task and a flow cannot call it. `
            + "Call it directly, before or after the flow.",
        ),
      };
    }
  }
  return ContextCommandRefusal as unknown as TaskConstructor;
}

/**
 * Walk all category tools and register every action as a flowkit task.
 *
 * - Bridge actions → factory classes with method + mapParams in closure
 * - Handler actions → factory classes wrapping the existing handler function
 * - Flow actions → composite classes whose children run through the runner
 *
 * Each is registered by its task name and by its base alias
 * (`ue-mcp.builtin/<category>.<action>`), which the default definitions point
 * at, so an override can replace the name and still reach the built-in.
 *
 * Also registers `ue-mcp.bridge` as a class_path for YAML-defined bridge tasks,
 * and the `internal.*` primitives the universal flows call, which are tasks
 * and never actions (src/flow/internal-tasks.ts).
 */
export function buildFlowRegistry(tools: ToolDef[]): DescribedTaskRegistry {
  // The registry keeps disabled categories so flows can name them directly;
  // the gateway resolves only the categories it was built from.
  const gateway = tools.find((tool) => tool.name === MICRO_GATEWAY_TOOL);
  const registry = gateway
    ? new MicroTaskRegistry(microGatewayTargets(gateway) ?? tools)
    : new DescribedTaskRegistry();

  // Register built-in task class paths
  registry.registerClassPath("ue-mcp.bridge", BridgeTask as unknown as TaskConstructor);
  registry.registerClassPath("ue-mcp.reconnect", ReconnectTask as unknown as TaskConstructor);
  for (const [part, read] of Object.entries(STATUS_PARTS)) {
    registry.registerClassPath(`${STATUS_PART_PREFIX}${part}`, readTaskClass(`${STATUS_PART_PREFIX}${part}`, read));
  }
  registry.register("shell", ShellTask as unknown as TaskConstructor);
  for (const [taskName, task] of internalTasks()) {
    registry.register(taskName, bridgeTaskClass(taskName, task.method));
    registry.declareOptions(taskName, () => {
      const params = RECORDED_HANDLER_SPECS[task.method]?.params ?? [];
      return Object.fromEntries(params.map((p) => [p.name, paramOptionSpec(p)]));
    });
  }

  for (const tool of tools) {
    for (const [actionName, spec] of Object.entries(tool.actions)) {
      const taskName = `${tool.name}.${actionName}`;
      // Context commands are dispatched by the runtime, never as tasks, so no
      // flow can re-point the editor it runs in.
      // The alias the defaults point at answers with why, instead of a failed lookup.
      if (isContextCommand(taskName)) {
        registry.registerClassPath(builtinClassPath(taskName), contextCommandRefusal(taskName));
        continue;
      }

      // The same preparation a category tool's own handler builds, so the
      // live route and every other route fold, repair and check alike.
      const prep = actionPreparation(tool.options, actionName, spec);

      if (spec.kind === "registry") {
        // A plugin registers its own class; its manifest schema describes it.
        if (spec.optionsSchema) registry.declareOptions(taskName, () => spec.optionsSchema, false);
        continue;
      }

      if (spec.kind === "flow") {
        // Children run through the runner; see flow/composite.ts.
        const ctor = compositeTaskClass(taskName, spec, prep);
        registry.register(taskName, ctor);
        registry.registerClassPath(builtinClassPath(taskName), ctor);
        registry.declareOptions(builtinClassPath(taskName), () => actionOptionSpecs(tool, actionName));
      } else if (spec.handler) {
        // FlowContext is a structural superset of ToolContext (see
        // context.ts), so we pass ctx straight through. Rebuilding it
        // field-by-field used to silently drop new accessors at this
        // boundary - never reintroduce that pattern.
        const originalHandler = spec.handler;
        // A declared handler's option types are checked on every runner path.
        // Its required flags are not: several fall back to a default, or
        // answer a missing value with guidance the check would replace.
        const optionsSchema = spec.options ? enforcedSpecs(actionOptionSpecs(tool, actionName)) : undefined;
        const ctor = handlerTaskClass(taskName, (ctx: FlowContext, params: Record<string, unknown>) => {
          return originalHandler(ctx, params);
        }, prep, optionsSchema);
        registry.declareOptions(builtinClassPath(taskName), () => actionOptionSpecs(tool, actionName));
        registry.register(taskName, ctor);
        registry.registerClassPath(builtinClassPath(taskName), ctor);
      } else if (spec.bridge) {
        // The action's authored budget travels with it. Three actions
        // (blueprint.flush_component_templates, widget.add_widget,
        // widget.remove_widget) declare 120s because their method has no
        // entry in the editor's own timeout table, and dropping it here gave
        // them the 30s default on every live call.
        const ctor = bridgeTaskClass(taskName, spec.bridge, paramMapperOf(spec), spec.timeoutMs, prep);
        registry.register(taskName, ctor);
        registry.registerClassPath(builtinClassPath(taskName), ctor);
        registry.declareOptions(builtinClassPath(taskName), () => actionOptionSpecs(tool, actionName));
      }
    }
  }

  return registry;
}
