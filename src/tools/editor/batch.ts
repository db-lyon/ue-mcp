/**
 * editor(batch): several actions, one game-thread job.
 *
 * Two agents driving one editor interleave at the call boundary. The bridge
 * runs handlers one at a time on the game thread, so no single call is ever
 * torn, but a multi-step edit is several calls: add a variable, add a node,
 * connect the pins. Another session's call lands between any two of them, and
 * a rename there means the third step is aimed at an asset that no longer
 * exists under that name. Per-asset locking narrows this to the calls that
 * name the asset; it cannot help a step that names nothing, or a step whose
 * target another session renamed between the lock being taken and the step
 * running, and it is opt-in.
 *
 * A batch closes the gap by never opening it: the whole list travels to the
 * editor as ONE bridge call, `execute_batch`, and the plugin runs every step
 * inside one game-thread job. Nothing from any other connection can run
 * between two steps, because nothing else runs on the game thread until the
 * job returns. The steps also share one editor transaction, so the user gets a
 * single Undo entry for whatever the batch changed that was transactional.
 *
 * What a batch is NOT: a rollback. A step that fails stops the batch (or not,
 * under onError: continue) and the steps before it stay applied. The editor
 * has no reliable inverse for every action, and pretending otherwise would be
 * worse than saying so. The result names exactly what ran.
 *
 * Every step goes through the same per-call preparation a direct call gets
 * (`prepareCall`, the category's folding, the action's mapParams, the
 * declared contract), here, before anything is sent. The batch itself goes
 * through dispatch as one action: the dialog guard checks it once, the asset
 * locks taken for it are the union of its steps' (see `classifyAction`), and
 * the write guards see every step's method and paths through
 * `bridgeMethodEffect` and `classifyWrite`.
 */
import { z } from "zod";
import { McpError as SdkMcpError } from "@modelcontextprotocol/sdk/types.js";
import type { ActionSpec, BridgeActionSpec, HandlerActionSpec, ToolContext } from "../../core/types.js";
import { McpError, ErrorCode } from "../../core/errors.js";
import { MAX_BRIDGE_TIMEOUT_MS, resolveBridgeTimeout } from "../../bridge/bridge-timeouts.js";
import { PLUGIN_UPGRADE_POINTER } from "../../bridge/bridge.js";
import { prepareCall, finishCall, forwardToBridge, withLiveContract, type CallPipeline } from "../../dispatch/call-pipeline.js";
import { NEVER_LOCKED } from "../../dispatch/lock-actions.js";
import { BATCH_TASK, MAX_BATCH_CALLS, type BatchCall } from "../../dispatch/batch-steps.js";
import { DialogGuard, isModalSafeMethod } from "../../editor/dialog-guard.js";
import { actionPreparation } from "../../flow/run-action.js";
import { unknownActionMessage } from "../../surface/action-schema.js";
import { validateCategoryParams } from "../../surface/context/call-envelope.js";
import { paramMapperOf } from "../../surface/epic-input.js";
import { isWithheld, registeredMethodsOf, unregisteredActionMessage } from "../../surface/registered-surface.js";
import { EDITOR_TARGET_PARAM, stripAction } from "../../surface/routing-params.js";
import { stripEditorTarget, toolGraphOf } from "../../surface/target-params.js";

export type { BatchCall };

export type BatchOnError = "stop" | "continue";

/**
 * Actions that act on the undo stack the batch itself is recording into. A
 * batch is one transaction; a step that opened, closed or cancelled another
 * inside it would split or abort that transaction, and an undo in the middle
 * of a batch would undo the batch's own earlier steps.
 */
const UNDO_STACK_ACTIONS = new Set([
  "editor.undo",
  "editor.redo",
  "editor.undo_redo_steps",
  "editor.begin_transaction",
  "editor.end_transaction",
  "editor.cancel_transaction",
]);

/** Whether a step's `editor` names the session the batch itself runs on. */
function targetsSession(ctx: ToolContext, target: unknown): boolean {
  if (!ctx.sessions || !ctx.session) return false;
  try {
    return ctx.sessions.resolve(target) === ctx.session;
  } catch {
    return false;
  }
}

/** A step prepared for the wire: what the editor runs, and what shapes its answer. */
interface PreparedStep {
  /** `category.method`, for the result and the error text. */
  task: string;
  method: string;
  params: Record<string, unknown>;
  pipeline: CallPipeline;
  /** What the editor is allowed for this step, in ms. */
  budgetMs: number;
}

function refuse(index: number, task: string, why: string): McpError {
  return new McpError(ErrorCode.INVALID_PARAMS, `ops[${index}] (${task}) ${why}, so the batch was not sent.`);
}

/** One step's admission: the bridge action it resolved to, or why it cannot be a step. */
export type StepAdmission =
  | { admitted: true; spec: BridgeActionSpec }
  | { admitted: false; why: string };

/**
 * Whether this action can be one step of a batch. Every reason is a property
 * of the action, read off its name or its spec, so the list is the same for
 * every caller and stated once.
 */
export function admitBatchStep(task: string, spec: ActionSpec): StepAdmission {
  const refused = (why: string): StepAdmission => ({ admitted: false, why });
  if (task === BATCH_TASK) return refused("is itself a batch; a batch cannot nest");
  if (NEVER_LOCKED.has(task)) {
    return refused("manages the asset locks the batch is run under; take or release locks with their own calls");
  }
  if (UNDO_STACK_ACTIONS.has(task)) {
    return refused("acts on the undo stack, and a batch is itself one undo transaction; call it on its own");
  }
  if (spec.kind !== "bridge") {
    return refused("runs in the server process rather than in the editor, so the editor cannot run it inside one game-thread job; call it on its own");
  }
  if (DialogGuard.actionAllowed(task) || isModalSafeMethod(spec.bridge)) {
    return refused("reads or answers a dialog and is served while the editor is blocked, so it runs alone");
  }
  if (spec.followUp) {
    return refused("needs work the server does after the editor answers, which a batch cannot interleave; call it on its own");
  }
  return { admitted: true, spec };
}

/** The client-side wait for one step, were it sent alone. */
function stepBudgetMs(spec: BridgeActionSpec, pipeline: CallPipeline): number {
  return pipeline.timeoutMs ?? spec.timeoutMs ?? resolveBridgeTimeout(spec.bridge);
}

/**
 * Check every step and prepare it for the wire, refusing the whole batch on
 * the first problem. Nothing is sent from here: a batch that cannot run
 * entirely is not run at all, which is the one guarantee a caller building an
 * edit out of several steps actually wants.
 */
export function prepareBatch(ctx: ToolContext, params: Record<string, unknown>): { steps: PreparedStep[]; onError: BatchOnError } {
  const rawCalls = params.ops;
  if (!Array.isArray(rawCalls) || rawCalls.length === 0) {
    throw new McpError(ErrorCode.INVALID_PARAMS, "'ops' must be a non-empty array of {category, method, args}.");
  }
  if (rawCalls.length > MAX_BATCH_CALLS) {
    throw new McpError(
      ErrorCode.INVALID_PARAMS,
      `'ops' holds ${rawCalls.length} steps and a batch carries at most ${MAX_BATCH_CALLS}. Split the edit into two batches.`,
    );
  }
  const onError = params.onError ?? "stop";
  if (onError !== "stop" && onError !== "continue") {
    throw new McpError(ErrorCode.INVALID_PARAMS, `onError must be "stop" or "continue", not ${JSON.stringify(onError)}.`);
  }

  const graph = toolGraphOf(ctx);
  const have = registeredMethodsOf(ctx.bridge);
  if (have && !have.has("execute_batch")) {
    throw new McpError(
      ErrorCode.NOT_FOUND,
      `${BATCH_TASK} dispatches to bridge method 'execute_batch', which the running plugin does not register `
      + `(${have.size} registered). ${PLUGIN_UPGRADE_POINTER}`,
    );
  }

  const steps: PreparedStep[] = [];
  for (let i = 0; i < rawCalls.length; i++) {
    const entry = rawCalls[i];
    if (entry === null || typeof entry !== "object" || Array.isArray(entry)) {
      throw new McpError(ErrorCode.INVALID_PARAMS, `ops[${i}] must be an object {category, method, args}, so the batch was not sent.`);
    }
    const call = entry as Record<string, unknown>;
    if (typeof call.category !== "string" || call.category === "" || typeof call.method !== "string" || call.method === "") {
      throw new McpError(ErrorCode.INVALID_PARAMS, `ops[${i}] needs a 'category' and a 'method', so the batch was not sent.`);
    }
    if (call.args !== undefined && (call.args === null || typeof call.args !== "object" || Array.isArray(call.args))) {
      throw new McpError(ErrorCode.INVALID_PARAMS, `ops[${i}].args must be an object, so the batch was not sent.`);
    }
    const task = `${call.category}.${call.method}`;
    const tool = graph.find((t) => t.name === call.category);
    if (!tool) {
      throw refuse(i, task, `names category '${call.category}', which this server does not have (${graph.map((t) => t.name).join(", ")})`);
    }
    const spec = tool.actions[call.method];
    if (!spec) {
      throw new McpError(
        ErrorCode.NOT_FOUND,
        `ops[${i}]: ${unknownActionMessage(call.method, tool.name, Object.keys(tool.actions))} The batch was not sent.`,
      );
    }
    const admission = admitBatchStep(task, spec);
    if (!admission.admitted) throw refuse(i, task, admission.why);
    const step = admission.spec;
    if (isWithheld(step, have)) {
      throw new McpError(
        ErrorCode.NOT_FOUND,
        `ops[${i}]: ${unregisteredActionMessage(task, step.bridge, have!.size)} The batch was not sent.`,
      );
    }

    const args = (call.args ?? {}) as Record<string, unknown>;
    // A batch runs on one editor. A step naming another would be stripped and
    // silently run here, so it is refused unless it names this one.
    const target = args[EDITOR_TARGET_PARAM];
    if (target !== undefined && target !== "" && !targetsSession(ctx, target)) {
      throw refuse(
        i,
        task,
        `targets editor '${String(target)}', but a batch runs on one editor${ctx.session ? ` ('${ctx.session.name}')` : ""}. `
        + "Put 'editor' on the batch call, or split the batch per editor",
      );
    }

    // The same validation a direct call gets, worded the same way, then the
    // same preparation: routing parameters off, paths repaired, the category's
    // folding, the action's contract, and its mapParams.
    const prep = withLiveContract(actionPreparation(tool.options, call.method, step), ctx.bridge.capabilities?.pluginHandlerSpecs);
    let pipeline: CallPipeline;
    let sent: Record<string, unknown>;
    try {
      const flat = validateCategoryParams(tool, { ...stripEditorTarget(args), action: call.method });
      pipeline = prepareCall(stripAction(flat), prep);
      sent = forwardToBridge(pipeline, pipeline.params, paramMapperOf(step), task);
    } catch (e) {
      if (e instanceof SdkMcpError || e instanceof McpError) {
        const code = e instanceof McpError ? e.code : ErrorCode.INVALID_PARAMS;
        throw new McpError(code, `ops[${i}] (${task}): ${e.message} The batch was not sent.`);
      }
      throw e;
    }
    steps.push({
      task,
      method: step.bridge,
      params: sent,
      pipeline,
      budgetMs: stepBudgetMs(step, pipeline),
    });
  }
  return { steps, onError };
}

/** The wait for the whole job: the caller's budget, or the sum of what each step would get alone. */
export function batchBudgetMs(steps: ReadonlyArray<{ budgetMs: number }>, callerMs?: number): number {
  if (callerMs !== undefined) return callerMs;
  return Math.min(steps.reduce((total, s) => total + s.budgetMs, 0), MAX_BRIDGE_TIMEOUT_MS);
}

/** The editor's account of a batch still running, read off its status snapshot. */
interface BatchProgress {
  completedCalls: number;
  requestedCalls: number;
  currentIndex?: number;
}

function progressOf(snapshot: unknown): BatchProgress | null {
  if (snapshot === null || typeof snapshot !== "object") return null;
  const batch = (snapshot as Record<string, unknown>).batch;
  if (batch === null || typeof batch !== "object") return null;
  const b = batch as Record<string, unknown>;
  if (typeof b.completedCalls !== "number" || typeof b.requestedCalls !== "number") return null;
  return {
    completedCalls: b.completedCalls,
    requestedCalls: b.requestedCalls,
    currentIndex: typeof b.currentIndex === "number" ? b.currentIndex : undefined,
  };
}

/** What a timed-out batch had done by the time the wait ended, as a sentence a caller can act on. */
function progressSentence(progress: BatchProgress | null, steps: PreparedStep[]): string {
  if (!progress) {
    return "The editor did not report how far the batch got; read the state back before deciding what to re-run.";
  }
  const running = progress.currentIndex !== undefined ? steps[progress.currentIndex] : undefined;
  return `The editor had finished ${progress.completedCalls} of ${progress.requestedCalls} calls`
    + (running ? ` and was still running ops[${progress.currentIndex}] (${running.task})` : "")
    + " when the wait ended. Those steps stay applied: nothing is rolled back, and the job keeps running"
    + " on the game thread until it is done. Read the state back before re-running any of them.";
}

/**
 * The editor's status snapshot, served on the socket thread, so it answers
 * while the game thread is still inside the batch. Null when even that fails.
 */
async function readProgress(ctx: ToolContext): Promise<BatchProgress | null> {
  try {
    return progressOf(await ctx.bridge.call("get_engine_state", {}));
  } catch {
    return null;
  }
}

/** One step's answer as the editor returned it. */
interface WireStepResult {
  index?: number;
  method?: string;
  success?: boolean;
  result?: unknown;
}

/**
 * Shape the editor's answer for the caller: each step's result goes through
 * its own outbound half (projection, path repairs, the unread-parameter
 * report), under the `category.method` the caller wrote rather than the bridge
 * method it became.
 */
function shapeResult(answered: Record<string, unknown>, steps: PreparedStep[], onError: BatchOnError): Record<string, unknown> {
  const wire = Array.isArray(answered.results) ? (answered.results as WireStepResult[]) : [];
  const results = wire.map((entry, position) => {
    const index = typeof entry?.index === "number" ? entry.index : position;
    const step = steps[index];
    return {
      index,
      action: step?.task ?? entry?.method ?? "?",
      success: entry?.success !== false,
      result: step ? finishCall(entry?.result, step.pipeline) : entry?.result,
    };
  });
  const { results: _wire, ...rest } = answered;
  const failedIndex = typeof answered.failedIndex === "number" ? answered.failedIndex : undefined;
  const failed = failedIndex !== undefined ? results.find((r) => r.index === failedIndex) : undefined;
  const shaped: Record<string, unknown> = { ...rest, onError, results };
  if (failed) {
    const failure = failed.result && typeof failed.result === "object"
      ? (failed.result as Record<string, unknown>).error
      : undefined;
    const notRun = answered.stoppedEarly === true && failedIndex! + 1 < steps.length
      ? ` ops[${failedIndex! + 1}..${steps.length - 1}] were not run.`
      : "";
    shaped.error = `ops[${failedIndex}] (${failed.action}) failed: ${typeof failure === "string" ? failure : "see results"}.${notRun}`
      + ` Nothing is rolled back: the steps that succeeded stay applied.`;
  }
  return shaped;
}

export const batchAction: HandlerActionSpec = {
  kind: "handler",
  effect: "mutate",
  description:
    "Run several actions as ONE game-thread job in the editor, so no call from any other session can land between them. "
    + "This is the fix for multi-step edits that other agents interleave with: add a variable, add a node and connect its pins "
    + "arrive as one unit, and a rename from another session cannot slip in between. calls is an ordered list of "
    + "{category, method, args}, each prepared exactly as a direct call to that action would be (paths repaired, the action's "
    + "own parameter contract checked) BEFORE anything is sent, and a batch with one bad step is refused whole. Every step must "
    + "be an action the editor runs: an action served by this server process (start_editor, execute_python, flows, another "
    + "batch), the asset lock actions, the undo and transaction actions, and the dialog actions are refused with the reason. "
    + "The steps share one editor transaction, so what they changed that is transactional is ONE Undo entry; "
    + "transactionDescription names it. The dialog guard checks the batch once, and when locking is enabled the locks taken are "
    + "the union of every step's asset paths, held for the whole batch. onError=stop (default) ends the batch at the first "
    + "failing step; continue runs every step and reports each verdict. NO ROLLBACK: the editor has no reliable inverse for "
    + "every action, so a step that fails leaves the steps before it applied, and the result says exactly which ran "
    + "(completedCalls, failedIndex, per-step success). Each step's answer comes back under results[i].result, including any "
    + "rollback descriptor that action reports on its own. The wait is the sum of what each step would get alone, or the "
    + "batch's timeoutMs; a batch that outlives the wait reports how many steps had finished, and keeps running. "
    + "Params: ops:[{category, method, args?}] (1-256), onError? (stop|continue), transactionDescription?",
  handler: async (ctx: ToolContext, params: Record<string, unknown>) => {
    const { steps, onError } = prepareBatch(ctx, params);
    const payload: Record<string, unknown> = {
      ops: steps.map((s) => ({ method: s.method, params: s.params })),
      onError,
    };
    if (typeof params.transactionDescription === "string" && params.transactionDescription.trim() !== "") {
      payload.transactionDescription = params.transactionDescription.trim();
    }

    let answered: unknown;
    try {
      answered = await ctx.bridge.call("execute_batch", payload, batchBudgetMs(steps, ctx.callTimeoutMs));
    } catch (e) {
      // The client stopped waiting; the game thread did not stop working. Say
      // how far it got, from the one source that answers while it is busy.
      if (e instanceof McpError && e.code === ErrorCode.BRIDGE_TIMEOUT) {
        const progress = await readProgress(ctx);
        throw new McpError(e.code, `${e.message} ${progressSentence(progress, steps)}`, {
          ...e.details,
          batch: { requestedCalls: steps.length, ...(progress ?? {}) },
        });
      }
      throw e;
    }
    if (answered === null || typeof answered !== "object") return answered;
    const body = answered as Record<string, unknown>;
    // The editor's own executor gave up on the job (its game-thread wait is the
    // same budget). The snapshot it attaches carries the batch's progress.
    if (body.success === false && body.reason === "timeout") {
      const progress = progressOf(body.engineState);
      return {
        ...body,
        error: `${String(body.error ?? "The batch timed out")}. ${progressSentence(progress, steps)}`,
        requestedCalls: steps.length,
        ...(progress ?? {}),
      };
    }
    return shapeResult(body, steps, onError);
  },
};

/** The batch's parameters, merged into the editor category's shape. */
export const batchSchema: Record<string, z.ZodType> = {
  ops: z.array(z.object({
    category: z.string().describe("The category tool the step calls (blueprint, asset, level, ...)"),
    method: z.string().describe("The action in that category"),
    args: z.record(z.string(), z.unknown()).optional().describe("That action's parameters, exactly as a direct call takes them"),
  })).optional().describe("batch: the steps, in order. Each runs in the editor as a direct call to that action would, with no other session's call between any two"),
  onError: z.enum(["stop", "continue"]).optional().describe("batch: stop (default) ends the batch at the first failing step; continue runs every step and reports each verdict. Neither rolls anything back"),
  transactionDescription: z.string().optional().describe("batch: the label of the one Undo entry the batch's transactional changes make (default 'MCP batch (N calls)')"),
};
