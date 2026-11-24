// @session per-session: Reads one project's observation files, found from the project directory it is handed.
/**
 * What the run screen draws beside the steps: the values the observer
 * tracked, read from the run's observation.csv and sampled down for charts,
 * and the run as results.json records it when no client holds a QaRun of it.
 * Both are read only from under the project's Saved/MCPObservations.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { qaRunFromAnswer, type QaRun } from "./contract.js";

/** The most points a series is drawn with. */
export const MAX_POINTS = 300;
/** A csv larger than this is not read on a request. */
const MAX_CSV_BYTES = 64 * 1024 * 1024;
/** Run ids are file names ue-mcp made: letters, digits, dashes and underscores. */
const RUN_ID = /^[A-Za-z0-9_-]{1,200}$/;

/** The run's folder under Saved/MCPObservations, or null for an id that could step outside it. */
export function runDir(projectDir: string, runId: string): string | null {
  return RUN_ID.test(runId) ? path.join(projectDir, "Saved", "MCPObservations", runId) : null;
}

/** One csv line's fields, quotes honoured. */
function fields(line: string): string[] {
  const out: string[] = [];
  let cur = "";
  let quoted = false;
  for (let i = 0; i < line.length; i++) {
    const ch = line[i];
    if (quoted) {
      if (ch === '"' && line[i + 1] === '"') {
        cur += '"';
        i++;
      } else if (ch === '"') {
        quoted = false;
      } else {
        cur += ch;
      }
    } else if (ch === '"') {
      quoted = true;
    } else if (ch === ",") {
      out.push(cur);
      cur = "";
    } else {
      cur += ch;
    }
  }
  out.push(cur);
  return out;
}

/** Every `step`-th point and the last, so the chart keeps the run's end. */
export function sample(points: Array<[number, number]>, max = MAX_POINTS): Array<[number, number]> {
  if (points.length <= max) return points;
  const step = (points.length - 1) / (max - 1);
  const out: Array<[number, number]> = [];
  for (let k = 0; k < max; k++) out.push(points[Math.round(k * step)]);
  return out;
}

/**
 * The tracked values of an observation.csv: lines starting with # are notes,
 * the first other line names the columns, `time` is game seconds. Series are
 * the tracked values (`t:<path>`, named by their path) and the observer's own
 * speed2d, game_ms and gpu_ms, on milliseconds from the first frame.
 */
export function seriesOf(csv: string, max = MAX_POINTS): Record<string, Array<[number, number]>> {
  const lines = csv.split(/\r?\n/);
  const head = lines.findIndex((l) => l.trim() && !l.startsWith("#"));
  if (head < 0) return {};
  const cols = fields(lines[head]).map((c) => c.trim());
  const time = cols.indexOf("time");
  const frame = cols.indexOf("frame");
  const wanted = cols.flatMap((c, i) => (c.startsWith("t:") || c === "speed2d" || c === "game_ms" || c === "gpu_ms" ? [{ i, name: c.startsWith("t:") ? c.slice(2) : c }] : []));
  if (wanted.length === 0) return {};
  const points = new Map<string, Array<[number, number]>>(wanted.map((w) => [w.name, []]));
  let t0: number | null = null;
  for (let n = head + 1; n < lines.length; n++) {
    const line = lines[n];
    if (!line.trim() || line.startsWith("#")) continue;
    const f = fields(line);
    const raw = time >= 0 ? Number(f[time]) : frame >= 0 ? Number(f[frame]) : n - head - 1;
    if (!Number.isFinite(raw)) continue;
    // Seconds of game time; without a time column, frames stand in at 60 a second.
    const ms = time >= 0 ? raw * 1000 : (raw * 1000) / 60;
    if (t0 === null) t0 = ms;
    for (const w of wanted) {
      const v = Number(f[w.i]);
      if (f[w.i] !== undefined && f[w.i].trim() !== "" && Number.isFinite(v)) points.get(w.name)!.push([Math.round((ms - t0) * 10) / 10, v]);
    }
  }
  const out: Record<string, Array<[number, number]>> = {};
  for (const [name, p] of points) if (p.length > 0) out[name] = sample(p, max);
  return out;
}

/** The run's observed series, or undefined when it has no observation.csv to read. */
export function readSeries(projectDir: string, runId: string): Record<string, Array<[number, number]>> | undefined {
  const dir = runDir(projectDir, runId);
  if (!dir) return undefined;
  const file = path.join(dir, "observation.csv");
  try {
    if (fs.statSync(file).size > MAX_CSV_BYTES) return undefined;
    const s = seriesOf(fs.readFileSync(file, "utf-8"));
    return Object.keys(s).length ? s : undefined;
  } catch {
    return undefined;
  }
}

/** The run's results.json as written, or null when there is none to read. */
export function readResultsFile(projectDir: string, runId: string): Record<string, unknown> | null {
  const dir = runDir(projectDir, runId);
  if (!dir) return null;
  try {
    const raw = JSON.parse(fs.readFileSync(path.join(dir, "results.json"), "utf-8")) as unknown;
    return raw && typeof raw === "object" && !Array.isArray(raw) ? (raw as Record<string, unknown>) : null;
  } catch {
    return null;
  }
}

/** What pie(script_run) recorded about the run when it started it (provenance.json), or null when there is none. */
export function readProvenanceFile(projectDir: string, runId: string): Record<string, unknown> | null {
  const dir = runDir(projectDir, runId);
  if (!dir) return null;
  try {
    const raw = JSON.parse(fs.readFileSync(path.join(dir, "provenance.json"), "utf-8")) as unknown;
    return raw && typeof raw === "object" && !Array.isArray(raw) ? (raw as Record<string, unknown>) : null;
  } catch {
    return null;
  }
}

/** The run as its results.json records it, for a run no client holds a QaRun of. */
export function readResults(projectDir: string, runId: string): QaRun | null {
  const raw = readResultsFile(projectDir, runId);
  return raw ? qaRunFromAnswer(raw, { seenAt: typeof raw.started_at === "string" ? raw.started_at : new Date(0).toISOString() }) : null;
}
