import { describe, expect, it } from "vitest";
import { arrangementFromText, type Arrangement, type ClipFacts, type InstanceKind } from "../../src/contract/hub-arrangement.js";
import { clipFacts, composeArrangement as compose, laneFindings, lanePerformer, type ClipJson } from "../../src/contract/hub-compose.js";
import {
  PERFORMER_CAPABILITIES,
  clipLaneOf,
  performerAccepts,
  performerFromText,
  performerKey,
  performerOf,
  performerToText,
  trackPerformer,
  type ClipLane,
} from "../../src/contract/hub-performers.js";
import { applySetOps, setOpsFromText, setToText, type SetModel } from "../../src/contract/hub-set.js";

// A clip that walks a guard to two points, makes it attack, and raises its alert over time (#2137).
const GUARD: ClipJson = {
  rate: 60,
  length: 240,
  performer: { kind: "actor", actor: "Guard" },
  lanes: [
    { id: "l1", kind: "move", points: [{ id: "p1", t: 0, at: [100, 0, 0] }, { id: "p2", t: 120, at: [400, 0, 0], radius: 30 }] },
    { id: "l2", kind: "action", calls: [{ id: "a1", t: 200, fn: "Attack", args: [2] }] },
    { id: "l3", kind: "value", target: "bb:Alert", type: "axis1d", keys: [{ id: "k1", t: 0, v: 0 }, { id: "k2", t: 180, v: 1 }] },
  ],
  checks: [{ id: "c1", start: 0, length: 240, mode: "by_end", expect: { value: "Guard.Health", op: "gt", target: 0 } }],
};
const JUMP: ClipJson = {
  rate: 60,
  length: 60,
  tracks: [{ action: "/Game/Input/IA_Jump.IA_Jump", type: "bool", notes: [{ id: "n1", start: 0, length: 3 }] }],
  checks: [{ id: "c1", start: 0, length: 60, mode: "by_end", expect: { value: "pos_z", op: "increasing", target: 50 } }],
};
const CLIPS: Record<string, ClipJson> = { "/Game/Clips/CL_Guard": GUARD, "/Game/Clips/CL_Jump": JUMP };
const clip = (t: string) => CLIPS[t];
const facts = (target: string, kind: InstanceKind): ClipFacts | undefined => (kind === "clip" && CLIPS[target] ? clipFacts(target, target.slice(target.lastIndexOf("/") + 1), CLIPS[target]) : undefined);
const track = (text: string): Arrangement => arrangementFromText(text).arrangement;

describe("performers", () => {
  it("reads and writes a performer as a tagged union and in text", () => {
    expect(performerOf({ kind: "player", client: 2 })).toEqual({ kind: "player", client: 2 });
    expect(performerOf({ kind: "player" })).toEqual({ kind: "player", client: 0 });
    expect(performerOf({ kind: "actor", actor: "  Guard " })).toEqual({ kind: "actor", actor: "Guard" });
    expect(performerOf({ kind: "player", client: 99 })).toBeNull();
    expect(performerOf({ kind: "group" })).toBeNull();
    expect(performerFromText("actor:Guard 2")).toEqual({ kind: "actor", actor: "Guard 2" });
    expect(performerFromText("player:3")).toEqual({ kind: "player", client: 3 });
    expect(performerFromText("1")).toEqual({ kind: "player", client: 1 });
    expect(performerFromText("ghost:1")).toBeNull();
    expect(performerToText({ kind: "actor", actor: "Guard 2" })).toBe('actor:"Guard 2"');
    expect(performerKey({ kind: "actor", actor: "Guard" })).toBe(performerKey({ kind: "actor", actor: "guard" }));
  });

  it("makes every track stored before performers the player case", () => {
    expect(trackPerformer({})).toEqual({ kind: "player", client: 0 });
    expect(trackPerformer({ player: 2 })).toEqual({ kind: "player", client: 2 });
    expect(trackPerformer({ player: 0, performer: { kind: "actor", actor: "Guard" } })).toEqual({ kind: "actor", actor: "Guard" });
  });

  it("types lanes by capability and lets each kind declare what it takes", () => {
    expect(PERFORMER_CAPABILITIES.player).toEqual(["input", "action", "value"]);
    expect(PERFORMER_CAPABILITIES.actor).toEqual(["move", "action", "value"]);
    expect(performerAccepts({ kind: "actor", actor: "Guard" }, "input")).toBe(false);
    expect(performerAccepts({ kind: "player", client: 0 }, "move")).toBe(false);
    const lanes = (GUARD.lanes ?? []).map(clipLaneOf) as ClipLane[];
    expect(lanes.map((l) => l.kind)).toEqual(["move", "action", "value"]);
    expect(clipLaneOf({ id: "x", kind: "teleport" })).toBeNull();
  });

  it("checks a clip's lanes against the performer of its track", () => {
    const actor = { kind: "actor" as const, actor: "Guard" };
    expect(laneFindings("i1", GUARD, actor)).toEqual([]);
    // The clip's own performer drives its lanes on a track that names nobody else.
    expect(lanePerformer({ kind: "player", client: 0 }, clipLaneOf(GUARD.lanes![0])!, GUARD)).toEqual(actor);
    expect(laneFindings("i1", GUARD, { kind: "player", client: 0 })).toEqual([]);
    // A remote client's track overrides the clip, and a player takes no move lane.
    const onClient = laneFindings("i1", GUARD, { kind: "player", client: 1 });
    expect(onClient).toEqual([expect.objectContaining({ id: "i1", field: "performer", blocks: true, candidates: ["input", "action", "value"] })]);
    expect(onClient[0].message).toContain("move lane");
    // An actor takes no Input Action lane.
    const jump = laneFindings("i1", JUMP, actor);
    expect(jump).toEqual([expect.objectContaining({ field: "performer", blocks: true, candidates: ["move", "action", "value"] })]);
    expect(jump[0].message).toContain("IA_Jump");
  });

  it("composes an actor track's lanes offset, cut to the trim and naming its performer", () => {
    const c = compose([{ goalId: "t", arrangement: track("clip i1 /Game/Clips/CL_Guard 60 in=60"), player: 0, performer: { kind: "actor", actor: "Guard_2" } }], { clip, facts });
    expect(c.findings.filter((f) => f.blocks)).toEqual([]);
    expect(c.instances[0].performer).toEqual({ kind: "actor", actor: "Guard_2" });
    const seg = c.segments[0];
    if (seg.kind !== "clip") throw new Error("a clip segment");
    const lanes = seg.clip.lanes as Array<Record<string, unknown>>;
    expect(lanes.map((l) => [l.id, l.kind, l.performer])).toEqual([
      ["i1.l1", "move", { kind: "actor", actor: "Guard_2" }],
      ["i1.l2", "action", { kind: "actor", actor: "Guard_2" }],
      ["i1.l3", "value", { kind: "actor", actor: "Guard_2" }],
    ]);
    // In at 60: the point at 0 is cut, the one at 120 lands at 60 + (120 - 60).
    expect(lanes[0].points).toEqual([{ id: "i1.p2", t: 120, at: [400, 0, 0], radius: 30 }]);
    expect(lanes[1].calls).toEqual([{ id: "i1.a1", t: 200, fn: "Attack", args: [2] }]);
    // The curve begins on its value at the trim.
    expect((lanes[2].keys as Array<{ id: string; t: number; v: number }>).map((k) => [k.id, k.t, Math.round(k.v * 1000) / 1000])).toEqual([["i1.in-l3", 60, 0.333], ["i1.k2", 180, 1]]);
  });

  it("refuses a run that plays an Input Action lane on an actor's track", () => {
    const c = compose([{ goalId: "t", arrangement: track("clip i1 /Game/Clips/CL_Jump 0"), player: 0, performer: { kind: "actor", actor: "Guard" } }], { clip, facts });
    expect(c.findings).toEqual([expect.objectContaining({ id: "i1", field: "performer", blocks: true })]);
  });

  it("finds two tracks moving one actor at once in a scene, and lets them call it", () => {
    const a = track("clip i1 /Game/Clips/CL_Guard 0");
    const guard = { kind: "actor" as const, actor: "Guard" };
    const clash = compose([{ goalId: "a", arrangement: a, player: 0, performer: guard }, { goalId: "b", arrangement: a, player: 0, performer: { kind: "actor", actor: "guard" } }], { clip, facts, scene: true });
    expect(clash.findings.map((f) => f.message)).toEqual([expect.stringContaining("its moves for Guard"), expect.stringContaining("bb:Alert for Guard")]);
    const apart = compose([{ goalId: "a", arrangement: a, player: 0, performer: guard }, { goalId: "b", arrangement: a, player: 0, performer: { kind: "actor", actor: "Other" } }], { clip, facts, scene: true });
    expect(apart.findings).toEqual([]);
  });

  it("sets a track's performer by op and writes it in the set's text", () => {
    const empty: SetModel = { tracks: [], scenes: [] };
    let n = 0;
    const o = { factsOf: facts, newId: (t: string) => `${t.toLowerCase()}-${++n}` };
    const parsed = setOpsFromText('track add "Guard" performer=actor:"Guard 2"\ntrack add "Hero" player=1\ntrack performer Hero actor:Door');
    expect(parsed.findings).toEqual([]);
    const r = applySetOps(empty, parsed.ops, o);
    if (!r.ok) throw new Error(r.finding.message);
    expect(r.set.tracks.map((t) => [t.title, t.performer, t.player])).toEqual([
      ["Guard", { kind: "actor", actor: "Guard 2" }, 0],
      ["Hero", { kind: "actor", actor: "Door" }, 0],
    ]);
    expect(setToText(r.set)).toContain('track guard-1 Guard performer=actor:"Guard 2"');
    const back = applySetOps(r.set, setOpsFromText("track player Hero 2").ops, o);
    if (!back.ok) throw new Error(back.finding.message);
    expect(back.set.tracks[1].performer).toEqual({ kind: "player", client: 2 });
    const bad = applySetOps(r.set, setOpsFromText("track performer Hero ghost:1").ops, o);
    expect(bad.ok).toBe(false);
    if (!bad.ok) expect(bad.finding).toMatchObject({ field: "performer", blocks: true });
  });
});
