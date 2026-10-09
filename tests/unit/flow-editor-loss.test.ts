/**
 * on_editor_loss: a step that failed while the editor dropped has an unknown
 * outcome; once the editor is back the policy decides. Driven through the
 * guard's wrapped registry with a stub bridge that drops and returns.
 */
import { describe, expect, it } from "vitest";
import type { TaskRegistry, TaskResult } from "@db-lyon/flowkit";
import { EditorLossGuard, type ConnectionSource, type EditorLossPolicy } from "../../src/flow/editor-loss.js";

class StubBridge implements ConnectionSource {
  isConnected = true;
  lastDisconnectCause: string | null = null;
  private readonly listeners = new Set<(c: { connected: boolean; detail?: string }) => void>();
  /** Milliseconds until the editor comes back after a drop, or -1 for never. */
  returnAfterMs = 10;

  onConnectionChanged(l: (c: { connected: boolean; detail?: string }) => void): () => void {
    this.listeners.add(l);
    return () => this.listeners.delete(l);
  }

  drop(cause = "crashed"): void {
    this.isConnected = false;
    this.lastDisconnectCause = cause;
    for (const l of this.listeners) l({ connected: false, detail: "lost" });
  }

  async waitForConnection(timeoutMs: number): Promise<boolean> {
    if (this.isConnected) return true;
    if (this.returnAfterMs < 0 || this.returnAfterMs > timeoutMs) {
      await new Promise((r) => setTimeout(r, Math.min(timeoutMs, 20)));
      return false;
    }
    await new Promise((r) => setTimeout(r, this.returnAfterMs));
    this.isConnected = true;
    return true;
  }
}

/** A registry whose tasks run the given behaviours in order, one per create. */
function registryOf(runs: Array<(bridge: StubBridge) => Promise<TaskResult> | TaskResult>, bridge: StubBridge) {
  let i = 0;
  const calls: number[] = [];
  const registry = {
    async create() {
      const index = i++;
      return { run: async () => { calls.push(index); return runs[Math.min(index, runs.length - 1)](bridge); } };
    },
    listRegistered: () => [],
  } as unknown as TaskRegistry;
  return { registry, calls };
}

function guardFor(bridge: StubBridge, policy: EditorLossPolicy, readOnly = false, restore?: () => boolean) {
  return new EditorLossGuard(bridge, { policy, timeoutMs: 200, isReadOnly: () => readOnly, restoreWhileDown: restore });
}

async function runStep(guard: EditorLossGuard, registry: TaskRegistry, name = "level.place_actor", phase = "task") {
  const task = await guard.wrap(registry).create(name, { executionPhase: phase } as never, {});
  return (task as unknown as { run(): Promise<TaskResult> }).run();
}

const dropsMidStep = (b: StubBridge): TaskResult => {
  b.drop();
  return { success: false, error: new Error("Bridge connection lost") };
};

describe("on_editor_loss", () => {
  it("passes a step through untouched when the editor stays", async () => {
    const bridge = new StubBridge();
    const guard = guardFor(bridge, "resume");
    const { registry } = registryOf([() => ({ success: true, data: { ok: 1 } })], bridge);
    expect(await runStep(guard, registry)).toEqual({ success: true, data: { ok: 1 } });
    expect(guard.report()).toBeUndefined();
  });

  it("resume: carries on with the outcome marked unknown for a mutating step", async () => {
    const bridge = new StubBridge();
    const guard = guardFor(bridge, "resume");
    const { registry, calls } = registryOf([dropsMidStep], bridge);
    const result = await runStep(guard, registry);
    expect(result.success).toBe(true);
    expect((result.data as { editorLoss: { outcome: string; cause: string } }).editorLoss).toMatchObject({ outcome: "unknown", cause: "crashed" });
    expect(calls).toEqual([0]);
    expect(guard.report()?.events).toEqual([expect.objectContaining({ outcome: "unknown", cause: "crashed" })]);
  });

  it("resume: re-runs a read-only step as its verification", async () => {
    const bridge = new StubBridge();
    const guard = guardFor(bridge, "resume", true);
    const { registry, calls } = registryOf([dropsMidStep, () => ({ success: true, data: { actors: 3 } })], bridge);
    const result = await runStep(guard, registry, "level.get_outliner");
    expect(result).toEqual({ success: true, data: { actors: 3 } });
    expect(calls).toEqual([0, 1]);
    expect(guard.report()?.events[0].outcome).toBe("verified");
  });

  it("fail: fails the step once the editor is back", async () => {
    const bridge = new StubBridge();
    const guard = guardFor(bridge, "fail");
    const { registry } = registryOf([dropsMidStep], bridge);
    const result = await runStep(guard, registry);
    expect(result.success).toBe(false);
    expect(result.error?.message).toMatch(/went away while level\.place_actor ran \(crashed\); its outcome is unknown/);
  });

  it("rollback: restores the snapshot while the editor is still away, then fails the step", async () => {
    const bridge = new StubBridge();
    let restoredWhileDown: boolean | null = null;
    const guard = guardFor(bridge, "rollback", false, () => {
      restoredWhileDown = !bridge.isConnected;
      return true;
    });
    const { registry } = registryOf([dropsMidStep], bridge);
    const result = await runStep(guard, registry);
    expect(result.success).toBe(false);
    expect(restoredWhileDown).toBe(true);
    expect(guard.report()?.snapshotRestoredWhileDown).toBe(true);
  });

  it("fails plainly when the editor does not come back in time", async () => {
    const bridge = new StubBridge();
    bridge.returnAfterMs = -1;
    const guard = guardFor(bridge, "resume");
    const { registry } = registryOf([dropsMidStep], bridge);
    const result = await runStep(guard, registry);
    expect(result.success).toBe(false);
    expect(result.error?.message).toMatch(/did not come back within/);
    expect(guard.report()?.events[0].outcome).toBe("editor-did-not-return");
  });

  it("waits at the next step boundary for an editor that dropped between steps", async () => {
    const bridge = new StubBridge();
    const guard = guardFor(bridge, "resume");
    const { registry } = registryOf([
      () => ({ success: true }),
      (b) => ({ success: b.isConnected }),
    ], bridge);
    await runStep(guard, registry);
    bridge.drop("restarting");
    expect((await runStep(guard, registry)).success).toBe(true);
  });

  it("a rollback step that hits a drop waits and runs once more", async () => {
    const bridge = new StubBridge();
    const guard = guardFor(bridge, "fail");
    const { registry, calls } = registryOf([dropsMidStep, () => ({ success: true })], bridge);
    expect((await runStep(guard, registry, "level.delete_actor", "rollback")).success).toBe(true);
    expect(calls).toEqual([0, 1]);
  });

  it("reports completed steps that were not saved when the editor went away", async () => {
    const bridge = new StubBridge();
    const guard = guardFor(bridge, "resume");
    guard.stepCompleted("1", { saved: true });
    guard.stepCompleted("2", { saved: false });
    bridge.drop();
    expect(guard.report()?.unsavedBeforeLoss).toEqual(["2"]);
  });
});
