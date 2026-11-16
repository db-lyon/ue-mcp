// @session per-session: Keyed by absolute project root: the settings files a project's hook went into.
import { ensureProjectEntry, projectEntry, readState, updateState } from "./state-store.js";

/**
 * The Claude Code settings files the feedback hook was installed into, per
 * project. Read by `uninstall-hooks` and on re-init; kept in state.json
 * because the paths are absolute and per machine.
 */
export function getInstalledHooks(projectRoot: string): string[] {
  const hooks = projectEntry(readState(), projectRoot)?.installedHooks;
  return Array.isArray(hooks) ? hooks.filter((h): h is string => typeof h === "string") : [];
}

export function setInstalledHooks(projectRoot: string, hooks: string[]): void {
  updateState((state) => {
    const entry = ensureProjectEntry(state, projectRoot);
    if (hooks.length > 0) entry.installedHooks = hooks;
    else delete entry.installedHooks;
  });
}
