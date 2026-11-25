import { afterEach, describe, expect, it } from "vitest";
import { diffClipStatus, startClipWatch, stopAllClipWatches } from "../../src/pie/clip-watch.js";
import { subscribeClipEvents, type ClipEvent } from "../../src/pie/clip-events.js";
import type { ToolContext } from "../../src/surface/tool-types.js";

const check = (id: string, status: string, extra: Record<string, unknown> = {}) => ({ id, mode: "by_end", status, start: 0, length: 60, ...extra });
const region = (id: string, status: string, extra: Record<string, unknown> = {}) => ({ id, name: id, start: 0, end: 120, status, ...extra });
const status = (over: Record<string, unknown> = {}) => ({
  success: true, run_id: "clip_1", clip: "/Game/C/CL_Door", clip_name: "CL_Door", mode: "play", state: "running", rate: 60,
  position: 0, length: 240, game_ms: 0, held_ms: 0, frames: 0, checks: [check("c1", "pending"), check("c2", "pending")],
  regions: [region("r1", "pending")], events: [], awaiting: [], ...over,
});

afterEach(() => stopAllClipWatches());

describe("diffClipStatus", () => {
  it("starts a run with where it stands and the checks already past pending", () => {
    const changes = diffClipStatus(null, status({ position: 12, checks: [check("c1", "running"), check("c2", "pending")] }));
    expect(changes.map((c) => c.type)).toEqual(["run_started", "check"]);
    expect(changes[0]).toMatchObject({ mode: "play", clipName: "CL_Door", rate: 60, position: 12, length: 240, state: "running" });
  });

  it("reports only what changed between two answers", () => {
    const a = status({ position: 30, frames: 30, checks: [check("c1", "running"), check("c2", "pending")] });
    const b = status({
      position: 60, frames: 60, holding: "c2", game_ms: 1000,
      checks: [check("c1", "passed", { ended_frame: 44 }), check("c2", "running")],
      events: [{ id: "e1", frame: 50, game_ms: 833, ok: true }],
    });
    const changes = diffClipStatus(a, b);
    expect(changes.map((c) => c.type)).toEqual(["position", "event_fired", "check", "check"]);
    expect(changes[0]).toMatchObject({ position: 60, holding: "c2", gameMs: 1000 });
    expect(changes[2]).toMatchObject({ check: { id: "c1", status: "passed", ended_frame: 44 } });
    expect(diffClipStatus(b, b)).toEqual([]);
  });

  it("asks for a verdict once per set of manual checks, and finishes once", () => {
    const a = status({ position: 90 });
    const b = status({ position: 90, awaiting: ["c3"], frames: 91 });
    expect(diffClipStatus(a, b).map((c) => c.type)).toEqual(["position", "verdict_requested"]);
    expect(diffClipStatus(b, { ...b, frames: 92 }).map((c) => c.type)).toEqual(["position"]);
    const done = status({
      state: "finished", position: 240, passed: false, stop_reason: "ended", summary: { total: 2, failed: 1 },
      regions: [region("r1", "failed", { drift: { max_position_cm: 3 } })], results_path: "C:/P/Saved/MCPObservations/clip_1/clip_results.json",
    });
    const fin = diffClipStatus(b, done);
    expect(fin.map((c) => c.type)).toEqual(["position", "region", "run_finished"]);
    expect(fin[2]).toMatchObject({ state: "finished", passed: false, stopReason: "ended", summary: { failed: 1 } });
    expect(diffClipStatus(done, done)).toEqual([]);
  });
});

describe("the clip watcher", () => {
  it("polls the run to its end and stamps each event with the run and an increasing seq", async () => {
    const answers = [
      status({ position: 30, checks: [check("c1", "running"), check("c2", "pending")] }),
      status({ state: "finished", position: 240, passed: true, checks: [check("c1", "passed"), check("c2", "passed")] }),
    ];
    let polls = 0;
    const ctx = { bridge: { call: async () => answers[Math.min(polls++, answers.length - 1)] } } as unknown as ToolContext;
    const seen: ClipEvent[] = [];
    const off = subscribeClipEvents((e) => seen.push(e));
    try {
      const w = startClipWatch(ctx, "clip_1", status(), { clock: { now: () => 0, sleep: async () => {} } });
      await new Promise<void>((resolve) => {
        const t = setInterval(() => {
          if (w.ended) {
            clearInterval(t);
            resolve();
          }
        }, 5);
      });
    } finally {
      off();
    }
    expect(seen.map((e) => e.type)).toEqual(["run_started", "position", "check", "position", "check", "check", "run_finished"]);
    expect(seen.map((e) => e.seq)).toEqual([1, 2, 3, 4, 5, 6, 7]);
    expect(seen.every((e) => e.runId === "clip_1" && e.clip === "/Game/C/CL_Door")).toBe(true);
    expect(polls).toBe(2);
  });
});
