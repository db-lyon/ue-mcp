// @session per-session: The hold on one editor, and the occupancy answer over that editor's signals.
/**
 * Who holds the editor. The editor is the developer's workspace first, so the
 * hub plays on it only when nothing else holds it, and holds it explicitly
 * while it plays:
 *
 * - occupancyOf: one answer from every signal the daemon already has. The
 *   editor not connected; an editor operation (restart, rebuild); an open
 *   modal dialog; Play In Editor the hub did not start; a client's agent or
 *   another MCP client with calls in flight; and, for a scheduled run only,
 *   someone using this computer lately. From the consent ledger
 *   (src/dispatch/disruption.ts): a session holding the editor, one with an
 *   editor transaction open, and an open request to stop, restart or rebuild
 *   it. A scheduled run also waits for every other session to go quiet.
 *   Empty means free.
 * - EditorLease: the hub's hold on one editor, taken before a queue item
 *   plays and released when it ends or a person releases the editor.
 */
import type { BusyState, EditorLease as Lease, OccupancyReason } from "./contract.js";

/** The developer's own editor. */
export const WORKSPACE = "workspace";

/** A scheduled run waits until nobody has used this computer for this long. */
export const INPUT_QUIET_MS = 5 * 60_000;

/** What the daemon already knows about the editor, read when the hub asks. */
export interface OccupancySignals {
  connected(): boolean;
  /** Play In Editor is running, whoever started it. */
  pie(): boolean;
  /** Modal dialogs open in the editor. */
  dialogs(): number;
  /** Editor calls in flight and the editor operation running. */
  busy(): BusyState;
  /** How long since someone last used this computer, or null when no window reports it. */
  inputAgeMs(): number | null;
  /** Every other session the consent ledger knows, the daemon's own calls left out. */
  sessions?(): SessionUse[];
  /** Requests to stop, restart or rebuild the editor still being decided or about to run. */
  disruptions?(): Array<{ label: string; op: string }>;
}

/** One other session's use of the editor, as the consent ledger has it. */
export interface SessionUse {
  label: string;
  hold?: string | null;
  transactions?: number;
  busy?: number;
  idleMs?: number | null;
}

/** A session that called within this long is still using the editor, for a run nobody asked for now. */
export const SESSION_IDLE_MS = 180_000;

/** What the hub itself holds, which never counts against it. */
export interface Held {
  /** The hub's run started PIE, so PIE running is the hub's. */
  pie?: boolean;
}

const OPERATION: Record<string, string> = { start: "starting", stop: "stopping", restart: "restarting", rebuild: "rebuilding" };

/** Why the editor is not free for the hub now, most decisive first; empty when it is free. */
export function occupancyOf(s: OccupancySignals, held: Held = {}, o: { scheduled?: boolean } = {}): OccupancyReason[] {
  if (!s.connected()) return [{ kind: "disconnected", text: "The editor is not connected" }];
  const out: OccupancyReason[] = [];
  const busy = s.busy();
  if (busy.operation) out.push({ kind: "operation", text: `The editor is ${OPERATION[busy.operation.op] ?? "busy"}` });
  const asked = s.disruptions?.()[0];
  if (asked) out.push({ kind: "consent", text: `${asked.label} asked to ${asked.op}` });
  for (const u of s.sessions?.() ?? []) {
    if (u.hold) out.push({ kind: "session", text: `${u.label} holds the editor: ${u.hold}` });
    else if ((u.transactions ?? 0) > 0) out.push({ kind: "session", text: `${u.label} has an editor transaction open` });
    else if (o.scheduled && (u.busy ?? 0) > 0) out.push({ kind: "session", text: `${u.label} is using the editor` });
    else if (o.scheduled && u.idleMs != null && u.idleMs < SESSION_IDLE_MS) out.push({ kind: "session", text: `${u.label} used the editor in the last ${Math.round(SESSION_IDLE_MS / 60_000)} minutes` });
  }
  if (s.dialogs() > 0) out.push({ kind: "dialog", text: "A dialog is open in the editor" });
  if (s.pie() && !held.pie) out.push({ kind: "pie", text: "Play In Editor is in use" });
  if (busy.calls.some((c) => c.source === "agent")) out.push({ kind: "agent", text: "A chat is using the editor" });
  if (busy.calls.some((c) => c.source === "client")) out.push({ kind: "client", text: "Another MCP client is using the editor" });
  const age = s.inputAgeMs();
  if (o.scheduled && age !== null && age < INPUT_QUIET_MS) out.push({ kind: "input", text: "Someone is using this computer" });
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
    if (this.lease && this.lease.item !== l.item) throw new Error(`the ${this.editor} editor is already leased for ${this.lease.purpose}`);
    this.lease = { ...l, editor: this.editor, since: l.since ?? this.lease?.since ?? new Date().toISOString() };
    this.changed(this.get());
    return this.get()!;
  }

  /** Record what the item started: its run, and whether that run started PIE. */
  update(patch: Partial<Pick<Lease, "runId" | "pie">>): void {
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

  held(): Held {
    return this.lease ? { pie: this.lease.pie === true } : {};
  }
}
