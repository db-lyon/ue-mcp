// @session per-session: Reads the addressed project's Saved folder, nothing process-wide.
/**
 * Run history per playtest clip (#2097).
 *
 * Every clip run writes clip_results.json under Saved/MCPObservations/<run_id>/ when it ends. pie(clip_runs) reads
 * those files, newest first, optionally for one clip, and pie(clip_list) adds each clip's last run. Reading files on
 * disk, both work with no editor attached.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import type { ToolContext } from "../surface/tool-types.js";

type Body = Record<string, unknown>;

const DEFAULT_LIMIT = 50;

/** One run as clip_runs lists it: the run's results without per-frame data. */
export interface ClipRunEntry {
  run_id: string;
  clip: string;
  clip_name?: string;
  mode?: string;
  state?: string;
  passed?: boolean;
  stop_reason?: string;
  region?: string;
  started_at?: string;
  ended_at?: string;
  length?: number;
  game_ms?: number;
  held_ms?: number;
  summary?: Body;
  drift?: Body;
  checks?: Body[];
  regions?: Body[];
  results_path: string;
}

/** `/Game/A/CL_X`, `/Game/A/CL_X.CL_X` and `/Game/A/CL_X.CL_X_C` all name the package `/Game/A/CL_X`. */
export function clipPackage(p: string): string {
  const dot = p.lastIndexOf(".");
  return (dot > p.lastIndexOf("/") ? p.slice(0, dot) : p).toLowerCase();
}

const pick = (o: Body, keys: string[]): Body => Object.fromEntries(keys.filter((k) => o[k] !== undefined).map((k) => [k, o[k]]));

function entryOf(results: Body, resultsPath: string, runId: string): ClipRunEntry {
  const checks = Array.isArray(results.checks)
    ? (results.checks as Body[]).map((c) => pick(c, ["id", "mode", "status", "start", "length", "started_frame", "ended_frame", "hold"]))
    : undefined;
  const regions = Array.isArray(results.regions)
    ? (results.regions as Body[]).map((r) => pick(r, ["id", "name", "start", "end", "status", "drift"]))
    : undefined;
  return {
    run_id: typeof results.run_id === "string" && results.run_id ? results.run_id : runId,
    clip: typeof results.clip === "string" ? results.clip : "inline",
    ...(pick(results, ["clip_name", "mode", "state", "passed", "stop_reason", "region", "started_at", "ended_at", "length", "game_ms", "held_ms", "summary", "drift"]) as Partial<ClipRunEntry>),
    ...(checks ? { checks } : {}),
    ...(regions ? { regions } : {}),
    results_path: resultsPath,
  };
}

/** Every clip run under the project's Saved/MCPObservations, newest first. */
export function readClipRuns(projectDir: string, opts: { clip?: string; limit?: number } = {}): { runs: ClipRunEntry[]; total: number } {
  const root = path.join(projectDir, "Saved", "MCPObservations");
  let dirs: string[];
  try {
    dirs = fs.readdirSync(root, { withFileTypes: true }).filter((d) => d.isDirectory()).map((d) => d.name);
  } catch {
    return { runs: [], total: 0 };
  }
  const want = opts.clip ? clipPackage(opts.clip) : null;
  const all: Array<{ entry: ClipRunEntry; sortKey: string }> = [];
  for (const dir of dirs) {
    const file = path.join(root, dir, "clip_results.json");
    let results: Body;
    try {
      results = JSON.parse(fs.readFileSync(file, "utf8").replace(/^﻿/, "")) as Body;
    } catch {
      continue;
    }
    if (!results || typeof results !== "object") continue;
    const entry = entryOf(results, file.replace(/\\/g, "/"), dir);
    if (want && clipPackage(entry.clip) !== want) continue;
    let sortKey = entry.ended_at || entry.started_at || "";
    if (!sortKey) {
      try {
        sortKey = fs.statSync(file).mtime.toISOString();
      } catch {
        sortKey = "";
      }
    }
    all.push({ entry, sortKey });
  }
  all.sort((a, b) => (a.sortKey < b.sortKey ? 1 : a.sortKey > b.sortKey ? -1 : 0));
  const limit = opts.limit && opts.limit > 0 ? Math.floor(opts.limit) : DEFAULT_LIMIT;
  return { runs: all.slice(0, limit).map((r) => r.entry), total: all.length };
}

/** The handler of pie(clip_runs). */
export function clipRuns(ctx: ToolContext, params: Body): Body {
  const projectDir = ctx.project.projectDir;
  if (!projectDir) return { success: false, error: "No project is open, so there are no clip runs to read." };
  const clip = typeof params.path === "string" && params.path ? params.path : undefined;
  const { runs, total } = readClipRuns(projectDir, { clip, limit: typeof params.limit === "number" ? params.limit : undefined });
  return { success: true, ...(clip ? { clip } : {}), runs, count: runs.length, total };
}

/** The follow-up of pie(clip_list): each clip with its newest run as last_run, when it has one. */
export async function clipListFollowUp(ctx: ToolContext, _params: Body, answered: unknown): Promise<unknown> {
  const projectDir = ctx.project.projectDir;
  if (!projectDir || !answered || typeof answered !== "object") return answered;
  const body = answered as Body;
  if (body.success === false || !Array.isArray(body.clips)) return answered;
  const latest = new Map<string, ClipRunEntry>();
  for (const run of readClipRuns(projectDir, { limit: Number.MAX_SAFE_INTEGER }).runs) {
    const key = clipPackage(run.clip);
    if (!latest.has(key)) latest.set(key, run);
  }
  const clips = (body.clips as Body[]).map((c) => {
    const p = typeof c.object_path === "string" ? c.object_path : typeof c.path === "string" ? c.path : "";
    const run = p ? latest.get(clipPackage(p)) : undefined;
    if (!run) return c;
    const { checks: _c, regions: _r, ...last } = run;
    return { ...c, last_run: last };
  });
  return { ...body, clips };
}
