/**
 * The declared effect of an action, read off the tool graph.
 *
 * Every gate in this server asks one of two questions and they are the same
 * question in different clothes: "is this call about to change the editor?"
 * The routing gate asks it to decide whether an untargeted call may fall
 * through to whichever editor is active; asset locking asks it to decide
 * whether to take a lock; the guard pipeline asks it to decide whether a guard
 * scoped to `mutations` sees the call at all.
 *
 * All three used to answer it by matching a NAME against a list of verbs, each
 * with its own list. That is why a guard declared `scope: mutations` matched
 * 542 of 1090 actions and let `write_cpp_file`, `build`, `sculpt`,
 * `place_actor` and every bare verb like `save` and `create` through
 * unguarded. Inverting the guess (block unless the name starts with a known
 * read verb) closed the dangerous direction and left the question a guess.
 *
 * So they read this instead, and this reads what the action itself declares.
 * The lexicon in `action-class.ts` survives for exactly the names that have no
 * declaration to read, and every caller of it records the answer as inferred.
 *
 * The index is derived from `getLiveToolGraph()` rather than `ALL_TOOLS`,
 * because the surface a running server dispatches is not the pristine
 * declaration: Epic enrichment injects wrapped engine tools and plugins inject
 * their own actions, and both of those carry an effect too. It is rebuilt when
 * the published graph is replaced, which the server does once at startup and
 * again whenever a session is registered.
 */
import { getLiveToolGraph } from "./tools.js";
import { splitTaskName } from "./action-class.js";
import { flowCategoryForCheck } from "./flow/skill-actions.js";
import { EPIC_TOOL_EFFECTS } from "./tools/epic/effects.js";
import type { ActionEffect, ActionEffectSource, ToolDef } from "./types.js";

export interface ResolvedEffect {
  effect: ActionEffect;
  /**
   * `declared` means an ActionSpec in the graph said so. `undeclared` means
   * nothing in this server claims that name, and the answer is the default
   * below rather than a reading of the name.
   */
  source: ActionEffectSource | "undeclared";
}

/**
 * What a name nobody claims is treated as.
 *
 * An ActionSpec cannot fail to state its effect - the type requires it in
 * every variant - so this is never an action that forgot. It is a name no
 * action in this server carries: a flow step naming a task that does not
 * exist, a bridge method reached from somewhere this package cannot see, a
 * plugin calling through the bridge API with a method of its own.
 *
 * The answer is `mutate`, and not the verb lexicon's opinion of the name. A
 * list of verbs is what produced the failure this whole change removes, and
 * running it here would reintroduce it at exactly the point where the
 * information is thinnest. Something unrecognised is treated as a change, so
 * the gates refuse it, guard it and lock it. Being wrong costs one
 * unnecessary refusal; the other direction costs an unguarded write.
 */
export const UNDECLARED_EFFECT: ActionEffect = "mutate";

/**
 * Bridge methods a HANDLER calls directly, with what each does.
 *
 * The guard pipeline sits on `IBridge.call`, so it sees bridge method names.
 * Most of them belong to an action and carry that action's declared effect.
 * These do not: they are reached from inside a handler's own body, so no
 * ActionSpec forwards to them and the index below cannot see them.
 *
 * Enumerated rather than defaulted, because two of them are reads, and a read
 * defaulted to `mutate` would put `asset(search)` and `editor(get_engine_state)`
 * in front of every guard scoped to mutations for no reason.
 * `tests/unit/action-effects.test.ts` scans the source for raw `bridge.call`
 * sites and fails when one appears that is not listed here, so this cannot
 * quietly fall behind the code. Same shape as the hand-written halves of
 * `offline.ts`, and for the same reason.
 */
export const RAW_BRIDGE_METHODS: Readonly<Record<string, ActionEffect>> = {
  // The lock registry lives in the editor, so taking and dropping a lock
  // changes it. asset(lock/unlock/unlock_all) reach these.
  acquire_lock: "mutate",
  release_lock: "mutate",
  release_session_locks: "mutate",
  // Arbitrary python inside the editor process. Its effect is the string it
  // was handed, which is what `unknown` is for.
  execute_python: "unknown",
  // Probes the running editor and reports. editor(get_engine_state).
  get_engine_state: "read",
  // Copies assets into another project's Content directory. asset(migrate).
  migrate: "mutate",
  // Asks the editor to quit. editor(request_editor_shutdown).
  request_editor_shutdown: "mutate",
  // Resolves content roots and queries the asset registry. asset(search).
  search_assets: "read",
};

interface EffectIndex {
  byAction: Map<string, ActionEffect>;
  byBridgeMethod: Map<string, ActionEffect>;
}

let indexedGraph: ToolDef[] | null = null;
let index: EffectIndex | null = null;

function build(graph: ToolDef[]): EffectIndex {
  const byAction = new Map<string, ActionEffect>();
  const byBridgeMethod = new Map<string, ActionEffect>();
  for (const tool of graph) {
    for (const [action, spec] of Object.entries(tool.actions)) {
      byAction.set(`${tool.name}.${action}`, spec.effect);
      if (spec.kind !== "bridge") continue;
      const seen = byBridgeMethod.get(spec.bridge);
      // One bridge method reached by two actions that declare different
      // effects does not have an effect of its own. `epic_call_tool` is the
      // real case: every one of the wrapped engine tools dispatches through
      // it, and between them they read and write. `unknown` is the honest
      // answer for the METHOD, and it gates exactly like `mutate`.
      byBridgeMethod.set(
        spec.bridge,
        seen === undefined || seen === spec.effect ? spec.effect : "unknown",
      );
    }
  }
  return { byAction, byBridgeMethod };
}

function current(): EffectIndex {
  const graph = getLiveToolGraph();
  if (index === null || indexedGraph !== graph) {
    indexedGraph = graph;
    // The flow tool is built per server, from a task registry and a config
    // source, so it is registered outside `ALL_TOOLS` and is not in the
    // published graph either: the graph is published during startup and the
    // flow tool is constructed after it. Its seventeen actions still need
    // their effects read, or `flow(plan)` and `flow(journal_list)` would be
    // refused as untargeted changes for being unrecognised. This is the same
    // stand-in the skill-pack check holds packs against, and a unit test keeps
    // it matching the tool the server actually registers.
    index = build([...graph, flowCategoryForCheck()]);
  }
  return index;
}

/** Drop the memo. Only tests that swap the published graph in place need this. */
export function resetEffectIndex(): void {
  indexedGraph = null;
  index = null;
}

/** What `${tool}.${action}` declares, or undefined when the graph has no such action. */
export function declaredActionEffect(tool: string, action: string): ActionEffect | undefined {
  return current().byAction.get(`${tool}.${action}`);
}

/**
 * What `${tool}.${action}` does: what it declared, or `mutate` when this
 * server carries no such action.
 *
 * The undeclared branch covers a task name that never was an MCP action here:
 * a flow step naming something the graph does not have, a category served by a
 * session this process is not holding, a typo.
 */
export function actionEffect(tool: string, action: string): ResolvedEffect {
  const declared = declaredActionEffect(tool, action);
  if (declared !== undefined) return { effect: declared, source: "declared" };
  return { effect: UNDECLARED_EFFECT, source: "undeclared" };
}

/** The same, over a `category.action` task name. */
export function taskEffect(taskName: string): ResolvedEffect {
  const { tool, action } = splitTaskName(taskName);
  return actionEffect(tool, action);
}

/**
 * What a `category.action` task name declares, or undefined when the graph has
 * no such action. Never guesses, for the caller that wants to choose its own
 * fallback rather than take the lexicon's.
 */
export function declaredTaskEffect(taskName: string): ActionEffect | undefined {
  const { tool, action } = splitTaskName(taskName);
  return declaredActionEffect(tool, action);
}

/**
 * Methods whose effect one of their own ARGUMENTS decides, and how to read it.
 *
 * `unknown` means exactly this: the action is not what settles the question, a
 * parameter is. Where that parameter is legible, leaving the answer at
 * `unknown` is not caution, it is refusing to look at evidence in hand.
 *
 * `epic_call_tool` is the case that matters. All 830 wrapped engine tools
 * dispatch through it, so the METHOD has no effect of its own and collapses to
 * `unknown` - which meant a guard scoped to mutations saw every wrapped read,
 * and one scoped to reads saw none of them. The `tool` argument names which
 * tool is being called, and that tool's effect was reviewed and is right here.
 *
 * `execute_python` and `execute_command` stay `unknown` and always will: their
 * deciding argument is a program, and nothing short of running it answers.
 */
const ARGUMENT_DECIDES: Readonly<Record<string, (p: Record<string, unknown>) => ActionEffect | undefined>> = {
  epic_call_tool: (p) => {
    const tool = typeof p.tool === "string" ? p.tool : undefined;
    return tool ? EPIC_TOOL_EFFECTS[tool] : undefined;
  },
};

/**
 * What a BRIDGE METHOD does, given the arguments it was called with.
 *
 * Four sources, in order: an argument that decides it, the effects declared by
 * the actions that forward to the method, the hand-written table above for the
 * ones a handler calls directly, and `mutate` for a method this server does not
 * recognise at all.
 *
 * Never undefined. The guard pipeline and the source-control classifier both
 * ask this on every call and neither should be re-deriving a default of its
 * own, which is how three modules came to hold three different opinions in the
 * first place.
 *
 * `params` is optional so a caller asking about a method in the abstract still
 * gets the method's own answer. Every caller on a live path has them.
 */
export function bridgeMethodEffect(
  method: string,
  params?: Record<string, unknown>,
): ResolvedEffect {
  if (params) {
    const decided = ARGUMENT_DECIDES[method]?.(params);
    if (decided !== undefined) return { effect: decided, source: "declared" };
  }
  const declared = current().byBridgeMethod.get(method);
  if (declared !== undefined) return { effect: declared, source: "declared" };
  const raw = RAW_BRIDGE_METHODS[method];
  if (raw !== undefined) return { effect: raw, source: "declared" };
  return { effect: UNDECLARED_EFFECT, source: "undeclared" };
}

/** Does this effect mean the call may change something? `unknown` counts. */
export function mayChangeState(effect: ActionEffect): boolean {
  return effect !== "read";
}
