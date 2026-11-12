/**
 * Which asset paths a call would change, for the lease check.
 *
 * Two sources, unioned. The asset-lock classifier (locking.ts), which knows
 * the batch shapes (`renames`, `items`, `assignments`) and editor(batch), and
 * says whether the action's declared effect is a read. Then every parameter
 * whose NAME says it holds a path, the rule the backslash repair uses
 * (path-params.ts), when its value is a mounted content path: that is what
 * catches a `blueprintPath`, a `meshPath` or a `targetPaths` array the lock
 * classifier does not list. A filesystem path or a drive path never matches,
 * because a content path starts with a single slash and a mount name.
 */
import type { ToolDef } from "../core/types.js";
import { classifyAction } from "./locking.js";
import { isPathParam } from "../surface/path-params.js";
import { normalizeLeasePath } from "./leases.js";

/** The lease actions themselves, which manage leases rather than touch assets. */
export const LEASE_ACTIONS: ReadonlySet<string> = new Set(["project.acquire_lease", "project.release_lease", "project.list_leases"]);

export function leasePathsOf(
  taskName: string,
  params: Record<string, unknown>,
  graph?: readonly ToolDef[],
): string[] {
  if (LEASE_ACTIONS.has(taskName)) return [];
  const { mutates, paths } = classifyAction(taskName, params, graph);
  if (!mutates) return [];
  const out = new Set<string>();
  const add = (v: unknown) => {
    const n = normalizeLeasePath(v);
    if (n) out.add(n);
  };
  for (const p of paths) add(p);
  const addNamed = (v: unknown) => {
    if (typeof v === "string" && NOT_CONTENT.test(v.replace(/\\/g, "/"))) return;
    add(v);
  };
  for (const [key, value] of Object.entries(params)) {
    // Path and Paths only: a Dir, File or Folder parameter names the disk.
    if (!isPathParam(key) || !/[Pp]aths?$/.test(key)) continue;
    if (Array.isArray(value)) value.forEach(addNamed);
    else addNamed(value);
  }
  return [...out];
}

/**
 * Leading segments that are not an asset: a class path under /Script, and the
 * filesystem roots a POSIX path starts with, which a `*Path` parameter naming a
 * file on disk would otherwise be mistaken for.
 */
const NOT_CONTENT = /^\/(?:Script|home|Users|tmp|var|mnt|opt|usr|etc|private|Volumes|root|srv|dev|proc)\//i;
