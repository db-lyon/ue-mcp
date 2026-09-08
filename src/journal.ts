/**
 * The workflow journal: what an agent did, what it produced, and how it ended.
 *
 * The flow engine solves the forward half of a repeatable operation - declare
 * a sequence, run it, roll it back. It keeps nothing afterwards. A session
 * that ends leaves the next one with the editor's state and no account of how
 * that state was arrived at, which is exactly the handover an unattended run
 * needs to leave behind.
 *
 * Two decisions shape this module.
 *
 * **Storage is user-scoped, not project-scoped.** It follows the convention
 * `~/.ue-mcp/state.json` established: files ue-mcp writes for itself, keyed by
 * absolute project root so one user can drive many projects without
 * collision, and never checked in alongside somebody's game. A journal under
 * `<project>/.ue-mcp/` would land in a colleague's diff and in source control
 * guards; a journal in memory would not survive the server restart that a
 * `npm run up:build` cycle performs several times an hour.
 *
 * **The file is append-only JSONL, and a run is a fold over its records.**
 * A note arrives long after the run started and an artifact later still, so
 * rewriting a run object on every mutation would mean a read-modify-write of
 * the whole file per call, and a crash mid-write would take the history with
 * it. Appending one line per event costs one `appendFileSync` and leaves a
 * partially written last line as the only possible damage, which the reader
 * skips. Deletion is the one operation that rewrites, and it is rare.
 *
 * An interrupted run therefore folds to `active` forever rather than
 * disappearing, which is information: `journal_list` reports it as a run that
 * never closed.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import * as os from "node:os";
import * as crypto from "node:crypto";

/** Schema version stamped on every record, so a later reader can migrate. */
export const JOURNAL_RECORD_VERSION = 1;

export type JournalRunStatus = "active" | "completed" | "failed" | "cancelled";

/** How a run ended. `active` is the absence of an end record, not a value. */
export type JournalEndStatus = Exclude<JournalRunStatus, "active">;

export interface JournalNote {
  at: number;
  text: string;
}

export interface JournalArtifact {
  at: number;
  /** Whatever the caller called the thing it produced: a content path
   *  (`/Game/Materials/PBR/M_Rock`), a file on disk, or a URL. */
  path: string;
  /** Free-form label for what kind of thing it is (`asset`, `screenshot`,
   *  `log`, `report`). Not an enum: the journal does not know what an agent
   *  will produce, and refusing an unlisted word would lose the record. */
  kind?: string;
  note?: string;
}

/** One step of an automatically journalled flow run. */
export interface JournalStep {
  stepNumber: number;
  name: string;
  type: string;
  success: boolean;
  skipped: boolean;
  durationMs: number;
  error?: string;
}

export interface JournalRun {
  runId: string;
  title: string;
  /** Set when this run was a `flow(action="run")`, absent when a caller
   *  opened it by hand. */
  flowName?: string;
  status: JournalRunStatus;
  tags: string[];
  startedAt: number;
  endedAt?: number;
  durationMs?: number;
  summary?: string;
  /** Why a cancelled run was cancelled. */
  reason?: string;
  notes: JournalNote[];
  artifacts: JournalArtifact[];
  steps?: JournalStep[];
  /** The project root this run was recorded against, when one was known. */
  project?: string;
}

type StartRecord = {
  v: number;
  e: "start";
  t: number;
  id: string;
  title: string;
  flowName?: string;
  tags?: string[];
  project?: string;
};
type NoteRecord = { v: number; e: "note"; t: number; id: string; text: string };
type ArtifactRecord = {
  v: number;
  e: "artifact";
  t: number;
  id: string;
  path: string;
  kind?: string;
  note?: string;
};
type EndRecord = {
  v: number;
  e: "end";
  t: number;
  id: string;
  status: JournalEndStatus;
  summary?: string;
  reason?: string;
  durationMs?: number;
  steps?: JournalStep[];
};

export type JournalRecord = StartRecord | NoteRecord | ArtifactRecord | EndRecord;

/* ── where it lives ────────────────────────────────────────────────── */

/**
 * The directory holding every project's journal.
 *
 * `UE_MCP_JOURNAL_DIR` redirects it, which is what the unit tests use and
 * what a CI runner that wants the journal as a build artifact would set.
 */
export function journalDir(): string {
  return (
    process.env.UE_MCP_JOURNAL_DIR ||
    path.join(os.homedir(), ".ue-mcp", "journal")
  );
}

/**
 * The journal file for one project root.
 *
 * Named `<basename>-<hash>.jsonl` rather than by a bare basename: two
 * checkouts of the same project on one machine are the common case, and a
 * bare `ue_mcp.jsonl` would merge their histories into one unreadable stream.
 * The hash is taken over the case-folded absolute path because Windows hands
 * back the same directory under two spellings.
 */
export function journalFile(projectRoot?: string | null): string {
  if (!projectRoot) return path.join(journalDir(), "unscoped.jsonl");
  const resolved = path.resolve(projectRoot);
  const hash = crypto
    .createHash("sha1")
    .update(resolved.toLowerCase())
    .digest("hex")
    .slice(0, 8);
  const slug = path.basename(resolved).replace(/[^A-Za-z0-9._-]/g, "_") || "project";
  return path.join(journalDir(), `${slug}-${hash}.jsonl`);
}

/**
 * Whether the journal records anything at all.
 *
 * On by default. An audit trail a user has to remember to switch on is not an
 * audit trail, and the cost is a few hundred bytes per run in the user's own
 * `~/.ue-mcp`. `UE_MCP_JOURNAL=0` switches it off for a machine or a session,
 * and every action reports the setting it is running under.
 */
export function journalEnabled(): boolean {
  return process.env.UE_MCP_JOURNAL !== "0";
}

/* ── reading ───────────────────────────────────────────────────────── */

function readRecords(file: string): JournalRecord[] {
  if (!fs.existsSync(file)) return [];
  const out: JournalRecord[] = [];
  for (const line of fs.readFileSync(file, "utf-8").split("\n")) {
    const trimmed = line.trim();
    if (trimmed.length === 0) continue;
    try {
      const parsed = JSON.parse(trimmed) as JournalRecord;
      if (parsed && typeof parsed === "object" && typeof parsed.id === "string") {
        out.push(parsed);
      }
    } catch {
      // A half-written final line is the only damage an append-only file can
      // take, and dropping it is the correct repair. Everything before it is
      // still a complete history.
    }
  }
  return out;
}

/** Fold the record stream into runs, newest start first. */
export function readRuns(file: string): JournalRun[] {
  const byId = new Map<string, JournalRun>();
  for (const rec of readRecords(file)) {
    if (rec.e === "start") {
      // A duplicate start for an id already present is ignored rather than
      // resetting the run: `journal_start` is idempotent on runId, and a
      // reader must agree with the writer about that.
      if (byId.has(rec.id)) continue;
      byId.set(rec.id, {
        runId: rec.id,
        title: rec.title,
        flowName: rec.flowName,
        status: "active",
        tags: rec.tags ?? [],
        startedAt: rec.t,
        notes: [],
        artifacts: [],
        project: rec.project,
      });
      continue;
    }
    const run = byId.get(rec.id);
    // A note for a run whose start record was deleted has nothing to attach
    // to. Dropping it is what makes delete a real delete.
    if (!run) continue;
    if (rec.e === "note") {
      run.notes.push({ at: rec.t, text: rec.text });
    } else if (rec.e === "artifact") {
      run.artifacts.push({ at: rec.t, path: rec.path, kind: rec.kind, note: rec.note });
    } else if (rec.e === "end") {
      run.status = rec.status;
      run.endedAt = rec.t;
      run.durationMs = rec.durationMs ?? rec.t - run.startedAt;
      run.summary = rec.summary;
      run.reason = rec.reason;
      if (rec.steps) run.steps = rec.steps;
    }
  }
  // Newest first, and DETERMINISTIC when two runs share a millisecond.
  //
  // Sorting on the timestamp alone left ties to whatever order the map
  // happened to yield, so two runs started in the same millisecond came back
  // in one order on a slow machine and the other on a fast one. The journal is
  // append-only, so its own file order is the true sequence: a later record is
  // the later run. Ties therefore break on position, reversed to match the
  // newest-first intent.
  const order = new Map([...byId.keys()].map((id, i) => [id, i]));
  return [...byId.values()].sort(
    (a, b) => b.startedAt - a.startedAt || order.get(b.runId)! - order.get(a.runId)!,
  );
}

/** One run by id, or undefined. */
export function readRun(file: string, runId: string): JournalRun | undefined {
  return readRuns(file).find((r) => r.runId === runId);
}

/**
 * The run a call means when it names none: the most recently started run that
 * has not ended. Concurrent runs are possible (two flows, two agents), so this
 * is a convenience, never an assumption - every mutating action accepts an
 * explicit runId and says so when it had to guess.
 */
export function activeRun(file: string): JournalRun | undefined {
  return readRuns(file).find((r) => r.status === "active");
}

/* ── writing ───────────────────────────────────────────────────────── */

function append(file: string, records: JournalRecord[]): void {
  const dir = path.dirname(file);
  if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
  const payload = records.map((r) => JSON.stringify(r)).join("\n") + "\n";
  fs.appendFileSync(file, payload, { mode: 0o600 });
}

let runCounter = 0;

/** A run id unique within this process and sortable by start time. */
export function newRunId(): string {
  runCounter++;
  return `j-${Date.now().toString(36)}-${runCounter.toString(36)}`;
}

export interface StartOptions {
  title: string;
  runId?: string;
  flowName?: string;
  tags?: string[];
  project?: string | null;
}

/**
 * Open a run. Idempotent on `runId`: starting an id that already exists
 * returns the run untouched and reports `existed`, so a retried call after a
 * timeout does not fork the history into two runs.
 */
export function startRun(
  file: string,
  opts: StartOptions,
): { run: JournalRun; existed: boolean } {
  const runId = opts.runId ?? newRunId();
  const already = readRun(file, runId);
  if (already) return { run: already, existed: true };
  const rec: StartRecord = {
    v: JOURNAL_RECORD_VERSION,
    e: "start",
    t: Date.now(),
    id: runId,
    title: opts.title,
    flowName: opts.flowName,
    tags: opts.tags && opts.tags.length > 0 ? opts.tags : undefined,
    project: opts.project ?? undefined,
  };
  append(file, [rec]);
  return {
    run: {
      runId,
      title: rec.title,
      flowName: rec.flowName,
      status: "active",
      tags: rec.tags ?? [],
      startedAt: rec.t,
      notes: [],
      artifacts: [],
      project: rec.project,
    },
    existed: false,
  };
}

/** Append a note to an open run. */
export function addNote(file: string, runId: string, text: string): JournalNote {
  const rec: NoteRecord = {
    v: JOURNAL_RECORD_VERSION,
    e: "note",
    t: Date.now(),
    id: runId,
    text,
  };
  append(file, [rec]);
  return { at: rec.t, text };
}

/**
 * Attach an artifact to a run. Idempotent on `path` within one run: attaching
 * the same path twice reports the existing entry rather than duplicating it,
 * because the common retry is "did that attach land?".
 */
export function addArtifact(
  file: string,
  runId: string,
  artifact: { path: string; kind?: string; note?: string },
): { artifact: JournalArtifact; existed: boolean } {
  const run = readRun(file, runId);
  const already = run?.artifacts.find((a) => a.path === artifact.path);
  if (already) return { artifact: already, existed: true };
  const rec: ArtifactRecord = {
    v: JOURNAL_RECORD_VERSION,
    e: "artifact",
    t: Date.now(),
    id: runId,
    path: artifact.path,
    kind: artifact.kind,
    note: artifact.note,
  };
  append(file, [rec]);
  return {
    artifact: { at: rec.t, path: artifact.path, kind: artifact.kind, note: artifact.note },
    existed: false,
  };
}

export interface EndOptions {
  status: JournalEndStatus;
  summary?: string;
  reason?: string;
  durationMs?: number;
  steps?: JournalStep[];
}

/**
 * Close a run. Idempotent: ending a run that already ended leaves the first
 * ending in place and reports `existed`, so the first verdict on a run is the
 * one that stands.
 */
export function endRun(
  file: string,
  runId: string,
  opts: EndOptions,
): { run: JournalRun; existed: boolean } | undefined {
  const run = readRun(file, runId);
  if (!run) return undefined;
  if (run.status !== "active") return { run, existed: true };
  const rec: EndRecord = {
    v: JOURNAL_RECORD_VERSION,
    e: "end",
    t: Date.now(),
    id: runId,
    status: opts.status,
    summary: opts.summary,
    reason: opts.reason,
    durationMs: opts.durationMs,
    steps: opts.steps,
  };
  append(file, [rec]);
  return {
    run: {
      ...run,
      status: rec.status,
      endedAt: rec.t,
      durationMs: rec.durationMs ?? rec.t - run.startedAt,
      summary: rec.summary,
      reason: rec.reason,
      steps: rec.steps ?? run.steps,
    },
    existed: false,
  };
}

/**
 * Drop one run, or every run, rewriting the file without those records.
 *
 * The only operation that does not append. It reports how many runs and how
 * many records went, because a delete that reports nothing cannot be told
 * apart from a delete that matched nothing.
 */
export function deleteRuns(
  file: string,
  match: { runId?: string; all?: boolean },
): { deletedRuns: string[]; deletedRecords: number; remainingRuns: number } {
  const records = readRecords(file);
  const before = readRuns(file);
  const doomed = new Set(
    match.all ? before.map((r) => r.runId) : before.filter((r) => r.runId === match.runId).map((r) => r.runId),
  );
  if (doomed.size === 0) {
    return { deletedRuns: [], deletedRecords: 0, remainingRuns: before.length };
  }
  const kept = records.filter((r) => !doomed.has(r.id));
  const dir = path.dirname(file);
  if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
  if (kept.length === 0) {
    // Nothing left: remove the file rather than leave an empty stub, matching
    // how `~/.ue-mcp/state.json` clears itself.
    if (fs.existsSync(file)) fs.unlinkSync(file);
  } else {
    fs.writeFileSync(file, kept.map((r) => JSON.stringify(r)).join("\n") + "\n", { mode: 0o600 });
  }
  return {
    deletedRuns: [...doomed],
    deletedRecords: records.length - kept.length,
    remainingRuns: before.length - doomed.size,
  };
}

/* ── querying ──────────────────────────────────────────────────────── */

export interface JournalFilter {
  status?: JournalRunStatus;
  flowName?: string;
  tag?: string;
  /** Epoch milliseconds; runs started strictly before this are dropped. */
  since?: number;
  /** Case-insensitive substring over the title, summary and note text. */
  contains?: string;
  limit?: number;
}

/** The filters `journal_list` accepts, applied in one place so the action and
 *  its tests cannot disagree about what a filter means. */
export function filterRuns(runs: JournalRun[], filter: JournalFilter): JournalRun[] {
  const needle = filter.contains?.toLowerCase();
  const matched = runs.filter((run) => {
    if (filter.status && run.status !== filter.status) return false;
    if (filter.flowName && run.flowName !== filter.flowName) return false;
    if (filter.tag && !run.tags.includes(filter.tag)) return false;
    if (filter.since !== undefined && run.startedAt < filter.since) return false;
    if (needle) {
      const hay = [run.title, run.summary ?? "", ...run.notes.map((n) => n.text)]
        .join("\n")
        .toLowerCase();
      if (!hay.includes(needle)) return false;
    }
    return true;
  });
  const limit = filter.limit ?? 20;
  return limit > 0 ? matched.slice(0, limit) : matched;
}

/** Parse a `since` parameter written as epoch milliseconds, an ISO date, or a
 *  relative age like `2h` / `7d` / `30m`. Returns undefined when unparseable,
 *  which the caller turns into an error naming the accepted spellings. */
export function parseSince(value: unknown, now = Date.now()): number | undefined {
  if (typeof value === "number" && Number.isFinite(value)) return value;
  if (typeof value !== "string" || value.trim().length === 0) return undefined;
  const text = value.trim();
  const relative = /^(\d+(?:\.\d+)?)\s*(m|h|d)$/i.exec(text);
  if (relative) {
    const scale = { m: 60_000, h: 3_600_000, d: 86_400_000 }[relative[2].toLowerCase() as "m" | "h" | "d"];
    return now - Number(relative[1]) * scale;
  }
  if (/^\d+$/.test(text)) return Number(text);
  const parsed = Date.parse(text);
  return Number.isNaN(parsed) ? undefined : parsed;
}

/** A run trimmed to a listing row: enough to choose one, small enough that
 *  twenty of them do not fill a context window. */
export function summariseRun(run: JournalRun): Record<string, unknown> {
  return {
    runId: run.runId,
    title: run.title,
    flowName: run.flowName,
    status: run.status,
    tags: run.tags.length > 0 ? run.tags : undefined,
    startedAt: new Date(run.startedAt).toISOString(),
    endedAt: run.endedAt ? new Date(run.endedAt).toISOString() : undefined,
    durationMs: run.durationMs,
    noteCount: run.notes.length,
    artifactCount: run.artifacts.length,
    stepCount: run.steps?.length,
    summary: run.summary,
    reason: run.reason,
  };
}

/** A run in full, with its notes and artifacts stamped as ISO times. */
export function detailRun(run: JournalRun): Record<string, unknown> {
  return {
    ...summariseRun(run),
    project: run.project,
    notes: run.notes.map((n) => ({ at: new Date(n.at).toISOString(), text: n.text })),
    artifacts: run.artifacts.map((a) => ({
      at: new Date(a.at).toISOString(),
      path: a.path,
      kind: a.kind,
      note: a.note,
    })),
    steps: run.steps,
  };
}
