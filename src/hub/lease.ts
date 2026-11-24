// @session per-session: The hold on one editor, and the occupancy answer over that editor's signals.
/**
 * Who holds the editor. The editor is the developer's workspace first, so the
 * hub starts work on it only when nothing else holds it, and holds it
 * explicitly while it works:
 *
 * - occupancyOf: one answer from every signal the daemon already has. The editor
 *   not connected; an editor operation (restart, rebuild); an open modal
 *   dialog; Play In Editor the hub did not start (always the developer's);
 *   a playtest the hub did not start, whichever client started it; a chat or
 *   fix loop with editor calls in flight; another MCP client's calls; and,
 *   for unattended work only, someone using this computer lately (the
 *   desktop's input age, which a client's window may report).
 *   Empty means free.
 * - EditorLease: the hub's hold on one editor, taken before a plan item or a
 *   fix loop starts and released when it ends or a person releases the
 *   editor. While held, the lease names what the hub may act on: the run its
 *   item started, and Play In Editor only when that run started it.
 *
 * A lease names its editor: today only the developer's ("workspace"). A
 * runner editor (docs/plans/playtest-hub/runner-editor.md) would hold a lease
 * of its own once ue-mcp can run one.
 */
import type { BusyState, EditorLease as Lease, OccupancyReason, QaRun, QaRunState } from "./contract.js";

/** Run states that hold the editor. A manual run plays nothing, so it holds nothing. */
export const ON_EDITOR: ReadonlySet<QaRunState> = new Set<QaRunState>(["armed", "running", "awaiting_verdict"]);

/** The developer's own editor. */
export const WORKSPACE = "workspace";

/** Unattended work waits until nobody has used this computer for this long. */
export const INPUT_QUIET_MS = 5 * 60_000;

/** What the daemon already knows about the editor, read when the hub asks. */
export interface OccupancySignals {
  connected(): boolean;
  /** Play In Editor is running, whoever started it (PieWatch). */
  pie(): boolean;
  /** Modal dialogs open in the editor (DialogBroker). */
  dialogs(): number;
  /** Editor calls in flight and the editor operation running (BusyTracker). */
  busy(): BusyState;
  /** Every run the broker knows (QaBroker). */
  runs(): QaRun[];
  /** How long since someone last used this computer, or null when no window reports it. */
  inputAgeMs(): number | null;
}

/** What the hub itself holds, which never counts against it. */
export interface Held {
  runId?: string | null;
  /** The hub's run started PIE, so PIE running is the hub's. */
  pie?: boolean;
  /** The fix loop's session, whose calls and loop are the hub's. */
  sessionId?: string | null;
}

const OPERATION: Record<string, string> = { start: "starting", stop: "stopping", restart: "restarting", rebuild: "rebuilding" };

/** Why the editor is not free for the hub now, most decisive first; empty when it is free. */
export function occupancyOf(s: OccupancySignals, held: Held = {}, o: { unattended?: boolean; now?: number } = {}): OccupancyReason[] {
  if (!s.connected()) return [{ kind: "disconnected", text: "The editor is not connected" }];
  const out: OccupancyReason[] = [];
  const busy = s.busy();
  if (busy.operation) out.push({ kind: "operation", text: `The editor is ${OPERATION[busy.operation.op] ?? "busy"}` });
  if (s.dialogs() > 0) out.push({ kind: "dialog", text: "A dialog is open in the editor" });
  if (s.pie() && !held.pie) out.push({ kind: "pie", text: "Play In Editor is in use" });
  const runs = s.runs();
  const other = runs.find((r) => r.runId !== held.runId && ON_EDITOR.has(r.state) && !r.manual && !r.deleted);
  if (other) out.push({ kind: "run", text: other.sessionId ? "Another playtest is running" : "Another client is running a playtest" });
  const loop = runs.some((r) => r.assignment?.state === "running" && r.assignment.sessionId !== held.sessionId);
  const chat = busy.calls.some((c) => c.source === "agent" && c.sessionId !== held.sessionId);
  if (loop) out.push({ kind: "agent", text: "A fix loop is running" });
  else if (chat) out.push({ kind: "agent", text: "A chat is using the editor" });
  if (busy.calls.some((c) => c.source === "client")) out.push({ kind: "client", text: "Another MCP client is using the editor" });
  const age = s.inputAgeMs();
  if (o.unattended && age !== null && age < INPUT_QUIET_MS) out.push({ kind: "input", text: "Someone is using this computer" });
  return out;
}

/** The hub's hold on one editor. */
export class EditorLease {
  private lease: Lease | null = null;

  constructor(readonly editor: string = WORKSPACE, private readonly changed: (lease: Lease | null) => void = () => {}) {}

  get(): Lease | null {
    return this.lease ? { ...this.lease } : null;
  }

  /** Take the lease for one item. Taking it again for the same item keeps it; for another it is a bug in the caller. */
  take(l: Omit<Lease, "editor" | "since"> & { since?: string }): Lease {
    if (this.lease && (this.lease.planRunId !== l.planRunId || this.lease.item !== l.item)) throw new Error(`the ${this.editor} editor is already leased for ${this.lease.purpose}`);
    this.lease = { ...l, editor: this.editor, since: l.since ?? this.lease?.since ?? new Date().toISOString() };
    this.changed(this.get());
    return this.get()!;
  }

  /** Record what the item started: its run, whether that run started PIE, its session. */
  update(patch: Partial<Pick<Lease, "runId" | "pie" | "sessionId">>): void {
    if (!this.lease) return;
    const next = { ...this.lease, ...patch };
    if (JSON.stringify(next) === JSON.stringify(this.lease)) return;
    this.lease = next;
    this.changed(this.get());
  }

  release(): Lease | null {
    const had = this.lease;
    if (!had) return null;
    this.lease = null;
    this.changed(null);
    return had;
  }

  /** What the lease holds, for occupancyOf. */
  held(): Held {
    return this.lease ? { runId: this.lease.runId ?? null, pie: this.lease.pie === true, sessionId: this.lease.sessionId ?? null } : {};
  }
}
