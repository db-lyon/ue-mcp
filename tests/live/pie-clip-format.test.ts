// @live-editor headless
/**
 * Playtest clips as data (#2097): an agent drafts a clip in its compact text form, checks it against the level with
 * clip_validate (a misspelt Input Action comes back with candidates, overlapping regions refuse the save), saves it,
 * reads it back as JSON and text, lists it, replaces it and deletes it. No PIE.
 * Refs #2097
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import { setupProbeArena, teardownProbeArena, type ProbeActions } from "./clip-fixture.js";

const DIR = "/Game/MCPTest/ClipFormat";
const CLIP = `${DIR}/CL_Format`;

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const rows = (v: unknown): Json[] => (Array.isArray(v) ? (v as Json[]) : []);
const obj = (v: unknown): Json => (v && typeof v === "object" ? (v as Json) : {});

let bridge: EditorBridge;
let a: ProbeActions;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

const clipText = () => [
  "clip rate=60 length=260",
  `region r1 0 120 "Walk forward"`,
  `region r2 120 260 "Charge"`,
  `track ${a.move} axis2d`,
  "  key k1 0 0,1 constant",
  "  key k2 90 0,0 constant",
  `track ${a.charge} bool`,
  "  note n1 130 45",
  `check c1 0 100 by_end pos_x increasing 200 name="Walks forward"`,
  // The character the arena spawns at play time is not in the level, so its counters resolve in PIE.
  "check c2 130 60 by_end ChargeCount eq 1 resolve_in_pie=true",
  "check c3 0 100 by_end BaseEyeHeight gt 0",
].join("\n");

beforeAll(async () => {
  bridge = await getBridge();
  a = await setupProbeArena(bridge, DIR, "ClipFormat");
}, 180_000);

afterAll(async () => {
  if (!bridge) return;
  await teardownProbeArena(bridge, DIR);
  disconnectBridge();
}, 120_000);

describe("drafting a clip", () => {
  it("clip_validate names a misspelt Input Action with candidates, and an overlap that cannot be saved", async () => {
    const typo = await call("pie_clip_validate", { text: clipText().replace(`track ${a.charge}`, `track ${a.charge.replace(/Charge/g, "Charg")}`) });
    expect(typo.valid).toBe(false);
    expect(typo.saveable).toBe(true);
    const issue = rows(typo.errors).find((e) => e.field === "action");
    expect(issue, JSON.stringify(typo.errors)).toBeDefined();
    expect(rows(issue?.candidates).map(String).join(",")).toContain("IA_ProbeCharge");

    const badValue = await call("pie_clip_validate", { text: clipText().replace("BaseEyeHeight", "BaseEyeHieght") });
    const valueIssue = rows(badValue.errors).find((e) => e.id === "c3" && e.field === "expect.value");
    expect(valueIssue, JSON.stringify(badValue.errors)).toBeDefined();
    expect(rows(valueIssue?.candidates).map(String)).toContain("BaseEyeHeight");

    const overlap = await call("pie_clip_validate", { text: `${clipText()}\nregion r3 250 300 "Late"` });
    expect(overlap.saveable).toBe(false);
    expect(rows(overlap.errors).some((e) => e.id === "r3" && String(e.message).includes("may not overlap"))).toBe(true);

    const clean = await call("pie_clip_validate", { text: clipText() });
    expect(clean.valid, JSON.stringify(clean.errors)).toBe(true);
  }, 30_000);
});

describe("storing a clip", () => {
  it("clip_create saves the text form, clip_read gives it back as JSON and text, and clip_list finds it", async () => {
    const refused = await callBridge(bridge, "pie_clip_create", { name: "CL_Format", package_path: DIR, text: `${clipText()}\nregion r3 250 300 "Late"` });
    expect(body(refused).success).toBe(false);
    expect(body(refused).errorCode).toBe("INVALID_CLIP");

    const created = await call("pie_clip_create", { name: "CL_Format", package_path: DIR, text: clipText() });
    expect(created.created).toBe(true);
    expect(obj(created.counts)).toMatchObject({ tracks: 2, notes: 1, keys: 2, checks: 3, regions: 2 });

    const read = await call("pie_clip_read", { path: CLIP });
    expect(String(read.text)).toContain("check c2 130 60 by_end ChargeCount eq 1 resolve_in_pie=true");
    const clip = obj(read.clip);
    expect(rows(clip.tracks).map((t) => t.action)).toEqual([a.move, a.charge]);
    expect(rows(clip.checks).find((c) => c.id === "c1")?.name).toBe("Walks forward");
    expect(clip.fixed_timestep).toBe(true);

    const list = await call("pie_clip_list", { directory: DIR });
    expect(rows(list.clips).map((c) => c.path)).toContain(CLIP);
  }, 30_000);

  it("clip_edit changes notes by id, all or none, and refuses an edit that names nothing with candidates", async () => {
    const edited = await call("pie_clip_edit", {
      path: CLIP,
      ops: [
        { op: "move", id: "n1", by: 10 },
        { op: "trim", id: "n1", length: 30 },
        { op: "add", kind: "note", track: a.charge.replace(/\.IA_ProbeCharge$/, ""), start: 200.4, length: 20 },
        { op: "set", id: "c1", expect: { target: 250 } },
      ],
    });
    expect(edited.updated).toBe(true);
    expect(rows(edited.changed)).toEqual(["n1", "n1", "n2", "c1"]);
    const notes = rows(rows(obj(edited.clip).tracks).find((t) => t.action === a.charge)?.notes);
    expect(notes.map((n) => [n.id, n.start, n.length])).toEqual([["n1", 140, 30], ["n2", 200, 20]]);
    expect(obj(rows(obj(edited.clip).checks).find((c) => c.id === "c1")?.expect)).toMatchObject({ op: "increasing", target: 250 });
    expect(edited.rollback).toBeDefined();

    const before = String((await call("pie_clip_read", { path: CLIP })).text);
    const refused = await callBridge(bridge, "pie_clip_edit", { path: CLIP, ops: [{ op: "delete", id: "n2" }, { op: "move", id: "n9", to: 0 }] });
    expect(body(refused).success).toBe(false);
    const issue = rows(body(refused).errors)[0];
    expect(String(issue?.message)).toContain("n9");
    expect(rows(issue?.candidates).length).toBeGreaterThan(0);
    const overlap = await callBridge(bridge, "pie_clip_edit", { path: CLIP, ops: [{ op: "move", id: "n2", to: 150 }] });
    expect(body(overlap).success).toBe(false);
    expect(String((await call("pie_clip_read", { path: CLIP })).text)).toBe(before);
  }, 30_000);

  it("clip_update replaces the clip from JSON, and clip_delete removes it", async () => {
    const updated = await call("pie_clip_update", {
      path: CLIP,
      clip: { rate: 30, regions: [{ name: "All", start: 0, end: 90 }], checks: [{ start: 0, length: 60, mode: "by_end", expect: { value: "pos_z", op: "gt", target: -100000 } }] },
    });
    expect(updated.updated).toBe(true);
    const clip = obj(updated.clip);
    expect(clip.rate).toBe(30);
    expect(rows(clip.regions)[0]?.id).toBe("r1");
    expect(rows(clip.checks)[0]?.id).toBe("c1");
    expect(String(updated.text)).toContain("check c1 0 60 by_end pos_z gt -100000");

    const deleted = await call("pie_clip_delete", { path: CLIP, confirm: true });
    expect(deleted.deleted).toBe(true);
    const gone = await callBridge(bridge, "pie_clip_read", { path: CLIP });
    expect(body(gone).success).toBe(false);
  }, 30_000);
});
