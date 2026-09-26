/**
 * Honouring `nativeTools:` now that Unreal's tools are declared rather than
 * injected.
 *
 * The config contract has not changed. `nativeTools.enabled: false` means "do
 * not advertise the wrapped engine tools", and `nativeTools.exclude: [gas]`
 * means "not in that category". What changed is the direction the work runs in:
 * the catalog used to be read from the live editor at startup and injected, so
 * honouring the config meant declining to inject; the actions are declared in
 * `ALL_TOOLS` now, so honouring it means REMOVING them from this session's
 * graph.
 *
 * Removal rather than conditional declaration is deliberate. One declaration
 * that every session starts from is what puts these actions in the golden
 * baseline, the parameter audits and `describe_action`; a surface that only
 * exists when an editor was reachable at startup was exactly what could not be
 * held to any of those.
 *
 * The `epic` category's own four actions (status, list_toolsets,
 * describe_toolset, call_tool) are never removed. They are the gateway, and
 * `call_tool` stays reachable for a toolset this package has not baked, which
 * is what makes turning the surface off survivable rather than a dead end.
 */
import type { ToolDef } from "../core/types.js";
import { actionEnum } from "./category-tool.js";

/** Prefix every generated engine-tool action carries. */
const EPIC_ACTION_PREFIX = "epic_";

/** The `epic` category's own actions, which are ue-mcp's and never removed. */
const GATEWAY_ACTIONS = new Set(["status", "list_toolsets", "describe_toolset", "call_tool"]);

export interface NativeToolsConfig {
  enabled?: boolean;
  exclude?: string[];
}

export interface EpicSurfaceResult {
  /** How many generated actions were removed. */
  removed: number;
  /** Per category, how many went. */
  byCategory: Record<string, number>;
  /** Categories that lost every action they had and were dropped entirely. */
  droppedCategories: string[];
}

/**
 * Apply `nativeTools:` to one session's graph, in place.
 *
 * A category left with no actions at all is dropped rather than advertised
 * empty: `dataflow` and `conversation` are wrapped engine tools and nothing
 * else, so switching those off should remove the category, not leave a tool
 * whose every action is gone.
 */
export function applyNativeToolsConfig(
  tools: ToolDef[],
  cfg: NativeToolsConfig | undefined,
): EpicSurfaceResult {
  const result: EpicSurfaceResult = { removed: 0, byCategory: {}, droppedCategories: [] };
  const disabled = cfg?.enabled === false;
  const excluded = new Set(cfg?.exclude ?? []);
  if (!disabled && excluded.size === 0) return result;

  for (const tool of tools) {
    if (!disabled && !excluded.has(tool.name)) continue;
    const doomed = Object.keys(tool.actions).filter(
      (a) => a.startsWith(EPIC_ACTION_PREFIX) && !(tool.name === "epic" && GATEWAY_ACTIONS.has(a)),
    );
    if (doomed.length === 0) continue;
    rebuildFilteredTool(tool, doomed);
    result.removed += doomed.length;
    result.byCategory[tool.name] = doomed.length;
  }

  for (const tool of tools) {
    if (Object.keys(tool.actions).length === 0) result.droppedCategories.push(tool.name);
  }
  return result;
}

/**
 * Rebuild the action identity and catalog after generated actions leave.
 *
 * The category schema is intentionally flat, so a field may be shared by an
 * action that forwards its entire parameter bag without naming individual
 * keys. Keep those fields until the graph carries explicit schema ownership;
 * guessing ownership here can make a valid parameter disappear at parsing.
 */
function rebuildFilteredTool(tool: ToolDef, doomed: string[]): void {
  const removed = new Set(doomed);
  for (const name of doomed) delete tool.actions[name];

  const names = Object.keys(tool.actions) as [string, ...string[]];
  if (names.length > 0) tool.schema.action = actionEnum(names);
  rebuildDescription(tool, removed);
}

/** Remove only the catalog lines for actions that are no longer callable. */
function rebuildDescription(tool: ToolDef, removed: ReadonlySet<string>): void {
  if (removed.size === 0) return;
  tool.description = tool.description
    .split("\n")
    .filter((line) => {
      const match = /^-\s+([A-Za-z_][A-Za-z0-9_]*)\b/.exec(line);
      return !match || !removed.has(match[1]);
    })
    .join("\n");
}
