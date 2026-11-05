import { getPlaytestModeScopes, getUserStatePath, type PlaytestMode } from "../config/user-state.js";

/**
 * Who starts playtest runs: a person (manual) or an agent (automatic).
 *
 * Unset is left to each client, and the server changes nothing. Manual is
 * enforced for MCP sessions only: their pie(script_run) and pie(script_replay)
 * calls are refused, not run. A daemon extension calling through
 * api.callAction is not gated here, because it reads the mode itself
 * (modes.get) and enforces its own rules.
 *
 * Stored like the dialog and feedback modes, per project (--editor) or per
 * user in ~/.ue-mcp/state.json, and never in project yaml: whether a person
 * watches each playtest is a choice about how this machine is used.
 */

/** The task names a manual mode refuses. */
export const PLAYTEST_GATED_TASKS: ReadonlySet<string> = new Set(["pie.script_run", "pie.script_replay"]);

export interface ResolvedPlaytestMode {
  /** null when nothing is set: each client decides. */
  mode: PlaytestMode | null;
  /** Whether this project sets it, rather than the per-user preference. */
  project: boolean;
}

/** The playtest mode that applies to a project: its own, else the user's, else null. */
export function resolvePlaytestMode(projectDir?: string | null): ResolvedPlaytestMode {
  const scopes = getPlaytestModeScopes(projectDir ?? null);
  return { mode: scopes.project ?? scopes.user ?? null, project: scopes.project !== undefined };
}

/**
 * The refusal for `taskName` in this project, or null when it may run. Only
 * the gated tasks under an effective manual mode are refused.
 */
export function playtestRefusal(taskName: string, projectDir?: string | null): Record<string, unknown> | null {
  if (!PLAYTEST_GATED_TASKS.has(taskName)) return null;
  const resolved = resolvePlaytestMode(projectDir);
  if (resolved.mode !== "manual") return null;
  const where = resolved.project ? "for this project" : "for this user";
  const change = resolved.project
    ? "`ue-mcp playtest mode automatic --editor <this project's .uproject>`"
    : "`ue-mcp playtest mode automatic`";
  return {
    success: false,
    refusedMethod: taskName,
    playtestMode: "manual",
    reason:
      `Playtest runs are set to manual ${where} (playtest mode in ${getUserStatePath()}), so ${taskName} was not run: `
      + `a person starts playtests here. To let an agent run them, a person can run ${change}.`,
  };
}
