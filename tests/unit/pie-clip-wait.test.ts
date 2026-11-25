import { describe, expect, it } from "vitest";
import type { ToolContext } from "../../src/surface/tool-types.js";
import { clipWaitFollowUp, clipWaitState, type ClipWaitClock } from "../../src/pie/clip-wait.js";

/** A bridge that answers pie_clip_status from a script, one answer per call (the last repeats). */
function scripted(answers: Array<Record<string, unknown>>) {
  const calls: string[] = [];
  let i = 0;
  const ctx = {
    bridge: {
      call: async (method: string) => {
        calls.push(method);
        const a = answers[Math.min(i, answers.length - 1)];
        i++;
        return { success: true, run_id: "clip_1", ...a };
      },
    },
  } as unknown as ToolContext;
  return { ctx, calls };
}

function fakeClock(): ClipWaitClock {
  let t = 0;
  return { now: () => t, sleep: async (ms) => { t += ms; } };
}

const armed = { success: true, run_id: "clip_1", state: "armed", awaiting: [] };

describe("pie(clip_run) wait (#2097)", () => {
  it("answers at once without wait", async () => {
    const { ctx, calls } = scripted([]);
    expect(await clipWaitFollowUp(ctx, {}, armed, fakeClock())).toBe(armed);
    expect(calls).toHaveLength(0);
  });

  it("polls until the run finishes and answers with the final status", async () => {
    const { ctx } = scripted([{ state: "running" }, { state: "finished", passed: true }]);
    const out = (await clipWaitFollowUp(ctx, { wait: true }, armed, fakeClock())) as Record<string, unknown>;
    expect(out.passed).toBe(true);
    expect(out.wait).toMatchObject({ state: "finished" });
  });

  it("stops waiting at a manual check that needs a verdict", async () => {
    const { ctx } = scripted([{ state: "running", awaiting: ["m1"] }]);
    const out = (await clipWaitFollowUp(ctx, { wait: true }, armed, fakeClock())) as Record<string, unknown>;
    expect(out.wait).toMatchObject({ state: "awaiting_verdict" });
    expect(out.awaiting).toEqual(["m1"]);
  });

  it("times out with the run still going and says how to follow it", async () => {
    const { ctx } = scripted([{ state: "running" }]);
    const out = (await clipWaitFollowUp(ctx, { wait: true, timeout_seconds: 1 }, armed, fakeClock())) as Record<string, unknown>;
    expect(out.wait).toMatchObject({ state: "timed_out" });
    expect(String((out.warnings as string[])[0])).toContain("clip_status");
  });

  it("reads which states end a wait", () => {
    expect(clipWaitState({ state: "stopped" })).toBe("stopped");
    expect(clipWaitState({ state: "idle" })).toBe("finished");
    expect(clipWaitState({ state: "running", awaiting: [] })).toBeNull();
  });
});
