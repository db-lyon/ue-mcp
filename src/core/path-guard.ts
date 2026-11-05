import * as fs from "node:fs";
import * as path from "node:path";

/**
 * Keeping an agent-supplied path inside the folders an action may touch.
 *
 * Every handler that joins a caller's path onto a project or engine folder
 * resolves it here. Links are followed, so a junction inside the project that
 * points elsewhere does not count as inside, and on Windows the comparison
 * ignores case the way the file system does.
 */

export class PathOutsideRootsError extends Error {
  constructor(readonly input: string, readonly resolved: string, readonly roots: string[]) {
    super(
      `'${input}' resolves to ${resolved}, which is outside the folders this action may use: ${roots.join(", ")}`,
    );
    this.name = "PathOutsideRootsError";
  }
}

const IGNORE_CASE = process.platform === "win32";

/** The real path of `p`, following links through the nearest ancestor that exists. */
function realpathNearest(p: string): string {
  let current = p;
  const rest: string[] = [];
  for (;;) {
    try {
      const real = fs.realpathSync.native(current);
      return rest.length > 0 ? path.join(real, ...rest.reverse()) : real;
    } catch {
      const parent = path.dirname(current);
      if (parent === current) return p;
      rest.push(path.basename(current));
      current = parent;
    }
  }
}

function fold(p: string): string {
  return IGNORE_CASE ? p.toLowerCase() : p;
}

/** Whether `child` is `root` or below it. Both must be absolute and normalized. */
export function isInside(root: string, child: string): boolean {
  const rel = path.relative(fold(root), fold(child));
  return rel === "" || (rel !== ".." && !rel.startsWith(`..${path.sep}`) && !path.isAbsolute(rel));
}

/**
 * Resolve `input` and require it to sit inside one of `roots`.
 *
 * A relative input is taken against `base`, or the first root. Returns the
 * absolute path as written (links not expanded), and throws
 * PathOutsideRootsError when the path, with links followed, leaves every root.
 */
export function resolveInside(roots: string[], input: string, opts: { base?: string } = {}): string {
  if (typeof input !== "string" || input.trim() === "") throw new Error("Missing path");
  if (input.includes("\0")) throw new Error("A path cannot contain a NUL character");
  const allowed = roots.filter((r): r is string => typeof r === "string" && r !== "").map((r) => path.resolve(r));
  if (allowed.length === 0) throw new Error("No folder is available to resolve this path against");
  const resolved = path.resolve(opts.base ?? allowed[0], input);
  const real = realpathNearest(resolved);
  for (const root of allowed) {
    if (isInside(realpathNearest(root), real)) return resolved;
  }
  throw new PathOutsideRootsError(input, resolved, allowed);
}

/** Unreal module, plugin and target names are identifiers. */
export const UE_IDENTIFIER_RE = /^[A-Za-z_][A-Za-z0-9_]*$/;
const MODULE_NAME = UE_IDENTIFIER_RE;

/** Unreal module names are identifiers. Throws on anything else, separators and '..' included. */
export function requireModuleName(name: string, field = "moduleName"): string {
  if (typeof name !== "string" || !MODULE_NAME.test(name)) {
    throw new Error(`'${field}' must be an Unreal module name (letters, digits and underscores, not starting with a digit); got '${name}'`);
  }
  return name;
}
