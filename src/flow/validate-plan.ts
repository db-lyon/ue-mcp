/**
 * Whole-plan validation: everything a run of a flow would trip over, found
 * before step 1 runs and reported together rather than one per attempt.
 *
 * Checked: the flow and every task and nested flow it names, a spec'd bridge
 * action's required options and choices (the ones the call itself would be
 * refused for), `${steps.N.x}` references, `${params.x}` references against the
 * run's params, `${error.x}` outside a hook, and every `when:` and check
 * expression, probe names included. Unknown option names are warnings: the
 * call runs without them today.
 */
import { FlowRunner } from "@db-lyon/flowkit";
import type { FlowDefinition, StepCheck, TaskDefinition, TaskRegistry } from "@db-lyon/flowkit";
import type { ToolDef } from "../core/types.js";
import { McpError, ErrorCode } from "../core/errors.js";
import { nearestActions, ROUTING_PARAMS } from "../surface/action-schema.js";
import { choiceViolation } from "../surface/handler-spec.js";
import { applyCategoryFolding } from "../dispatch/call-pipeline.js";
import { expressionProblem, PROBE_NAMESPACE, probesNamed } from "./condition.js";
import { PROBE_NAMES } from "./probes.js";
import { builtinClassPath } from "./task-call.js";

export type PlanProblemKind = "flow" | "task" | "option" | "reference" | "expression" | "probe";

export interface PlanProblem {
  kind: PlanProblemKind;
  /** The flow the problem is in. */
  flow: string;
  /** The step's path from the flow run (`2`, `2/1`, `finally/1`); absent for the flow itself. */
  path?: string;
  message: string;
  didYouMean?: string[];
}

export interface PlanValidation {
  ok: boolean;
  problems: PlanProblem[];
  warnings?: PlanProblem[];
}

export interface PlanValidationInput {
  config: { tasks: Record<string, unknown>; flows: Record<string, unknown> };
  registry: TaskRegistry;
  graph: readonly ToolDef[];
  flowName: string;
  params?: Record<string, unknown>;
  skip?: string[];
}

/** Names a `when:` or check may read, beside `steps`, `params` and `error`. */
const EXPRESSION_NAMES = ["project", "editor", "session", "step", "call", "gate", PROBE_NAMESPACE];
const HOOK_PHASES = ["on_start", "on_success", "on_failure", "finally"] as const;

interface StepDef {
  task?: string;
  flow?: string;
  options?: Record<string, unknown>;
  when?: string | boolean;
  checks?: StepCheck[];
}

/** Every `${ns.path}` in a value, as [namespace, path]. */
function referencesIn(value: unknown, out: Array<[string, string]> = []): Array<[string, string]> {
  if (typeof value === "string") {
    for (const m of value.matchAll(/\$\{(\w+)\.([^}]+)\}/g)) out.push([m[1], m[2]]);
  } else if (Array.isArray(value)) {
    for (const v of value) referencesIn(v, out);
  } else if (value && typeof value === "object") {
    for (const v of Object.values(value)) referencesIn(v, out);
  }
  return out;
}

function isRecord(v: unknown): v is Record<string, unknown> {
  return v !== null && typeof v === "object" && !Array.isArray(v);
}

export async function validatePlan(input: PlanValidationInput): Promise<PlanValidation> {
  const { config, registry, graph, flowName } = input;
  const flows = config.flows as Record<string, FlowDefinition | undefined>;
  const tasks = config.tasks as Record<string, TaskDefinition | undefined>;
  const params = input.params ?? {};
  const skip = new Set(input.skip ?? []);
  const problems: PlanProblem[] = [];
  const warnings: PlanProblem[] = [];
  const registered = new Set(registry.listRegistered());
  const knownTasks = [...new Set([...Object.keys(tasks), ...registered])];
  /** Where each (flow, step) first appears in the run, for the reference report. */
  const pathOf = new Map<string, string>();

  if (!flows[flowName]) {
    problems.push({
      kind: "flow", flow: flowName, message: `No flow named '${flowName}'.`,
      didYouMean: nearestActions(flowName, Object.keys(flows)),
    });
    return finish(problems, warnings);
  }

  const checkExpression = (flow: string, path: string | undefined, where: string, expr: string | boolean) => {
    const bad = expressionProblem(expr, EXPRESSION_NAMES);
    if (bad) problems.push({ kind: "expression", flow, path, message: `${where}: ${bad}` });
    if (typeof expr !== "string") return;
    for (const probe of probesNamed(expr)) {
      if (PROBE_NAMES.includes(probe)) continue;
      problems.push({
        kind: "probe", flow, path, message: `${where} reads probe.${probe}, and there is no such probe.`,
        didYouMean: nearestActions(probe, PROBE_NAMES),
      });
    }
  };

  const taskExists = async (name: string): Promise<boolean> => {
    const def = tasks[name];
    const target = def?.class_path ?? name;
    if (registered.has(target) || registered.has(name)) return true;
    if (!def) return false;
    try {
      await registry.resolve(target);
      return true;
    } catch {
      return false;
    }
  };

  const checkOptions = (
    flow: string,
    path: string,
    taskName: string,
    options: Record<string, unknown>,
    written: readonly string[],
  ) => {
    const def = tasks[taskName];
    // An overridden task runs its own class, which this cannot read.
    if (def?.class_path && def.class_path !== builtinClassPath(taskName) && def.class_path !== taskName) return;
    const dot = taskName.indexOf(".");
    const tool = dot > 0 ? graph.find((t) => t.name === taskName.slice(0, dot)) : undefined;
    const action = dot > 0 ? taskName.slice(dot + 1) : "";
    const spec = tool?.actions[action];
    if (!tool || !spec) return;

    let folded = options;
    try {
      folded = applyCategoryFolding(options, { action, normalizeParams: tool.options?.normalizeParams });
    } catch {
      // A reference not yet resolved can trip a normalizer; the run reports it then.
    }
    const recorded = spec.kind === "bridge" || spec.kind === "flow" ? spec.paramSpec : undefined;
    if (recorded) {
      const given = (name: string, aliases: readonly string[] = []) =>
        [name, ...aliases].some((k) => folded[k] !== undefined);
      for (const p of recorded) {
        if (p.required && !given(p.name, p.aliases)) {
          problems.push({
            kind: "option", flow, path,
            message: `${taskName} needs '${p.name}', and neither the step, the task's defaults nor the run's params give it.`,
          });
        }
      }
      const choices = spec.kind === "bridge" || spec.kind === "flow" ? spec.paramChoices : undefined;
      const violation = choices ? choiceViolation({ params: recorded, choices }, folded) : undefined;
      if (violation) problems.push({ kind: "option", flow, path, message: `${taskName} ${violation}.` });
    }
    const accepted = new Set([
      ...Object.keys(tool.schema),
      ...ROUTING_PARAMS,
      ...(recorded ?? []).flatMap((p) => [p.name, ...(p.aliases ?? [])]),
    ]);
    // Only what the flow itself wrote: runtime params reach every step by design.
    for (const key of written) {
      if (accepted.has(key)) continue;
      warnings.push({
        kind: "option", flow, path, message: `${taskName} does not take '${key}', so it has no effect.`,
        didYouMean: nearestActions(key, [...accepted]),
      });
    }
  };

  const checkReferences = (flow: string, path: string, value: unknown, inHook: boolean) => {
    for (const [ns, ref] of referencesIn(value)) {
      if (ns === "params") {
        const key = ref.split(".")[0];
        if (!Object.hasOwn(params, key)) {
          problems.push({
            kind: "reference", flow, path,
            message: `\${params.${ref}} needs params.${key}, and the run was given none. Pass it in params.`,
          });
        }
      } else if (ns === "error" && !inHook) {
        problems.push({ kind: "reference", flow, path, message: `\${error.${ref}} is only set in on_failure and finally.` });
      }
    }
  };

  const runtimeLayer = (scope: string | undefined, taskName: string, path: string): Record<string, unknown> => {
    if (scope !== "step") return params;
    return {
      ...(isRecord(params[taskName]) ? params[taskName] as Record<string, unknown> : {}),
      ...(isRecord(params[path]) ? params[path] as Record<string, unknown> : {}),
    };
  };

  const walk = async (
    name: string,
    prefix: string,
    stack: string[],
    inherited: Record<string, Record<string, unknown>>,
    scope: string | undefined,
  ): Promise<void> => {
    const flow = flows[name]!;
    for (const check of flow.checks ?? []) checkExpression(name, prefix || undefined, "flow check", check.when);

    const visit = async (def: StepDef, path: string, stepNumber: string, inHook: boolean) => {
      if (!pathOf.has(`${name}#${stepNumber}`)) pathOf.set(`${name}#${stepNumber}`, path);
      const target = def.task ?? def.flow;
      if (!target || target === "None" || def.flow === "None" || def.task === "None") return;
      if (stack.length === 1 && !inHook && (skip.has(stepNumber) || skip.has(target))) return;
      if (def.when !== undefined) checkExpression(name, path, "when", def.when);
      for (const check of def.checks ?? []) checkExpression(name, path, "check", check.when);
      checkReferences(name, path, def.options, inHook);

      if (def.flow) {
        if (!flows[def.flow]) {
          problems.push({
            kind: "flow", flow: name, path, message: `No flow named '${def.flow}'.`,
            didYouMean: nearestActions(def.flow, Object.keys(flows)),
          });
          return;
        }
        if (stack.includes(def.flow)) {
          problems.push({ kind: "flow", flow: name, path, message: `'${def.flow}' runs itself: ${[...stack, def.flow].join(" > ")}.` });
          return;
        }
        // A flow step's options are per-task overrides for the flow it runs.
        const overrides = { ...inherited };
        for (const [task, opts] of Object.entries(def.options ?? {})) {
          if (isRecord(opts)) overrides[task] = { ...(overrides[task] ?? {}), ...opts };
        }
        await walk(def.flow, `${path}/`, [...stack, def.flow], overrides, scope);
        return;
      }

      const task = def.task!;
      if (!(await taskExists(task))) {
        problems.push({
          kind: "task", flow: name, path, message: `No task named '${task}'.`,
          didYouMean: nearestActions(task, knownTasks),
        });
        return;
      }
      const defaults = tasks[task]?.options ?? {};
      checkReferences(name, path, defaults, inHook);
      const written = { ...defaults, ...(def.options ?? {}), ...(inherited[task] ?? {}) };
      checkOptions(name, path, task, { ...written, ...runtimeLayer(scope, task, path) }, Object.keys(written));
    };

    const steps = Object.entries(flow.steps ?? {}).sort(([a], [b]) => Number(a) - Number(b));
    for (const [n, def] of steps) await visit(def as StepDef, `${prefix}${n}`, n, false);
    for (const phase of HOOK_PHASES) {
      const hooks = (flow[phase] ?? []) as StepDef[];
      for (let i = 0; i < hooks.length; i++) await visit(hooks[i], `${prefix}${phase}/${i + 1}`, `${phase}/${i + 1}`, true);
    }
  };

  const root = flows[flowName]!;
  await walk(flowName, "", [flowName], {}, root.options_scope);

  // Step references, by the runner's own binding rules.
  const runner = new FlowRunner({
    tasks: tasks as Record<string, TaskDefinition>,
    flows: flows as Record<string, FlowDefinition>,
    registry,
    context: {} as never,
  });
  try {
    for (const issue of runner.checkStepReferences(flowName)) {
      const key = `${issue.flowName}#${issue.phase ? `${issue.phase}/` : ""}${issue.stepNumber}`;
      problems.push({
        kind: "reference", flow: issue.flowName,
        path: pathOf.get(key) ?? pathOf.get(`${issue.flowName}#${issue.stepNumber}`),
        message: issue.message,
      });
    }
  } catch {
    // A cycle or a missing flow is already reported above.
  }
  return finish(problems, warnings);
}

function finish(problems: PlanProblem[], warnings: PlanProblem[]): PlanValidation {
  return { ok: problems.length === 0, problems, ...(warnings.length ? { warnings } : {}) };
}

/** One line per problem, as the refusal lists them. */
export function renderProblem(p: PlanProblem): string {
  const at = p.path ? `step ${p.path} of ${p.flow}` : p.flow;
  const hint = p.didYouMean?.length ? ` Did you mean: ${p.didYouMean.join(", ")}?` : "";
  return `${at}: ${p.message}${hint}`;
}

/** The usage error a run is refused with, listing every problem. */
export function planValidationError(flowName: string, validation: PlanValidation): McpError {
  const n = validation.problems.length;
  return new McpError(
    ErrorCode.INVALID_PARAMS,
    `Flow '${flowName}' was not started: ${n} problem${n === 1 ? "" : "s"} in its plan.\n`
      + validation.problems.map((p) => `  - ${renderProblem(p)}`).join("\n"),
    { problems: validation.problems },
  );
}
