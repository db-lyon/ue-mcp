/**
 * What happens to a call's parameters on the way in, and to its result on the
 * way out, regardless of which route dispatched it.
 *
 * There is more than one route. An MCP tool call goes through `index.ts` into
 * the flow registry, which runs the per-action task classes in
 * `flow/task-factory.ts`; a flow step goes through the same task classes from
 * the runner; and `categoryTool.handler` dispatches directly for tests and for
 * the embedders that still call it. Those routes reach the same handlers by
 * different paths, so a behaviour implemented in one of them is absent from
 * the others.
 *
 * That is not hypothetical, and it is not a single incident. Three separate
 * pieces of per-call behaviour were written inside `categoryTool.handler`
 * alone, every unit test passed because unit tests call that handler, and none
 * of the three ran on a live MCP call:
 *
 *   1. The path repair and the field projection. Caught by a live test.
 *   2. `CategoryOptions.normalizeParams`, so a whole category's advertised
 *      parameter spellings were accepted by the schema and then dropped.
 *   3. `timeoutMs`, which neither reached `bridge.call` nor left the
 *      parameters, so the advertised budget did nothing and the key travelled
 *      to the editor as a stray argument.
 *
 * The answer is that there is exactly ONE per-call preparation, and it lives
 * here. `prepareCall` is the whole inbound half: the routing parameters come
 * off, the paths are repaired, and the category's own folding runs. A route
 * calls it, it does not reimplement any part of it. Nothing new belongs in a
 * dispatcher; it belongs in `prepareCall`, where every route gets it.
 */
import { normalizePathParams, attachPathRepairs, type PathRepair } from "./path-params.js";
import { MAX_BRIDGE_TIMEOUT_MS } from "./bridge-timeouts.js";
import {
  takeFieldSelection,
  projectResult,
  attachFieldReport,
  type FieldSelection,
} from "./field-select.js";

/**
 * Separate the per-call timeout budget from the action's own parameters.
 * A non-positive or non-numeric value is discarded rather than refused: the
 * schema already rejects it, and a direct caller gets the default.
 *
 * Lives here rather than in `types.ts` so `prepareCall` can own the whole
 * inbound half without importing back from the module that imports it.
 * `types.ts` re-exports it, so every existing importer is unaffected.
 */
export function takeTimeout(
  params: Record<string, unknown>,
): { timeoutMs?: number; rest: Record<string, unknown> } {
  const { timeoutMs, ...rest } = params;
  const usable = typeof timeoutMs === "number" && Number.isFinite(timeoutMs) && timeoutMs > 0
    ? Math.min(timeoutMs, MAX_BRIDGE_TIMEOUT_MS)
    : undefined;
  return { timeoutMs: usable, rest };
}

/**
 * What a route knows about the call that the preparation cannot work out for
 * itself: which action was named, the category-wide parameter folding, and
 * whether the real parameters are nested one level down.
 */
export interface CallPreparation {
  /**
   * The action being invoked.
   *
   * A normalizer may branch on it (`widget(create)` splits its `assetPath`
   * back into `name` + `packagePath`, no other action does). On the live route
   * the action name is consumed by dispatch before the task ever sees the
   * parameters, so it has to be handed over separately.
   */
  action?: string;
  /** The category's `normalizeParams`, folding accepted spellings into the canonical ones. */
  normalizeParams?: (params: Record<string, unknown>) => Record<string, unknown>;
  /**
   * The real parameters live under this key rather than at the top level.
   *
   * Set to `args` by the micro-context gateway, whose every call arrives as
   * `tools(call, category, method, args)`. Without it the preparation runs
   * over `{category, method, args}`, where no key is a path and no key is a
   * routing parameter, so a backslashed path inside `args` reached the editor
   * unrepaired and `args.select` reached it as a method argument.
   * `routeEditorCall` already reads both levels for the same reason.
   */
  nestedParamsKey?: string;
}

/** What the inbound half decided, carried to the outbound half. */
export interface CallPipeline {
  /** The parameters a handler or bridge method should actually receive. */
  params: Record<string, unknown>;
  repairs: PathRepair[];
  selection: FieldSelection;
  /**
   * The budget the CALLER asked for, in milliseconds, or undefined when it
   * said nothing. The action's own authored budget is not folded in here: a
   * route resolves `pipeline.timeoutMs ?? spec.timeoutMs` so the caller's
   * value wins and the authored one is the floor it falls back to.
   */
  timeoutMs?: number;
}

function isParamBag(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

/**
 * Run the category's parameter folding over one bag.
 *
 * The normalizer reads `action` out of the bag, because that is the shape a
 * category tool's own handler hands it. Dispatch on the live route strips
 * `action` before the parameters reach a task, so it is put back for the
 * duration of the call and taken off again afterwards. Injecting and removing
 * rather than leaving it in place is deliberate: a bridge action without a
 * `mapParams` forwards its whole bag, and an `action` key arriving at a C++
 * handler is exactly the stray-parameter defect this module exists to stop.
 */
export function applyCategoryFolding(
  params: Record<string, unknown>,
  prep: CallPreparation,
): Record<string, unknown> {
  const fold = prep.normalizeParams;
  if (!fold) return params;

  const carriesAction = "action" in params;
  if (carriesAction || prep.action === undefined) return fold(params);

  const folded = fold({ ...params, action: prep.action });
  if (!("action" in folded)) return folded;
  const { action: _injected, ...rest } = folded;
  return rest;
}

/**
 * Fold every routing parameter out of a call, repair its paths, and apply the
 * category's own parameter folding.
 *
 * `timeoutMs`, `select` and `omit` are consumed here, so none of them can ever
 * reach a bridge method as an argument, and the backslash repair happens
 * before anything else reads a path - including the category's normalizer,
 * which resolves asset paths of its own.
 */
export function prepareCall(
  rawParams: Record<string, unknown>,
  prep: CallPreparation = {},
): CallPipeline {
  const outerTimeout = takeTimeout(rawParams);
  const outerSelection = takeFieldSelection(outerTimeout.rest);

  const key = prep.nestedParamsKey;
  const nested = key === undefined ? undefined : outerSelection.rest[key];

  if (key !== undefined && isParamBag(nested)) {
    // A gateway call: `category` and `method` name the target, and everything
    // the target actually takes is one level down. The routing parameters are
    // accepted at either level, the outer one winning, because a caller
    // writing `timeoutMs` beside `args` means the same thing as writing it
    // inside them.
    const innerTimeout = takeTimeout(nested);
    const innerSelection = takeFieldSelection(innerTimeout.rest);
    const repaired = normalizePathParams(innerSelection.rest);
    return {
      params: {
        ...applyCategoryFolding(outerSelection.rest, prep),
        [key]: repaired.params,
      },
      repairs: repaired.repairs,
      selection: {
        select: outerSelection.selection.select ?? innerSelection.selection.select,
        omit: outerSelection.selection.omit ?? innerSelection.selection.omit,
      },
      timeoutMs: outerTimeout.timeoutMs ?? innerTimeout.timeoutMs,
    };
  }

  const repaired = normalizePathParams(outerSelection.rest);
  return {
    params: applyCategoryFolding(repaired.params, prep),
    repairs: repaired.repairs,
    selection: outerSelection.selection,
    timeoutMs: outerTimeout.timeoutMs,
  };
}

/**
 * Apply the caller's projection, then attach the reports.
 *
 * Order matters: the reports are attached AFTER the projection, so a narrow
 * `select` cannot filter away the account of what the server repaired or of a
 * path that matched nothing.
 */
export function finishCall(raw: unknown, pipeline: CallPipeline): unknown {
  const projection = projectResult(raw, pipeline.selection);
  return attachPathRepairs(attachFieldReport(projection.result, projection), pipeline.repairs);
}

/**
 * True when this call asked for nothing the pipeline does.
 *
 * The task classes return a `TaskResult` whose `data` must stay the same
 * object when nothing was requested, so an untouched call is byte-identical to
 * what it was before any of this existed.
 */
export function isPipelineNoop(pipeline: CallPipeline): boolean {
  return (
    pipeline.repairs.length === 0
    && pipeline.selection.select === undefined
    && pipeline.selection.omit === undefined
  );
}
