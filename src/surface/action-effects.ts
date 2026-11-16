// @session independent: Indexes whichever tool graph the caller hands it, one index per graph; an action's effect does not change with the editor it runs in.
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
 * Every lookup takes the graph to read. The server passes the graph it
 * dispatches, which carries plugin-injected actions and their effects too;
 * omitted, the pristine declaration in `ALL_TOOLS` answers.
 */
import { ALL_TOOLS } from "../tools.js";
import { splitTaskName } from "./action-class.js";
import { flowCategoryForCheck } from "../flow/flow-surface.js";
import { DESTRUCTIVE_VERBS, leadingVerb } from "./action-verbs.js";
import type { ActionEffect, ActionEffectSource, ActionSpec, RawBridgeMethod, ToolDef } from "./tool-types.js";
import type { ActionEffectInfo } from "../contract/daemon.js";

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

interface EffectIndex {
  byAction: Map<string, ActionSpec>;
  byBridgeMethod: Map<string, ActionEffect>;
  /** The actions that forward to each bridge method. */
  specsByBridgeMethod: Map<string, ActionSpec[]>;
  /**
   * Bridge methods a HANDLER calls directly, as the actions calling them
   * declare in `rawBridgeMethods`. No action forwards to them, so this is the
   * only place their effect is stated.
   */
  rawMethods: Map<string, RawBridgeMethod>;
  /** Readers of a method's effect off its arguments, from `bridgeEffect`. */
  deciders: Map<string, Array<(params: Record<string, unknown>) => ActionEffect | undefined>>;
  /** What each wrapped engine tool does, keyed `method tool` (its qualified name), from the epic_* actions wrapping it. */
  epicTools: Map<string, ActionEffect>;
}

/** One index per graph, dropped with the graph. */
const indexes = new WeakMap<readonly ToolDef[], EffectIndex>();

function build(graph: readonly ToolDef[]): EffectIndex {
  const index: EffectIndex = {
    byAction: new Map(),
    byBridgeMethod: new Map(),
    specsByBridgeMethod: new Map(),
    rawMethods: new Map(),
    deciders: new Map(),
    epicTools: new Map(),
  };
  for (const tool of graph) {
    for (const [action, spec] of Object.entries(tool.actions)) {
      index.byAction.set(`${tool.name}.${action}`, spec);
      for (const [method, raw] of Object.entries(spec.rawBridgeMethods ?? {})) index.rawMethods.set(method, raw);
      if (spec.kind !== "bridge") continue;
      const seen = index.byBridgeMethod.get(spec.bridge);
      // One bridge method reached by two actions that declare different
      // effects does not have an effect of its own. `epic_call_tool` is the
      // real case: every one of the wrapped engine tools dispatches through
      // it, and between them they read and write. `unknown` is the honest
      // answer for the METHOD, and it gates exactly like `mutate`.
      index.byBridgeMethod.set(
        spec.bridge,
        seen === undefined || seen === spec.effect ? spec.effect : "unknown",
      );
      const specs = index.specsByBridgeMethod.get(spec.bridge) ?? [];
      specs.push(spec);
      index.specsByBridgeMethod.set(spec.bridge, specs);
      if (spec.bridgeEffect) {
        const list = index.deciders.get(spec.bridge) ?? [];
        list.push(spec.bridgeEffect);
        index.deciders.set(spec.bridge, list);
      }
      if (spec.epicTool) index.epicTools.set(`${spec.bridge} ${spec.epicTool.name}`, spec.effect);
    }
  }
  return index;
}

function indexFor(graph: readonly ToolDef[] = ALL_TOOLS): EffectIndex {
  let index = indexes.get(graph);
  if (!index) {
    // The flow tool is built per server, outside every graph, so its actions
    // are indexed from a stand-in a unit test keeps matching the real tool.
    index = build([...graph, flowCategoryForCheck()]);
    indexes.set(graph, index);
  }
  return index;
}

/** What `${tool}.${action}` declares, or undefined when the graph has no such action. */
export function declaredActionEffect(
  tool: string,
  action: string,
  graph?: readonly ToolDef[],
): ActionEffect | undefined {
  return indexFor(graph).byAction.get(`${tool}.${action}`)?.effect;
}

/**
 * The declaration of a `category.action` task, or undefined when the graph
 * has no such action. How a gate reads a per-action fact (`offline`,
 * `assetCoordination`, `steps`, ...) instead of keeping a list of names.
 */
export function actionSpecOf(taskName: string, graph?: readonly ToolDef[]): ActionSpec | undefined {
  return indexFor(graph).byAction.get(taskName);
}

/** The actions that forward to a bridge method, empty when none does. */
export function bridgeMethodSpecs(method: string, graph?: readonly ToolDef[]): readonly ActionSpec[] {
  return indexFor(graph).specsByBridgeMethod.get(method) ?? [];
}

/** A bridge method a handler calls directly, as the action calling it declares it. */
export function rawBridgeMethod(method: string, graph?: readonly ToolDef[]): RawBridgeMethod | undefined {
  return indexFor(graph).rawMethods.get(method);
}

/** The bridge calls a raw method runs as one job (execute_batch), or undefined for one that runs none. */
export function rawMethodSteps(
  method: string,
  params: Record<string, unknown>,
  graph?: readonly ToolDef[],
): Array<{ method: string; params: Record<string, unknown> }> | undefined {
  const raw = rawBridgeMethod(method, graph);
  return raw !== undefined && typeof raw === "object" ? raw.steps(params) : undefined;
}

/** Every raw bridge method the graph's actions declare, with its effect. */
export function rawBridgeMethods(graph?: readonly ToolDef[]): ReadonlyMap<string, ActionEffect> {
  const out = new Map<string, ActionEffect>();
  for (const [method, raw] of indexFor(graph).rawMethods) out.set(method, typeof raw === "object" ? raw.effect : raw);
  return out;
}

/**
 * What `${tool}.${action}` does: what it declared, or `mutate` when this
 * server carries no such action.
 *
 * The undeclared branch covers a task name that never was an MCP action here:
 * a flow step naming something the graph does not have, a category served by a
 * session this process is not holding, a typo.
 */
export function actionEffect(tool: string, action: string, graph?: readonly ToolDef[]): ResolvedEffect {
  const declared = declaredActionEffect(tool, action, graph);
  if (declared !== undefined) return { effect: declared, source: "declared" };
  return { effect: UNDECLARED_EFFECT, source: "undeclared" };
}

/** The same, over a `category.action` task name. */
export function taskEffect(taskName: string, graph?: readonly ToolDef[]): ResolvedEffect {
  const { tool, action } = splitTaskName(taskName);
  return actionEffect(tool, action, graph);
}

/**
 * The effect a method's ARGUMENTS decide, when they are legible.
 *
 * `unknown` means exactly this: the action is not what settles the question, a
 * parameter is. Where that parameter is legible, leaving the answer at
 * `unknown` is not caution, it is refusing to look at evidence in hand.
 *
 * `epic_call_tool` is the case that matters. Every wrapped engine tool
 * dispatches through it, so the METHOD has no effect of its own and collapses
 * to `unknown`. The `tool` argument names which tool is being called: the
 * epic_* action wrapping it declares its effect, and epic(call_tool) declares
 * how to read the rest.
 *
 * A raw method that runs steps (execute_batch) does what its steps do. The
 * strongest step decides, and `unknown` outranks `mutate` here: a guard scoped
 * to `unknown` exists to put a person in front of the escape hatches, and an
 * execute_command hidden among declared mutations is still an escape hatch. A
 * batch of reads reads.
 *
 * `execute_python` and `execute_command` stay `unknown` and always will: their
 * deciding argument is a program, and nothing short of running it answers.
 */
function decidedEffect(method: string, params: Record<string, unknown>, graph?: readonly ToolDef[]): ActionEffect | undefined {
  const index = indexFor(graph);
  if (typeof params.tool === "string") {
    const wrapped = index.epicTools.get(`${method} ${params.tool}`);
    if (wrapped !== undefined) return wrapped;
  }
  for (const decide of index.deciders.get(method) ?? []) {
    const decided = decide(params);
    if (decided !== undefined) return decided;
  }
  const steps = rawMethodSteps(method, params, graph);
  if (steps === undefined || !Array.isArray(params.ops)) return undefined;
  let strongest: ActionEffect = "read";
  for (const step of steps) {
    const { effect } = bridgeMethodEffect(step.method, step.params, graph);
    if (effect === "unknown") return "unknown";
    if (effect === "mutate") strongest = "mutate";
  }
  return strongest;
}

/**
 * What a BRIDGE METHOD does, given the arguments it was called with.
 *
 * Four sources, in order: an argument that decides it, the effects declared by
 * the actions that forward to the method, the `rawBridgeMethods` an action
 * declares for the ones its handler calls directly, and `mutate` for a method
 * this server does not recognise at all.
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
  graph?: readonly ToolDef[],
): ResolvedEffect {
  if (params) {
    const decided = decidedEffect(method, params, graph);
    if (decided !== undefined) return { effect: decided, source: "declared" };
  }
  const declared = indexFor(graph).byBridgeMethod.get(method);
  if (declared !== undefined) return { effect: declared, source: "declared" };
  const raw = rawBridgeMethod(method, graph);
  if (raw !== undefined) return { effect: typeof raw === "object" ? raw.effect : raw, source: "declared" };
  return { effect: UNDECLARED_EFFECT, source: "undeclared" };
}

/** Does this effect mean the call may change something? `unknown` counts. */
export function mayChangeState(effect: ActionEffect): boolean {
  return effect !== "read";
}

/**
 * Whether an action destroys something: what its spec declares, else, for a
 * mutate or unknown effect, whether its name leads with a DESTRUCTIVE_VERBS
 * verb. The lexicon is a fallback; an action it misjudges declares the answer.
 */
export function actionDestructive(
  action: string,
  spec: { destructive?: boolean } | undefined,
  effect: ActionEffect,
): { destructive: boolean; source: ActionEffectInfo["destructiveSource"] } {
  if (typeof spec?.destructive === "boolean") return { destructive: spec.destructive, source: "declared" };
  if (effect === "read") return { destructive: false, source: "none" };
  return { destructive: DESTRUCTIVE_VERBS.has(leadingVerb(action)), source: "lexicon" };
}

/**
 * Every action of `tools` with its effect, read from `graph` (the graph the
 * server dispatches, plugin-injected actions included) and its destructive
 * flag. `categories` narrows the list; an unknown name throws.
 */
export function actionEffectInfo(
  tools: readonly ToolDef[],
  graph: readonly ToolDef[],
  categories?: readonly string[],
): ActionEffectInfo[] {
  if (categories) {
    const known = new Set(tools.map((t) => t.name));
    const unknown = categories.filter((c) => !known.has(c));
    if (unknown.length > 0) throw new RangeError(`no such category: ${unknown.join(", ")}`);
  }
  const wanted = categories ? new Set(categories) : null;
  const out: ActionEffectInfo[] = [];
  for (const tool of tools) {
    if (wanted && !wanted.has(tool.name)) continue;
    for (const [action, spec] of Object.entries(tool.actions)) {
      const resolved = actionEffect(tool.name, action, graph);
      const source = resolved.source === "declared" ? (spec.effectSource ?? "declared") : resolved.source;
      const d = actionDestructive(action, spec, resolved.effect);
      out.push({
        category: tool.name,
        action,
        effect: resolved.effect,
        effectSource: source,
        destructive: d.destructive,
        destructiveSource: d.source,
      });
    }
  }
  return out;
}
