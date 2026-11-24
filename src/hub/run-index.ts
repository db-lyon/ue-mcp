// @session per-session: Keeps one project's run history in that project's data folder.
/**
 * The hub's run history, per machine, the local EvidenceStore: an append-only
 * log of RunRecords in time-ordered segments, <dataDir>/hub/runs/
 * runs-<seq>.jsonl, one record per line, appended as runs change and finish.
 * The newest line for a run wins. A line is never rewritten:
 *
 * - A segment that reaches SEGMENT_LINES is closed and the next line starts
 *   a new one.
 * - When more than MAX_RUNS runs are held, the oldest closed segment is
 *   archived, moved whole into runs/archive/. Its runs leave the history,
 *   except a run someone linked to a tracker item, whose record is appended
 *   to the open segment first.
 * - A line cut short by a crash is skipped.
 * - runs.jsonl, the single file an earlier Studio kept, is moved in as the
 *   first segment.
 *
 * Moved scripts (aliases) are kept beside the segments, in aliases.json,
 * written whole and atomically: { "<from check id>": "<to check id>" }.
 *
 * The broker's own runs are recorded as the QaBroker saves them. Runs ue-mcp
 * has a record of (pie(script_runs), or results.json without it) are
 * backfilled, so runs started outside the broker, or before this daemon, are history too; they
 * carry no provenance, since nobody read the workspace when they started.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import {
  checkId,
  outcomeOf,
  qaEnded,
  qaIsDraft,
  qaScriptKey,
  type QaRun,
  type RunOutcome,
  type RunProvenance,
  type RunRecord,
  type RunSource,
  type RunTrigger,
  type TrackerLink,
} from "./contract.js";
import { writeFileAtomic } from "./atomic-write.js";
import type { EvidenceStore, RunTag } from "./evidence-store.js";
import { readLinks } from "./util.js";

export type { RunTag } from "./evidence-store.js";

/** The most runs kept before the oldest segment is archived. */
export const MAX_RUNS = 5000;
/** Lines a segment holds before the next starts. */
export const SEGMENT_LINES = 1000;
const OUTCOMES: readonly RunOutcome[] = ["passed", "failed", "needs_you", "running", "incomplete", "unjudged", "stopped", "error"];
const SOURCES: readonly RunSource[] = ["person", "agent", "plan", "outside"];
const TRIGGERS: readonly RunTrigger[] = ["run", "replay", "loop"];
const SEGMENT = /^runs-(\d{6})\.jsonl$/;
const segmentName = (seq: number): string => `runs-${String(seq).padStart(6, "0")}.jsonl`;

/** What started a run, as far as its QaRun tells: no session is outside any client; a subagent or tool call is an agent; a card's button is a person. */
export function sourceOf(run: QaRun): RunSource {
  if (!run.sessionId) return "outside";
  if (run.subagent || run.assignment || run.toolUseId) return "agent";
  return "person";
}

/**
 * Why a run happened. A replay says so by replayOf. A fix loop's attempt
 * carries the loop's assignment when it is first seen; a run that is
 * assigned to a loop later (the failure handed to it) stays a plain run.
 */
function triggerOf(run: QaRun, prev: RunRecord | undefined): RunTrigger {
  if (run.replayOf) return "replay";
  if (prev) return prev.trigger ?? "run";
  return run.assignment ? "loop" : "run";
}

/**
 * A run's record from its QaRun, keeping what the earlier record already
 * settled (when it started, when it ended, who started it, why, and what it
 * tested). Null for what is no run of a check: a draft, inline steps, a
 * manual run (it judges the manual steps alone, the full run holds the
 * evidence), an idle answer.
 */
export function recordOf(run: QaRun, o: { prev?: RunRecord; times?: { startedAt?: string; endedAt?: string }; tag?: RunTag; now: string }): RunRecord | null {
  if (qaIsDraft(run) || run.script === "inline" || run.manual || run.state === "idle") return null;
  const prev = o.prev;
  const outcome = outcomeOf(run);
  const ended = qaEnded(run.state);
  const startedAt = prev?.startedAt ?? o.times?.startedAt ?? run.seenAt;
  const endedAt = ended ? prev?.endedAt ?? o.times?.endedAt ?? o.now : null;
  const ms = endedAt ? Date.parse(endedAt) - Date.parse(startedAt) : NaN;
  // A plan's run stays the plan's; otherwise the better-informed QaRun says.
  const source = o.tag?.source ?? (prev?.source === "plan" ? prev.source : sourceOf(run));
  const failedSteps = run.steps.filter((s) => s.status === "failed").map((s) => s.index);
  return {
    runId: run.runId,
    kind: "script",
    target: qaScriptKey(run.script),
    name: run.scriptName,
    outcome,
    startedAt,
    endedAt,
    durationMs: Number.isFinite(ms) && ms >= 0 ? ms : null,
    summary: run.summary,
    ...(failedSteps.length ? { failedSteps } : {}),
    source,
    trigger: triggerOf(run, prev),
    sessionId: run.sessionId,
    planRunId: o.tag?.planRunId ?? prev?.planRunId ?? null,
    replayOf: run.replayOf ?? null,
    recordingId: run.recordingId ?? null,
    map: run.map ?? null,
    // Provenance and links are the hub's, never the QaRun's: a run changing keeps them.
    ...(prev?.provenance ? { provenance: prev.provenance } : {}),
    ...(prev?.tainted ? { tainted: true } : {}),
    ...(prev?.links?.length ? { links: prev.links } : {}),
  };
}

/** A record read from the file, or null when the line is not one. A field that does not read is dropped, never guessed. */
function recordFrom(v: unknown): RunRecord | null {
  if (!v || typeof v !== "object") return null;
  const r = v as Partial<RunRecord>;
  if (typeof r.runId !== "string" || !r.runId || typeof r.target !== "string" || typeof r.name !== "string" || typeof r.startedAt !== "string") return null;
  // Schedules are gone: a schedule's run was a plan's.
  const source = (r.source as string) === "schedule" ? "plan" : r.source;
  if (r.kind !== "script" || !OUTCOMES.includes(r.outcome as RunOutcome) || !SOURCES.includes(source as RunSource)) return null;
  const { links: rawLinks, trigger, provenance, ...rest } = r as RunRecord;
  const links = rawLinks === undefined ? [] : readLinks(rawLinks).links;
  return {
    ...rest,
    source: source as RunSource,
    ...(TRIGGERS.includes(trigger as RunTrigger) ? { trigger } : {}),
    ...(provenance && typeof provenance === "object" && !Array.isArray(provenance) ? { provenance } : {}),
    ...(links.length ? { links } : {}),
  };
}

/** Newest first: by start, then by id, which carries the start time too. */
const newest = (a: RunRecord, b: RunRecord): number => (a.startedAt < b.startedAt ? 1 : a.startedAt > b.startedAt ? -1 : a.runId < b.runId ? 1 : a.runId > b.runId ? -1 : 0);

export interface RunIndexOptions {
  dataDir: string;
  max?: number;
  segmentLines?: number;
  now?: () => string;
  /** A record was added or changed. */
  changed?: (runId: string) => void;
  log?: { warn(message: string, detail?: unknown): void };
}

export class RunIndex implements EvidenceStore {
  readonly scope = "local" as const;
  private readonly runs = new Map<string, RunRecord>();
  /** The segment each run's newest line is in. */
  private readonly segmentOf = new Map<string, number>();
  /** Segments held, oldest first; the last is open. */
  private segments: number[] = [];
  /** Lines in the open segment. */
  private openLines = 0;
  /** Runs by check id, newest first; rebuilt when a record changes. */
  private byCheckCache: Map<string, RunRecord[]> | null = null;
  private sortedCache: RunRecord[] | null = null;
  /** Hub runs' tags, kept until their record takes them. */
  private readonly tags = new Map<string, RunTag>();
  private readonly moved = new Map<string, string>();
  private readonly dir: string;
  private readonly max: number;
  private readonly segmentLines: number;
  private readonly now: () => string;

  constructor(private readonly o: RunIndexOptions) {
    this.dir = path.join(o.dataDir, "hub", "runs");
    this.max = o.max ?? MAX_RUNS;
    this.segmentLines = o.segmentLines ?? SEGMENT_LINES;
    this.now = o.now ?? (() => new Date().toISOString());
    this.load();
    this.archiveOld();
  }

  private segmentFile(seq: number): string {
    return path.join(this.dir, segmentName(seq));
  }

  private load(): void {
    // The single file an earlier Studio kept becomes the first segment, moved, not rewritten.
    const legacy = path.join(this.o.dataDir, "hub", "runs.jsonl");
    try {
      if (fs.existsSync(legacy)) {
        fs.mkdirSync(this.dir, { recursive: true });
        if (!fs.existsSync(this.segmentFile(0))) fs.renameSync(legacy, this.segmentFile(0));
      }
    } catch (e) {
      this.o.log?.warn("hub: the earlier run history could not be moved in", e);
    }
    let names: string[] = [];
    try {
      names = fs.readdirSync(this.dir);
    } catch {
      // No history yet.
    }
    this.segments = names.map((n) => SEGMENT.exec(n)).filter((m): m is RegExpExecArray => !!m).map((m) => Number(m[1])).sort((a, b) => a - b);
    for (const seq of this.segments) {
      let text = "";
      try {
        text = fs.readFileSync(this.segmentFile(seq), "utf-8");
      } catch {
        continue;
      }
      let lines = 0;
      for (const line of text.split("\n")) {
        if (!line.trim()) continue;
        lines++;
        try {
          const r = recordFrom(JSON.parse(line));
          if (r) {
            this.runs.set(r.runId, r);
            this.segmentOf.set(r.runId, seq);
          }
        } catch {
          // A line cut short by a crash: the rest of the segment still counts.
        }
      }
      this.openLines = lines;
    }
    try {
      const saved = JSON.parse(fs.readFileSync(path.join(this.o.dataDir, "hub", "aliases.json"), "utf-8")) as unknown;
      if (saved && typeof saved === "object" && !Array.isArray(saved)) {
        for (const [from, to] of Object.entries(saved)) if (typeof to === "string") this.moved.set(from, to);
      }
    } catch {
      // No script has moved.
    }
  }

  /**
   * Past the cap, the oldest closed segments are archived whole. A linked run
   * whose newest line is in one is appended to the open segment first, so
   * it stays.
   */
  private archiveOld(): void {
    while (this.segments.length > 1 && this.runs.size > this.max) {
      const seq = this.segments[0];
      const kept: RunRecord[] = [];
      for (const [id, at] of this.segmentOf) {
        if (at !== seq) continue;
        const r = this.runs.get(id)!;
        if (r.links?.length) kept.push(r);
        else {
          this.runs.delete(id);
          this.segmentOf.delete(id);
        }
      }
      for (const r of kept) this.append(r);
      try {
        fs.mkdirSync(path.join(this.dir, "archive"), { recursive: true });
        fs.renameSync(this.segmentFile(seq), path.join(this.dir, "archive", segmentName(seq)));
      } catch (e) {
        this.o.log?.warn("hub: a full segment of the run history could not be archived", e);
      }
      this.segments.shift();
      this.dirty();
      // Only linked runs were left in it: archiving more would not bring the count down.
      if (kept.length && this.segments.length === 1) break;
    }
  }

  /** One line onto the open segment, starting a new one when it is full. */
  private append(r: RunRecord): void {
    if (this.segments.length === 0 || this.openLines >= this.segmentLines) {
      this.segments.push(this.segments.length ? this.segments[this.segments.length - 1] + 1 : 1);
      this.openLines = 0;
    }
    const seq = this.segments[this.segments.length - 1];
    try {
      fs.mkdirSync(this.dir, { recursive: true });
      fs.appendFileSync(this.segmentFile(seq), `${JSON.stringify(r)}\n`);
      this.openLines++;
      this.segmentOf.set(r.runId, seq);
    } catch (e) {
      this.o.log?.warn("hub: a run could not be added to the history", e);
    }
  }

  private dirty(): void {
    this.byCheckCache = null;
    this.sortedCache = null;
  }

  /** Keep a record; a change is appended to the log and announced. Returns whether anything changed. */
  put(r: RunRecord): boolean {
    const had = this.runs.get(r.runId);
    if (had && JSON.stringify(had) === JSON.stringify(r)) return false;
    this.runs.set(r.runId, r);
    this.dirty();
    const before = this.segments.length;
    this.append(r);
    if (this.segments.length > before) this.archiveOld();
    this.o.changed?.(r.runId);
    return true;
  }

  alias(from: string, to: string): void {
    if (from === to || this.moved.get(from) === to) return;
    this.moved.set(from, to);
    this.dirty();
    try {
      fs.mkdirSync(path.join(this.o.dataDir, "hub"), { recursive: true });
      writeFileAtomic(path.join(this.o.dataDir, "hub", "aliases.json"), `${JSON.stringify(Object.fromEntries(this.moved), null, 2)}\n`);
    } catch (e) {
      this.o.log?.warn("hub: a moved script could not be recorded", e);
    }
  }

  aliases(): ReadonlyMap<string, string> {
    return this.moved;
  }

  /** What a run tested. Returns the record, or undefined for a run not held. */
  setProvenance(runId: string, provenance: RunProvenance): RunRecord | undefined {
    const had = this.runs.get(runId);
    if (!had) return undefined;
    this.put({ ...had, provenance });
    return this.runs.get(runId);
  }

  /** Project files changed while the run played. Returns the record, or undefined for a run not held. */
  setTainted(runId: string): RunRecord | undefined {
    const had = this.runs.get(runId);
    if (!had) return undefined;
    if (!had.tainted) this.put({ ...had, tainted: true });
    return this.runs.get(runId);
  }

  /** A run's links replaced; an empty list removes them. Returns the record, or undefined for a run not held. */
  setLinks(runId: string, links: TrackerLink[]): RunRecord | undefined {
    const had = this.runs.get(runId);
    if (!had) return undefined;
    const { links: _old, ...rest } = had;
    this.put(links.length ? { ...rest, links } : rest);
    return this.runs.get(runId);
  }

  /** Mark a run the hub started as a plan's, now or when its record is first made. */
  tag(runId: string, tag: RunTag): void {
    this.tags.set(runId, tag);
    const had = this.runs.get(runId);
    if (had && (had.source !== tag.source || had.planRunId !== tag.planRunId)) this.put({ ...had, source: tag.source, planRunId: tag.planRunId });
  }

  /** A run as the hub has it now. */
  record(run: QaRun, times?: { startedAt?: string; endedAt?: string }): RunRecord | null {
    const r = recordOf(run, { prev: this.runs.get(run.runId), times, tag: this.tags.get(run.runId), now: this.now() });
    if (!r) return null;
    this.put(r);
    // The record keeps its source and plan from here on, so the tag is not kept for ever.
    this.tags.delete(r.runId);
    return this.runs.get(r.runId) ?? r;
  }

  /** A run ue-mcp recorded, kept unless the hub already holds a finished record of it. */
  backfill(run: QaRun, times: { startedAt?: string; endedAt?: string }): void {
    const had = this.runs.get(run.runId);
    if (had && had.outcome !== "running" && had.outcome !== "needs_you") return;
    // A record from results.json that says nothing new about an open run is left as the hub has it.
    if (had && !qaEnded(run.state)) return;
    this.record(run, times);
  }

  get(runId: string): RunRecord | undefined {
    return this.runs.get(runId);
  }

  has(runId: string): boolean {
    return this.runs.has(runId);
  }

  size(): number {
    return this.runs.size;
  }

  /** Every run, newest first. */
  all(): RunRecord[] {
    if (!this.sortedCache) this.sortedCache = [...this.runs.values()].sort(newest);
    return this.sortedCache;
  }

  /** One check's runs, newest first, with the runs of every check that moved to it, at any remove. */
  byCheck(id: string): RunRecord[] {
    const own = this.ownRuns();
    const from = [...this.moved].filter(([, to]) => to === id).map(([f]) => f);
    if (from.length === 0) return own.get(id) ?? [];
    const seen = new Set<string>([id]);
    const lists = [own.get(id) ?? []];
    for (let f = from.pop(); f !== undefined; f = from.pop()) {
      if (seen.has(f)) continue;
      seen.add(f);
      lists.push(own.get(f) ?? []);
      for (const [g, to] of this.moved) if (to === f) from.push(g);
    }
    return lists.flat().sort(newest);
  }

  private ownRuns(): Map<string, RunRecord[]> {
    if (!this.byCheckCache) {
      const m = new Map<string, RunRecord[]>();
      for (const r of this.all()) {
        const k = checkId(r);
        const list = m.get(k);
        if (list) list.push(r);
        else m.set(k, [r]);
      }
      this.byCheckCache = m;
    }
    return this.byCheckCache;
  }

  /** Every check id that has runs of its own. */
  checkIds(): string[] {
    return [...this.ownRuns().keys()];
  }

  /**
   * A page of runs, newest first: of one check, or of all. `before` is a run
   * id (runs after it in the list) or an ISO time (runs that started earlier).
   */
  page(o: { check?: string | null; limit: number; before?: string | null }): { runs: RunRecord[]; more: boolean } {
    const list = o.check ? this.byCheck(o.check) : this.all();
    let start = 0;
    if (o.before) {
      const at = list.findIndex((r) => r.runId === o.before);
      start = at >= 0 ? at + 1 : list.findIndex((r) => r.startedAt < o.before!);
      if (start < 0) start = list.length;
    }
    const runs = list.slice(start, start + o.limit);
    return { runs, more: start + o.limit < list.length };
  }
}
