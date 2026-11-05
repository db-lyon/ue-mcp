import { describe, expect, it } from "vitest";
import type { ToolContext } from "../../src/core/types.js";
import { pcgTool } from "../../src/tools/pcg.js";
import { waitAfterRegenerate, waitForGeneration, type WaitClock } from "../../src/tools/pcg-wait.js";
import { handlerSpecs } from "../../src/tools/specs/pcg.generated.js";

/** A bridge that answers get_pcg_generation_state from a script, one state per poll (the last repeats). */
function scripted(states: Array<Record<string, unknown>>) {
  const calls: Array<{ method: string; params: Record<string, unknown> | undefined }> = [];
  let i = 0;
  const ctx = {
    bridge: {
      call: async (method: string, params?: Record<string, unknown>) => {
        calls.push({ method, params });
        const state = states[Math.min(i, states.length - 1)];
        i++;
        return { success: true, componentName: "PCG", ...state };
      },
    },
  } as unknown as ToolContext;
  return { ctx, calls };
}

/** Time advances only when the wait sleeps. */
function fakeClock(): WaitClock & { slept: number[] } {
  let t = 0;
  const slept: number[] = [];
  return {
    slept,
    now: () => t,
    sleep: async (ms) => { slept.push(ms); t += ms; },
  };
}

describe("pcg wait_for_generation (#1365)", () => {
  it("waits for a generation in flight to finish, whoever started it", async () => {
    const { ctx, calls } = scripted([
      { busy: true, finishCount: 0 },
      { busy: true, finishCount: 0 },
      { busy: false, finishCount: 1, lastFinishState: "generated" },
    ]);
    const result = await waitForGeneration(ctx, { actorLabel: "Region" }, fakeClock());
    expect(result).toMatchObject({ success: true, state: "generated", polls: 3, componentName: "PCG" });
    expect(calls.every((c) => c.method === "get_pcg_generation_state" && c.params?.actorLabel === "Region")).toBe(true);
  });

  it("catches a run that starts after the call, within the grace window", async () => {
    const { ctx } = scripted([
      { busy: false, finishCount: 4 },
      { busy: false, finishCount: 4 },
      { busy: true, finishCount: 4 },
      { busy: false, finishCount: 5, lastFinishState: "cancelled" },
    ]);
    const result = await waitForGeneration(ctx, { actorPath: "/Game/Map.Map:PersistentLevel.Region" }, fakeClock());
    expect(result).toMatchObject({ success: true, state: "cancelled" });
  });

  it("reports idle when nothing generates within the grace window", async () => {
    const { ctx } = scripted([{ busy: false, finishCount: 2 }]);
    const clock = fakeClock();
    const result = await waitForGeneration(ctx, { actorLabel: "Region", graceSeconds: 1 }, clock);
    expect(result).toMatchObject({ success: true, state: "idle" });
    expect(clock.now()).toBeGreaterThanOrEqual(1000);
  });

  it("waits for the run it was given, not an earlier finish", async () => {
    const { ctx } = scripted([
      { busy: true, finishCount: 3, lastRun: { id: 7, state: "generating" } },
      { busy: false, finishCount: 4, lastRun: { id: 7, state: "generated" } },
    ]);
    const result = await waitForGeneration(ctx, { actorLabel: "Region", runId: 7 }, fakeClock());
    expect(result).toMatchObject({ success: true, state: "generated", runId: 7 });
  });

  it("follows a run whose Generate deferred its start, instead of reporting not_started", async () => {
    const { ctx } = scripted([
      { busy: false, finishCount: 2, lastRun: { id: 9, state: "not_started" } },
      { busy: true, finishCount: 2, lastRun: { id: 9, state: "generating", deferredStart: true } },
      { busy: false, finishCount: 3, lastRun: { id: 9, state: "generated", deferredStart: true } },
    ]);
    const result = await waitForGeneration(ctx, { actorLabel: "Region", runId: 9 }, fakeClock());
    expect(result).toMatchObject({ success: true, state: "generated", runId: 9, lastRun: { deferredStart: true } });
  });

  it("reports not_started after the grace window with what the component already holds", async () => {
    const { ctx } = scripted([{
      busy: false, finishCount: 1, generated: true, lastFinishState: "generated", lastFinishUtc: "2026-10-27T09:00:00.000Z",
      lastRun: { id: 4, state: "not_started" },
    }]);
    const clock = fakeClock();
    const result = await waitForGeneration(ctx, { actorLabel: "Region", runId: 4, graceSeconds: 1 }, clock);
    expect(result).toMatchObject({
      success: true, state: "not_started", alreadyGenerated: true, lastFinishUtc: "2026-10-27T09:00:00.000Z", lastFinishState: "generated",
    });
    expect(result.note).toContain("already up to date");
    expect(clock.now()).toBeGreaterThanOrEqual(1000);
  });

  it("times out, fails, and backs off between polls", async () => {
    const { ctx } = scripted([{ busy: true, finishCount: 0 }]);
    const clock = fakeClock();
    const result = await waitForGeneration(ctx, { actorLabel: "Region", timeoutSeconds: 5 }, clock);
    expect(result.success).toBe(false);
    expect(result.state).toBe("timed_out");
    expect(result.error).toMatch(/5s/);
    expect(clock.slept.slice(0, 4)).toEqual([250, 500, 1000, 1000]);
  });

  it("passes on a refusal from the editor as a failure", async () => {
    const ctx = { bridge: { call: async () => ({ success: false, error: "No PCGComponent on actor X." }) } } as unknown as ToolContext;
    const result = await waitForGeneration(ctx, { actorLabel: "X" }, fakeClock());
    expect(result.success).toBe(false);
    expect(result.error).toContain("No PCGComponent");
  });
});

describe("pcg execute and force_regenerate wait=true (#1365)", () => {
  it("leaves the answer alone without wait", async () => {
    const { ctx, calls } = scripted([]);
    const answered = { success: true, runId: 3 };
    expect(await waitAfterRegenerate(ctx, {}, answered, fakeClock())).toBe(answered);
    expect(calls).toHaveLength(0);
  });

  it("waits for the returned run and attaches the outcome", async () => {
    const { ctx, calls } = scripted([{ busy: false, finishCount: 1, lastRun: { id: 3, state: "generated" } }]);
    const out = await waitAfterRegenerate(ctx, { wait: true }, { success: true, runId: 3, actorPath: "/A", componentName: "PCG" }, fakeClock()) as Record<string, unknown>;
    expect(out.wait).toMatchObject({ state: "generated", runId: 3 });
    expect(calls[0].params).toEqual({ actorPath: "/A", componentName: "PCG" });
  });

  it("turns a timed-out wait into a warning, not a failed regenerate", async () => {
    const { ctx } = scripted([{ busy: true, lastRun: { id: 3, state: "generating" } }]);
    const out = await waitAfterRegenerate(ctx, { wait: true, timeoutSeconds: 1 }, { success: true, runId: 3, actorPath: "/A" }, fakeClock()) as Record<string, unknown>;
    expect(out.success).toBe(true);
    expect((out.wait as { state: string }).state).toBe("timed_out");
    expect(out.warnings).toHaveLength(1);
  });

  it("does not wait on a refused regenerate", async () => {
    const { ctx, calls } = scripted([]);
    const refused = { success: false, error: "generation_in_progress" };
    expect(await waitAfterRegenerate(ctx, { wait: true }, refused, fakeClock())).toBe(refused);
    expect(calls).toHaveLength(0);
  });

  it("is wired onto both actions, whose C++ specs declare wait and timeoutSeconds", () => {
    for (const [action, method] of [["execute", "execute_pcg_graph"], ["force_regenerate", "force_regenerate_pcg"]] as const) {
      const spec = pcgTool.actions[action];
      expect(spec.kind).toBe("bridge");
      expect(spec.kind === "bridge" && spec.followUp).toBe(waitAfterRegenerate);
      expect(handlerSpecs[method].params.map((p) => p.name)).toEqual(expect.arrayContaining(["wait", "timeoutSeconds"]));
    }
    expect(pcgTool.actions.wait_for_generation.kind).toBe("handler");
    expect(pcgTool.actions.wait_for_generation.effect).toBe("read");
  });

  it("runs through the category dispatcher: the bridge answers, then the follow-up waits", async () => {
    const methods: string[] = [];
    const ctx = {
      bridge: {
        isConnected: true,
        connect: async () => {},
        call: async (method: string) => {
          methods.push(method);
          return method === "execute_pcg_graph"
            ? { success: true, runId: 9, actorPath: "/A", componentName: "PCG" }
            : { success: true, busy: false, finishCount: 1, lastRun: { id: 9, state: "generated" } };
        },
      },
    } as unknown as ToolContext;
    const out = await pcgTool.handler(ctx, { action: "execute", actorPath: "/A", wait: true }) as Record<string, unknown>;
    expect(methods).toEqual(["execute_pcg_graph", "get_pcg_generation_state"]);
    expect(out.wait).toMatchObject({ state: "generated", runId: 9 });
  });
});
