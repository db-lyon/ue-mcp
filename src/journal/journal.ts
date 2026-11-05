/**
 * The mutation journal (#1481): every call that may change state, made while
 * a client has named a group, recorded with how to restore what it touched,
 * so a whole group (a chat turn) or everything after one can be reverted.
 *
 * Restoring builds on what already exists. A call whose handler returned a
 * rollback descriptor (MCPSetRollback) is reverted by replaying it. Otherwise
 * the call is bracketed by two checkpoints (#1482), and its files are put back
 * to the first one. A call with neither is recorded as not revertible and is
 * reported as such by every revert, never skipped.
 *
 * The journal is a JSON Lines file in the project's Saved/UE_MCP_Bridge, so it
 * outlives the server and the editor. The current group is per editor session
 * and lives in memory: a client names it again after a restart.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { warn } from "../core/log.js";
import type { ActionEffect } from "../core/types.js";
import type { EditorSession } from "../sessions/session.js";
import { checkpointsFor, type RestoreResult } from "../checkpoints/service.js";

export interface JournalRollback {
  method: string;
  payload: Record<string, unknown>;
}

export interface JournalSnapshot {
  /** Checkpoint taken just before the call. */
  before: string;
  /** Checkpoint taken just after it. */
  after: string;
  /** Project-relative paths that differ between the two. */
  paths: string[];
}

export interface JournalEntry {
  seq: number;
  group: string;
  at: string;
  /** `<tool>.<action>` as dispatched. */
  task: string;
  effect: ActionEffect;
  /** False when the call failed but still left files changed. */
  succeeded: boolean;
  rollback?: JournalRollback;
  snapshot?: JournalSnapshot;
  revertible: boolean;
  /** Why the entry cannot be reverted, when it cannot. */
  reason?: string;
  reverted?: { at: string };
}

export interface JournalGroup {
  group: string;
  entries: number;
  revertible: number;
  reverted: number;
  firstAt: string;
  lastAt: string;
}

export interface JournalState {
  editor: string;
  /** The group calls are recorded into now, or null when nothing is recorded. */
  group: string | null;
  checkpointsEnabled: boolean;
  journal: string | null;
  groups: JournalGroup[];
  entries?: JournalEntry[];
}

export type RevertOutcome =
  | { seq: number; task: string; group: string; status: "reverted"; via: "rollback" | "snapshot"; restored?: string[] }
  | { seq: number; task: string; group: string; status: "failed"; via: "rollback" | "snapshot"; error: string }
  | { seq: number; task: string; group: string; status: "not-revertible"; reason: string };

export interface RevertResult {
  editor: string;
  groups: string[];
  /** Every unreverted entry of those groups, newest first, with what happened to it. */
  results: RevertOutcome[];
  reverted: number;
  failed: number;
  notRevertible: number;
}

/** What a journal needs from its editor session. */
export interface JournalHost {
  name: string;
  projectDir: string | null;
  checkpointsEnabled(): boolean;
  checkpoint(label: string): Promise<{ id: string; changes: Array<{ path: string }> }>;
  restore(checkpoint: string, paths: string[]): Promise<Pick<RestoreResult, "restored" | "refused">>;
  /** Run a rollback descriptor's bridge method through the session's guarded bridge. */
  replay(method: string, payload: Record<string, unknown>): Promise<unknown>;
}

/** Calls never journaled: the journal's own actions and the checkpoint actions it is built on. */
export const UNJOURNALED_TASKS: ReadonlySet<string> = new Set([
  "editor.journal_group",
  "editor.list_journal",
  "editor.revert_group",
  "editor.revert_after_group",
  "editor.checkpoint",
  "editor.restore",
  "editor.clear_checkpoints",
]);

export class JournalError extends Error {
  constructor(readonly status: number, message: string) {
    super(message);
  }
}

type Line =
  | ({ type: "entry" } & JournalEntry)
  | { type: "reverted"; seq: number; at: string };

/** Read a rollback descriptor off a task result: the lifted record, or the raw one in its data. */
export function rollbackOf(result: { rollback?: unknown; data?: unknown }): JournalRollback | undefined {
  const lifted = result.rollback as { taskName?: unknown; payload?: unknown } | undefined;
  if (lifted && typeof lifted === "object" && lifted.payload && typeof lifted.payload === "object") {
    const { method, ...payload } = lifted.payload as Record<string, unknown>;
    if (typeof method === "string" && method) return { method, payload };
  }
  const raw = (result.data && typeof result.data === "object" ? (result.data as Record<string, unknown>).rollback : undefined) as
    | { method?: unknown; payload?: unknown }
    | undefined;
  if (raw && typeof raw === "object" && typeof raw.method === "string" && raw.method) {
    return { method: raw.method, payload: raw.payload && typeof raw.payload === "object" ? (raw.payload as Record<string, unknown>) : {} };
  }
  return undefined;
}

export class MutationJournal {
  private current: string | null = null;
  private loadedFrom: string | null = null;
  private entries: JournalEntry[] = [];
  private nextSeq = 1;
  /** Journaled calls and reverts run one at a time, so each call's checkpoints bracket only it. */
  private chain: Promise<unknown> = Promise.resolve();

  constructor(private readonly host: JournalHost) {}

  /** `<project>/Saved/UE_MCP_Bridge/journal.jsonl`, or null without a project. */
  get file(): string | null {
    return this.host.projectDir ? path.join(this.host.projectDir, "Saved", "UE_MCP_Bridge", "journal.jsonl") : null;
  }

  get group(): string | null {
    return this.current;
  }

  /** Name the group later calls are recorded into; null stops recording. */
  setGroup(group: string | null): JournalState {
    if (group !== null) {
      if (typeof group !== "string" || group.trim() === "") throw new JournalError(400, "journalGroup is a non-empty string, or null to stop recording");
      if (!this.host.projectDir) throw new JournalError(400, "The journal needs a project; this session has none");
    }
    this.current = group === null ? null : group.trim();
    return this.state();
  }

  private load(): void {
    const file = this.file;
    if (file === this.loadedFrom) return;
    this.loadedFrom = file;
    this.entries = [];
    this.nextSeq = 1;
    if (!file || !fs.existsSync(file)) return;
    const bySeq = new Map<number, JournalEntry>();
    for (const text of fs.readFileSync(file, "utf-8").split("\n")) {
      if (!text.trim()) continue;
      let line: Line;
      try {
        line = JSON.parse(text) as Line;
      } catch {
        warn("journal", `skipping an unreadable line in ${file}`);
        continue;
      }
      if (line.type === "entry") {
        const { type: _t, ...entry } = line;
        bySeq.set(entry.seq, entry);
        this.entries.push(entry);
        this.nextSeq = Math.max(this.nextSeq, entry.seq + 1);
      } else if (line.type === "reverted") {
        const e = bySeq.get(line.seq);
        if (e) e.reverted = { at: line.at };
      }
    }
  }

  private append(line: Line): void {
    const file = this.file;
    if (!file) return;
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.appendFileSync(file, `${JSON.stringify(line)}\n`);
  }

  private serialize<T>(fn: () => Promise<T>): Promise<T> {
    const run = this.chain.then(fn, fn);
    this.chain = run.catch(() => undefined);
    return run;
  }

  state(opts: { group?: string; entries?: boolean } = {}): JournalState {
    this.load();
    const groups = new Map<string, JournalGroup>();
    for (const e of this.entries) {
      const g = groups.get(e.group) ?? { group: e.group, entries: 0, revertible: 0, reverted: 0, firstAt: e.at, lastAt: e.at };
      g.entries++;
      if (e.revertible) g.revertible++;
      if (e.reverted) g.reverted++;
      g.lastAt = e.at;
      groups.set(e.group, g);
    }
    const wantEntries = opts.entries || opts.group !== undefined;
    return {
      editor: this.host.name,
      group: this.current,
      checkpointsEnabled: this.host.checkpointsEnabled(),
      journal: this.file && fs.existsSync(this.file) ? this.file : null,
      groups: [...groups.values()],
      ...(wantEntries ? { entries: this.entries.filter((e) => opts.group === undefined || e.group === opts.group) } : {}),
    };
  }

  /**
   * Run one dispatched call, journaling it when a group is named and its
   * declared effect may change state. `run` must resolve to the task result.
   */
  async record<R extends { success: boolean; rollback?: unknown; data?: unknown }>(
    task: string,
    effect: ActionEffect,
    run: () => Promise<R>,
  ): Promise<R> {
    if (this.current === null || effect === "read" || UNJOURNALED_TASKS.has(task) || !this.host.projectDir) return run();
    const group = this.current;
    return this.serialize(async () => {
      const snapshots = this.host.checkpointsEnabled();
      let before: string | null = null;
      if (snapshots) {
        try {
          before = (await this.host.checkpoint(`journal ${group}: before ${task}`)).id;
        } catch (e) {
          warn("journal", `checkpoint before ${task} failed`, e);
        }
      }
      let result: R | undefined;
      let thrown: unknown;
      try {
        result = await run();
      } catch (e) {
        thrown = e;
      }
      let snapshot: JournalSnapshot | undefined;
      if (before) {
        try {
          const after = await this.host.checkpoint(`journal ${group}: after ${task}`);
          if (after.changes.length > 0) snapshot = { before, after: after.id, paths: after.changes.map((c) => c.path) };
        } catch (e) {
          warn("journal", `checkpoint after ${task} failed`, e);
        }
      }
      const succeeded = thrown === undefined && result?.success === true;
      if (succeeded || snapshot) {
        this.load();
        const rollback = succeeded && result ? rollbackOf(result) : undefined;
        const entry: JournalEntry = {
          seq: this.nextSeq++,
          group,
          at: new Date().toISOString(),
          task,
          effect,
          succeeded,
          ...(rollback ? { rollback } : {}),
          ...(snapshot ? { snapshot } : {}),
          revertible: !!(rollback || snapshot),
        };
        if (!entry.revertible) {
          entry.reason = snapshots
            ? "The call returned no rollback descriptor and wrote no file the checkpoints recorded, so nothing can restore what it changed in the editor."
            : "The call returned no rollback descriptor, and checkpoints are off, so no snapshot of its files was taken.";
        }
        this.entries.push(entry);
        this.append({ type: "entry", ...entry });
      }
      if (thrown !== undefined) throw thrown;
      return result as R;
    });
  }

  /** Revert every unreverted entry of `group`, newest first. */
  revert(group: string): Promise<RevertResult> {
    return this.serialize(async () => {
      this.load();
      this.requireGroup(group);
      return this.revertEntries([group], this.entries.filter((e) => e.group === group));
    });
  }

  /** Revert every unreverted entry of every group first recorded after `group`, newest first. */
  revertAfter(group: string): Promise<RevertResult> {
    return this.serialize(async () => {
      this.load();
      this.requireGroup(group);
      const order: string[] = [];
      for (const e of this.entries) if (!order.includes(e.group)) order.push(e.group);
      const later = order.slice(order.indexOf(group) + 1);
      const set = new Set(later);
      return this.revertEntries(later, this.entries.filter((e) => set.has(e.group)));
    });
  }

  private requireGroup(group: string): void {
    if (typeof group !== "string" || group === "") throw new JournalError(400, "journalGroup is required: the group to revert");
    if (!this.entries.some((e) => e.group === group)) throw new JournalError(404, `The journal has no group '${group}'`);
  }

  private async revertEntries(groups: string[], entries: JournalEntry[]): Promise<RevertResult> {
    const results: RevertOutcome[] = [];
    const pending = entries.filter((e) => !e.reverted).sort((a, b) => b.seq - a.seq);
    for (const e of pending) {
      const base = { seq: e.seq, task: e.task, group: e.group };
      if (!e.revertible) {
        results.push({ ...base, status: "not-revertible", reason: e.reason ?? "Nothing recorded how to restore it." });
        continue;
      }
      let outcome: RevertOutcome | null = null;
      if (e.rollback) {
        outcome = await this.replay(e, e.rollback);
        // A failed inverse falls back to the files when they were recorded too.
        if (outcome.status === "failed" && e.snapshot) outcome = await this.restoreSnapshot(e, e.snapshot);
      } else if (e.snapshot) {
        outcome = await this.restoreSnapshot(e, e.snapshot);
      }
      if (!outcome) continue;
      if (outcome.status === "reverted") {
        e.reverted = { at: new Date().toISOString() };
        this.append({ type: "reverted", seq: e.seq, at: e.reverted.at });
      }
      results.push(outcome);
    }
    const count = (s: RevertOutcome["status"]) => results.filter((r) => r.status === s).length;
    return {
      editor: this.host.name,
      groups,
      results,
      reverted: count("reverted"),
      failed: count("failed"),
      notRevertible: count("not-revertible"),
    };
  }

  private async replay(e: JournalEntry, rb: JournalRollback): Promise<RevertOutcome> {
    const base = { seq: e.seq, task: e.task, group: e.group, via: "rollback" as const };
    try {
      const answer = (await this.host.replay(rb.method, rb.payload)) as Record<string, unknown> | null;
      if (answer && typeof answer === "object" && answer.success === false) {
        return { ...base, status: "failed", error: String(answer.error ?? `${rb.method} failed`) };
      }
      return { ...base, status: "reverted" };
    } catch (err) {
      return { ...base, status: "failed", error: err instanceof Error ? err.message : String(err) };
    }
  }

  private async restoreSnapshot(e: JournalEntry, s: JournalSnapshot): Promise<RevertOutcome> {
    const base = { seq: e.seq, task: e.task, group: e.group, via: "snapshot" as const };
    try {
      const r = await this.host.restore(s.before, s.paths);
      if (r.refused.length > 0) {
        return { ...base, status: "failed", error: `restore refused ${r.refused.map((x) => `${x.path} (${x.reason})`).join(", ")}` };
      }
      return { ...base, status: "reverted", restored: r.restored.map((x) => x.path) };
    } catch (err) {
      return { ...base, status: "failed", error: err instanceof Error ? err.message : String(err) };
    }
  }
}

const bySession = new WeakMap<EditorSession, MutationJournal>();

/** The session's journal, created on first use; creating it creates nothing on disk. */
export function journalFor(session: EditorSession): MutationJournal {
  let j = bySession.get(session);
  if (!j) {
    // Looked up per use: a session no group is named on never builds its checkpoints.
    const checkpoints = () => checkpointsFor(session);
    j = new MutationJournal({
      get name() {
        return session.name;
      },
      get projectDir() {
        return session.project.projectDir;
      },
      checkpointsEnabled: () => checkpoints().enabled(),
      checkpoint: (label) => checkpoints().create(label),
      restore: (checkpoint, paths) => checkpoints().restore(checkpoint, { paths }),
      replay: (method, payload) => session.guarded.call(method, payload),
    });
    bySession.set(session, j);
  }
  return j;
}
