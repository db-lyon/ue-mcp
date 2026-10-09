/**
 * editor.restart and editor.rebuild as flow tasks: through the daemon's
 * announced operation when it is there, through the actions otherwise.
 */
import { describe, expect, it } from "vitest";
import { TaskRegistry, BaseTask, type TaskConstructor, type TaskResult } from "@db-lyon/flowkit";
import { editorOperationTask } from "../../src/flow/editor-op-task.js";

function registryWith(results: Record<string, TaskResult>, calls: string[]): TaskRegistry {
  const registry = new TaskRegistry();
  registry.register("editor.restart", editorOperationTask("restart"));
  registry.register("editor.rebuild", editorOperationTask("rebuild"));
  for (const name of ["editor.restart_editor", "editor.stop_editor", "project.build", "editor.start_editor"]) {
    registry.register(name, class extends BaseTask {
      get taskName() { return name; }
      async execute(): Promise<TaskResult> {
        calls.push(name);
        return results[name] ?? { success: true };
      }
    } as unknown as TaskConstructor);
  }
  return registry;
}

async function run(registry: TaskRegistry, name: string, ctx: Record<string, unknown> = {}): Promise<TaskResult> {
  const task = await registry.create(name, { ...ctx, registry } as never, {});
  return task.run();
}

describe("editor operation tasks", () => {
  it("go through the daemon's announced operation when it is there", async () => {
    const calls: string[] = [];
    const ops: string[] = [];
    const registry = registryWith({}, calls);
    const result = await run(registry, "editor.rebuild", {
      sessions: { editorOperation: async (op: string) => { ops.push(op); return { op, steps: [{ step: "project.build", ok: true, result: "" }] }; } },
    });
    expect(result.success).toBe(true);
    expect(ops).toEqual(["rebuild"]);
    expect(calls).toEqual([]);
  });

  it("run stop, build and start in process, and stop at a failed build", async () => {
    const calls: string[] = [];
    const registry = registryWith({ "project.build": { success: false, error: new Error("compile error") } }, calls);
    const result = await run(registry, "editor.rebuild");
    expect(result.success).toBe(false);
    expect(result.error?.message).toMatch(/compile error/);
    expect(calls).toEqual(["editor.stop_editor", "project.build"]);
  });

  it("restart runs restart_editor in process", async () => {
    const calls: string[] = [];
    const result = await run(registryWith({}, calls), "editor.restart");
    expect(result.success).toBe(true);
    expect(calls).toEqual(["editor.restart_editor"]);
  });
});
