/**
 * The Playtest Hub's core (#2146): the contract's rules (clips without checks,
 * arrangements with a Checks track and regions, tests, nested suites, stable
 * file text, health, flakiness and pass-rate history) and the Hub service
 * over a fake runner (a test queued, played, judged by a verdict and
 * recorded on this machine; the agent actions over the same methods).
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import {
  arrangementOf,
  clipOf,
  fileText,
  flakiness,
  parseTestId,
  passRateHistory,
  stateFromChecks,
  suiteHealth,
  suiteLoop,
  suiteOf,
  suiteTests,
  testChecks,
  testHealth,
  type Clip,
  type PlayRequest,
  type PlayStatus,
  type RunRecord,
} from "../../src/contract/hub.js";
import { Hub } from "../../src/hub/hub.js";
import { runHubAction } from "../../src/hub/hub-tools.js";
import type { HubRunner } from "../../src/hub/ports.js";

const walk = { title: "Walk", performer: "player", input: [{ action: "/Game/IA_Move.IA_Move", type: "axis2d", keys: [{ id: "k2", t: 120, v: [0, 0] }, { id: "k1", t: 0, v: [0, 1] }] }] };
const arr = (over: Record<string, unknown> = {}) => ({
  title: "Door",
  tracks: [{ id: "hero", title: "Hero", performer: { kind: "player", client: 0 }, instances: [{ id: "i1", clip: "walk", at: 0 }] }],
  checks: [
    { id: "moved", title: "Moved", at: 0, length: 120, mode: "by_end", expect: { value: "pos_x", op: "increasing" } },
    { id: "open", title: "Open", at: 200, length: 0, mode: "manual", instruction: "Is it open?" },
  ],
  regions: [{ id: "walk", name: "Walk", start: 0, end: 150 }],
  ...over,
});

const run = (o: Partial<RunRecord>): RunRecord => ({ runId: "r", test: "t", title: "t", runner: "editor", source: "person", batch: "b", startedAt: "2026-01-01T00:00:00Z", endedAt: "x", state: "passed", checks: [], revision: "a", dirty: 0, ...o });

describe("the contract", () => {
  it("refuses checks in a clip and a lane its performer kind does not take", () => {
    expect(clipOf({ ...walk, checks: [] }, "walk").findings.map((f) => f.path)).toContain("checks");
    const actorInput = clipOf({ ...walk, performer: "actor" }, "walk").findings;
    expect(actorInput.some((f) => f.path === "input" && f.blocks)).toBe(true);
    const { clip, findings } = clipOf(walk, "walk");
    expect(findings).toEqual([]);
    expect(clip.length).toBe(120);
    expect(clip.input[0]!.keys.map((k) => k.id)).toEqual(["k1", "k2"]);
  });

  it("writes the same bytes for the same value, whatever the order given", () => {
    const a = clipOf(walk, "walk").clip;
    const b = clipOf({ ...walk, input: [{ ...walk.input[0], keys: [...walk.input[0]!.keys].reverse() }] }, "walk").clip;
    expect(fileText("clip", a)).toBe(fileText("clip", b));
    expect(fileText("clip", a).endsWith("}\n")).toBe(true);
    expect(fileText("clip", clipOf(JSON.parse(fileText("clip", a)), "walk").clip)).toBe(fileText("clip", a));
  });

  it("validates an arrangement's tracks, Checks track and regions", () => {
    const clips = (id: string) => (id === "walk" ? clipOf(walk, "walk").clip : undefined);
    expect(arrangementOf(arr(), "door", clips).findings).toEqual([]);
    const bad = arrangementOf(arr({
      tracks: [{ id: "g", title: "G", performer: { kind: "actor", actor: "Guard" }, instances: [{ id: "a", clip: "walk", at: 0 }, { id: "b", clip: "walk", at: 60 }] }],
      checks: [{ id: "m", at: 0, length: 0, mode: "manual" }, { id: "x", at: 0, length: 1, mode: "throughout" }],
      regions: [{ id: "r1", start: 0, end: 10 }, { id: "r2", start: 5, end: 20 }],
    }), "door", clips).findings.map((f) => f.message).join("\n");
    expect(bad).toMatch(/for a player/);
    expect(bad).toMatch(/before a ends/);
    expect(bad).toMatch(/needs an instruction/);
    expect(bad).toMatch(/needs expect/);
    expect(bad).toMatch(/overlap/);
  });

  it("makes tests of an arrangement and its regions", () => {
    const a = arrangementOf(arr(), "door").arrangement;
    expect(parseTestId("door:walk")).toEqual({ arrangement: "door", region: "walk" });
    expect(parseTestId("Door")).toBeNull();
    expect(testChecks(a).map((c) => c.id)).toEqual(["moved", "open"]);
    expect(testChecks(a, "walk").map((c) => c.id)).toEqual(["moved"]);
  });

  it("nests suites, finds loops and refuses one holding itself", () => {
    const suites = new Map([
      ["a", suiteOf({ items: [{ test: "door" }, { suite: "b" }] }, "a").suite],
      ["b", suiteOf({ items: [{ test: "door:walk" }, { test: "door" }, { suite: "gone" }] }, "b").suite],
    ]);
    expect(suiteTests(suites, "a")).toEqual({ tests: ["door", "door:walk"], missingSuites: ["gone"] });
    expect(suiteLoop(suites, "a")).toBeNull();
    suites.set("b", suiteOf({ items: [{ suite: "a" }] }, "b").suite);
    expect(suiteLoop(suites, "a")).toEqual(["a", "b", "a"]);
    expect(suiteOf({ items: [{ suite: "a" }] }, "a").findings[0]!.message).toMatch(/itself/);
  });

  it("works out health, flakiness on one revision, and pass-rate history per batch", () => {
    const pass = run({ state: "passed", checks: [{ id: "c", status: "passed" }] });
    const fail = run({ state: "failed", checks: [{ id: "c", status: "failed" }] });
    expect(testHealth([])).toBe("never_run");
    expect(testHealth([fail, pass])).toBe("failing");
    expect(testHealth([pass, fail, pass])).toBe("flaky");
    expect(flakiness([pass, fail]).checks).toEqual(["c"]);
    expect(flakiness([pass, { ...fail, revision: "older" }]).flaky).toBe(false);
    expect(testHealth([run({ state: "running", endedAt: null }), pass])).toBe("running");
    expect(stateFromChecks([{ status: "passed" }, { status: "skipped" }])).toBe("passed");
    expect(stateFromChecks([])).toBe("error");
    const counted = { quarantined: false, flaky: { detected: false, marked: false, checks: [] } };
    expect(suiteHealth([{ ...counted, health: "passing" }, { ...counted, health: "failing", quarantined: true }])).toBe("passing");
    expect(suiteHealth([{ ...counted, health: "passing", flaky: { detected: false, marked: true, checks: [] } }])).toBe("flaky");
    expect(passRateHistory(new Set(["t", "u"]), [run({ batch: "b1" }), run({ batch: "b1", test: "u", state: "failed" }), run({ batch: "b2", startedAt: "2026-02-01T00:00:00Z" })]))
      .toEqual([{ batch: "b1", at: "2026-01-01T00:00:00Z", passed: 1, total: 2, rate: 0.5 }, { batch: "b2", at: "2026-02-01T00:00:00Z", passed: 1, total: 1, rate: 1 }]);
  });
});

class FakeRunner implements HubRunner {
  readonly kind = "editor" as const;
  played: PlayRequest[] = [];
  status_: PlayStatus | null = null;
  verdicts: string[] = [];
  unavailable(): string | null {
    return null;
  }
  async play(r: PlayRequest) {
    this.played.push(r);
    return { pieStarted: true };
  }
  async status(runId: string): Promise<PlayStatus> {
    return this.status_ ?? { runId, state: "running", checks: [] };
  }
  async stop(): Promise<void> {}
  async verdict(_r: string, check: string, verdict: string): Promise<void> {
    this.verdicts.push(`${check}:${verdict}`);
  }
}

describe("the Hub service", () => {
  let dir: string;
  let runner: FakeRunner;
  let hub: Hub;
  const events: Array<{ type: string; data?: Record<string, unknown> }> = [];

  beforeEach(() => {
    dir = fs.mkdtempSync(path.join(os.tmpdir(), "hub-core-"));
    runner = new FakeRunner();
    events.length = 0;
    hub = new Hub({
      host: {
        projectDir: path.join(dir, "project"),
        dataDir: path.join(dir, "data"),
        callAction: async () => ({ isError: true, content: [{ type: "text", text: "no editor" }] }),
        editors: () => [{ connected: true }],
        events: { publish: (type, data) => events.push({ type, data }), subscribe: () => () => undefined },
        log: { warn: () => undefined },
      },
      editor: { pie: () => false, dialogs: () => 0, busy: () => ({ now: 0, calls: [] }), inputAgeMs: () => null },
      editorRunner: runner,
      workspace: async () => ({ vcs: "git", revision: "abc", dirty: 0 }),
      timers: false,
    });
  });

  afterEach(() => {
    hub.dispose();
    fs.rmSync(dir, { recursive: true, force: true });
  });

  it("plays a queued test with its audible tracks and clips, takes a verdict and records the result", async () => {
    hub.createClip(walk);
    hub.createArrangement(arr({ tracks: [...arr().tracks, { id: "muted", title: "Muted", performer: { kind: "player", client: 1 }, mute: true, instances: [] }] }));
    const q = hub.enqueue({ test: "door" }, "person");
    expect(q.queued).toHaveLength(1);
    await hub.tick();
    expect(runner.played).toHaveLength(1);
    const req = runner.played[0]!;
    expect(req.arrangement.tracks.map((t) => t.id)).toEqual(["hero"]);
    expect(Object.keys(req.clips)).toEqual(["walk"]);
    expect(hub.leaseView().lease).toMatchObject({ pie: true, runId: req.runId });

    runner.status_ = { runId: req.runId, state: "awaiting_verdict", checks: [{ id: "moved", status: "passed", frame: 80 }, { id: "open", status: "awaiting_verdict" }] };
    await hub.tick();
    expect(hub.run(req.runId).state).toBe("awaiting_verdict");
    await hub.verdict(req.runId, { check: "open", verdict: "pass" }, "person");
    expect(runner.verdicts).toEqual(["open:pass"]);

    runner.status_ = { runId: req.runId, state: "finished", checks: [{ id: "moved", status: "passed", frame: 80 }, { id: "open", status: "passed" }] };
    await hub.tick();
    const r = hub.run(req.runId);
    expect(r).toMatchObject({ state: "passed", revision: "abc", source: "person" });
    expect(r.checks.find((c) => c.id === "open")!.verdict).toMatchObject({ by: "person" });
    expect(hub.leaseView().lease).toBeNull();
    expect(hub.queueView().queued).toEqual([]);
    expect(hub.test("door").test).toMatchObject({ health: "passing", passRate: 1 });
    expect(fs.existsSync(path.join(dir, "data", "hub", "runs.json"))).toBe(true);
  });

  it("plays a region with only its checks, and refuses an agent's verdict on a person-only check", async () => {
    hub.createClip(walk);
    hub.createArrangement(arr({ checks: [...arr().checks.slice(0, 1), { id: "open", at: 100, length: 0, mode: "manual", instruction: "Look", personOnly: true }] }));
    hub.enqueue({ test: "door:walk" }, "agent");
    await hub.tick();
    const req = runner.played[0]!;
    expect(req.region).toBe("walk");
    expect(hub.run(req.runId).checks.map((c) => c.id)).toEqual(["moved", "open"]);
    await expect(hub.verdict(req.runId, { check: "open", verdict: "pass" }, "agent")).rejects.toThrow(/person's verdict only/);
    runner.status_ = { runId: req.runId, state: "finished", checks: [{ id: "moved", status: "failed", message: "pos_x did not increase" }] };
    await hub.tick();
    expect(hub.run(req.runId)).toMatchObject({ state: "failed" });
    expect(hub.run(req.runId).checks.find((c) => c.id === "open")!.status).toBe("skipped");
  });

  it("waits while the editor is busy and records a failed start as an error", async () => {
    hub.createClip(walk);
    hub.createArrangement(arr());
    runner.play = async () => {
      throw new Error("PIE could not start");
    };
    hub.enqueue({ test: "door" }, "person");
    await hub.tick();
    expect(hub.runs().runs[0]).toMatchObject({ state: "error", error: "PIE could not start" });
    expect(hub.leaseView().lease).toBeNull();
  });

  it("serves the agent actions over the same files", async () => {
    await runHubAction(hub, "write_clip", { body: walk });
    await expect(runHubAction(hub, "write_arrangement", { body: arr({ tracks: [{ id: "h", title: "H", performer: { kind: "player", client: 0 }, instances: [{ id: "i", clip: "wlak", at: 0 }] }] }) })).rejects.toThrow(/did you mean walk/);
    await runHubAction(hub, "write_arrangement", { body: JSON.stringify(arr()) });
    const suite = (await runHubAction(hub, "write_suite", { body: { title: "Smoke", items: [{ test: "door" }] } })) as { id: string };
    expect(suite.id).toBe("smoke");
    const quarantined = (await runHubAction(hub, "quarantine", { suite: "smoke", test: "door" })) as { health: string };
    expect(quarantined.health).toBe("empty");
    const queued = (await runHubAction(hub, "run", { suite: "smoke" })) as { queued: Array<{ source: string }> };
    expect(queued.queued[0]!.source).toBe("agent");
    const file = fs.readFileSync(path.join(dir, "project", "PlaytestHub", "arrangements", "door.json"), "utf8");
    expect(JSON.parse(file)).toMatchObject({ schema: 2, title: "Door", checks: [{ id: "moved" }, { id: "open" }] });
    expect(events.some((e) => e.type === "hub.changed")).toBe(true);
  });

  it("refuses a test with no checks with the way to add one", () => {
    hub.createClip(walk);
    hub.createArrangement(arr({ checks: [] }));
    expect(() => hub.enqueue({ test: "door" }, "person")).toThrow(/add one to the arrangement's Checks track/);
    expect((hub.clip("walk") as Clip).performer).toBe("player");
  });
});
