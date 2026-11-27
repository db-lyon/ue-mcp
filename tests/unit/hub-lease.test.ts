/**
 * Who holds the editor: the occupancy answer from each signal the hub has,
 * PIE the hub's own run started never counting against it, and the lease.
 */
import { beforeEach, describe, expect, it } from "vitest";
import type { BusyState } from "../../src/hub/contract.js";
import { EditorLease, INPUT_QUIET_MS, occupancyOf, type OccupancySignals } from "../../src/hub/lease.js";

let s: { connected: boolean; pie: boolean; dialogs: number; busy: BusyState; inputAgeMs: number | null };
const signals = (): OccupancySignals => ({
  connected: () => s.connected,
  pie: () => s.pie,
  dialogs: () => s.dialogs,
  busy: () => s.busy,
  inputAgeMs: () => s.inputAgeMs,
});
const kinds = (held = {}, o = {}) => occupancyOf(signals(), held, o).map((r) => r.kind);

beforeEach(() => {
  s = { connected: true, pie: false, dialogs: 0, busy: { now: 0, calls: [] }, inputAgeMs: null };
});

describe("occupancyOf", () => {
  it("is free when nothing holds the editor", () => {
    expect(occupancyOf(signals())).toEqual([]);
  });

  it("names every signal that holds it, most decisive first", () => {
    s.busy = { now: 0, calls: [{ id: 1, category: "asset", action: "save", startedAt: 0, source: "client", client: "cli", stopped: false }, { id: 2, category: "asset", action: "list", startedAt: 0, source: "agent", sessionId: "chat", stopped: false }], operation: { op: "rebuild", step: "build", startedAt: 0, stepStartedAt: 0 } };
    s.dialogs = 1;
    s.pie = true;
    expect(occupancyOf(signals()).map((r) => r.text)).toEqual([
      "The editor is rebuilding",
      "A dialog is open in the editor",
      "Play In Editor is in use",
      "A chat is using the editor",
      "Another MCP client is using the editor",
    ]);
  });

  describe("the consent ledger", () => {
    const withConsent = (sessions: ReturnType<NonNullable<OccupancySignals["sessions"]>>, disruptions: Array<{ label: string; op: string }> = []): OccupancySignals => ({ ...signals(), sessions: () => sessions, disruptions: () => disruptions });

    it("a hold, an open transaction or an open restart request keeps every run off the editor", () => {
      expect(occupancyOf(withConsent([{ label: "Claude Code", hold: "refactoring the door blueprint" }])).map((r) => r.text)).toEqual(["Claude Code holds the editor: refactoring the door blueprint"]);
      expect(occupancyOf(withConsent([{ label: "Cursor", transactions: 2 }])).map((r) => r.kind)).toEqual(["session"]);
      expect(occupancyOf(withConsent([], [{ label: "Claude Code", op: "restart the editor" }])).map((r) => r.text)).toEqual(["Claude Code asked to restart the editor"]);
    });

    it("a session working or active lately keeps a scheduled run off it, not a person's", () => {
      const ledger = withConsent([{ label: "Claude Code", busy: 1 }, { label: "Cursor", idleMs: 60_000 }, { label: "Old", idleMs: 3_600_000 }]);
      expect(occupancyOf(ledger)).toEqual([]);
      expect(occupancyOf(ledger, {}, { scheduled: true }).map((r) => r.text)).toEqual(["Claude Code is using the editor", "Cursor used the editor in the last 3 minutes"]);
    });
  });

  it("the editor not connected says only that", () => {
    s.connected = false;
    s.pie = true;
    expect(kinds()).toEqual(["disconnected"]);
  });

  it("PIE the hub's own run started never counts", () => {
    s.pie = true;
    expect(kinds()).toEqual(["pie"]);
    expect(kinds({ pie: true })).toEqual([]);
  });

  it("someone using this computer holds it for a scheduled run only", () => {
    s.inputAgeMs = 30_000;
    expect(kinds()).toEqual([]);
    expect(kinds({}, { scheduled: true })).toEqual(["input"]);
    s.inputAgeMs = INPUT_QUIET_MS;
    expect(kinds({}, { scheduled: true })).toEqual([]);
  });
});

describe("EditorLease", () => {
  it("is taken for one item at a time, records what it started, and announces each change", () => {
    const seen: unknown[] = [];
    const lease = new EditorLease("workspace", (l) => seen.push(l));
    lease.take({ purpose: "Run Door", item: "q1", since: "t" });
    expect(() => lease.take({ purpose: "Other", item: "q2" })).toThrow(/already leased/);
    lease.update({ runId: "r1", pie: true });
    expect(lease.held()).toEqual({ pie: true });
    expect(lease.release()).toMatchObject({ editor: "workspace", runId: "r1" });
    expect(lease.held()).toEqual({});
    expect(seen.at(-1)).toBeNull();
    expect(seen).toHaveLength(3);
  });
});
