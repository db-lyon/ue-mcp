import { describe, expect, it } from "vitest";
import { nextProgressUpdate } from "../../src/editor-control.js";

describe("nextProgressUpdate", () => {
  it("never goes backwards even as the engine's percentage swings", () => {
    // Startup alternates between having a slow task and not, and each task
    // restarts its own percentage. Feeding those percentages to the client as
    // `progress` produced 68 -> 12 -> 33, which a strict client discards.
    const fractions = [undefined, 0.1, 0.68, undefined, 0.05, 0.9, undefined, 1];
    let last = -1;
    const sent: number[] = [];

    fractions.forEach((slowTaskFraction, i) => {
      const update = nextProgressUpdate({
        elapsedSeconds: i * 1.4,
        maxWaitSeconds: 300,
        lastReportedProgress: last,
        label: "Compiling Shaders",
        detail: `${i} modules`,
        slowTaskFraction,
      });
      if (update) {
        last = update.progress;
        sent.push(update.progress);
      }
    });

    expect(sent.length).toBeGreaterThan(1);
    for (let i = 1; i < sent.length; i++) {
      expect(sent[i]).toBeGreaterThan(sent[i - 1]);
    }
  });

  it("suppresses updates that would repeat the last value", () => {
    // Polling runs four times a second; only whole seconds are new information.
    expect(
      nextProgressUpdate({ elapsedSeconds: 7.2, maxWaitSeconds: 300, lastReportedProgress: 7, label: "x", detail: "" }),
    ).toBeNull();
  });

  it("carries the engine percentage in the message, not the progress value", () => {
    const update = nextProgressUpdate({
      elapsedSeconds: 12,
      maxWaitSeconds: 300,
      lastReportedProgress: 11,
      label: "Loading Default Modules",
      detail: "351 modules",
      slowTaskFraction: 0.73,
    });
    expect(update).not.toBeNull();
    expect(update!.progress).toBe(12);
    expect(update!.total).toBe(300);
    expect(update!.message).toBe("Loading Default Modules 73% (351 modules)");
  });

  it("clamps at the timeout so progress never exceeds total", () => {
    const update = nextProgressUpdate({
      elapsedSeconds: 999,
      maxWaitSeconds: 300,
      lastReportedProgress: 299,
      label: "x",
      detail: "",
    });
    expect(update!.progress).toBe(300);
    expect(update!.progress).toBeLessThanOrEqual(update!.total);
  });
});
