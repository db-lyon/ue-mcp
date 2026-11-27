// @session per-session: Reads the workspace of the project directory it is handed.
/**
 * What a run tested: the workspace's source control state when it started,
 * and a step-list script's version for pie(script_run)'s provenance file
 * (src/pie/script-provenance.ts).
 */
import * as crypto from "node:crypto";
import type { CurrentProvenance, RunVcs } from "./contract.js";
import { detectVcs, revisionOf, run as runTool, type Run } from "./vcs.js";
import { canonical } from "./util.js";

/** The fields of a script_read answer that decide what a run does; its path, name and prose do not. */
const SCRIPT_FIELDS = ["profile", "map", "default_within_ms", "manual_only", "steps"] as const;

/** A script's version from its pie(script_read) answer: "sha256:<hex>" of its canonical definition, or null for an answer with no steps. */
export function scriptHashOf(answer: Record<string, unknown> | null): string | null {
  if (!answer || !Array.isArray(answer.steps)) return null;
  const def: Record<string, unknown> = {};
  for (const k of SCRIPT_FIELDS) if (answer[k] !== undefined) def[k] = answer[k];
  return `sha256:${crypto.createHash("sha256").update(canonical(def)).digest("hex")}`;
}

/** The workspace now: its source control, revision and how many files are changed or opened. */
export async function workspaceNow(projectDir: string, r: Run = runTool): Promise<CurrentProvenance> {
  const vcs = await detectVcs(projectDir, r).catch(() => null);
  if (!vcs) return { vcs: "none", revision: null, dirty: null };
  const revision = await revisionOf(vcs, projectDir, r).catch(() => null);
  return { vcs: vcs.kind as RunVcs, revision, dirty: vcs.changes };
}
