import * as fs from "node:fs";
import * as path from "node:path";
import * as os from "node:os";

/**
 * User-scoped, machine-only state. Lives at `~/.ue-mcp/state.json`. Stores
 * things that:
 *
 *   - Vary per machine (absolute filesystem paths)
 *   - Are written by ue-mcp commands, not by hand
 *   - Have no business being committed alongside the project
 *
 * Keyed by absolute project root so a single user can run ue-mcp across many
 * projects without state collision.
 *
 * Currently just `installedHooks` — the list of Claude Code settings files
 * where the feedback PostToolUse hook was installed for a given project.
 * Read on `npx ue-mcp uninstall-hooks` and on re-init to seed the hook
 * checkbox.
 *
 * NOT for project policy. Anything a collaborator should also see goes in
 * `ue-mcp.yml`, not here.
 */

interface ProjectState {
  installedHooks?: string[];
}

interface UserState {
  projects?: Record<string, ProjectState>;
}

function statePath(): string {
  return (
    process.env.UE_MCP_USER_STATE ||
    path.join(os.homedir(), ".ue-mcp", "state.json")
  );
}

function readState(): UserState {
  const file = statePath();
  if (!fs.existsSync(file)) return {};
  try {
    return JSON.parse(fs.readFileSync(file, "utf-8")) as UserState;
  } catch {
    return {};
  }
}

function writeState(state: UserState): void {
  const file = statePath();
  // Drop empty containers so the file shrinks toward {} as state clears.
  if (state.projects) {
    for (const key of Object.keys(state.projects)) {
      const proj = state.projects[key];
      if (
        proj.installedHooks !== undefined &&
        proj.installedHooks.length === 0
      ) {
        delete proj.installedHooks;
      }
      if (Object.keys(proj).length === 0) {
        delete state.projects[key];
      }
    }
    if (Object.keys(state.projects).length === 0) {
      delete state.projects;
    }
  }

  // No state at all → delete the file rather than leave an empty stub.
  if (Object.keys(state).length === 0) {
    if (fs.existsSync(file)) fs.unlinkSync(file);
    return;
  }

  const dir = path.dirname(file);
  if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(file, JSON.stringify(state, null, 2), { mode: 0o600 });
}

function projectKey(projectRoot: string): string {
  return path.resolve(projectRoot);
}

export function getInstalledHooks(projectRoot: string): string[] {
  const state = readState();
  return state.projects?.[projectKey(projectRoot)]?.installedHooks ?? [];
}

export function setInstalledHooks(projectRoot: string, hooks: string[]): void {
  const state = readState();
  const key = projectKey(projectRoot);
  if (!state.projects) state.projects = {};
  if (!state.projects[key]) state.projects[key] = {};
  if (hooks.length > 0) {
    state.projects[key].installedHooks = hooks;
  } else {
    delete state.projects[key].installedHooks;
  }
  writeState(state);
}

export function getUserStatePath(): string {
  return statePath();
}
