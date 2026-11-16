// @session independent: Pure extraction of the asset paths one call names; the same rule in every editor.
/**
 * Which asset paths a call would change, for the lease check: the paths the
 * one written-path rule (written-paths.ts) finds, normalized the way a lease
 * names an asset, so a lease and an asset lock cover the same paths.
 */
import type { ToolDef } from "../surface/tool-types.js";
import { classifyAction } from "./written-paths.js";
import { normalizeLeasePath } from "./leases.js";

export function leasePathsOf(
  taskName: string,
  params: Record<string, unknown>,
  graph?: readonly ToolDef[],
): string[] {
  const { mutates, paths } = classifyAction(taskName, params, graph);
  if (!mutates) return [];
  const out = new Set<string>();
  for (const p of paths) {
    const n = normalizeLeasePath(p);
    if (n) out.add(n);
  }
  return [...out];
}
