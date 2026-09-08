/**
 * The `flow(journal_*)` actions: writing and reading the workflow journal.
 *
 * Why these live on the flow tool rather than in a category of their own: a
 * flow is the forward half of a repeatable operation and the journal is the
 * record it leaves, so the two are one subject. `flow(action="run")` opens and
 * closes a journal run by itself, and these actions are what an agent uses for
 * the work it did NOT wrap in a flow, plus the reading half for both.
 *
 * The storage rules and the reasons for them are in `src/journal.ts`. What is
 * decided here is the shape of the surface:
 *
 *   - Every mutating action takes an optional `runId`. Omitted, it means the
 *     most recently started run that has not ended, and the response says
 *     which run it chose, because a journal that silently wrote to the wrong
 *     run is worse than one that refused.
 *   - Every mutating action is idempotent and says whether it changed
 *     anything, and every one names the call that undoes it.
 *   - Deletion granularity is the run, deliberately. Notes and artifacts
 *     accumulate; an audit trail whose individual entries can be edited away
 *     is not an audit trail. A correction is another note.
 */
import type { ActionSpec, ToolContext } from "../types.js";
import { McpError, ErrorCode } from "../errors.js";
import {
  activeRun,
  addArtifact,
  addNote,
  deleteRuns,
  detailRun,
  endRun,
  filterRuns,
  journalEnabled,
  journalFile,
  parseSince,
  readRun,
  readRuns,
  startRun,
  summariseRun,
  type JournalEndStatus,
  type JournalRunStatus,
} from "../journal.js";

const RUN_STATUSES: JournalRunStatus[] = ["active", "completed", "failed", "cancelled"];
const END_STATUSES: JournalEndStatus[] = ["completed", "failed", "cancelled"];

/**
 * The journal file this call is about: the addressed session's project.
 *
 * A journal with no project to key it would merge every projectless server on
 * the machine into one stream, which is a worse answer than refusing, so a
 * session without a project directory has no journal rather than a shared one.
 */
function fileFor(ctx: ToolContext): string {
  const projectDir = ctx.project.projectDir;
  if (!projectDir) {
    throw new McpError(
      ErrorCode.PROJECT_NOT_LOADED,
      "This session has no project directory, and the journal is keyed by project root, so there "
        + "is no journal to read or write. Start the server with a .uproject path, or address an "
        + 'editor that has one with the `editor` parameter (project(action="list_editors") lists them).',
    );
  }
  return journalFile(projectDir);
}

/**
 * Refuse every write while the journal is switched off, rather than accepting
 * it and dropping it. A call that reports success for a record nobody kept is
 * the failure this whole surface exists to avoid.
 */
function requireEnabled(): void {
  if (journalEnabled()) return;
  throw new McpError(
    ErrorCode.INVALID_PARAMS,
    "The workflow journal is disabled: UE_MCP_JOURNAL is set to '0' in this server's environment. "
      + "Unset it (or set it to '1') and restart the server to record runs. "
      + "Reading still works and reports whatever was recorded before it was disabled.",
  );
}

function requireString(params: Record<string, unknown>, name: string, hint: string): string {
  const value = params[name];
  if (typeof value === "string" && value.trim().length > 0) return value.trim();
  throw new McpError(
    ErrorCode.INVALID_PARAMS,
    `'${name}' is required and must be a non-empty string. ${hint}`,
  );
}

/** Resolve the run a write is about: the named one, or the open one. */
function resolveRun(
  file: string,
  params: Record<string, unknown>,
  verb: string,
): { runId: string; implicit: boolean } {
  const named = params.runId;
  if (typeof named === "string" && named.trim().length > 0) {
    const run = readRun(file, named.trim());
    if (!run) {
      const known = readRuns(file).slice(0, 10).map((r) => r.runId);
      throw new McpError(
        ErrorCode.NOT_FOUND,
        `No journal run '${named}' in ${file}. `
          + (known.length > 0
            ? `Recent runIds: ${known.join(", ")}. `
            : "This project has no journal runs yet. ")
          + `flow(action="journal_list") lists them, and flow(action="journal_start", title="...") opens one.`,
      );
    }
    return { runId: run.runId, implicit: false };
  }
  const open = activeRun(file);
  if (!open) {
    const recent = readRuns(file).slice(0, 5).map((r) => `${r.runId} (${r.status})`);
    throw new McpError(
      ErrorCode.NOT_FOUND,
      `No open journal run to ${verb}. Open one with `
        + `flow(action="journal_start", title="what you are about to do"), or name an existing `
        + `run with runId. `
        + (recent.length > 0 ? `Most recent runs: ${recent.join(", ")}.` : "This project has no runs yet."),
    );
  }
  return { runId: open.runId, implicit: true };
}

export const journalActions: Record<string, ActionSpec> = {
  journal_start: {
    kind: "handler",
    effect: "mutate",
    description:
      "Open a journal run: a named unit of work whose notes, artifacts and outcome are recorded "
      + "under ~/.ue-mcp/journal, keyed by this project, so it survives a server restart and never "
      + "lands in the project's own tree. Use it for work you are NOT wrapping in a flow; "
      + 'flow(action="run") opens and closes its own run. Idempotent on runId: starting an id that '
      + "already exists returns it untouched with existed=true. Params: title, runId? (supply one to "
      + "make a retry safe), tags? (string list, filterable later), flowName? (label this run as "
      + "belonging to a named flow). Returns the run plus the undo call.",
    handler: async (ctx, params) => journalStart(ctx, params),
  },
  journal_note: {
    kind: "handler",
    effect: "mutate",
    description:
      "Append a note to a journal run: what you decided, what surprised you, what the next session "
      + "needs to know. Notes accumulate and are never rewritten, so a correction is another note. "
      + "Params: text, runId? (defaults to the open run, and the response says which it chose). "
      + "Returns the note and the run it landed on.",
    handler: async (ctx, params) => journalNote(ctx, params),
  },
  journal_attach: {
    kind: "handler",
    effect: "mutate",
    description:
      "Record something a run produced: a content path, a file on disk, a URL. Idempotent per run on "
      + "artifactPath, so re-attaching the same path reports existed=true instead of duplicating it. "
      + "Params: artifactPath, artifactKind? (free-form label such as asset, screenshot, log, report), "
      + "note?, runId? (defaults to the open run). Returns the artifact and the run it landed on.",
    handler: async (ctx, params) => journalAttach(ctx, params),
  },
  journal_finish: {
    kind: "handler",
    effect: "mutate",
    description:
      "Close a journal run with a verdict. Idempotent: a run that already ended keeps its first "
      + "verdict and reports existed=true, so the first answer about a run is the one that stands. "
      + "Params: status? (completed|failed, default completed), summary? (one or two sentences the "
      + "next session reads first), runId? (defaults to the open run). Returns the closed run.",
    handler: async (ctx, params) => journalFinish(ctx, params),
  },
  journal_cancel: {
    kind: "handler",
    effect: "mutate",
    description:
      "Close a journal run as cancelled, for work abandoned rather than finished. Same idempotency as "
      + "journal_finish. Params: reason? (why it was abandoned), runId? (defaults to the open run). "
      + "Returns the cancelled run.",
    handler: async (ctx, params) => journalCancel(ctx, params),
  },
  journal_list: {
    kind: "handler",
    effect: "read",
    description:
      "List this project's journal runs, newest first, with every filter applied together. Params: "
      + "status? (active|completed|failed|cancelled), flowName? (only runs of that flow), tag? (one tag "
      + "the run carries), since? (epoch ms, an ISO date, or a relative age like 2h / 7d / 30m), "
      + "contains? (case-insensitive substring over the title, summary and notes), limit? (default 20, "
      + "0 for all), detail? (include every note and artifact rather than counts). Returns the matching "
      + "rows plus the totals they were selected from.",
    handler: async (ctx, params) => journalList(ctx, params),
  },
  journal_get: {
    kind: "handler",
    effect: "read",
    description:
      "Read one journal run in full: every note, every artifact, the outcome, and the per-step record "
      + "if it was a flow run. This is the handover call - give the next session a runId and it can "
      + "reconstruct what happened. Params: runId. Returns the run.",
    handler: async (ctx, params) => journalGet(ctx, params),
  },
  journal_delete: {
    kind: "handler",
    effect: "mutate",
    description:
      "Delete journal runs, rewriting the file without their records. The granularity is the run: "
      + "notes and artifacts are append-only by design, and a correction is another note. Reports how "
      + "many runs and records went, so a delete that matched nothing is distinguishable from one that "
      + "did. Not undoable. Params: exactly one of runId (that run) or all (true, every run for this "
      + "project). Returns what was removed and what remains.",
    handler: async (ctx, params) => journalDelete(ctx, params),
  },
  journal_status: {
    kind: "handler",
    effect: "read",
    description:
      "Where this project's journal lives, whether it is recording, how much is in it, and which run "
      + "is open. There is no initialise step: the file is created on the first write and read fresh on "
      + "every call, so nothing has to be started or shut down. Set UE_MCP_JOURNAL=0 to stop recording "
      + "and UE_MCP_JOURNAL_DIR to move the directory. Params: none",
    handler: async (ctx) => journalStatus(ctx),
  },
};

/* ── handlers ──────────────────────────────────────────────────────── */

function journalStart(ctx: ToolContext, params: Record<string, unknown>): Record<string, unknown> {
  requireEnabled();
  const file = fileFor(ctx);
  const title = requireString(
    params,
    "title",
    'It is what a later session reads first, so say what the work is: "rebuild the shrine lighting".',
  );
  const tags = stringList(params.tags, "tags");
  const runId = typeof params.runId === "string" && params.runId.trim() ? params.runId.trim() : undefined;
  const flowName = typeof params.flowName === "string" && params.flowName.trim() ? params.flowName.trim() : undefined;
  const { run, existed } = startRun(file, {
    title,
    runId,
    tags,
    flowName,
    project: ctx.project.projectDir,
  });
  return {
    started: !existed,
    existed,
    run: detailRun(run),
    journalFile: file,
    undo: undoDelete(run.runId),
  };
}

function journalNote(ctx: ToolContext, params: Record<string, unknown>): Record<string, unknown> {
  requireEnabled();
  const file = fileFor(ctx);
  const text = requireString(params, "text", "It is the note's whole content.");
  const { runId, implicit } = resolveRun(file, params, "note");
  const note = addNote(file, runId, text);
  return {
    recorded: true,
    runId,
    resolvedFrom: implicit ? "the open run" : "runId",
    note: { at: new Date(note.at).toISOString(), text: note.text },
    noteCount: readRun(file, runId)?.notes.length,
    undo: undoDelete(runId),
  };
}

function journalAttach(ctx: ToolContext, params: Record<string, unknown>): Record<string, unknown> {
  requireEnabled();
  const file = fileFor(ctx);
  const artifactPath = requireString(
    params,
    "artifactPath",
    "It is whatever names the thing produced: /Game/Materials/PBR/M_Rock, a file on disk, or a URL.",
  );
  const { runId, implicit } = resolveRun(file, params, "attach an artifact to");
  const kind = typeof params.artifactKind === "string" ? params.artifactKind : undefined;
  const note = typeof params.note === "string" ? params.note : undefined;
  const { artifact, existed } = addArtifact(file, runId, { path: artifactPath, kind, note });
  return {
    attached: !existed,
    existed,
    runId,
    resolvedFrom: implicit ? "the open run" : "runId",
    artifact: {
      at: new Date(artifact.at).toISOString(),
      path: artifact.path,
      kind: artifact.kind,
      note: artifact.note,
    },
    artifactCount: readRun(file, runId)?.artifacts.length,
    undo: undoDelete(runId),
  };
}

function journalFinish(ctx: ToolContext, params: Record<string, unknown>): Record<string, unknown> {
  requireEnabled();
  const file = fileFor(ctx);
  const status = readEndStatus(params.status, ["completed", "failed"]);
  const { runId, implicit } = resolveRun(file, params, "finish");
  const summary = typeof params.summary === "string" ? params.summary : undefined;
  const closed = endRun(file, runId, { status, summary });
  if (!closed) throw new McpError(ErrorCode.NOT_FOUND, `No journal run '${runId}' in ${file}.`);
  return {
    finished: !closed.existed,
    existed: closed.existed,
    resolvedFrom: implicit ? "the open run" : "runId",
    run: detailRun(closed.run),
    undo: undoDelete(runId),
  };
}

function journalCancel(ctx: ToolContext, params: Record<string, unknown>): Record<string, unknown> {
  requireEnabled();
  const file = fileFor(ctx);
  const { runId, implicit } = resolveRun(file, params, "cancel");
  const reason = typeof params.reason === "string" ? params.reason : undefined;
  const closed = endRun(file, runId, { status: "cancelled", reason });
  if (!closed) throw new McpError(ErrorCode.NOT_FOUND, `No journal run '${runId}' in ${file}.`);
  return {
    cancelled: !closed.existed,
    existed: closed.existed,
    resolvedFrom: implicit ? "the open run" : "runId",
    run: detailRun(closed.run),
    undo: undoDelete(runId),
  };
}

function journalList(ctx: ToolContext, params: Record<string, unknown>): Record<string, unknown> {
  const file = fileFor(ctx);
  const all = readRuns(file);
  const status = readRunStatus(params.status);
  const since = params.since === undefined ? undefined : parseSince(params.since);
  if (params.since !== undefined && since === undefined) {
    throw new McpError(
      ErrorCode.INVALID_PARAMS,
      `'since' could not be read as a time: ${JSON.stringify(params.since)}. Accepted spellings are `
        + `epoch milliseconds (1756400000000), an ISO date (2026-08-29 or 2026-08-29T10:00:00Z), or a `
        + `relative age (30m, 2h, 7d).`,
    );
  }
  const limit = readLimit(params.limit);
  const detail = params.detail === true;
  const rows = filterRuns(all, {
    status,
    flowName: typeof params.flowName === "string" ? params.flowName : undefined,
    tag: typeof params.tag === "string" ? params.tag : undefined,
    since,
    contains: typeof params.contains === "string" ? params.contains : undefined,
    limit,
  });
  return {
    journalFile: file,
    enabled: journalEnabled(),
    totalRuns: all.length,
    activeRuns: all.filter((r) => r.status === "active").length,
    matched: rows.length,
    runs: rows.map((r) => (detail ? detailRun(r) : summariseRun(r))),
  };
}

function journalGet(ctx: ToolContext, params: Record<string, unknown>): Record<string, unknown> {
  const file = fileFor(ctx);
  const runId = requireString(
    params,
    "runId",
    'flow(action="journal_list") reports the ids.',
  );
  const run = readRun(file, runId);
  if (!run) {
    const known = readRuns(file).slice(0, 10).map((r) => r.runId);
    throw new McpError(
      ErrorCode.NOT_FOUND,
      `No journal run '${runId}' in ${file}. `
        + (known.length > 0
          ? `Recent runIds: ${known.join(", ")}.`
          : 'This project has no journal runs yet; flow(action="journal_start", title="...") opens one.'),
    );
  }
  return { journalFile: file, run: detailRun(run) };
}

function journalDelete(ctx: ToolContext, params: Record<string, unknown>): Record<string, unknown> {
  requireEnabled();
  const file = fileFor(ctx);
  const runId = typeof params.runId === "string" && params.runId.trim() ? params.runId.trim() : undefined;
  const all = params.all === true;
  if (runId && all) {
    throw new McpError(
      ErrorCode.INVALID_PARAMS,
      "Pass runId or all, never both: one deletes a single run and the other deletes every run for "
        + "this project, and there is no reading of the call that wants both.",
    );
  }
  if (!runId && !all) {
    throw new McpError(
      ErrorCode.INVALID_PARAMS,
      'journal_delete needs a target: pass runId to delete one run, or all=true to delete every run '
        + `for this project. flow(action="journal_list") reports the ids.`,
    );
  }
  const before = readRuns(file);
  if (runId && !before.some((r) => r.runId === runId)) {
    return {
      deleted: false,
      reason: `No journal run '${runId}' in ${file}, so nothing was removed.`,
      deletedRuns: [],
      remainingRuns: before.length,
      journalFile: file,
    };
  }
  const result = deleteRuns(file, { runId, all });
  return {
    deleted: result.deletedRuns.length > 0,
    deletedRuns: result.deletedRuns,
    deletedRecords: result.deletedRecords,
    remainingRuns: result.remainingRuns,
    journalFile: file,
    undo: "None. The records are gone; the journal keeps no tombstones.",
  };
}

function journalStatus(ctx: ToolContext): Record<string, unknown> {
  const file = fileFor(ctx);
  const runs = readRuns(file);
  const open = runs.filter((r) => r.status === "active");
  return {
    enabled: journalEnabled(),
    journalFile: file,
    exists: runs.length > 0,
    project: ctx.project.projectDir,
    totalRuns: runs.length,
    byStatus: Object.fromEntries(
      RUN_STATUSES.map((s) => [s, runs.filter((r) => r.status === s).length]),
    ),
    openRuns: open.map(summariseRun),
    noteCount: runs.reduce((n, r) => n + r.notes.length, 0),
    artifactCount: runs.reduce((n, r) => n + r.artifacts.length, 0),
    autoRecording:
      'Every flow(action="run") opens and closes a run of its own, named after the flow, carrying '
      + "its per-step outcome. Work outside a flow is recorded with journal_start / journal_note / "
      + "journal_attach / journal_finish.",
    settings: {
      UE_MCP_JOURNAL: process.env.UE_MCP_JOURNAL ?? "(unset, recording)",
      UE_MCP_JOURNAL_DIR: process.env.UE_MCP_JOURNAL_DIR ?? "(unset, ~/.ue-mcp/journal)",
    },
  };
}

/* ── parameter reading ─────────────────────────────────────────────── */

function undoDelete(runId: string): string {
  return `flow(action="journal_delete", runId="${runId}")`;
}

function stringList(raw: unknown, name: string): string[] {
  if (raw === undefined || raw === null) return [];
  if (typeof raw === "string") return raw.split(",").map((s) => s.trim()).filter(Boolean);
  if (Array.isArray(raw)) {
    return raw.filter((x): x is string => typeof x === "string").map((s) => s.trim()).filter(Boolean);
  }
  throw new McpError(
    ErrorCode.INVALID_PARAMS,
    `'${name}' must be a list of strings, or one comma-separated string. Got ${typeof raw}.`,
  );
}

function readRunStatus(raw: unknown): JournalRunStatus | undefined {
  if (raw === undefined || raw === null || raw === "") return undefined;
  if (typeof raw === "string" && (RUN_STATUSES as string[]).includes(raw)) {
    return raw as JournalRunStatus;
  }
  throw new McpError(
    ErrorCode.INVALID_PARAMS,
    `'status' must be one of ${RUN_STATUSES.join(", ")}. Got ${JSON.stringify(raw)}.`,
  );
}

function readEndStatus(raw: unknown, allowed: JournalEndStatus[]): JournalEndStatus {
  if (raw === undefined || raw === null || raw === "") return allowed[0];
  if (typeof raw === "string" && (allowed as string[]).includes(raw)) return raw as JournalEndStatus;
  const cancelHint = (END_STATUSES as string[]).includes(String(raw))
    ? ` Use flow(action="journal_cancel") to end a run as cancelled.`
    : "";
  throw new McpError(
    ErrorCode.INVALID_PARAMS,
    `'status' must be one of ${allowed.join(", ")}. Got ${JSON.stringify(raw)}.${cancelHint}`,
  );
}

function readLimit(raw: unknown): number | undefined {
  if (raw === undefined || raw === null) return undefined;
  if (typeof raw === "number" && Number.isFinite(raw) && raw >= 0) return raw;
  throw new McpError(
    ErrorCode.INVALID_PARAMS,
    `'limit' must be a non-negative number (0 means every match). Got ${JSON.stringify(raw)}.`,
  );
}
