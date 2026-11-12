/**
 * editor(summarize_csv_profile): read a CSV profiler capture on the server side.
 *
 * editor(start_csv_profile) / editor(stop_csv_profile) record a capture and
 * report the file it landed in. A capture is one row per frame and one column
 * per stat, often hundreds of columns over thousands of frames, which is far
 * too much to hand an agent raw. This reduces it to what a profiling pass
 * reads first: frame count, per-stat average, percentiles and peak (with the
 * frame the peak happened on), the frame-time hitches, the events the capture
 * recorded, and the metadata block.
 *
 * It runs without the editor: the file is read from disk, so a capture taken
 * earlier, on another machine, or by `csvprofile start` at the console reads
 * the same way.
 *
 * The file format, as FCsvStreamWriter writes it (Core/Private/ProfilingDebugging
 * /CsvProfiler.cpp): a header row naming every column, one of them EVENTS; one
 * row per frame; then, when the capture finished cleanly, the header row again
 * and a metadata row of [Key],Value pairs whose first key is HasHeaderRowAtEnd
 * and whose last is Commandline (the only value that may contain commas). The
 * EVENTS cell holds the frame's events separated by semicolons. A forced
 * compression setting writes the same text gzipped as .csv.gz.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { gunzipSync } from "node:zlib";
import type { ToolContext } from "../core/types.js";

export interface CsvStatSummary {
  name: string;
  samples: number;
  avg: number;
  min: number;
  max: number;
  p50: number;
  p95: number;
  p99: number;
  /** Zero-based index of the data row holding the maximum. */
  peakFrame: number;
}

export interface ParsedCsvProfile {
  header: string[];
  rows: string[][];
  metadata: Record<string, string>;
  /** False when the file has no trailing metadata row: a capture still being written, or one cut short. */
  complete: boolean;
}

export interface CsvProfileSummaryOptions {
  /** Case-insensitive substrings; a stat is kept when its name contains any. */
  stats?: string[];
  /** Stat rows returned (default 50). */
  maxStats?: number;
  /** Event rows returned (default 50). */
  maxEvents?: number;
  /** Order of the stat rows (default avg). */
  sortBy?: "avg" | "max" | "p95" | "name";
  /** A FrameTime sample at or over this is a hitch (default 50 ms). */
  hitchThresholdMs?: number;
}

export interface CsvProfileSummary {
  frameCount: number;
  complete: boolean;
  statCount: number;
  matchedStatCount: number;
  stats: CsvStatSummary[];
  statsTruncated: boolean;
  frameTime: (CsvStatSummary & { hitchThresholdMs: number; hitchCount: number; hitchFrames: number[]; avgFps: number }) | null;
  events: { count: number; list: Array<{ frame: number; text: string }>; truncated: boolean };
  metadata: Record<string, string>;
}

const METADATA_KEY = /^\[(.+)\]$/;

/** Split one CSV line. The profiler sanitises commas out of names and events, so no quoting is needed. */
function cells(line: string): string[] {
  return line.split(",").map((c) => c.trim());
}

function parseMetadata(line: string): Record<string, string> {
  const tokens = line.split(",");
  const out: Record<string, string> = {};
  let i = 0;
  while (i < tokens.length) {
    const key = METADATA_KEY.exec(tokens[i].trim());
    if (!key) { i++; continue; }
    const name = key[1];
    if (name.toLowerCase() === "commandline") {
      // Written last on purpose: its value is the rest of the line, commas included.
      out[name] = tokens.slice(i + 1).join(",").trim();
      break;
    }
    let j = i + 1;
    const value: string[] = [];
    while (j < tokens.length && !METADATA_KEY.test(tokens[j].trim())) value.push(tokens[j++]);
    out[name] = value.join(",").trim();
    i = j;
  }
  return out;
}

export function parseCsvProfile(text: string): ParsedCsvProfile {
  const lines = text.split(/\r?\n/).filter((l) => l.trim() !== "");
  if (lines.length === 0) throw new Error("The CSV file is empty.");
  const header = cells(lines[0]);
  let body = lines.slice(1);
  let metadata: Record<string, string> = {};
  let complete = false;

  const last = body.length > 0 ? body[body.length - 1] : "";
  if (last.trimStart().startsWith("[")) {
    metadata = parseMetadata(last);
    complete = true;
    body = body.slice(0, -1);
    // The summary header row written before the metadata, when the file says it has one.
    const tail = body.length > 0 ? cells(body[body.length - 1]) : [];
    if (tail.length > 0 && tail[0] === header[0] && tail.join(",") === header.join(",")) body = body.slice(0, -1);
  }
  return { header, rows: body.map(cells), metadata, complete };
}

function percentile(sorted: number[], p: number): number {
  if (sorted.length === 0) return 0;
  // Nearest rank: every value reported is a sample that occurred.
  const rank = Math.min(sorted.length - 1, Math.max(0, Math.ceil(p * sorted.length) - 1));
  return sorted[rank];
}

function summariseColumn(name: string, values: Array<number | null>): CsvStatSummary | null {
  const present: number[] = [];
  let peakFrame = -1;
  let max = -Infinity;
  values.forEach((v, frame) => {
    if (v === null) return;
    present.push(v);
    if (v > max) { max = v; peakFrame = frame; }
  });
  if (present.length === 0) return null;
  const sorted = [...present].sort((a, b) => a - b);
  const sum = present.reduce((a, b) => a + b, 0);
  return {
    name,
    samples: present.length,
    avg: sum / present.length,
    min: sorted[0],
    max: sorted[sorted.length - 1],
    p50: percentile(sorted, 0.5),
    p95: percentile(sorted, 0.95),
    p99: percentile(sorted, 0.99),
    peakFrame,
  };
}

const round = (n: number): number => Math.round(n * 1000) / 1000;
const rounded = <T extends CsvStatSummary>(s: T): T => ({
  ...s,
  avg: round(s.avg), min: round(s.min), max: round(s.max), p50: round(s.p50), p95: round(s.p95), p99: round(s.p99),
});

export function summarizeCsvProfile(text: string, options: CsvProfileSummaryOptions = {}): CsvProfileSummary {
  const parsed = parseCsvProfile(text);
  const maxStats = Math.max(0, Math.floor(options.maxStats ?? 50));
  const maxEvents = Math.max(0, Math.floor(options.maxEvents ?? 50));
  const hitchThresholdMs = options.hitchThresholdMs && options.hitchThresholdMs > 0 ? options.hitchThresholdMs : 50;
  const filters = (options.stats ?? []).map((s) => s.toLowerCase()).filter((s) => s !== "");
  const sortBy = options.sortBy ?? "avg";

  const eventsColumn = parsed.header.findIndex((h) => h.toUpperCase() === "EVENTS");
  const columnValues = (index: number): Array<number | null> => parsed.rows.map((row) => {
    const raw = row[index];
    if (raw === undefined || raw === "") return null;
    const n = Number(raw);
    return Number.isFinite(n) ? n : null;
  });

  const all: CsvStatSummary[] = [];
  parsed.header.forEach((name, index) => {
    if (index === eventsColumn || name === "") return;
    const summary = summariseColumn(name, columnValues(index));
    if (summary) all.push(summary);
  });

  const matched = filters.length === 0 ? all : all.filter((s) => filters.some((f) => s.name.toLowerCase().includes(f)));
  const ordered = [...matched].sort((a, b) => {
    if (sortBy === "name") return a.name.localeCompare(b.name);
    const key = sortBy === "max" ? "max" : sortBy === "p95" ? "p95" : "avg";
    return b[key] - a[key] || a.name.localeCompare(b.name);
  });

  let frameTime: CsvProfileSummary["frameTime"] = null;
  const ft = all.find((s) => s.name.toLowerCase() === "frametime");
  if (ft) {
    const index = parsed.header.findIndex((h) => h.toLowerCase() === "frametime");
    const hitchFrames: number[] = [];
    columnValues(index).forEach((v, frame) => { if (v !== null && v >= hitchThresholdMs) hitchFrames.push(frame); });
    frameTime = {
      ...rounded(ft),
      hitchThresholdMs,
      hitchCount: hitchFrames.length,
      hitchFrames: hitchFrames.slice(0, maxEvents),
      avgFps: ft.avg > 0 ? round(1000 / ft.avg) : 0,
    };
  }

  const eventList: Array<{ frame: number; text: string }> = [];
  let eventCount = 0;
  if (eventsColumn >= 0) {
    parsed.rows.forEach((row, frame) => {
      const cell = row[eventsColumn] ?? "";
      for (const text of cell.split(";").map((e) => e.trim()).filter((e) => e !== "")) {
        eventCount++;
        if (eventList.length < maxEvents) eventList.push({ frame, text });
      }
    });
  }

  return {
    frameCount: parsed.rows.length,
    complete: parsed.complete,
    statCount: all.length,
    matchedStatCount: matched.length,
    stats: ordered.slice(0, maxStats).map(rounded),
    statsTruncated: ordered.length > maxStats,
    frameTime,
    events: { count: eventCount, list: eventList, truncated: eventList.length < eventCount },
    metadata: parsed.metadata,
  };
}

/** Read a capture from disk, gunzipping a .csv.gz. */
export function readCsvProfileText(file: string): string {
  const raw = fs.readFileSync(file);
  return file.toLowerCase().endsWith(".gz") ? gunzipSync(raw).toString("utf8") : raw.toString("utf8");
}

/** The newest .csv or .csv.gz in a folder, or null. */
export function newestCsvProfile(dir: string): string | null {
  let best: { file: string; mtime: number } | null = null;
  let entries: string[];
  try { entries = fs.readdirSync(dir); } catch { return null; }
  for (const name of entries) {
    const lower = name.toLowerCase();
    if (!lower.endsWith(".csv") && !lower.endsWith(".csv.gz")) continue;
    const file = path.join(dir, name);
    let mtime: number;
    try { mtime = fs.statSync(file).mtimeMs; } catch { continue; }
    if (!best || mtime > best.mtime) best = { file, mtime };
  }
  return best ? best.file : null;
}

const SORT_KEYS = new Set(["avg", "max", "p95", "name"]);

export async function summarizeCsvProfileAction(ctx: ToolContext, p: Record<string, unknown>): Promise<Record<string, unknown>> {
  const projectDir = ctx.project?.projectPath ? path.dirname(ctx.project.projectPath) : null;
  const csvDir = projectDir ? path.join(projectDir, "Saved", "Profiling", "CSV") : null;

  let file = typeof p.csvPath === "string" ? p.csvPath.trim() : "";
  if (file === "") {
    const newest = csvDir ? newestCsvProfile(csvDir) : null;
    if (!newest) {
      return {
        success: false,
        error: csvDir
          ? `No csvPath was given and no .csv capture is in ${csvDir}. Record one with editor(start_csv_profile) and editor(stop_csv_profile), or pass csvPath.`
          : "No csvPath was given and no project is loaded to look for captures in. Pass csvPath.",
      };
    }
    file = newest;
  } else if (!path.isAbsolute(file)) {
    const candidates = [csvDir, projectDir].filter((d): d is string => d !== null).map((d) => path.join(d, file));
    file = candidates.find((c) => fs.existsSync(c)) ?? path.resolve(file);
  }
  if (!fs.existsSync(file)) return { success: false, error: `CSV capture not found: ${file}` };

  const sortBy = typeof p.sortBy === "string" ? p.sortBy : "avg";
  if (!SORT_KEYS.has(sortBy)) return { success: false, error: `Unknown sortBy '${sortBy}'. Use avg, max, p95 or name.` };
  const stats = Array.isArray(p.stats) ? p.stats.filter((s): s is string => typeof s === "string")
    : typeof p.stats === "string" ? p.stats.split(",") : undefined;

  let summary: CsvProfileSummary;
  try {
    summary = summarizeCsvProfile(readCsvProfileText(file), {
      stats,
      maxStats: typeof p.maxStats === "number" ? p.maxStats : undefined,
      maxEvents: typeof p.maxEvents === "number" ? p.maxEvents : undefined,
      sortBy: sortBy as CsvProfileSummaryOptions["sortBy"],
      hitchThresholdMs: typeof p.hitchThresholdMs === "number" ? p.hitchThresholdMs : undefined,
    });
  } catch (e) {
    return { success: false, error: `Could not read ${file} as a CSV profile: ${e instanceof Error ? e.message : String(e)}` };
  }

  const out: Record<string, unknown> = { success: true, csvPath: file, csvBytes: fs.statSync(file).size, ...summary };
  if (!summary.complete) {
    out.warning = "The file has no trailing metadata row, so the capture was still being written or was cut short. Stop it with editor(stop_csv_profile) and read it again for the whole run.";
  }
  return out;
}
