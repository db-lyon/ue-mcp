import { describe, expect, it } from "vitest";
import { emitScriptEvent, subscribeScriptEvents, type ScriptEvent } from "../../src/pie/script-events.js";

const event: ScriptEvent = {
  type: "step_passed",
  runId: "run-a",
  script: "/Game/PIEStudio/Scripts/Door",
  at: "2026-10-23T10:00:00.000Z",
  atMs: 1200,
  seq: 3,
  step: { index: 0, instruction: "Open the door", judge: "auto", status: "passed" },
};

describe("script event bus", () => {
  it("delivers every event to every subscriber until it unsubscribes", () => {
    const a: ScriptEvent[] = [];
    const b: ScriptEvent[] = [];
    const offA = subscribeScriptEvents((e) => a.push(e));
    const offB = subscribeScriptEvents((e) => b.push(e));
    emitScriptEvent(event);
    offA();
    emitScriptEvent({ ...event, seq: 4 });
    offB();
    emitScriptEvent({ ...event, seq: 5 });
    expect(a.map((e) => e.seq)).toEqual([3]);
    expect(b.map((e) => e.seq)).toEqual([3, 4]);
  });

  it("keeps delivering when a subscriber throws", () => {
    const seen: number[] = [];
    const offBad = subscribeScriptEvents(() => {
      throw new Error("dead connection");
    });
    const offGood = subscribeScriptEvents((e) => seen.push(e.seq));
    expect(() => emitScriptEvent(event)).not.toThrow();
    offBad();
    offGood();
    expect(seen).toEqual([3]);
  });
});
