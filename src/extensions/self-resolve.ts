// @session independent: Registers the process-wide resolve hook for ue-mcp imports; no editor.
import * as module from "node:module";
import * as path from "node:path";
import { pathToFileURL } from "node:url";
import { packageRoot } from "../core/package-root.js";

let registered = false;

/**
 * Route every later `import "ue-mcp/..."` to this package (see
 * self-resolve-hooks.ts). Only the built package registers: the hooks run in
 * Node's loader thread, which cannot load the TypeScript sources.
 */
export function resolveServerImportsToSelf(): boolean {
  if (registered) return true;
  if (typeof module.register !== "function" || !import.meta.url.endsWith(".js")) return false;
  try {
    module.register("./self-resolve-hooks.js", import.meta.url, {
      data: { selfUrl: pathToFileURL(path.join(packageRoot(), "package.json")).href },
    });
    registered = true;
  } catch {
    // Older Node: a plugin then resolves ue-mcp from its own node_modules.
  }
  return registered;
}
