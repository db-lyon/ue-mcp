/**
 * The server's gates as declared flow checks, for `flow(plan)`.
 *
 * Four gates can refuse a call: the untargeted-write gate (a change with more
 * than one editor and none named), the editor-connected gate (a bridge action
 * with no editor), the dialog gate (a modal on screen) and the Python gate
 * (execute_python before its candidates are ruled out). They are declared once
 * in flow/gates.ts. Here each becomes a `checks:` entry over the `gate`
 * namespace, whose values are those declarations evaluated over facts read
 * without acting, so a plan reports what a run would be refused for.
 *
 * A run is refused by the same declarations, evaluated live where each route
 * always refused: dispatch, runAction, the guarded bridge and execute_python.
 * These checks are added to the plan's copy of the config only.
 */
import { FlowRunner } from "@db-lyon/flowkit";
import type {
  CheckOutcome,
  ConditionContext,
  FlowDefinition,
  StepCheck,
  TaskDefinition,
  TaskRegistry,
} from "@db-lyon/flowkit";
import type { ToolContext, ToolDef } from "../core/types.js";
import { taskEffect } from "../surface/action-effects.js";
import { ensureGuard } from "../editor/dialog-guard.js";
import { pythonGateRefusal } from "../dispatch/python-gate.js";
import { needsExplicitEditor, refuseUntargetedInRegistry } from "../dispatch/editor-gate.js";
import { hostNamespaces, makeConditionEvaluator, type StepScope } from "./condition.js";
import { stepAvailability } from "./flow-describe.js";
import { GATE_IDS, GATES, gateFires } from "./gates.js";

/** What a plan gates: a run of the flow, whatever step is being looked at. */
const PLANNED_CALL = "flow.run";

/**
 * `step.*`, `call.*` and `gate.*`, read for the step being gated. `gate.<id>`
 * is the declared gate evaluated over the others and the host namespaces. A
 * flow-level check sees no step, and only `gate.untargeted` can fire there.
 */
export function gateScope(ctx: ToolContext, graph: readonly ToolDef[], oneReading = false): StepScope {
  let dialogProbe: Promise<((subject: string, kind: "bridge" | "action") => Promise<boolean>) | undefined> | undefined;
  // A plan reads the screen once for all its steps; a run reads it per step.
  const dialog = () => (dialogProbe ??= ctx.session
    ? ensureGuard(ctx.session).then(async (g) => {
      if (!oneReading) return (s: string, k: "bridge" | "action") => g.wouldRefuse(s, k);
      const now = await g.refusalsNow();
      return async (s: string, k: "bridge" | "action") => now(s, k);
    }, () => undefined)
    : Promise.resolve(undefined));

  const gates = (facts: Record<string, unknown>) =>
    Object.fromEntries(GATE_IDS.map((id) => [id, gateFires(id, { ...hostNamespaces(ctx), ...facts })]));

  return {
    names: ["step", "call", "gate"],
    read: async (c: ConditionContext) => {
      const call = { task: PLANNED_CALL, targeted: ctx.callTargeted === true, needs_target: needsExplicitEditor(PLANNED_CALL, graph) };
      const step = c.step;
      if (!step || step.type !== "task") {
        const facts = { call, step: step ? { name: step.name, type: step.type } : {} };
        return { ...facts, gate: gates(facts) };
      }
      const name = step.name;
      const dot = name.indexOf(".");
      const spec = dot > 0 ? graph.find((t) => t.name === name.slice(0, dot))?.actions[name.slice(dot + 1)] : undefined;
      const availability = stepAvailability(name, graph);
      // A bridge step is gated at the bridge by its method, any other by its task name.
      const refuses = await dialog();
      const blocked = refuses
        ? await (spec?.kind === "bridge" ? refuses(spec.bridge, "bridge") : refuses(name, "action"))
        : false;
      // Runtime params reach every step under the default flat scope.
      const options = { ...(step.options ?? {}), ...(c.params ?? {}) };
      const python = name === "editor.execute_python" && (await pythonGateRefusal(ctx, options)) !== null;
      const facts = {
        call,
        step: {
          name, type: step.type, effect: taskEffect(name, graph).effect, availability, bridge: spec?.bridge,
          dialog_blocked: blocked, python_refused: python,
        },
      };
      return { ...facts, gate: gates(facts) };
    },
  };
}

/** The addressed editor's graph, or none when this context carries no surface. */
export function gateGraph(ctx: ToolContext): ToolDef[] {
  try {
    return ctx.getToolGraph?.() ?? [];
  } catch {
    return [];
  }
}

/** The declared gates as checks: the flow's on the flow, the rest on every step. */
export function gateChecks(ctx: ToolContext, graph: readonly ToolDef[]): { flow: StepCheck[]; step: StepCheck[] } {
  const untargeted = ctx.sessions ? refuseUntargetedInRegistry(ctx.sessions, PLANNED_CALL, false, graph) : null;
  const check = (id: keyof typeof GATES): StepCheck => ({
    when: `gate.${id}`,
    action: "error",
    message: GATES[id].message ?? untargeted ?? "An untargeted run would be refused.",
  });
  return {
    flow: GATE_IDS.filter((id) => GATES[id].on === "flow").map(check),
    step: GATE_IDS.filter((id) => GATES[id].on === "step").map(check),
  };
}

type StepMap = Record<string, Record<string, unknown> | undefined>;

function withChecks(steps: unknown, checks: StepCheck[]): unknown {
  if (!steps || typeof steps !== "object") return steps;
  return Object.fromEntries(Object.entries(steps as StepMap).map(([n, step]) => {
    if (!step || typeof step !== "object") return [n, step];
    const existing = Array.isArray(step.checks) ? (step.checks as StepCheck[]) : [];
    return [n, { ...step, checks: [...existing, ...checks] }];
  }));
}

/** A copy of `flows` with the gate checks declared on every flow, step and hook step. */
export function withGateChecks(
  flows: Record<string, unknown>,
  checks: { flow: StepCheck[]; step: StepCheck[] },
): Record<string, FlowDefinition> {
  const out: Record<string, FlowDefinition> = {};
  for (const [name, def] of Object.entries(flows)) {
    const flow = { ...(def as Record<string, unknown>) };
    flow.checks = [...((flow.checks as StepCheck[] | undefined) ?? []), ...checks.flow];
    for (const key of ["steps", "on_start", "on_success", "on_failure", "finally"]) {
      if (flow[key] !== undefined) flow[key] = withChecks(flow[key], checks.step);
    }
    out[name] = flow as unknown as FlowDefinition;
  }
  return out;
}

export interface PlanRefusal {
  /** The step's path, or absent for a check on the whole flow. */
  path?: string;
  name: string;
  message: string;
}

export interface PlanPreflight {
  /** False when a run would be refused at some step, or as a whole. */
  ok: boolean;
  /** Every step or flow a run would refuse, and why. */
  refused: PlanRefusal[];
  /** Fired `warn` checks and deprecations. */
  warnings?: Array<{ message: string }>;
  steps: Array<{ path: string; name: string; type: "task" | "flow"; status: string; checks?: Array<{ action: string; message: string }> }>;
}

function fired(outcomes: CheckOutcome[]): Array<{ action: string; message: string }> {
  return outcomes
    .filter((o) => o.triggered || o.error)
    .map((o) => ({ action: o.action, message: o.error ? `${o.message} (${o.error.message})` : o.message }));
}

/** What a run of `flowName` would be refused for, evaluated without running anything. */
export async function planPreflight(
  registry: TaskRegistry,
  config: { tasks: Record<string, unknown>; flows: Record<string, unknown> },
  ctx: ToolContext,
  graph: readonly ToolDef[],
  flowName: string,
  params?: Record<string, unknown>,
  skip?: string[],
): Promise<PlanPreflight> {
  const runner = new FlowRunner({
    tasks: config.tasks as Record<string, TaskDefinition>,
    flows: withGateChecks(config.flows, gateChecks(ctx, graph)),
    registry,
    context: { ...ctx } as never,
    references: hostNamespaces(ctx),
    conditionEvaluator: makeConditionEvaluator(hostNamespaces(ctx), gateScope(ctx, graph, true)),
  });
  const result = await runner.preflight(flowName, params, { skip });
  const refused: PlanRefusal[] = [];
  for (const o of result.checks) if (o.triggered && o.action === "error") refused.push({ name: flowName, message: o.message });
  const steps = result.steps.map((row) => {
    const checks = fired(row.checks);
    for (const o of row.checks) {
      if (o.triggered && o.action === "error") refused.push({ path: row.path, name: row.name, message: o.message });
    }
    return { path: row.path, name: row.name, type: row.type, status: row.status, ...(checks.length ? { checks } : {}) };
  });
  return {
    ok: result.ok,
    refused,
    ...(result.warnings?.length ? { warnings: result.warnings.map((w) => ({ message: w.message })) } : {}),
    steps,
  };
}
