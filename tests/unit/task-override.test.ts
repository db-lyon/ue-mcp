/**
 * A `tasks:` override replaces a built-in action, and reaches the built-in it
 * replaced through the base alias. Calling the overridden name from inside the
 * override is a loop, and is refused instead of recursing.
 */
import { describe, expect, it } from "vitest";
import { FlowRunner } from "@db-lyon/flowkit";
import type { TaskConstructor, TaskDefinition, TaskResult } from "@db-lyon/flowkit";
import { buildFlowRegistry } from "../../src/flow/registry.js";
import { buildDefaults } from "../../src/flow/loader.js";
import { bp, categoryTool } from "../../src/surface/category-tool.js";
import { UeMcpTask } from "../../src/task.js";
import { createLiveTask } from "../../src/flow/live-task.js";
import { buildMicroGateway } from "../../src/surface/context/micro-context.js";
import type { FlowContext } from "../../src/flow/context.js";
import type { IBridge } from "../../src/bridge/bridge.js";

const assetTool = categoryTool("asset", "Assets.", {
  list: bp("read", "list_assets"),
  delete: bp("mutate", "delete_asset"),
});

function context() {
  const calls: Array<{ method: string; params: Record<string, unknown> }> = [];
  const ctx = {
    project: {},
    bridge: {
      isConnected: true,
      call: async (method: string, params: Record<string, unknown>) => {
        calls.push({ method, params });
        return { assets: [{ path: "/Game/Keep/A" }, { path: "/Game/Developers/B" }] };
      },
    } as unknown as IBridge,
  } as FlowContext;
  return { ctx, calls };
}

/** Wraps asset.list through its base alias and filters the answer. */
class FilteredList extends UeMcpTask<{ excludePrefix?: string; directory?: string }> {
  get taskName() { return "asset.list"; }
  async execute(): Promise<TaskResult> {
    const result = await this.call("ue-mcp.builtin/asset.list", { directory: this.options.directory });
    const exclude = this.options.excludePrefix ?? "/Game/Developers/";
    const assets = (result.data?.assets as Array<{ path: string }>).filter((a) => !a.path.startsWith(exclude));
    return { success: result.success, data: { assets } };
  }
}

/** The documented mistake: calls the name it replaced. */
class SelfCalling extends UeMcpTask {
  get taskName() { return "asset.list"; }
  async execute(): Promise<TaskResult> {
    return this.call("asset.list", {});
  }
}

/** Two overrides that call each other. */
class PingTask extends UeMcpTask {
  get taskName() { return "asset.list"; }
  async execute(): Promise<TaskResult> { return this.call("asset.delete", {}); }
}
class PongTask extends UeMcpTask {
  get taskName() { return "asset.delete"; }
  async execute(): Promise<TaskResult> { return this.call("asset.list", {}); }
}

function setup(overrides: Record<string, Partial<TaskDefinition>>, classes: Record<string, unknown>) {
  const registry = buildFlowRegistry([assetTool]);
  for (const [classPath, ctor] of Object.entries(classes)) {
    registry.registerClassPath(classPath, ctor as TaskConstructor);
  }
  const defaults = buildDefaults([assetTool]).tasks as Record<string, TaskDefinition>;
  const tasks = { ...defaults, ...overrides } as Record<string, TaskDefinition>;
  return { registry, tasks };
}

function runner(overrides: Record<string, Partial<TaskDefinition>>, classes: Record<string, unknown>) {
  const { registry, tasks } = setup(overrides, classes);
  const { ctx, calls } = context();
  const flows = { go: { steps: { 1: { task: "asset.list", options: { directory: "/Game" } } } } };
  return { runner: new FlowRunner({ tasks, flows: flows as never, registry, context: ctx }), calls };
}

describe("built-in base aliases", () => {
  it("registers every built-in under its alias, and the defaults point there", () => {
    const { registry, tasks } = setup({}, {});
    expect(registry.listRegistered()).toEqual(expect.arrayContaining([
      "asset.list", "ue-mcp.builtin/asset.list", "asset.delete", "ue-mcp.builtin/asset.delete",
    ]));
    expect(tasks["asset.list"].class_path).toBe("ue-mcp.builtin/asset.list");
  });

  it("keeps the task name as the class path of a plugin's registry-kind action", () => {
    const pie = categoryTool("pie", "Plugin.", {
      inject_input: { kind: "registry", effect: "mutate", description: "Inject." },
    });
    const tasks = buildDefaults([pie]).tasks as Record<string, TaskDefinition>;
    expect(tasks["pie.inject_input"].class_path).toBe("pie.inject_input");
  });

  it("runs an override that calls the built-in through its alias", async () => {
    const { runner: r, calls } = runner(
      { "asset.list": { class_path: "tests.FilteredList", options: { excludePrefix: "/Game/Developers/" } } },
      { "tests.FilteredList": FilteredList },
    );
    const result = await r.run({ flowName: "go" });
    expect(result.success).toBe(true);
    expect(result.steps[0].result?.data).toEqual({ assets: [{ path: "/Game/Keep/A" }] });
    expect(calls).toEqual([{ method: "list_assets", params: { directory: "/Game" } }]);
  });

  it("refuses an override that calls the name it replaced, instead of recursing", async () => {
    const { runner: r, calls } = runner(
      { "asset.list": { class_path: "tests.SelfCalling" } },
      { "tests.SelfCalling": SelfCalling },
    );
    const result = await r.run({ flowName: "go" });
    expect(result.success).toBe(false);
    expect(result.steps[0].result?.error?.name).toBe("TaskCycleError");
    expect(result.steps[0].result?.error?.message).toContain("ue-mcp.builtin/asset.list");
    expect(calls).toEqual([]);
  });

  it("refuses two overrides that call each other", async () => {
    const { runner: r } = runner(
      { "asset.list": { class_path: "tests.Ping" }, "asset.delete": { class_path: "tests.Pong" } },
      { "tests.Ping": PingTask, "tests.Pong": PongTask },
    );
    const result = await r.run({ flowName: "go" });
    expect(result.success).toBe(false);
    expect(result.steps[0].result?.error?.message).toMatch(/already running on this call path/);
  });
});

describe("live calls resolve through the task definitions", () => {
  it("runs the override for a live call, with its option defaults under the caller's", async () => {
    const { registry, tasks } = setup(
      { "asset.list": { class_path: "tests.FilteredList", options: { excludePrefix: "/Game/Keep/", directory: "/Default" } } },
      { "tests.FilteredList": FilteredList },
    );
    const { ctx, calls } = context();
    const task = await createLiveTask({ registry, definitions: tasks }, ctx, "asset.list", { directory: "/Game" });
    const result = await task.run();
    expect(result.data).toEqual({ assets: [{ path: "/Game/Developers/B" }] });
    expect(calls).toEqual([{ method: "list_assets", params: { directory: "/Game" } }]);
  });

  it("gives a live task a registry, so call() works outside a flow", async () => {
    class Composite extends UeMcpTask {
      get taskName() { return "asset.delete"; }
      async execute(): Promise<TaskResult> { return this.call("asset.list", { directory: "/Game" }); }
    }
    const { registry, tasks } = setup({ "asset.delete": { class_path: "tests.Composite" } }, { "tests.Composite": Composite });
    const { ctx, calls } = context();
    const result = await (await createLiveTask({ registry, definitions: tasks }, ctx, "asset.delete", {})).run();
    expect(result.success).toBe(true);
    expect(calls.map((c) => c.method)).toEqual(["list_assets"]);
  });

  it("refuses a self-calling override on a live call instead of recursing", async () => {
    const { registry, tasks } = setup({ "asset.list": { class_path: "tests.SelfCalling" } }, { "tests.SelfCalling": SelfCalling });
    const { ctx, calls } = context();
    const result = await (await createLiveTask({ registry, definitions: tasks }, ctx, "asset.list", {})).run();
    expect(result.success).toBe(false);
    expect(result.error?.name).toBe("TaskCycleError");
    expect(calls).toEqual([]);
  });

  it("keeps a success:false body on a successful result, as the live route renders it", async () => {
    const { registry, tasks } = setup({}, {});
    const refusing = {
      project: {},
      bridge: { isConnected: true, call: async () => ({ success: false, error: "no such asset" }) } as unknown as IBridge,
    } as FlowContext;
    const result = await (await createLiveTask({ registry, definitions: tasks, namespaces: {} }, refusing, "asset.list", {})).run();
    expect(result.success).toBe(true);
    expect(result.data).toEqual({ success: false, error: "no such asset" });
  });

  it("runs the built-in by name when there are no definitions", async () => {
    const { registry } = setup({}, {});
    const { ctx, calls } = context();
    const result = await (await createLiveTask({ registry }, ctx, "asset.list", { directory: "/Game" })).run();
    expect(result.success).toBe(true);
    expect(calls).toEqual([{ method: "list_assets", params: { directory: "/Game" } }]);
  });

  it("applies the override behind the micro gateway too", async () => {
    const registry = buildFlowRegistry([buildMicroGateway([assetTool]), assetTool]);
    registry.registerClassPath("tests.FilteredList", FilteredList as unknown as TaskConstructor);
    const defaults = buildDefaults([assetTool]).tasks as Record<string, TaskDefinition>;
    const tasks = { ...defaults, "asset.list": { class_path: "tests.FilteredList", options: {} } };
    const { ctx } = context();
    const task = await createLiveTask({ registry, definitions: tasks }, ctx, "tools.call", {
      category: "asset", method: "list", args: { directory: "/Game" },
    });
    expect(task.constructor).toBe(FilteredList);
    expect((await task.run()).data).toEqual({ assets: [{ path: "/Game/Keep/A" }] });
  });
});
