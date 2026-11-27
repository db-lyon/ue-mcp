// @session per-session: Resolves the playtest mode for the project it is handed, from that project's stored preference.
import { PLAYTEST_MODE, type PlaytestMode } from "../config/modes.js";
import { getUserStatePath } from "../config/state-store.js";

/**
 * Who starts playtest runs: a person (manual) or an agent (automatic).
 *
 * Unset is left to each client, and the server changes nothing. Manual is
 * enforced for MCP sessions only: their pie(arrangement_play) and
 * pie(clip_record_start) calls are refused, not run. The hub and a daemon
 * extension calling through api.callAction are not gated here, because they
 * read the mode themselves (modes.get) and enforce their own rules.
 *
 * Stored like every scoped mode (config/modes.ts), per project (--editor) or
 * per user in ~/.ue-mcp/state.json.
 */

/** The task names a manual mode refuses. */
export const PLAYTEST_GATED_TASKS: ReadonlySet<string> = new Set(["pie.arrangement_play", "pie.clip_record_start"]);

export interface ResolvedPlaytestMode {
  /** null when nothing is set: each client decides. */
  mode: PlaytestMode | null;
  /** Whether this project sets it, rather than the per-user preference. */
  project: boolean;
}

/** The playtest mode that applies to a project: its own, else the user's, else null. */
export function resolvePlaytestMode(projectDir?: string | null): ResolvedPlaytestMode {
  const resolved = PLAYTEST_MODE.resolve(projectDir ?? null);
  return { mode: resolved.value ?? null, project: resolved.source === "project" };
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
