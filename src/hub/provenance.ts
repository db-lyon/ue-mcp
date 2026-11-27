// @session per-session: Reads the workspace of the project directory it is handed.
/**
 * What a run tested: the workspace's source control state when it started.
 */
import type { CurrentProvenance, RunVcs } from "./contract.js";
import { detectVcs, revisionOf, run as runTool, type Run } from "./vcs.js";

/** The workspace now: its source control, revision and how many files are changed or opened. */
export async function workspaceNow(projectDir: string, r: Run = runTool): Promise<CurrentProvenance> {
  const vcs = await detectVcs(projectDir, r).catch(() => null);
  if (!vcs) return { vcs: "none", revision: null, dirty: null };
  const revision = await revisionOf(vcs, projectDir, r).catch(() => null);
  return { vcs: vcs.kind as RunVcs, revision, dirty: vcs.changes };
}
