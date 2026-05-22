import { describe, it, expect } from "vitest";
import {
  subscribeFlowEvents,
  emitFlowEvent,
  nextRunId,
  type FlowEvent,
} from "../../src/flow/events.js";

describe("flow events bus", () => {
  it("delivers emitted events to subscribers", () => {
    const received: FlowEvent[] = [];
    const unsub = subscribeFlowEvents((e) => received.push(e));

    emitFlowEvent({
      type: "step_started",
      runId: "run-x",
      flowName: "f",
      step: { stepNumber: 1, type: "task", name: "t", skipped: false },
      timestamp: 1,
    });

    expect(received).toHaveLength(1);
    expect(received[0].type).toBe("step_started");
    unsub();
  });

  it("stops delivering after unsubscribe", () => {
    const received: FlowEvent[] = [];
    const unsub = subscribeFlowEvents((e) => received.push(e));
    unsub();

    emitFlowEvent({
      type: "run_completed",
      runId: "run-y",
      flowName: "f",
      success: true,
      duration: 1,
      stepCount: 0,
      timestamp: 1,
    });

    expect(received).toHaveLength(0);
  });

  it("isolates listener errors so a bad subscriber doesn't break others", () => {
    const received: FlowEvent[] = [];
    const unsubBad = subscribeFlowEvents(() => {
      throw new Error("listener oops");
    });
    const unsubGood = subscribeFlowEvents((e) => received.push(e));

    expect(() =>
      emitFlowEvent({
        type: "step_failed",
        runId: "run-z",
        flowName: "f",
        step: { stepNumber: 2, type: "task", name: "t", skipped: false },
        error: { message: "boom", name: "Error" },
        timestamp: 1,
      }),
    ).not.toThrow();

    expect(received).toHaveLength(1);
    unsubBad();
    unsubGood();
  });

  it("nextRunId returns distinct ids on consecutive calls", () => {
    const a = nextRunId();
    const b = nextRunId();
    expect(a).not.toBe(b);
    expect(a).toMatch(/^run-/);
    expect(b).toMatch(/^run-/);
  });
});
