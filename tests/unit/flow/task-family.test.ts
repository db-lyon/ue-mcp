/**
 * The task family: one bridge executor, requirements declared on the class,
 * and the env view every task reads.
 */
import { describe, expect, it, vi } from "vitest";
import type { TaskResult } from "@db-lyon/flowkit";
import { BridgeTask } from "../../../src/flow/bridge-task.js";
import { bridgeTaskClass } from "../../../src/flow/task-factory.js";
import { buildFlowRegistry } from "../../../src/flow/registry.js";
import { ALL_TOOLS } from "../../../src/tools.js";
import { UeMcpTask, type TaskEnv } from "../../../src/task.js";
import type { FlowContext } from "../../../src/flow/context.js";

function bridge(reply: unknown = { success: true, value: 1 }) {
  return {
    isConnected: true,
    call: vi.fn(async () => reply),
    connect: async () => {},
    getTarget: () => ({ projectPath: null, port: 0, portSource: "default" as const, verified: true }),
    retargetProject: () => ({ projectPath: null, port: 0, portSource: "default" as const, verified: true }),
  };
}

describe("the bridge executor", () => {
  it("is the class every built-in bridge action and ue-mcp.bridge share", async () => {
    const registry = buildFlowRegistry(ALL_TOOLS);
    const bare = await registry.resolve("ue-mcp.bridge");
    expect(bare).toBe(BridgeTask);
    const bound = await registry.resolve("level.get_current");
    expect(Object.getPrototypeOf(bound)).toBe(BridgeTask);
  });

  it("runs a bound method and a YAML method through the same lifecycle", async () => {
    const b = bridge();
    const ctx = { bridge: b, project: {} } as unknown as FlowContext;
    const Bound = bridgeTaskClass("level.get_current", "get_current_level");
    const bound = await new (Bound as new (c: unknown, o: unknown) => BridgeTask)(ctx, { editor: "x" }).run();
    const bare = await new BridgeTask(ctx, { method: "get_current_level", editor: "x", timeoutMs: 5 }).run();
    expect(bound.success).toBe(true);
    expect(bare.success).toBe(true);
    // Neither the routing key nor the method name reach the editor as parameters.
    expect(b.call.mock.calls[0]).toEqual(["get_current_level", {}, undefined]);
    expect(b.call.mock.calls[1]).toEqual(["get_current_level", {}, 5]);
  });

  it("refuses to run without an editor", async () => {
    const result = await new BridgeTask({ project: {} } as unknown as FlowContext, { method: "get_current_level" }).run();
    expect(result.success).toBe(false);
    expect(result.error?.message).toContain("needs an editor");
  });
});

describe("TaskEnv", () => {
  it("names the project, editor, call and locks a task runs against", async () => {
    let seen: TaskEnv | undefined;
    class Probe extends UeMcpTask {
      get taskName() { return "probe"; }
      async execute(): Promise<TaskResult> {
        seen = this.env;
        return { success: true };
      }
    }
    const b = bridge();
    const project = { projectName: "Alpha", projectConfig: null };
    const locks = { acquireFor: async () => {}, releaseAll: async () => {} };
    await new Probe({ bridge: b, project, callTargeted: true, callTimeoutMs: 42, assetLocks: locks } as unknown as FlowContext, {}).run();
    expect(seen?.project).toBe(project);
    expect(seen?.editor?.bridge).toBe(b);
    expect(seen?.call).toMatchObject({ targeted: true, timeoutMs: 42 });
    expect(seen?.locks).toBe(locks);
  });

  it("checks a plugin task's declared requirements before it runs", async () => {
    const execute = vi.fn(async () => ({ success: true }));
    class NeedsProject extends UeMcpTask {
      static requires = { project: true };
      get taskName() { return "needs_project"; }
      execute = execute;
    }
    const result = await new NeedsProject({ bridge: bridge() } as unknown as FlowContext, {}).run();
    expect(result.success).toBe(false);
    expect(execute).not.toHaveBeenCalled();
  });
});
