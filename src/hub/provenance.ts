// @session per-session: Reads the workspace of the project directory it is handed and the results of that project's runs.
/**
 * What a run tested (RunProvenance): the workspace's source control state,
 * the script's version, the engine and the players, read when the hub first
 * sees a run running; and the fields recorded in the run's results.json
 * under `provenance` once it ends. Those are feature-checked by their
 * presence, and win over the hub's own reading.
 */
import * as crypto from "node:crypto";
import type { CurrentProvenance, QaRun, RunProvenance, RunVcs } from "./contract.js";
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

/** What Studio knows of a run as it starts: the workspace, the script's version, the engine and the players Studio set. */
export function provenanceAtStart(o: { workspace: CurrentProvenance; scriptHash: string | null; engine: string | null; run: Pick<QaRun, "pieSetup"> }): RunProvenance {
  return {
    vcs: o.workspace.vcs,
    revision: o.workspace.revision,
    dirty: o.workspace.dirty,
    scriptHash: o.scriptHash,
    engine: o.engine,
    players: o.run.pieSetup ? { count: o.run.pieSetup.players, netMode: o.run.pieSetup.netMode } : null,
  };
}

const NET_MODES = ["standalone", "listen", "client"] as const;
const isObj = (v: unknown): v is Record<string, unknown> => !!v && typeof v === "object" && !Array.isArray(v);

/**
 * The fields ue-mcp records in results.json under `provenance` (ue-mcp ask 4),
 * each only when present: script_hash, dependencies, and the environment's
 * engine_version, net_mode and clients.
 */
export function ueMcpProvenance(results: Record<string, unknown> | null, sidecar: Record<string, unknown> | null = null): Partial<RunProvenance> {
  // results.json's own record wins over provenance.json, which pie(script_run) writes beside it (src/pie/script-provenance.ts).
  const own = isObj(results?.provenance) ? (results!.provenance as Record<string, unknown>) : null;
  const p = own || sidecar ? { ...(sidecar ?? {}), ...(own ?? {}) } : null;
  if (!p) return {};
  const out: Partial<RunProvenance> = {};
  if (typeof p.vcs === "string" && p.vcs) out.vcs = p.vcs as RunVcs;
  if (typeof p.revision === "string" && p.revision) out.revision = p.revision;
  if (typeof p.dirty === "number") out.dirty = p.dirty;
  if (typeof p.script_hash === "string" && p.script_hash) out.scriptHash = `ue-mcp:${p.script_hash}`;
  if (isObj(p.dependencies)) {
    const deps = Object.fromEntries(Object.entries(p.dependencies).filter((e): e is [string, string] => typeof e[1] === "string"));
    if (Object.keys(deps).length) out.dependencies = deps;
  }
  const env = isObj(p.environment) ? p.environment : null;
  if (env && typeof env.engine_version === "string" && env.engine_version) out.engine = env.engine_version;
  const mode = NET_MODES.find((m) => m === env?.net_mode);
  if (mode && Number.isInteger(env!.clients) && (env!.clients as number) > 0) out.players = { count: env!.clients as number, netMode: mode };
  return out;
}

/** Provenance with ue-mcp's own fields laid over Studio's; a run Studio did not see start knows nothing else. */
export function withUeMcp(had: RunProvenance | undefined, ue: Partial<RunProvenance>): RunProvenance | undefined {
  if (Object.keys(ue).length === 0) return had;
  const base: RunProvenance = had ?? { vcs: null, revision: null, dirty: null, scriptHash: null, engine: null, players: null };
  return { ...base, ...ue };
}
