import * as fs from "node:fs";
import * as path from "node:path";

/** Unreal plugin names: what UnrealBuildTool accepts, and never a path. */
export const UE_PLUGIN_NAME_RE = /^[A-Za-z_][A-Za-z0-9_]*$/;

function inside(base: string, abs: string): boolean {
  const rel = path.relative(base, abs);
  return rel === "" || (rel !== ".." && !rel.startsWith(`..${path.sep}`) && !path.isAbsolute(rel));
}

/**
 * A path a plugin manifest names, resolved inside the plugin's package. A
 * path that leaves the package, lexically or through a link, is refused.
 */
export function pathInPackage(pkgDir: string, rel: string, what: string): string {
  const base = path.resolve(pkgDir);
  const abs = path.resolve(base, rel);
  if (path.isAbsolute(rel) || !inside(base, abs)) {
    throw new Error(`${what} '${rel}' is outside the plugin package`);
  }
  if (fs.existsSync(abs) && !inside(fs.realpathSync(base), fs.realpathSync(abs))) {
    throw new Error(`${what} '${rel}' resolves outside the plugin package through a link`);
  }
  return abs;
}

/** Whether a path recorded relative to `base` stays inside it. */
export function staysInside(base: string, rel: string): boolean {
  return !path.isAbsolute(rel) && inside(path.resolve(base), path.resolve(base, rel));
}
