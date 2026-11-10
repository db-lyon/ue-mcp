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
import { ENV_VARS, readEnv } from "../core/env.js";
import { normalizePathParams, attachPathRepairs, type PathRepair } from "../surface/path-params.js";
import { MAX_BRIDGE_TIMEOUT_MS } from "../bridge/bridge-timeouts.js";
import {
  takeFieldSelection,
  projectResult,
  attachFieldReport,
  type FieldSelection,
} from "../surface/context/field-select.js";
import { mapTracked } from "../surface/param-forwarding.js";
import { isDirectiveResponse } from "../core/directive.js";
import { McpError, ErrorCode } from "../core/errors.js";
import { contractViolation, liveContract, type ParamContract } from "../surface/handler-spec.js";
import { ROUTING_PARAMS, undeclaredMessage, type ActionAccepts } from "../surface/action-schema.js";
import type { EpicToolRef } from "../surface/epic-input.js";

/**
 * Separate the per-call timeout budget from the action's own parameters.
 * A non-positive or non-numeric value is discarded rather than refused: the
 * schema already rejects it, and a direct caller gets the default.
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
  /** Keys that are one parameter under different names; see `CategoryOptions.paramGroups`. */
  paramGroups?: readonly (readonly string[])[];
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
  /**
   * The spec'd action's contract (#1057, #1282). The category's zod shape is one
   * flat bag shared by every action, so it cannot say "actorLabel OR actorPath"
   * or hold a key to this action's enum; the call is checked here instead,
   * after folding, and refused before anything is sent.
   */
  paramContract?: ParamContract;
  /** The keys the action reads; any other key is refused before sending (#1336). */
  accepts?: ActionAccepts;
  /**
   * The plugin bridge method whose live registered spec replaces
   * paramContract when the running module's differs from the recording (#1290).
   */
  liveSpecMethod?: string;
  /** Appended to a contract refusal, e.g. why the live spec could not be adopted. */
  contractNote?: string;
  /** The engine tool a generated `epic_*` action wraps, whose engines gate the call. */
  epicTool?: EpicToolRef;
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
  /** Keys the caller sent, before folding. Empty on a gateway call. */
  supplied: string[];
  /** Supplied keys the action's mapParams did not send. Set by `forwardToBridge`. */
  unforwarded?: string[];
  /** Keys that are one parameter under different names, from the category. */
  paramGroups?: readonly (readonly string[])[];
  /** Keys actually sent to the bridge. Set by `forwardToBridge`. */
  sentKeys?: string[];
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
      supplied: [],
    };
  }

  const repaired = normalizePathParams(outerSelection.rest);
  const folded = applyCategoryFolding(repaired.params, prep);
  if (prep.paramContract) {
    const violation = contractViolation(prep.paramContract, folded);
    if (violation) {
      throw new McpError(
        ErrorCode.INVALID_PARAMS,
        `${prep.action ?? "This action"} ${violation}, so the call was not sent. `
        + `project(action="describe_action") lists what it takes.`
        + (prep.contractNote ? ` ${prep.contractNote}` : ""),
      );
    }
  }
  refuseUndeclared(prep, folded);
  return {
    params: folded,
    repairs: repaired.repairs,
    selection: outerSelection.selection,
    timeoutMs: outerTimeout.timeoutMs,
    supplied: Object.keys(outerSelection.rest),
    paramGroups: prep.paramGroups,
  };
}

/**
 * Refuse a key the action does not read, naming what it does take (#1336).
 * Routing names are the dispatcher's and always pass.
 */
export function refuseUndeclared(prep: CallPreparation, params: Record<string, unknown>): void {
  const accepts = prep.accepts;
  if (!accepts) return;
  // A category spelling of an accepted parameter (a paramGroups member) is the same parameter.
  const grouped = (k: string): boolean =>
    (prep.paramGroups ?? []).some((group) => group.includes(k) && group.some((m) => accepts.keys.has(m)));
  const unknown = Object.keys(params).filter((k) =>
    params[k] !== undefined && !accepts.keys.has(k) && !ROUTING_PARAMS.has(k) && !grouped(k));
  if (unknown.length === 0) return;
  throw new McpError(ErrorCode.INVALID_PARAMS, undeclaredMessage(prep.action ?? "This action", unknown, accepts));
}

/**
 * A plugin call's preparation held to the contract its running module
 * registered, when that differs from the recording (#1290). A live spec that
 * is malformed leaves the recording in force and says so on a refusal.
 */
export function withLiveContract(
  prep: CallPreparation | undefined,
  livePluginSpecs: Record<string, unknown> | undefined,
): CallPreparation | undefined {
  if (!prep?.liveSpecMethod || !prep.paramContract) return prep;
  const live = liveContract(prep.paramContract, prep.liveSpecMethod, livePluginSpecs);
  if (live.adopted) return { ...prep, paramContract: live.contract };
  if (!live.problem) return prep;
  return {
    ...prep,
    contractNote: `The running module registered a different contract for ${prep.liveSpecMethod}, which could not be used (${live.problem}); this check uses the recording. Reconnect the MCP client after rebuilding to pick up a corrected spec.`,
  };
}

/** Set to 1 to refuse a call instead of warning when a parameter is not forwarded (#1057). */
export const STRICT_PARAMS_ENV = ENV_VARS.strictParams;

/**
 * Map a bridge action's parameters and record what the mapper ignored (#1057).
 *
 * The category shape accepts every parameter any of its actions declares, so a
 * key meant for another action, or one this mapper forgot, used to vanish here
 * while the call reported success. It is now reported as `paramsNotForwarded`
 * on the result, or refused under the strict flag.
 */
export function forwardToBridge(
  pipeline: CallPipeline,
  bag: Record<string, unknown>,
  mapParams: ((p: Record<string, unknown>) => Record<string, unknown>) | undefined,
  label: string,
): Record<string, unknown> {
  if (!mapParams) {
    pipeline.sentKeys = Object.keys(bag);
    return bag;
  }
  const { params, unforwarded } = mapTracked(mapParams, bag, pipeline.supplied);
  pipeline.sentKeys = Object.keys(params);
  if (unforwarded.length === 0) return params;
  if (readEnv("strictParams") === "1") {
    throw new McpError(
      ErrorCode.INVALID_PARAMS,
      `${label} does not take ${unforwarded.join(", ")}, so the call was not sent. `
      + `project(action="describe_action") lists what it takes.`,
    );
  }
  pipeline.unforwarded = unforwarded;
  return params;
}

/** Report the parameters an action dropped, beside the result. */
export function attachUnforwarded<T>(result: T, unforwarded: string[] | undefined): T {
  if (!unforwarded || unforwarded.length === 0) return result;
  if (result === null || typeof result !== "object" || Array.isArray(result)) return result;
  const record = result as Record<string, unknown>;
  if ("paramsNotForwarded" in record) return result;
  if (isDirectiveResponse(record)) {
    return { ...record, result: attachUnforwarded(record.result, unforwarded) } as T;
  }
  return {
    ...record,
    paramsNotForwarded: {
      params: unforwarded,
      note: "This action does not send these parameters to the editor, so they had no effect. "
        + "project(action=\"describe_action\") lists what it takes.",
    },
  } as T;
}

/**
 * Reshape the editor's `paramsNotRead` into a report beside the result (#1057).
 *
 * A C++ handler of a reporting category lists the keys that arrived and were
 * never read. The list is taken from the unprojected answer and written after
 * the projection, so a narrow `select` cannot filter it away. A key whose
 * group (the same parameter under another name) had a member read is dropped:
 * the server mirrored it, the caller did not misspell it.
 */
export function attachNotRead<T>(result: T, answered: unknown, pipeline?: Pick<CallPipeline, "paramGroups" | "sentKeys" | "params">): T {
  if (answered === null || typeof answered !== "object" || Array.isArray(answered)) return result;
  const listed = (answered as Record<string, unknown>).paramsNotRead;
  if (!Array.isArray(listed)) return result;
  const unread = listed.filter((n): n is string => typeof n === "string");
  const unreadSet = new Set(unread);
  const sent = new Set(pipeline?.sentKeys ?? Object.keys(pipeline?.params ?? {}));
  const groupRead = (key: string): boolean =>
    (pipeline?.paramGroups ?? []).some((group) =>
      group.includes(key) && group.some((member) => sent.has(member) && !unreadSet.has(member)));
  const names = unread.filter((key) => !groupRead(key));
  if (names.length === 0) return result;
  if (result === null || typeof result !== "object" || Array.isArray(result)) return result;
  const record = result as Record<string, unknown>;
  if (isDirectiveResponse(record)) {
    return { ...record, result: attachNotRead(record.result, answered, pipeline) } as T;
  }
  return {
    ...record,
    paramsNotRead: {
      params: names,
      note: "The editor received these parameters and this action never read them, so they had no effect. "
        + "project(action=\"describe_action\") lists what it takes.",
    },
  } as T;
}

/**
 * Apply the caller's projection, then attach the reports.
 *
 * Order matters: the reports are attached AFTER the projection, so a narrow
 * `select` cannot filter away the account of what the server repaired or of a
 * path that matched nothing.
 */
export function finishCall(raw: unknown, pipeline: CallPipeline): unknown {
  // A directive response is an instruction wrapped around the result. The
  // projection narrows the result it wraps, never the envelope: a
  // `select: ["path"]` on execute_python must not filter away the directive
  // the agent is required to see, and the reports belong beside the result.
  if (isDirectiveResponse(raw)) return { ...raw, result: finishCall(raw.result, pipeline) };
  const projection = projectResult(raw, pipeline.selection);
  return attachNotRead(
    attachUnforwarded(
      attachPathRepairs(attachFieldReport(projection.result, projection), pipeline.repairs),
      pipeline.unforwarded,
    ),
    raw,
    pipeline,
  );
}

