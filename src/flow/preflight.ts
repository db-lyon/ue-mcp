/**
 * The server's gates as declared flow checks, for `flow(plan)`.
 *
 * Four gates can refuse a call: the untargeted-write gate (a change with more
 * than one editor and none named), the editor-connected gate (a bridge action
 * with no editor), the dialog gate (a modal on screen) and the Python gate
 * (execute_python before its candidates are ruled out). Each is written here as
 * a `checks:` entry over the `gate` namespace, so flowkit's preflight evaluates
 * them for every step and a plan reports what a run would be refused for.
 *
 * Enforcement stays where it is: dispatch and runAction refuse live calls and
 * flow steps exactly as before. These checks are added to the plan's copy of
 * the config only, so a run behaves as it always did.
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
import { refuseUntargetedCall } from "../dispatch/editor-gate.js";
import { EDITOR_TARGET_PARAM } from "../surface/routing-params.js";
import { hostNamespaces, makeConditionEvaluator, type StepScope } from "./condition.js";
import { stepAvailability } from "./flow-describe.js";

/**
 * `step.*` and `gate.*`, read for the step being gated. A flow-level check
 * sees no step, and only `gate.untargeted` applies there.
 */
export function gateScope(ctx: ToolContext, graph: readonly ToolDef[]): StepScope {
  let dialogProbe: Promise<((subject: string, kind: "bridge" | "action") => Promise<boolean>) | undefined> | undefined;
  const dialog = () => (dialogProbe ??= ctx.session
    ? ensureGuard(ctx.session).then((g) => (s: string, k: "bridge" | "action") => g.wouldRefuse(s, k), () => undefined)
    : Promise.resolve(undefined));

  return {
    names: ["step", "gate"],
    read: async (c: ConditionContext) => {
      const untargeted = (ctx.sessions?.size ?? 1) > 1 && ctx.callTargeted !== true;
      const step = c.step;
      if (!step || step.type !== "task") {
        return { step: step ? { name: step.name, type: step.type } : {}, gate: { untargeted, editor: false, dialog: false, python: false } };
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
      return {
        step: { name, type: step.type, effect: taskEffect(name, graph).effect, availability, bridge: spec?.bridge },
        gate: { untargeted, editor: availability === "editor" && !ctx.bridge.isConnected, dialog: blocked, python },
      };
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

/** The gates as checks: one on the flow, the rest on every step. */
export function gateChecks(ctx: ToolContext, graph: readonly ToolDef[]): { flow: StepCheck[]; step: StepCheck[] } {
  const sessions = ctx.sessions;
  const untargeted = sessions && sessions.size > 1
    ? refuseUntargetedCall({
        taskName: "flow.run",
        editors: sessions.list().map((s) => s.name),
        activeEditor: sessions.active.name,
        targetParam: EDITOR_TARGET_PARAM,
        graph,
      })
    : null;
  return {
    flow: [{ when: "gate.untargeted", action: "error", message: untargeted ?? "An untargeted run would be refused." }],
    step: [
      { when: "gate.editor", action: "error", message: "No editor is connected, and this step dispatches to one." },
      { when: "gate.dialog", action: "error", message: "A modal dialog is blocking the editor, and this step would be refused until it is answered." },
      {
        when: "gate.python",
        action: "error",
        message: "execute_python's gate would refuse this step: give it a taskSummary and rule out, in ruledOut, every "
          + "candidate action a search for it returns. editor(execute_python) with the same taskSummary lists them.",
      },
    ],
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
    conditionEvaluator: makeConditionEvaluator(hostNamespaces(ctx), gateScope(ctx, graph)),
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
