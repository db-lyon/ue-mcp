/**
 * Actions backed by a flow or a composite task (`kind: "flow"`).
 *
 * The children always run through flowkit's runner via `ctx.step`, so each is
 * locked, guarded, recorded on `TaskResult.children` and rolled back like a
 * flow step. Under a flow the runner is already there; a live call gets one
 * built for it here.
 */
import { FlowRunner } from "@db-lyon/flowkit";
import type {
  ChildStepRunner,
  FlowDefinition,
  FlowStepResult,
  ReferenceContext,
  TaskDefinition,
  TaskRegistry,
  TaskResult,
} from "@db-lyon/flowkit";
import type { ChildOutcome, ChildRun, FlowActionSpec, ToolDef } from "../core/types.js";
import type { FlowContext } from "./context.js";
import { finishCall, type CallPipeline } from "../dispatch/call-pipeline.js";
import { applyHandlerOutcome, inFlowRun } from "./handler-outcome.js";
import { builtinClassPath, LIVE_FLOWS_KEY, LIVE_REFERENCES_KEY } from "./task-call.js";
import { hostNamespaces, makeConditionEvaluator } from "./condition.js";
import { buildFlowRegistry } from "./registry.js";
import { builtinFlows } from "./loader.js";

/** Context key for the composite a live runner was built to run. */
const HELD_KEY = "__ueMcpHeldComposite";

const NESTED_STEPS = Symbol("ue-mcp.nestedSteps");

interface HeldComposite {
  name: string;
  taken: boolean;
  settle: (runCtx: FlowContext) => Promise<TaskResult>;
}

/**
 * `afterStep` hook: hand a flow child's step results back to the composite
 * that ran it. `ctx.step` returns only the child flow's summary, and a reducer
 * needs what each step answered. Non-enumerable, so no report serializes it.
 */
export function keepNestedSteps(record: FlowStepResult): void {
  if (record.type !== "flow" || !record.result || !record.nestedSteps) return;
  Object.defineProperty(record.result, NESTED_STEPS, {
    value: record.nestedSteps,
    enumerable: false,
    configurable: true,
  });
}

function outcomeOf(result: TaskResult): ChildOutcome {
  const nested = (result as unknown as Record<symbol, FlowStepResult[] | undefined>)[NESTED_STEPS];
  return {
    success: result.success,
    data: result.data,
    error: result.error,
    ...(nested
      ? {
          steps: nested.map((s) => ({
            name: s.name,
            success: s.result?.success ?? false,
            skipped: s.skipped,
            data: s.result?.data,
            error: s.result?.error,
          })),
        }
      : {}),
  };
}

function runnerChildRun(step: ChildStepRunner): ChildRun {
  return async (target, options = {}) => outcomeOf(await step(target, options));
}

/**
 * A registry for a context that carries none (a handler invoked directly, as
 * tests and embedders do): the context's graph, plus the action's own tool.
 */
function registryFor(ctx: FlowContext, home?: ToolDef): TaskRegistry {
  if (ctx.registry) return ctx.registry as TaskRegistry;
  let graph: ToolDef[] = [];
  try {
    graph = ctx.getToolGraph?.() ?? [];
  } catch {
    // A session with no surface still runs the action's own tool.
  }
  const tools = home && !graph.some((t) => t.name === home.name) ? [...graph, home] : graph;
  return buildFlowRegistry(tools);
}

function flowsOf(ctx: FlowContext): Record<string, FlowDefinition> {
  return (ctx[LIVE_FLOWS_KEY] as Record<string, FlowDefinition> | undefined) ?? builtinFlows() as Record<string, FlowDefinition>;
}

/** The runner a live call's composite runs under, with any flows the action declares for itself. */
function liveRunner(ctx: FlowContext, registry: TaskRegistry, held: HeldComposite, own?: Record<string, unknown>): FlowRunner {
  const live = ctx[LIVE_REFERENCES_KEY] as ReferenceContext | undefined;
  const namespaces = live?.namespaces ?? hostNamespaces(ctx);
  return new FlowRunner({
    tasks: (ctx.taskDefinitions ?? {}) as Record<string, TaskDefinition>,
    flows: own ? { ...flowsOf(ctx), ...(own as Record<string, FlowDefinition>) } : flowsOf(ctx),
    registry,
    context: { ...ctx, [HELD_KEY]: held },
    hooks: { afterStep: async (_step, record) => keepNestedSteps(record) },
    references: namespaces,
    conditionEvaluator: makeConditionEvaluator(namespaces),
  });
}

/** The held composite for `name`, taken once, by the task the live runner built for it. */
export function takeHeldComposite(ctx: FlowContext, name: string): HeldComposite | undefined {
  const held = ctx[HELD_KEY] as HeldComposite | undefined;
  if (!held || held.taken || held.name !== name) return undefined;
  held.taken = true;
  return held;
}

/**
 * Run the children, reduce them into the response, and read the verdict.
 * `live` keeps a live call's own verdict: a body saying `success: false` is
 * the answer, not a failed task, exactly as a handler's is.
 */
async function settle(
  spec: FlowActionSpec,
  runCtx: FlowContext,
  pipeline: CallPipeline,
  run: ChildRun,
  live: boolean,
): Promise<TaskResult> {
  const input = pipeline.params;
  const answered = spec.result(await spec.compose(run, input, runCtx), input);
  const data = finishCall(answered, pipeline);
  const result: TaskResult = {
    success: true,
    data: typeof data === "object" && data !== null ? (data as Record<string, unknown>) : { result: data },
  };
  return live ? result : applyHandlerOutcome(runCtx, answered, result);
}

/**
 * Run a flow-backed action's children through a runner: the one the context
 * came from, or one built for this call. `ctx` is the prepared, locked call
 * context; `outer` decides the verdict.
 */
export async function composeAction(
  outer: FlowContext,
  name: string,
  spec: FlowActionSpec,
  ctx: FlowContext,
  pipeline: CallPipeline,
  home?: ToolDef,
): Promise<TaskResult> {
  // An enclosing runner cannot resolve the action's own flows, so those run under one of their own.
  if (typeof ctx.step === "function" && !spec.flows) {
    return settle(spec, ctx, pipeline, runnerChildRun(ctx.step), false);
  }
  const live = !inFlowRun(outer);
  const held: HeldComposite = {
    name,
    taken: false,
    settle: (runCtx) => settle(spec, runCtx, pipeline, runnerChildRun(runCtx.step as ChildStepRunner), live),
  };
  // The input rides on the held composite, not in the options, so runTask
  // never interpolates a caller's `${...}` data.
  return liveRunner(ctx, registryFor(ctx, home), held, spec.flows).runTask(builtinClassPath(name), {});
}
