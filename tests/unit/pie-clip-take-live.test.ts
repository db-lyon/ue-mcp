import { afterEach, describe, expect, it } from "vitest";
import { LiveTake } from "../../src/pie/clip-take-live.js";
import { startClipWatch, stopAllClipWatches, withLiveTake } from "../../src/pie/clip-watch.js";
import { subscribeClipEvents, type ClipEvent } from "../../src/pie/clip-events.js";
import type { ToolContext } from "../../src/surface/tool-types.js";

const JUMP = "/Game/Input/IA_Jump.IA_Jump";
const MOVE = "/Game/Input/IA_Move.IA_Move";
const win = (from: number, frames: number[], jump: number[], move: number[][]) => ({
  from, frames,
  tracks: [{ action: JUMP, type: "bool", values: jump }, { action: MOVE, type: "axis2d", values: move }],
});

afterEach(() => stopAllClipWatches());

describe("a live take", () => {
  it("draws notes from edges, holding the last one open, and keys where the value changes", () => {
    const t = new LiveTake();
    const a = t.apply(win(0, [10, 11, 12, 13], [0, 1, 1, 1], [[0, 0], [0, 1], [0, 1], [0, 1]]))!;
    expect(a.samples).toBe(4);
    expect(a.tracks[0]).toEqual({ action: JUMP, type: "bool", notes: [{ start: 11, length: 3, open: true }], keys: [], keysFrom: 0 });
    expect(a.tracks[1].keys).toEqual([{ t: 10, v: [0, 0] }, { t: 11, v: [0, 1] }]);

    // The next window overlaps the first; only the new samples count.
    const b = t.apply(win(2, [12, 13, 14, 15], [1, 1, 0, 0], [[0, 1], [0, 1], [1, 0], [1, 0]]))!;
    expect(b.samples).toBe(6);
    expect(b.frame).toBe(15);
    expect(b.tracks[0].notes).toEqual([{ start: 11, length: 3 }]);
    expect(b.tracks[1]).toMatchObject({ keys: [{ t: 14, v: [1, 0] }], keysFrom: 2 });
    expect(t.apply(win(2, [12, 13, 14, 15], [1, 1, 0, 0], [[0, 1], [0, 1], [1, 0], [1, 0]]))).toBeNull();

    expect(t.snapshot().tracks[1].keys).toHaveLength(3);
  });
});

describe("the clip watcher during a recording", () => {
  it("emits take_progress before the run ends and hands clip_status the take so far", async () => {
    const base = { success: true, run_id: "clip_r", clip: "/Game/C/CL", mode: "record", position: 0, length: 120, checks: [], regions: [], events: [] };
    const take = (live: object | null, extra: object = {}) => ({ take_id: "t1", punch_in: 10, punched_in: true, ...(live ? { live } : {}), ...extra });
    const answers = [
      { ...base, state: "running", position: 14, take: take(win(0, [10, 11, 12, 13], [0, 1, 1, 1], [[0, 0], [0, 1], [0, 1], [0, 1]])) },
      { ...base, state: "finished", position: 16, take: take(win(0, [10, 11, 12, 13, 14, 15], [0, 1, 1, 1, 0, 0], [[0, 0], [0, 1], [0, 1], [0, 1], [0, 1], [0, 1]]), { saved: true }) },
    ];
    let polls = 0;
    let release!: () => void;
    const gate = new Promise<void>((r) => (release = r));
    const ctx = {
      bridge: {
        call: async () => {
          if (polls === 1) await gate;
          return answers[Math.min(polls++, answers.length - 1)];
        },
      },
    } as unknown as ToolContext;
    const seen: ClipEvent[] = [];
    const off = subscribeClipEvents((e) => seen.push(e));
    try {
      const w = startClipWatch(ctx, "clip_r", { ...base, state: "armed" }, { clock: { now: () => 0, sleep: async () => {} } });
      await new Promise<void>((resolve) => {
        const t = setInterval(() => {
          if (polls >= 1) {
            clearInterval(t);
            resolve();
          }
        }, 2);
      });
      await new Promise((r) => setTimeout(r, 10));
      // Mid-take, clip_status gets the take as notes and keys instead of the raw window.
      const mid = withLiveTake(answers[0]) as { take: Record<string, unknown> };
      expect(mid.take).not.toHaveProperty("live");
      expect(mid.take.live_take).toMatchObject({ samples: 4, tracks: [{ action: JUMP, notes: [{ start: 11, open: true }] }, { action: MOVE }] });
      release();
      await new Promise<void>((resolve) => {
        const t = setInterval(() => {
          if (w.ended) {
            clearInterval(t);
            resolve();
          }
        }, 2);
      });
    } finally {
      off();
    }
    const types = seen.map((e) => e.type);
    expect(types.filter((t) => t === "take_progress")).toHaveLength(2);
    expect(types.indexOf("take_progress", types.indexOf("take_progress") + 1)).toBeLessThan(types.indexOf("run_finished"));
    const last = seen.filter((e) => e.type === "take_progress").pop() as Extract<ClipEvent, { type: "take_progress" }>;
    expect(last).toMatchObject({ takeId: "t1", punchIn: 10, samples: 6, tracks: [{ action: JUMP, notes: [{ start: 11, length: 3 }] }, { action: MOVE, keys: [], keysFrom: 2 }] });
  });
});
