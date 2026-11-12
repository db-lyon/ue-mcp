/**
 * Honouring `nativeTools:` now that Unreal's tools are declared rather than
 * injected.
 *
 * `nativeTools.enabled: false` means "do not advertise the wrapped engine
 * tools", and `nativeTools.exclude: [gas]` means "not in that category", which
 * `disable: [gas:epic]` now says (see category-state.ts, which does the work
 * for both). The actions are declared in `ALL_TOOLS`, so honouring either
 * means REMOVING them from this session's graph.
 *
 * Removal rather than conditional declaration is deliberate. One declaration
 * that every session starts from is what puts these actions in the golden
 * baseline, the parameter audits and `describe_action`; a surface that only
 * exists when an editor was reachable at startup was exactly what could not be
 * held to any of those.
 *
 * The `epic` category's own actions (status, list_toolsets, describe_toolset,
 * call_tool and the rest that wrap no toolset) are never removed. They are the
 * gateway, and `call_tool` stays reachable for a toolset this package has not
 * baked, which is what makes turning the surface off survivable rather than a
 * dead end.
 */
import type { ToolDef } from "../core/types.js";
import { applyCategoryState, readCategoryState, snapshotGraph } from "./category-state.js";

export interface NativeToolsConfig {
  enabled?: boolean;
  exclude?: string[];
}

export interface EpicSurfaceResult {
  /** How many generated actions were removed. */
  removed: number;
  /** Per category, how many went. */
  byCategory: Record<string, number>;
  /** Categories that lost every action they had. */
  droppedCategories: string[];
}

/**
 * Apply `nativeTools:` to a graph, in place.
 *
 * A category left with no actions at all is reported: `dataflow` and
 * `conversation` are wrapped engine tools and nothing else, so switching those
 * off should remove the category, not leave a tool whose every action is gone.
 */
export function applyNativeToolsConfig(
  tools: ToolDef[],
  cfg: NativeToolsConfig | undefined,
): EpicSurfaceResult {
  const applied = applyCategoryState(tools, snapshotGraph(tools), readCategoryState({ nativeTools: cfg }));
  const byCategory: Record<string, number> = {};
  let removed = 0;
  for (const [name, n] of Object.entries(applied.removed)) {
    if (n.epic === 0) continue;
    byCategory[name] = n.epic;
    removed += n.epic;
  }
  return { removed, byCategory, droppedCategories: applied.empty };
}
