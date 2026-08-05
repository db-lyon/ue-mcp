/**
 * Switching the loaded project (#818).
 *
 * The server holds two things that describe one editor: a ProjectContext that
 * resolves asset paths on disk, and a bridge socket that executes actions
 * inside the running editor. Moving one without the other is not a degraded
 * state, it is a destructive one: path-resolving actions read project B while
 * every bridge action mutates project A, and both halves report success.
 *
 * This module is the only supported way to move them, and it moves them
 * together:
 *
 *   1. Resolve and validate the target first. A bad path throws before
 *      anything has changed, so a failed switch leaves the pair on the
 *      project they were already on.
 *   2. Retarget the bridge, then re-point the ProjectContext, with no await
 *      between them. The socket to the previous editor is dropped inside step
 *      one of that pair, so the only window between them is synchronous and
 *      no handler can run inside it.
 *   3. Only then try to connect. A failure here leaves the pair consistent
 *      and simply disconnected, which is safe: nothing can reach any editor.
 */
import * as path from "node:path";
import type { BridgeTarget, IBridge } from "./bridge.js";
import { ProjectContext, readUeMcpConfig, resolveUProjectPath } from "./project.js";
import { debug, info } from "./log.js";

export interface ProjectSwitchResult {
  /** Absolute .uproject now loaded. */
  projectPath: string;
  /** What was loaded before, null on the first load. */
  previousProjectPath: string | null;
  /** Where the bridge now points. Always the same project as projectPath. */
  target: BridgeTarget;
  connected: boolean;
  /** Why the editor could not be reached, when it could not. */
  connectError?: string;
}

export async function switchProject(
  project: ProjectContext,
  bridge: IBridge,
  inputPath: string,
  opts?: { connectTimeoutMs?: number },
): Promise<ProjectSwitchResult> {
  const previousProjectPath = project.projectPath;
  // Throws on a bad path, before either half has moved.
  const resolved = resolveUProjectPath(inputPath);
  // The new project's own bridge.port, if it pins one. Read before the switch
  // so the bridge never has to fall back on the previous project's setting.
  const configPort = readUeMcpConfig(path.dirname(resolved)).bridge?.port;

  // ── The pair moves here. Do not introduce an await between these two. ──
  // retargetProject drops the socket to the previous editor before it
  // returns, so from this point no bridge call can reach the project we are
  // leaving, and setProject cannot fail in a way that strands the socket
  // there either.
  const target = bridge.retargetProject(resolved, configPort);
  project.setProject(resolved);
  // ──────────────────────────────────────────────────────────────────────

  info(
    "project",
    `switched to ${project.projectName} (${resolved}); bridge port ${target.port} (${target.portSource})`,
  );

  let connected = false;
  let connectError: string | undefined;
  try {
    await bridge.connect(opts?.connectTimeoutMs);
    connected = bridge.isConnected;
  } catch (e) {
    // Not fatal. The editor for this project may simply not be running, and
    // the pair is consistent either way.
    connectError = e instanceof Error ? e.message : String(e);
    debug("project", `no editor for ${resolved} after the switch`, e);
  }

  return {
    projectPath: resolved,
    previousProjectPath,
    target: bridge.getTarget(),
    connected,
    connectError,
  };
}

/**
 * True when the bridge is connected to an editor other than the loaded
 * project's. The switch path makes this unreachable; get_status reports it so
 * that if some future path ever breaks the invariant, it is visible rather
 * than silent (#818).
 */
export function isTargetDiverged(project: ProjectContext, target: BridgeTarget): boolean {
  if (!project.projectPath || !target.projectPath) return false;
  return path.resolve(project.projectPath) !== path.resolve(target.projectPath);
}
