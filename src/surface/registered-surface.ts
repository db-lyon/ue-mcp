/**
 * The surface as the running plugin can serve it (spec 6.4).
 *
 * A bridge action whose method the connected plugin did not register is
 * withheld from what is advertised and discovered. Nothing is withheld while
 * no plugin has published its method list. An `epic_*` action is withheld the
 * same way when the editor's toolset registry lacks the toolset it wraps, once
 * that list is known (#1323). The views built here are read views: dispatch
 * keeps the full graph and refuses a withheld call by name.
 */
import type { ActionSpec, ToolContext, ToolDef } from "../core/types.js";
import type { IBridge } from "../bridge/bridge.js";
import { PLUGIN_UPGRADE_POINTER } from "../bridge/bridge.js";
import { actionEnum } from "./category-tool.js";
import { toolGraphOf } from "./target-params.js";
import { searchToolGraph } from "./context/tool-search.js";

/** Marks a set that also carries the editor's live toolset names. */
const EPIC_TOOLSETS_KNOWN = "\u0000epic-toolsets";
const EPIC_TOOLSET_PREFIX = "\u0000epic-toolset:";

type RegisteredSource = Pick<IBridge, "registeredActions"> & { readonly epicToolsets?: readonly string[] | null };

const setCache = new WeakMap<readonly string[], ReadonlySet<string>>();
const toolsetCache = new WeakMap<readonly string[], WeakMap<readonly string[], ReadonlySet<string>>>();

/**
 * The registered methods as a set, or null when there is nothing to filter by.
 * With `toolsets`, the set also says which engine toolsets the editor has.
 */
export function registeredSet(
  actions: readonly string[] | null | undefined,
  toolsets?: readonly string[] | null,
): ReadonlySet<string> | null {
  if (!actions || actions.length === 0) return null;
  if (toolsets) {
    let byToolsets = toolsetCache.get(actions);
    if (!byToolsets) toolsetCache.set(actions, (byToolsets = new WeakMap()));
    let set = byToolsets.get(toolsets);
    if (!set) {
      set = new Set([...actions, EPIC_TOOLSETS_KNOWN, ...toolsets.map((t) => EPIC_TOOLSET_PREFIX + t)]);
      byToolsets.set(toolsets, set);
    }
    return set;
  }
  let set = setCache.get(actions);
  if (!set) {
    set = new Set(actions);
    setCache.set(actions, set);
  }
  return set;
}

export function registeredMethodsOf(bridge: RegisteredSource | undefined): ReadonlySet<string> | null {
  return registeredSet(bridge?.registeredActions, bridge?.epicToolsets);
}

/** Every editor's registered methods together. Null when any editor has published none. */
export function registeredUnion(bridges: readonly RegisteredSource[]): ReadonlySet<string> | null {
  if (bridges.length === 0) return null;
  const sets = bridges.map(registeredMethodsOf);
  if (sets.some((s) => s === null)) return null;
  if (sets.length === 1) return sets[0];
  const union = new Set<string>();
  for (const s of sets) for (const m of s!) union.add(m);
  // A toolset is only judged absent when every editor's list is known.
  if (!sets.every((s) => s!.has(EPIC_TOOLSETS_KNOWN))) union.delete(EPIC_TOOLSETS_KNOWN);
  return union;
}

/** The engine toolset a wrapped action needs and the editor lacks, if any. */
export function absentToolset(spec: ActionSpec | undefined, have: ReadonlySet<string> | null): string | undefined {
  if (!have || spec?.kind !== "bridge" || !have.has(EPIC_TOOLSETS_KNOWN)) return undefined;
  const toolset = spec.epicTool?.toolset;
  return toolset && !have.has(EPIC_TOOLSET_PREFIX + toolset) ? toolset : undefined;
}

export function isWithheld(spec: ActionSpec | undefined, have: ReadonlySet<string> | null): boolean {
  if (!have || spec?.kind !== "bridge") return false;
  return !have.has(spec.bridge) || absentToolset(spec, have) !== undefined;
}

/**
 * The tool without its withheld actions: the same object when nothing is
 * withheld, null when nothing would remain.
 */
export function withholdFromTool(tool: ToolDef, have: ReadonlySet<string> | null): ToolDef | null {
  if (!have) return tool;
  const withheld = Object.keys(tool.actions).filter((a) => isWithheld(tool.actions[a], have));
  if (withheld.length === 0) return tool;
  const kept = Object.keys(tool.actions).filter((a) => !withheld.includes(a));
  if (kept.length === 0) return null;
  const actions: Record<string, ActionSpec> = {};
  for (const a of kept) actions[a] = tool.actions[a];
  const gone = new Set(withheld);
  return {
    ...tool,
    actions,
    description: tool.description
      .split("\n")
      .filter((line) => !gone.has(/^- ([^:\s]+)/.exec(line)?.[1] ?? ""))
      .join("\n"),
    schema: tool.schema.action ? { ...tool.schema, action: actionEnum(kept as [string, ...string[]]) } : tool.schema,
  };
}

export function withholdUnregistered(tools: readonly ToolDef[], have: ReadonlySet<string> | null): ToolDef[] {
  if (!have) return [...tools];
  return tools.map((t) => withholdFromTool(t, have)).filter((t): t is ToolDef => t !== null);
}

/** The session's graph as its running plugin can serve it, for discovery. */
export function visibleToolGraphOf(ctx: ToolContext): ToolDef[] {
  return withholdUnregistered(toolGraphOf(ctx), registeredMethodsOf(ctx.bridge));
}

/** The refusal for a call to a withheld action, made before any round trip. */
export function unregisteredActionMessage(task: string, method: string, registered: number): string {
  return (
    `${task} dispatches to bridge method '${method}', which the running plugin does not register `
    + `(${registered} registered), so it is withheld from this server's surface. ${PLUGIN_UPGRADE_POINTER}`
  );
}

/** Every engine toolset the graph's wrapped actions need. */
export function epicToolsetsOf(graph: readonly ToolDef[]): string[] {
  const toolsets = new Set<string>();
  for (const tool of graph) {
    for (const spec of Object.values(tool.actions)) {
      if (spec.kind === "bridge" && spec.epicTool?.toolset) toolsets.add(spec.epicTool.toolset);
    }
  }
  return [...toolsets].sort();
}

/** Whether this task is a generated action wrapping an engine toolset. */
export function wrapsEpicTool(graph: readonly ToolDef[], task: string): boolean {
  const dot = task.indexOf(".");
  const spec = graph.find((t) => t.name === task.slice(0, dot))?.actions[task.slice(dot + 1)];
  return spec?.kind === "bridge" && !!spec.epicTool;
}

/**
 * The refusal for an `epic_*` action whose toolset the editor lacks, naming
 * the native actions that do the same job (#1323, #1182).
 */
export function absentToolsetMessage(graph: readonly ToolDef[], task: string, toolset: string, description = ""): string {
  // A native action of the same name first, then a search on the name, then on the summary.
  const bare = task.slice(task.indexOf(".") + 1).replace(/^epic_/, "");
  const sameName = graph.flatMap((t) =>
    [bare, bare.replace(/s$/, "")].filter((a, i, all) => all.indexOf(a) === i && a in t.actions).map((a) => `${t.name}(${a})`));
  const summary = description.replace(/^\[Epic [^\]]*\]\s*/, "").replace(/\bParams:.*$/s, "");
  const searched = [bare.replace(/_/g, " "), summary].flatMap((q) =>
    searchToolGraph(graph as ToolDef[], q, 8).filter((h) => !h.action.startsWith("epic_")).map((h) => `${h.tool}(${h.action})`));
  const native = [...new Set([...sameName, ...searched])].slice(0, 3);
  return (
    `${task} wraps the engine toolset '${toolset}', which the running editor's toolset registry does not `
    + `register, so it is withheld from this server's surface and the call was not sent. `
    + (native.length > 0 ? `Native actions for the same job: ${native.join(", ")}. ` : "")
    + `epic(list_toolsets) lists the toolsets this editor has.`
  );
}

/**
 * The refusal for a task whose bridge method the session's plugin did not
 * register, or whose engine toolset the editor lacks; null when it may run.
 */
export function withheldTarget(
  graph: readonly ToolDef[],
  task: string,
  bridge: RegisteredSource | undefined,
): string | null {
  const have = registeredMethodsOf(bridge);
  if (!have) return null;
  const dot = task.indexOf(".");
  const spec = graph.find((t) => t.name === task.slice(0, dot))?.actions[task.slice(dot + 1)];
  if (!isWithheld(spec, have) || spec?.kind !== "bridge") return null;
  const toolset = absentToolset(spec, have);
  if (toolset && have.has(spec.bridge)) return absentToolsetMessage(graph, task, toolset, spec.description);
  return unregisteredActionMessage(task, spec.bridge, bridge?.registeredActions?.length ?? have.size);
}
