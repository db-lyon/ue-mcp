import * as fs from "node:fs";
import * as path from "node:path";
import { warn as logWarn } from "./log.js";

/**
 * Symmetric install/uninstall for the Claude Code PostToolUse hook that
 * prompts the agent to file feedback when execute_python was used as a
 * workaround.
 *
 * Two invariants:
 *   1. Every install records the settings path in .ue-mcp.json
 *      `installedHooks[]`, so uninstall can reach every site even after
 *      the user moves their MCP client config.
 *   2. Uninstall is idempotent — calling it against a settings file that
 *      doesn't have our matcher is a no-op, not an error.
 */

const MATCHER = "mcp__ue-mcp__editor";
const COMMAND = "npx ue-mcp hook post-tool-use";

interface ClaudeHookEntry {
  type: string;
  command: string;
}

interface ClaudeHookMatcher {
  matcher: string;
  hooks: ClaudeHookEntry[];
}

interface ClaudeSettings {
  hooks?: Record<string, ClaudeHookMatcher[]>;
  [key: string]: unknown;
}

function readSettings(settingsPath: string): ClaudeSettings {
  if (!fs.existsSync(settingsPath)) return {};
  try {
    return JSON.parse(fs.readFileSync(settingsPath, "utf-8"));
  } catch (e) {
    logWarn(
      "hook-installer",
      `Claude settings at ${settingsPath} was not valid JSON — treating as empty`,
      e,
    );
    return {};
  }
}

function writeSettings(settingsPath: string, settings: ClaudeSettings): void {
  const dir = path.dirname(settingsPath);
  if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(settingsPath, JSON.stringify(settings, null, 2));
}

interface ProjectConfig {
  installedHooks?: string[];
  [key: string]: unknown;
}

function readProjectConfig(projectDir: string): {
  configPath: string;
  config: ProjectConfig;
} {
  const configPath = path.join(projectDir, ".ue-mcp.json");
  if (!fs.existsSync(configPath)) return { configPath, config: {} };
  try {
    return {
      configPath,
      config: JSON.parse(fs.readFileSync(configPath, "utf-8")),
    };
  } catch (e) {
    logWarn(
      "hook-installer",
      `.ue-mcp.json at ${configPath} was not valid JSON — registry will be rewritten`,
      e,
    );
    return { configPath, config: {} };
  }
}

function writeProjectConfig(configPath: string, config: ProjectConfig): void {
  fs.writeFileSync(configPath, JSON.stringify(config, null, 2));
}

function registerInstalledHook(projectDir: string, settingsPath: string): void {
  const abs = path.resolve(settingsPath);
  const { configPath, config } = readProjectConfig(projectDir);
  const installed = new Set(config.installedHooks ?? []);
  installed.add(abs);
  config.installedHooks = [...installed].sort();
  writeProjectConfig(configPath, config);
}

function unregisterInstalledHook(
  projectDir: string,
  settingsPath: string,
): void {
  const abs = path.resolve(settingsPath);
  const { configPath, config } = readProjectConfig(projectDir);
  if (!config.installedHooks || config.installedHooks.length === 0) return;
  config.installedHooks = config.installedHooks.filter(
    (p) => path.resolve(p) !== abs,
  );
  if (config.installedHooks.length === 0) delete config.installedHooks;
  writeProjectConfig(configPath, config);
}

/**
 * Install the ue-mcp PostToolUse hook into a Claude Code settings.json. If
 * `projectDir` is supplied, the settings path is also recorded in the
 * project's .ue-mcp.json installedHooks registry.
 */
export function installClaudeHooks(
  settingsPath: string,
  projectDir?: string,
): void {
  const settings = readSettings(settingsPath);
  if (!settings.hooks) settings.hooks = {};
  if (!settings.hooks.PostToolUse) settings.hooks.PostToolUse = [];

  const already = settings.hooks.PostToolUse.some(
    (h) => h.matcher === MATCHER,
  );
  if (!already) {
    settings.hooks.PostToolUse.push({
      matcher: MATCHER,
      hooks: [{ type: "command", command: COMMAND }],
    });
  }

  writeSettings(settingsPath, settings);
  if (projectDir) registerInstalledHook(projectDir, settingsPath);
}

/**
 * Remove the ue-mcp PostToolUse matcher from a Claude Code settings.json.
 * Idempotent: a missing file, missing hooks block, or missing matcher is
 * treated as already-uninstalled. Returns true if a matcher was actually
 * removed, false otherwise.
 */
export function uninstallClaudeHooks(
  settingsPath: string,
  projectDir?: string,
): boolean {
  let removed = false;
  if (fs.existsSync(settingsPath)) {
    const settings = readSettings(settingsPath);
    if (settings.hooks?.PostToolUse) {
      const before = settings.hooks.PostToolUse.length;
      settings.hooks.PostToolUse = settings.hooks.PostToolUse.filter(
        (h) => h.matcher !== MATCHER,
      );
      if (settings.hooks.PostToolUse.length !== before) {
        removed = true;
        if (settings.hooks.PostToolUse.length === 0) {
          delete settings.hooks.PostToolUse;
        }
        if (Object.keys(settings.hooks).length === 0) {
          delete settings.hooks;
        }
        writeSettings(settingsPath, settings);
      }
    }
  }
  if (projectDir) unregisterInstalledHook(projectDir, settingsPath);
  return removed;
}

/**
 * Uninstall the hook from every path recorded in .ue-mcp.json
 * `installedHooks[]`. Used by `npx ue-mcp uninstall-hooks` and by init when
 * the user disables feedback or opts out of the prompt checkbox.
 */
export function uninstallAllRegisteredHooks(projectDir: string): {
  removed: string[];
  skipped: string[];
} {
  const { configPath, config } = readProjectConfig(projectDir);
  const paths = config.installedHooks ?? [];
  const removed: string[] = [];
  const skipped: string[] = [];
  for (const p of paths) {
    const didRemove = uninstallClaudeHooks(p);
    if (didRemove) removed.push(p);
    else skipped.push(p);
  }
  delete config.installedHooks;
  writeProjectConfig(configPath, config);
  return { removed, skipped };
}
