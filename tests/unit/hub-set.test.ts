import { describe, expect, it } from "vitest";
import type { ClipFacts, InstanceKind } from "../../src/contract/hub-arrangement.js";
import { applySetOps, audibleTracks, setOpsFromText, setToText, type SetModel, type SetOp } from "../../src/contract/hub-set.js";

const CLIPS: Record<string, number> = { "/Game/Clips/CL_Walk": 120, "/Game/Clips/CL_Jump": 30 };
const factsOf = (target: string, kind: InstanceKind): ClipFacts | undefined =>
  kind === "clip" && CLIPS[target] !== undefined ? { kind, target, name: target.slice(target.lastIndexOf("/") + 1), length: CLIPS[target], rate: 60 } : kind === "script" && target === "/Game/Tests/PT_Door" ? { kind, target, name: "PT_Door", length: 300, rate: 60 } : undefined;
let ids = 0;
const opts = { factsOf, library: (k: InstanceKind) => (k === "clip" ? Object.keys(CLIPS) : ["/Game/Tests/PT_Door"]), newId: (t: string) => `${t.toLowerCase().replace(/\W+/g, "-")}-${++ids}` };

const empty = (): SetModel => ({ tracks: [], scenes: [] });
const run = (m: SetModel, text: string) => {
  const parsed = setOpsFromText(text);
  expect(parsed.findings).toEqual([]);
  return applySetOps(m, parsed.ops, opts);
};
const ok = (m: SetModel, text: string): SetModel => {
  const r = run(m, text);
  if (!r.ok) throw new Error(r.finding.message);
  return r.set;
};

describe("the set", () => {
  it("builds tracks, scenes, slots and instances from op lines", () => {
    ids = 0;
    const m = ok(empty(), `track add "Walk" color=green
track add Guard player=1
scene add "Boss fight" color=orange
scene insert Intro at 0
slot set "Boss fight" Walk clip /Game/Clips/CL_Walk
slot set boss-fight-3 guard clip /Game/Clips/CL_Jump
add Walk clip /Game/Clips/CL_Walk
add Walk script /Game/Tests/PT_Door at 200`);
    expect(m.tracks.map((t) => [t.id, t.order, t.player])).toEqual([["walk-1", 0, 0], ["guard-2", 1, 1]]);
    expect(m.scenes.map((s) => [s.title, s.order])).toEqual([["Boss fight", 1], ["Intro", 0]]);
    expect(m.scenes[0].slots.map((s) => s.track)).toEqual(["walk-1", "guard-2"]);
    expect(m.tracks[0].arrangement.instances.map((i) => [i.id, i.start])).toEqual([["i1", 0], ["i2", 200]]);
    expect(setToText(m)).toBe(`set rate=60
track walk-1 Walk color=green player=0
  clip i1 /Game/Clips/CL_Walk 0
  script i2 /Game/Tests/PT_Door 200
track guard-2 Guard player=1
scene intro-4 Intro
scene boss-fight-3 "Boss fight" color=orange
  slot walk-1 clip /Game/Clips/CL_Walk
  slot guard-2 clip /Game/Clips/CL_Jump`);
  });

  it("edits clips: duplicate, copy, move between tracks and slots, color, rename, capture", () => {
    ids = 0;
    let m = ok(empty(), `track add A
track add B
scene add S
slot set S A clip /Game/Clips/CL_Jump
add A clip /Game/Clips/CL_Walk`);
    m = ok(m, `duplicate a-1/i1
copy a-1/i1 to b-2 at 50
move a-1/i2 to b-2 at 400
color b-2/i1 red
rename b-2/i1 "First"
slot copy S A to S B
slot color S B teal
scene capture S at 600
scene duplicate S`);
    const a = m.tracks.find((t) => t.id === "a-1")!;
    const b = m.tracks.find((t) => t.id === "b-2")!;
    expect(a.arrangement.instances.map((i) => [i.id, i.start, i.target])).toEqual([["i1", 0, "/Game/Clips/CL_Walk"], ["i3", 600, "/Game/Clips/CL_Jump"]]);
    expect(b.arrangement.instances.map((i) => [i.id, i.start, i.color, i.name])).toEqual([["i1", 50, "red", "First"], ["i2", 400, undefined, undefined], ["i3", 600, "teal", undefined]]);
    expect(m.scenes.map((s) => [s.title, s.order, s.slots.length])).toEqual([["S", 0, 2], ["S", 1, 2]]);
  });

  it("reorders, mutes, solos and deletes, a track's slots going with it", () => {
    ids = 0;
    let m = ok(empty(), "track add A\ntrack add B\ntrack add C\nscene add S\nslot set S B clip /Game/Clips/CL_Jump");
    m = ok(m, "track move C to 0\ntrack mute A\ntrack solo B on");
    expect([...m.tracks].sort((x, y) => x.order - y.order).map((t) => t.title)).toEqual(["C", "A", "B"]);
    expect(audibleTracks(m.tracks).map((t) => t.title)).toEqual(["B"]);
    m = ok(m, "track solo B off\ntrack delete B");
    expect(audibleTracks(m.tracks).map((t) => t.title)).toEqual(["C"]);
    expect(m.scenes[0].slots).toEqual([]);
  });

  it("answers a name that matches nothing with candidates, and changes nothing", () => {
    ids = 0;
    const m = ok(empty(), "track add Walk\nscene add Boss");
    const r = run(m, "track rename Wlk Run");
    expect(r.ok).toBe(false);
    if (!r.ok) expect(r.finding.candidates).toContain("Walk");
    const c = applySetOps(m, [{ op: "track.color", track: "Walk", color: "tael" } as SetOp], opts);
    expect(!c.ok && c.finding.candidates?.[0]).toBe("teal");
    const clip = run(m, "slot set Boss Walk clip /Game/Clips/CL_Jmp");
    expect(!clip.ok && clip.finding.candidates?.[0]).toBe("/Game/Clips/CL_Jump");
    expect(m.tracks[0].title).toBe("Walk");
  });

  it("names a line it cannot read", () => {
    const r = setOpsFromText("tack add A\nslot set S A clp /x");
    expect(r.findings.map((f) => f.candidates?.[0])).toEqual(["track", undefined]);
  });
});
