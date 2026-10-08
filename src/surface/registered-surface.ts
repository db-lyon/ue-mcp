/**
 * The surface as the running plugin can serve it (spec 6.4).
 *
 * A bridge action whose method the connected plugin did not register is
 * withheld from what is advertised and discovered. Nothing is withheld while
 * no plugin has published its method list. The views built here are read
 * views: dispatch keeps the full graph and refuses a withheld call by name.
 */
import type { ActionSpec, ToolContext, ToolDef } from "../core/types.js";
import type { IBridge } from "../bridge/bridge.js";
import { PLUGIN_UPGRADE_POINTER } from "../bridge/bridge.js";
import { actionEnum } from "./category-tool.js";
import { toolGraphOf } from "./target-params.js";

const setCache = new WeakMap<readonly string[], ReadonlySet<string>>();

/** The registered methods as a set, or null when there is nothing to filter by. */
export function registeredSet(actions: readonly string[] | null | undefined): ReadonlySet<string> | null {
  if (!actions || actions.length === 0) return null;
  let set = setCache.get(actions);
  if (!set) {
    set = new Set(actions);
    setCache.set(actions, set);
  }
  return set;
}

export function registeredMethodsOf(bridge: Pick<IBridge, "registeredActions"> | undefined): ReadonlySet<string> | null {
  return registeredSet(bridge?.registeredActions);
}

/** Every editor's registered methods together. Null when any editor has published none. */
export function registeredUnion(bridges: readonly Pick<IBridge, "registeredActions">[]): ReadonlySet<string> | null {
  if (bridges.length === 0) return null;
  const sets = bridges.map(registeredMethodsOf);
  if (sets.some((s) => s === null)) return null;
  if (sets.length === 1) return sets[0];
  const union = new Set<string>();
  for (const s of sets) for (const m of s!) union.add(m);
  return union;
}

export function isWithheld(spec: ActionSpec | undefined, have: ReadonlySet<string> | null): boolean {
  return !!have && spec?.kind === "bridge" && !have.has(spec.bridge);
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

/**
 * The refusal for a task whose bridge method the session's plugin did not
 * register, or null when it may run.
 */
export function withheldTarget(
  graph: readonly ToolDef[],
  task: string,
  bridge: Pick<IBridge, "registeredActions"> | undefined,
): string | null {
  const have = registeredMethodsOf(bridge);
  if (!have) return null;
  const dot = task.indexOf(".");
  const spec = graph.find((t) => t.name === task.slice(0, dot))?.actions[task.slice(dot + 1)];
  if (!isWithheld(spec, have) || spec?.kind !== "bridge") return null;
  return unregisteredActionMessage(task, spec.bridge, have.size);
}
