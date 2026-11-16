// @session per-session: Removes the retired Python bridge from one project's deployed plugin directory.
import * as fs from "node:fs";
import * as path from "node:path";

/**
 * The deployed plugin's `Content/Python`, left by the retired Python bridge.
 * Its init_unreal.py still runs in every process that loads the plugin and
 * logs an Error from commandlets, which turns a passing commandlet into exit
 * code 1 (#1273). The current plugin ships no Content, so the directory is
 * only ever stale.
 */
export function legacyPythonDir(pluginDir: string): string {
  return path.join(pluginDir, "Content", "Python");
}

/**
 * Remove the stale `Content/Python` from a deployed plugin, and `Content` too
 * when nothing else is left in it. Returns true when something was removed.
 * Never throws: a file the editor holds open is left for the next install.
 */
export function removeLegacyPythonContent(pluginDir: string): boolean {
  const pythonDir = legacyPythonDir(pluginDir);
  if (!fs.existsSync(pythonDir)) return false;
  try {
    fs.rmSync(pythonDir, { recursive: true, force: true });
    const contentDir = path.dirname(pythonDir);
    if (fs.existsSync(contentDir) && fs.readdirSync(contentDir).length === 0) {
      fs.rmdirSync(contentDir);
    }
    return !fs.existsSync(pythonDir);
  } catch {
    return false;
  }
}
