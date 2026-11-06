import { describe, expect, it } from "vitest";
import type { ToolContext } from "../../src/core/types.js";
import { pieTool } from "../../src/tools/pie.js";
import { driveFollowUp, type DriveClock } from "../../src/pie/drive-wait.js";
import { handlerSpecs } from "../../src/tools/specs/pie.generated.js";

/** A bridge that answers pie_drive_status from a script, one answer per call (the last repeats). */
function scripted(answers: Array<Record<string, unknown>>) {
  const calls: Array<{ method: string; params: Record<string, unknown> | undefined }> = [];
  let i = 0;
  const ctx = {
    bridge: {
      call: async (method: string, params?: Record<string, unknown>) => {
        calls.push({ method, params });
        const a = answers[Math.min(i, answers.length - 1)];
        i++;
        return { success: true, driveId: "drive_1", ...a };
      },
    },
  } as unknown as ToolContext;
  return { ctx, calls };
}

function fakeClock(): DriveClock {
  let t = 0;
  return { now: () => t, sleep: async (ms) => { t += ms; } };
}

const started = { success: true, driveId: "drive_1", running: true, state: "running", durationSeconds: 2 };

describe("pie(drive_pawn) wait (#1565)", () => {
  it("polls the summary until the drive ends, then answers with the full track", async () => {
    const { ctx, calls } = scripted([
      { running: true, state: "running" },
      { running: false, state: "arrived" },
      { running: false, state: "arrived", samples: [{ t: 0 }, { t: 0.1 }] },
    ]);
    const out = (await driveFollowUp(ctx, {}, started, fakeClock())) as Record<string, unknown>;
    expect(out.state).toBe("arrived");
    expect(out.samples).toHaveLength(2);
    expect((out.wait as Record<string, unknown>).state).toBe("ended");
    expect(calls.slice(0, 2).every((c) => c.params?.includeSamples === false)).toBe(true);
    expect(calls[2].params).toEqual({ driveId: "drive_1" });
  });

  it("returns the start answer untouched with wait=false", async () => {
    const { ctx, calls } = scripted([]);
    expect(await driveFollowUp(ctx, { wait: false }, started, fakeClock())).toBe(started);
    expect(calls).toHaveLength(0);
  });

  it("reads the track once for a drive that ended on its first frame", async () => {
    const { ctx, calls } = scripted([{ running: false, state: "no_path", navPath: { valid: false } }]);
    const out = (await driveFollowUp(ctx, {}, { ...started, running: false, state: "no_path" }, fakeClock())) as Record<string, unknown>;
    expect(out.state).toBe("no_path");
    expect(calls).toHaveLength(1);
  });

  it("gives up at timeoutSeconds with the drive still running and says how to reach it", async () => {
    const { ctx } = scripted([{ running: true, state: "running" }]);
    const out = (await driveFollowUp(ctx, { timeoutSeconds: 1 }, started, fakeClock())) as Record<string, unknown>;
    expect((out.wait as Record<string, unknown>).state).toBe("timed_out");
    expect(String((out.warnings as string[])[0])).toContain("drive_stop");
  });

  it("passes a refused start through", async () => {
    const { ctx, calls } = scripted([]);
    const refused = { success: false, error: "No PIE player to drive" };
    expect(await driveFollowUp(ctx, {}, refused, fakeClock())).toBe(refused);
    expect(calls).toHaveLength(0);
  });

  it("is wired to pie(drive_pawn) with the C++ parameter names", () => {
    const action = pieTool.actions.drive_pawn as { bridge?: string; followUp?: unknown };
    expect(action.bridge).toBe("pie_drive_pawn");
    expect(action.followUp).toBe(driveFollowUp);
    const names = handlerSpecs.pie_drive_pawn.params.map((p) => p.name);
    expect(names).toEqual(expect.arrayContaining(["direction", "targetLocation", "mode", "durationSeconds", "wait", "timeoutSeconds"]));
  });
});
