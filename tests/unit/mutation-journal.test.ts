/**
 * The mutation journal (#1481): what it records, where it persists, and how
 * revert and revert_after restore each entry, with a fake session host.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { MutationJournal, rollbackOf, type JournalHost } from "../../src/journal/journal.js";

let dir: string;
let checkpoints: Array<{ id: string; label: string }>;
let changesNext: string[];
let replayed: Array<{ method: string; payload: Record<string, unknown> }>;
let restored: Array<{ checkpoint: string; paths: string[] }>;
let enabled: boolean;
let replayFails: boolean;

function host(): JournalHost {
  return {
    name: "test",
    projectDir: dir,
    checkpointsEnabled: () => enabled,
    checkpoint: async (label) => {
      const id = `cp${checkpoints.length}`;
      checkpoints.push({ id, label });
      // A before checkpoint reports nothing; an after one reports what the call wrote.
      const changes = label.includes(": after ") ? changesNext.map((p) => ({ path: p })) : [];
      if (label.includes(": after ")) changesNext = [];
      return { id, changes };
    },
    restore: async (checkpoint, paths) => {
      restored.push({ checkpoint, paths });
      return { restored: paths.map((p) => ({ path: p, action: "written" as const })), refused: [] };
    },
    replay: async (method, payload) => {
      replayed.push({ method, payload });
      return replayFails ? { success: false, error: "inverse failed" } : { success: true };
    },
  };
}

beforeEach(() => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-journal-"));
  checkpoints = [];
  changesNext = [];
  replayed = [];
  restored = [];
  enabled = true;
  replayFails = false;
});

afterEach(() => {
  fs.rmSync(dir, { recursive: true, force: true });
});

const withRollback = (method: string, payload: Record<string, unknown>) => async () => ({
  success: true,
  data: { success: true },
  rollback: { taskName: "ue-mcp.bridge", payload: { ...payload, method } },
});
const writes = (...paths: string[]) => async () => {
  changesNext = paths;
  return { success: true, data: { success: true } };
};
const inMemory = async () => ({ success: true, data: { success: true } });

describe("MutationJournal", () => {
  it("records nothing until a group is named, and never a read", async () => {
    const j = new MutationJournal(host());
    await j.record("level.spawn_actor", "mutate", inMemory);
    j.setGroup("turn-1");
    await j.record("level.get_actors", "read", inMemory);
    expect(j.state().groups).toEqual([]);
    expect(checkpoints).toEqual([]);
    expect(fs.existsSync(path.join(dir, "Saved", "UE_MCP_Bridge", "journal.jsonl"))).toBe(false);
  });

  it("journals mutate and unknown calls with their rollback or snapshot, bracketed by checkpoints", async () => {
    const j = new MutationJournal(host());
    j.setGroup("turn-1");
    await j.record("level.spawn_actor", "mutate", withRollback("delete_actors", { actorLabels: ["A"] }));
    await j.record("project.set_config", "unknown", writes("Config/DefaultGame.ini"));
    await j.record("editor.undo", "mutate", inMemory);
    const s = j.state({ group: "turn-1" });
    expect(s.groups).toEqual([expect.objectContaining({ group: "turn-1", entries: 3, revertible: 2, reverted: 0 })]);
    const [a, b, c] = s.entries!;
    expect(a).toMatchObject({ task: "level.spawn_actor", revertible: true, rollback: { method: "delete_actors", payload: { actorLabels: ["A"] } } });
    expect(a.snapshot).toBeUndefined();
    expect(b).toMatchObject({ task: "project.set_config", effect: "unknown", revertible: true, snapshot: { before: "cp2", after: "cp3", paths: ["Config/DefaultGame.ini"] } });
    expect(c).toMatchObject({ task: "editor.undo", revertible: false });
    expect(c.reason).toMatch(/no rollback descriptor/);
    expect(checkpoints).toHaveLength(6);
  });

  it("never journals its own actions or the checkpoint actions", async () => {
    const j = new MutationJournal(host());
    j.setGroup("t");
    await j.record("editor.restore", "mutate", inMemory);
    await j.record("editor.revert_group", "mutate", inMemory);
    expect(j.state().groups).toEqual([]);
  });

  it("says why an entry is not revertible when checkpoints are off", async () => {
    enabled = false;
    const j = new MutationJournal(host());
    j.setGroup("t");
    await j.record("project.write_file", "mutate", inMemory);
    expect(checkpoints).toEqual([]);
    expect(j.state({ group: "t" }).entries![0].reason).toMatch(/checkpoints are off/);
  });

  it("reverts a group newest first and reports a not-revertible entry rather than skipping it", async () => {
    const j = new MutationJournal(host());
    j.setGroup("t");
    await j.record("level.spawn_actor", "mutate", withRollback("delete_actors", { actorLabels: ["A"] }));
    await j.record("asset.save", "mutate", writes("Content/BP.uasset"));
    await j.record("editor.undo", "mutate", inMemory);
    const r = await j.revert("t");
    expect(r.results.map((x) => [x.seq, x.status])).toEqual([[3, "not-revertible"], [2, "reverted"], [1, "reverted"]]);
    expect(r).toMatchObject({ reverted: 2, failed: 0, notRevertible: 1 });
    expect(restored).toEqual([{ checkpoint: "cp2", paths: ["Content/BP.uasset"] }]);
    expect(replayed).toEqual([{ method: "delete_actors", payload: { actorLabels: ["A"] } }]);
    // Reverting again replays nothing; the not-revertible entry is still reported.
    const again = await j.revert("t");
    expect(again.results.map((x) => x.status)).toEqual(["not-revertible"]);
    expect(replayed).toHaveLength(1);
  });

  it("falls back to the snapshot when the inverse fails, and reports a failure when there is none", async () => {
    replayFails = true;
    const j = new MutationJournal(host());
    j.setGroup("t");
    await j.record("asset.create", "mutate", async () => {
      changesNext = ["Content/New.uasset"];
      return { success: true, data: { success: true, rollback: { method: "delete_asset", payload: { assetPath: "/Game/New" } } } };
    });
    await j.record("level.spawn_actor", "mutate", withRollback("delete_actors", { actorLabels: ["B"] }));
    const r = await j.revert("t");
    expect(r.results[0]).toMatchObject({ seq: 2, status: "failed", via: "rollback", error: "inverse failed" });
    expect(r.results[1]).toMatchObject({ seq: 1, status: "reverted", via: "snapshot", restored: ["Content/New.uasset"] });
  });

  it("revert_after reverts every later group and keeps the named one", async () => {
    const j = new MutationJournal(host());
    for (const g of ["one", "two", "three"]) {
      j.setGroup(g);
      await j.record("level.spawn_actor", "mutate", withRollback("delete_actors", { actorLabels: [g] }));
    }
    const r = await j.revertAfter("one");
    expect(r.groups).toEqual(["two", "three"]);
    expect(replayed.map((x) => x.payload.actorLabels)).toEqual([["three"], ["two"]]);
    await expect(j.revert("missing")).rejects.toThrow(/no group 'missing'/);
  });

  it("persists in Saved and reverts from a fresh journal, as after a restart", async () => {
    const first = new MutationJournal(host());
    first.setGroup("t");
    await first.record("level.spawn_actor", "mutate", withRollback("delete_actors", { actorLabels: ["A"] }));
    await first.record("asset.save", "mutate", writes("Content/BP.uasset"));
    expect(fs.existsSync(path.join(dir, "Saved", "UE_MCP_Bridge", "journal.jsonl"))).toBe(true);

    const second = new MutationJournal(host());
    expect(second.group).toBeNull();
    const r = await second.revert("t");
    expect(r.reverted).toBe(2);
    // The reverted marks persist too.
    const third = new MutationJournal(host());
    expect(third.state().groups[0]).toMatchObject({ entries: 2, reverted: 2 });
    // Sequence numbers continue rather than restart.
    third.setGroup("u");
    await third.record("level.spawn_actor", "mutate", inMemory);
    expect(third.state({ group: "u" }).entries![0].seq).toBe(3);
  });

  it("records a failed call that still wrote files, and rethrows a thrown one", async () => {
    const j = new MutationJournal(host());
    j.setGroup("t");
    await j.record("asset.import", "mutate", async () => {
      changesNext = ["Content/Half.uasset"];
      return { success: false };
    });
    await expect(j.record("asset.import", "mutate", async () => { throw new Error("boom"); })).rejects.toThrow("boom");
    await j.record("asset.import", "mutate", async () => ({ success: false }));
    const entries = j.state({ group: "t" }).entries!;
    expect(entries).toHaveLength(1);
    expect(entries[0]).toMatchObject({ succeeded: false, revertible: true });
  });
});

describe("rollbackOf", () => {
  it("reads the lifted record and the raw descriptor", () => {
    expect(rollbackOf({ rollback: { taskName: "ue-mcp.bridge", payload: { method: "m", a: 1 } } })).toEqual({ method: "m", payload: { a: 1 } });
    expect(rollbackOf({ data: { rollback: { method: "m", payload: { b: 2 } } } })).toEqual({ method: "m", payload: { b: 2 } });
    expect(rollbackOf({ data: { success: true } })).toBeUndefined();
  });
});
