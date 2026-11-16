// @session per-session: Installs the hook for one project.
import * as fs from "node:fs";
import * as path from "node:path";
import { warn as logWarn } from "../../core/log.js";
import { getInstalledHooks, setInstalledHooks } from "../../config/hook-state.js";
import { MCP_SERVER_ENTRY, UE_MCP_LAUNCH } from "../mcp-clients/mcp-client-config.js";

/**
 * Symmetric install/uninstall for the Claude Code PostToolUse hook that
 * prompts the agent to file feedback when execute_python was used as a
 * workaround.
 *
 * Two invariants:
 *   1. Every install records the settings path in `~/.ue-mcp/state.json`
 *      keyed by absolute project root, so uninstall can reach every site
 *      even after the user moves their MCP client config. Lives in the
 *      user-state file (not the project tree) because those absolute
 *      paths are user-machine-specific and have no business in tracked
 *      config.
 *   2. Uninstall is idempotent - calling it against a settings file that
 *      doesn't have our matcher is a no-op, not an error.
 */

/** Claude Code names an MCP tool `mcp__<server>__<tool>`. */
const MATCHER = `mcp__${MCP_SERVER_ENTRY}__editor`;
/**
 * Launched the way the server entry is, so the hook runs the same release.
 * Hooks written earlier as `npx ue-mcp hook post-tool-use` are found by the
 * matcher, rewritten on install and removed on uninstall.
 */
export const HOOK_COMMAND = `${UE_MCP_LAUNCH} hook post-tool-use`;
const HOOK_COMMAND_PATTERN = /\bue-mcp(@\S+)?\s+hook\s+post-tool-use\b/;

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
      `Claude settings at ${settingsPath} was not valid JSON - treating as empty`,
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

function registerInstalledHook(projectDir: string, settingsPath: string): void {
  const abs = path.resolve(settingsPath);
  const installed = new Set(getInstalledHooks(projectDir));
  installed.add(abs);
  setInstalledHooks(projectDir, [...installed].sort());
}

function unregisterInstalledHook(
  projectDir: string,
  settingsPath: string,
): void {
  const abs = path.resolve(settingsPath);
  const existing = getInstalledHooks(projectDir);
  if (existing.length === 0) return;
  setInstalledHooks(
    projectDir,
    existing.filter((p) => path.resolve(p) !== abs),
  );
}

/**
 * Install the ue-mcp PostToolUse hook into a Claude Code settings.json. If
 * `projectDir` is supplied, the settings path is also recorded in
 * `~/.ue-mcp/state.json` under this project's entry so uninstall can find
 * every site later.
 */
export function installClaudeHooks(
  settingsPath: string,
  projectDir?: string,
): void {
  const settings = readSettings(settingsPath);
  if (!settings.hooks) settings.hooks = {};
  if (!settings.hooks.PostToolUse) settings.hooks.PostToolUse = [];

  const ours = settings.hooks.PostToolUse.filter((h) => h.matcher === MATCHER);
  if (ours.length === 0) {
    settings.hooks.PostToolUse.push({ matcher: MATCHER, hooks: [{ type: "command", command: HOOK_COMMAND }] });
  }
  // Bring an earlier launch form up to date, leaving any hook the user added beside it.
  for (const entry of ours) {
    entry.hooks = entry.hooks.map((h) => (HOOK_COMMAND_PATTERN.test(h.command) ? { ...h, command: HOOK_COMMAND } : h));
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
 * Uninstall the hook from every path recorded in `~/.ue-mcp/state.json`'s
 * `projects[<root>].installedHooks[]`. Used by `npx ue-mcp uninstall-hooks`
 * and by init when the user disables feedback or opts out of the prompt
 * checkbox.
 */
export function uninstallAllRegisteredHooks(projectDir: string): {
  removed: string[];
  skipped: string[];
} {
  const paths = getInstalledHooks(projectDir);
  const removed: string[] = [];
  const skipped: string[] = [];
  for (const p of paths) {
    const didRemove = uninstallClaudeHooks(p);
    if (didRemove) removed.push(p);
    else skipped.push(p);
  }
  setInstalledHooks(projectDir, []);
  return { removed, skipped };
}
