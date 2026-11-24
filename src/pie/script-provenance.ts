// @session per-session: Reads one editor's script and the project's workspace and writes beside that editor's run.
/**
 * What a script run tested, recorded when pie(script_run) or pie(script_replay)
 * starts it: the workspace's source control state (kind, revision, changed
 * files), the script's version (a hash of the definition that decides what the
 * run does), the engine, and the players the editor played with.
 *
 * It lands in Saved/MCPObservations/<run_id>/provenance.json, beside the run's
 * own results.json, in the shape the hub reads (src/hub/provenance.ts): the
 * fields `script_hash` and `dependencies`, an `environment` with
 * `engine_version`, `net_mode` and `clients`, and the workspace's `vcs`,
 * `revision` and `dirty`. A client that lists runs gets the same record whoever
 * started the run, which is why it is written here and not by a client.
 *
 * Best effort and never in the way: a run that cannot be recorded still runs.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { scriptHashOf, workspaceNow } from "../hub/provenance.js";
import { writeFileAtomic } from "../hub/atomic-write.js";
import type { ToolContext } from "../surface/tool-types.js";

/** The folder ue-mcp keeps a run's observation in; null for an id that could step outside it. */
export function observationDir(projectDir: string, runId: string): string | null {
  return /^[A-Za-z0-9_-]{1,200}$/.test(runId) ? path.join(projectDir, "Saved", "MCPObservations", runId) : null;
}

const isObject = (v: unknown): v is Record<string, unknown> => !!v && typeof v === "object" && !Array.isArray(v);

/** The engine version, major.minor, from the project's EngineAssociation; null for a source build's id. */
function engineVersionOf(ctx: ToolContext): string | null {
  const assoc = ctx.project.engineAssociation;
  const m = assoc ? /^\d+\.\d+/.exec(assoc) : null;
  return m ? m[0] : null;
}

/** The players PIE has now and how they are networked, from pie_clients; null when the editor does not say. */
async function playersOf(ctx: ToolContext): Promise<{ clients: number; net_mode: "standalone" | "listen" | "client" } | null> {
  try {
    const s = await ctx.bridge.call("pie_clients", {});
    if (!isObject(s) || s.success === false) return null;
    const remote = Array.isArray(s.clients) ? s.clients.length : 0;
    if (s.networked !== true) return { clients: 1, net_mode: "standalone" };
    if (s.server_net_mode === "listen_server") return { clients: remote + (s.host_player === true ? 1 : 0), net_mode: "listen" };
    return remote > 0 ? { clients: remote, net_mode: "client" } : null;
  } catch {
    return null;
  }
}

/**
 * Record what a run that has just started tests. `script` is the script's
 * asset path; an inline script has none to hash. Resolves once written, and
 * never rejects.
 */
export async function recordRunProvenance(ctx: ToolContext, runId: string, script: string | undefined): Promise<void> {
  try {
    const projectDir = ctx.project.projectDir;
    const dir = projectDir ? observationDir(projectDir, runId) : null;
    if (!projectDir || !dir) return;
    const [workspace, read, players] = await Promise.all([
      workspaceNow(projectDir),
      script && script !== "inline" ? ctx.bridge.call("pie_script_read", { path: script }).catch(() => null) : Promise.resolve(null),
      playersOf(ctx),
    ]);
    const hash = scriptHashOf(isObject(read) ? read : null);
    const engine = engineVersionOf(ctx);
    const record = {
      recorded_at: new Date().toISOString(),
      vcs: workspace.vcs,
      revision: workspace.revision,
      dirty: workspace.dirty,
      ...(hash ? { script_hash: hash.replace(/^sha256:/, "") } : {}),
      environment: {
        ...(engine ? { engine_version: engine } : {}),
        ...(players ?? {}),
      },
    };
    fs.mkdirSync(dir, { recursive: true });
    writeFileAtomic(path.join(dir, "provenance.json"), `${JSON.stringify(record, null, 2)}\n`);
  } catch {
    // A run whose provenance could not be written is still a run.
  }
}
