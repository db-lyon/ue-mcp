import { describe, expect, it } from "vitest";
import {
  applyArrangementOps as applyOps,
  arrangementFromChecks,
  arrangementFromText,
  arrangementOf,
  arrangementRefs,
  arrangementToText,
  arrangementLayout as layoutOf,
  arrangementOpsFromText as opsFromText,
  validateArrangement,
  type Arrangement,
  type ClipFacts,
  type InstanceKind,
} from "../../src/contract/hub-arrangement.js";
import { clipFacts, composeArrangement as compose, instanceResults, type ClipJson } from "../../src/contract/hub-compose.js";

const WALK: ClipJson = {
  rate: 60,
  length: 120,
  map: "/Game/Maps/L_Arena",
  seed: 7,
  start: { location: [0, 0, 100] },
  tracks: [
    { action: "/Game/Input/IA_Move.IA_Move", type: "axis2d", keys: [{ id: "k1", t: 0, v: [0, 1], interp: "constant" }, { id: "k2", t: 100, v: [0, 0] }] },
    { action: "/Game/Input/IA_Jump.IA_Jump", type: "bool", notes: [{ id: "n1", start: 40, length: 6 }] },
  ],
  events: [{ id: "e1", at: 10, do: { press: { action: "/Game/Input/IA_Jump" } } }],
  checks: [
    { id: "c1", start: 0, length: 100, mode: "by_end", expect: { value: "pos_x", op: "increasing", target: 200 } },
    { id: "c2", start: 110, length: 0, mode: "hold", expect: { value: "speed2d", op: "lt", target: 1 } },
  ],
  regions: [{ id: "r1", name: "Walk", start: 0, end: 120 }],
};
const JUMP: ClipJson = {
  rate: 30,
  length: 30,
  map: "/Game/Maps/L_Arena",
  tracks: [{ action: "/Game/Input/IA_Jump.IA_Jump", type: "bool", notes: [{ id: "n1", start: 0, length: 3 }] }],
  checks: [{ id: "c1", start: 0, length: 30, mode: "by_end", expect: { value: "pos_z", op: "increasing", target: 50 } }],
};
const CLIPS: Record<string, ClipJson> = { "/Game/Clips/CL_Walk": WALK, "/Game/Clips/CL_Jump": JUMP };
const SCRIPT_FACTS: ClipFacts = { kind: "script", target: "/Game/Tests/PT_Door", name: "PT_Door", length: 300, rate: 60 };

const facts = (target: string, kind: InstanceKind): ClipFacts | undefined => {
  if (kind === "script") return target === SCRIPT_FACTS.target ? SCRIPT_FACTS : undefined;
  const c = CLIPS[target];
  return c ? clipFacts(target, target.slice(target.lastIndexOf("/") + 1), c) : undefined;
};
const library = (kind: InstanceKind) => (kind === "clip" ? Object.keys(CLIPS) : [SCRIPT_FACTS.target]);
const opts = { factsOf: facts, library };

const track = (text: string): Arrangement => {
  const r = arrangementFromText(text);
  expect(r.findings).toEqual([]);
  return r.arrangement;
};

describe("arrangement model", () => {
  it("reads and writes its text form", () => {
    const a = track(`arrangement rate=60
clip i1 /Game/Clips/CL_Walk.CL_Walk 0 in=10 out=100
script - /Game/Tests/PT_Door 90 length=300
clip - /Game/Clips/CL_Jump 390 name="Big jump"`);
    expect(a.instances.map((i) => i.id)).toEqual(["i1", "i2", "i3"]);
    expect(a.instances[0].target).toBe("/Game/Clips/CL_Walk");
    expect(arrangementFromText(arrangementToText(a)).arrangement).toEqual(a);
    expect(arrangementOf(JSON.parse(JSON.stringify(a))).arrangement).toEqual(a);
  });

  it("names a line it cannot read, with candidates", () => {
    const r = arrangementFromText("clp i1 /Game/Clips/CL_Walk 0\nclip i2 /Game/Clips/CL_Walk 0 trim=4");
    expect(r.findings[0].candidates).toContain("clip");
    expect(r.findings[1].candidates?.[0]).toBe("in");
  });

  it("lays instances out against their clips, scaling another clip rate", () => {
    const l = layoutOf(track("clip i1 /Game/Clips/CL_Walk 0 in=20\nclip i2 /Game/Clips/CL_Jump 100\nscript i3 /Game/Tests/PT_Door 200"), facts);
    expect(l.map((x) => [x.id, x.start, x.end])).toEqual([["i1", 0, 100], ["i2", 100, 160], ["i3", 200, 500]]);
    // A client trims in the clip's own frames, so each clip says its rate and length.
    expect(l.map((x) => [x.clipRate, x.clipLength])).toEqual([[60, 120], [30, 30], [undefined, undefined]]);
    expect(layoutOf(track("clip i1 /Game/Clips/CL_Gone 0"), facts)[0]).not.toHaveProperty("clipRate");
  });

  it("finds overlaps, bad trims and unknown clips with candidates", () => {
    const a = track(`clip i1 /Game/Clips/CL_Walk 0
clip i2 /Game/Clips/CL_Walk 60 in=50 out=40
script i3 /Game/Tests/PT_Door 400 in=3
clip i4 /Game/Clips/CL_Wlak 900
clip i5 /Game/Clips/CL_Walk 950 out=500`);
    const f = validateArrangement(a, opts);
    const by = (id: string, field: string) => f.find((x) => x.id === id && x.field === field);
    expect(by("i2", "start")?.candidates?.[0]).toBe("120");
    expect(by("i2", "out")?.blocks).toBe(true);
    expect(by("i3", "in")?.blocks).toBe(true);
    expect(by("i4", "target")).toMatchObject({ blocks: false, candidates: ["/Game/Clips/CL_Walk", "/Game/Clips/CL_Jump"] });
    expect(by("i5", "out")?.candidates).toEqual(["120"]);
  });

  it("edits with add, duplicate, move, trim, rename and delete, all or none", () => {
    const a = track("clip i1 /Game/Clips/CL_Walk 0");
    const { ops } = opsFromText(`add clip /Game/Clips/CL_Jump at 200
duplicate i1
move i2 by 300
trim i1 in=20 out=100
rename i3 "Walk again"
add script /Game/Tests/PT_Door`);
    const r = applyOps(a, ops, opts);
    expect(r.ok).toBe(true);
    if (!r.ok) return;
    expect(r.changed).toEqual(["i2", "i3", "i2", "i1", "i3", "i4"]);
    expect(r.arrangement.instances.map((i) => [i.id, i.start])).toEqual([["i1", 0], ["i3", 260], ["i2", 500], ["i4", 560]]);
    expect(r.arrangement.instances.find((i) => i.id === "i3")!.name).toBe("Walk again");
    const gone = applyOps(r.arrangement, opsFromText("delete i3").ops, opts);
    expect(gone.ok && gone.arrangement.instances.map((i) => i.id)).toEqual(["i1", "i2", "i4"]);
  });

  it("refuses an op that names nothing or overlaps, and changes nothing", () => {
    const a = track("clip i1 /Game/Clips/CL_Walk 0\nclip i2 /Game/Clips/CL_Walk 200");
    const missing = applyOps(a, [{ op: "move", id: "i9", to: 5 }], opts);
    expect(missing.ok).toBe(false);
    if (!missing.ok) expect(missing.finding.candidates).toContain("i1");
    const overlap = applyOps(a, opsFromText("move i2 to 50").ops, opts);
    expect(overlap.ok).toBe(false);
    if (!overlap.ok) expect(overlap.finding.candidates).toContain("120");
    const unknown = applyOps(a, opsFromText("add clip /Game/Clips/CL_Jmp").ops, opts);
    expect(!unknown.ok && unknown.finding.candidates?.[0]).toBe("/Game/Clips/CL_Jump");
    expect(a.instances.map((i) => i.start)).toEqual([0, 200]);
  });

  it("lays a goal's scripts end to end and lists its refs", () => {
    const a = arrangementFromChecks(["/Game/Tests/A", "/Game/Tests/B"], (t) => (t.endsWith("A") ? 2500 : null));
    expect(a.instances).toEqual([
      { id: "i1", kind: "script", target: "/Game/Tests/A", start: 0, length: 150 },
      { id: "i2", kind: "script", target: "/Game/Tests/B", start: 150, length: 600 },
    ]);
    expect(arrangementRefs(track("clip i1 /Game/Clips/CL_Walk 0\nclip i2 /Game/Clips/CL_Walk 200"))).toEqual([{ kind: "clip", target: "/Game/Clips/CL_Walk" }]);
  });
});

describe("composition", () => {
  const clip = (t: string) => CLIPS[t];

  it("composes a track into one clip with offset, trimmed and prefixed parts", () => {
    const c = compose([{ goalId: "g", arrangement: track("clip i1 /Game/Clips/CL_Walk 0 out=110\nclip i2 /Game/Clips/CL_Jump 200"), player: 0 }], { clip, facts });
    expect(c.findings).toEqual([]);
    expect(c.segments).toHaveLength(1);
    const seg = c.segments[0];
    if (seg.kind !== "clip") throw new Error("a clip segment");
    const j = seg.clip as Required<ClipJson>;
    expect(j.length).toBe(260);
    expect(j.map).toBe("/Game/Maps/L_Arena");
    expect(j.seed).toBe(7);
    expect(j.start).toEqual({ location: [0, 0, 100] });
    const jump = j.tracks.find((t) => t.action.endsWith("IA_Jump"))!;
    expect(jump.notes!.map((n) => [n.id, n.start, n.length])).toEqual([["i1.n1", 40, 6], ["i2.n1", 200, 6]]);
    const move = j.tracks.find((t) => t.action.endsWith("IA_Move"))!;
    // The curve ends at the instance's trimmed end with a zero key, so it never carries into the next instance.
    expect(move.keys!.map((k) => [k.t, k.v])).toEqual([[0, [0, 1]], [100, [0, 0]], [110, [0, 0]]]);
    expect(j.checks.map((k) => [k.id, k.start, k.length])).toEqual([["i1.c1", 0, 100], ["i2.c1", 200, 60]]);
    expect(j.events[0]).toMatchObject({ id: "i1.e1", at: 10 });
    expect(c.instances.map((i) => [i.key, i.start, i.end, i.checks])).toEqual([["i1", 0, 110, ["i1.c1"]], ["i2", 200, 260, ["i2.c1"]]]);
  });

  it("holds a curve's value up to a trim edge, then lets it go", () => {
    const c = compose([{ goalId: "g", arrangement: track("clip i1 /Game/Clips/CL_Walk 0 in=10 out=50"), player: 0 }], { clip, facts });
    const seg = c.segments[0];
    if (seg.kind !== "clip") throw new Error("a clip segment");
    const move = (seg.clip as Required<ClipJson>).tracks.find((t) => t.action.endsWith("IA_Move"))!;
    expect(move.keys!.map((k) => [k.t, k.v, k.interp])).toEqual([[0, [0, 1], "constant"], [39.99, [0, 1], "constant"], [40, [0, 0], "constant"]]);
    // The start pose belongs to the clip's frame 0, which the trim cut.
    expect((seg.clip as ClipJson).start).toBeUndefined();
  });

  it("splits at a step-list instance and resumes at its end", () => {
    const c = compose([{ goalId: "g", arrangement: track("clip i1 /Game/Clips/CL_Jump 0\nscript i2 /Game/Tests/PT_Door 100\nclip i3 /Game/Clips/CL_Jump 450"), player: 0 }], { clip, facts });
    expect(c.segments.map((s) => [s.kind, s.offset, s.length])).toEqual([["clip", 0, 100], ["script", 100, 300], ["clip", 400, 110]]);
    const last = c.segments[2];
    if (last.kind !== "clip") throw new Error("a clip segment");
    expect((last.clip as Required<ClipJson>).checks[0]).toMatchObject({ id: "i3.c1", start: 50 });
    expect((last.clip as ClipJson).map).toBeUndefined();
  });

  it("gives a scene's lanes their track's player and finds overlaps on one player", () => {
    const a = track("clip i1 /Game/Clips/CL_Jump 0");
    const ok = compose([{ goalId: "a", arrangement: a, player: 0 }, { goalId: "b", arrangement: a, player: 1 }], { clip, facts, scene: true });
    expect(ok.findings).toEqual([]);
    const seg = ok.segments[0];
    if (seg.kind !== "clip") throw new Error("a clip segment");
    const lanes = (seg.clip as Required<ClipJson>).tracks;
    expect(lanes.map((l) => [l.client ?? 0, l.notes!.map((n) => n.id)])).toEqual([[0, ["1-i1.n1"]], [1, ["2-i1.n1"]]]);
    const clash = compose([{ goalId: "a", arrangement: a, player: 0 }, { goalId: "b", arrangement: a, player: 0 }], { clip, facts, scene: true });
    expect(clash.findings[0]).toMatchObject({ id: "2-i1", field: "player", blocks: true });
    const beside = compose([{ goalId: "a", arrangement: track("script i1 /Game/Tests/PT_Door 0"), player: 0 }, { goalId: "b", arrangement: a, player: 1 }], { clip, facts, scene: true });
    expect(beside.findings[0]).toMatchObject({ id: "2-i1", field: "start", blocks: true });
  });

  it("gives remote players' act entries their client", () => {
    const a = track("clip i1 /Game/Clips/CL_Walk 0");
    const c = compose([{ goalId: "b", arrangement: a, player: 2 }], { clip, facts, scene: true });
    const seg = c.segments[0];
    if (seg.kind !== "clip") throw new Error("a clip segment");
    expect((seg.clip as Required<ClipJson>).events[0].do).toEqual({ press: { action: "/Game/Input/IA_Jump" }, client: 2 });
  });

  it("reports each instance at its place from its segment's results", () => {
    const c = compose([{ goalId: "g", arrangement: track("clip i1 /Game/Clips/CL_Jump 0\nscript i2 /Game/Tests/PT_Door 100\nclip i3 /Game/Clips/CL_Jump 450"), player: 0 }], { clip, facts });
    const r = instanceResults(c, [
      { runId: "clip_1", state: "finished", position: 100, checks: { "i1.c1": "passed" } },
      { runId: "script_1", state: "finished", outcome: "failed" },
      { runId: "clip_2", state: "running", position: 70, checks: { "i3.c1": "running" } },
    ]);
    expect(r.map((x) => [x.key, x.status, x.start, x.end, x.segmentRunId])).toEqual([
      ["i1", "passed", 0, 60, "clip_1"],
      ["i2", "failed", 100, 400, "script_1"],
      ["i3", "running", 450, 510, "clip_2"],
    ]);
  });
});
