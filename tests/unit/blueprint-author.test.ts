/**
 * blueprint(author) runs each step through the step's own action, so a step
 * the editor refuses with success:false is reported as failed.
 */
import { describe, expect, it } from "vitest";
import { blueprintTool } from "../../src/tools/blueprint.js";
import { buildFlowRegistry } from "../../src/flow/registry.js";
import { createLiveTask } from "../../src/flow/live-task.js";
import type { FlowContext } from "../../src/flow/context.js";
import type { IBridge } from "../../src/bridge/bridge.js";

function context(answer: (method: string, params: Record<string, unknown>) => unknown) {
  const calls: Array<{ method: string; params: Record<string, unknown> }> = [];
  const ctx = {
    project: {},
    bridge: {
      isConnected: true,
      call: async (method: string, params: Record<string, unknown>) => {
        calls.push({ method, params });
        return answer(method, params);
      },
    } as unknown as IBridge,
  } as FlowContext;
  return { ctx, calls };
}

const request = {
  assetPath: "/Game/BP_Door",
  parentClass: "Actor",
  components: [{ componentClass: "StaticMeshComponent" }],
  variables: [{ name: "Open", varType: "bool" }],
  functions: [{ functionName: "Toggle" }],
};

const refuseVariable = (method: string) =>
  method === "add_variable"
    ? { success: false, error: "Variable 'Open' already exists" }
    : { success: true, created: true };

describe("blueprint(author)", () => {
  it("reports a child that answered success:false as a failed step", async () => {
    const { ctx, calls } = context(refuseVariable);
    const out = await blueprintTool.handler(ctx, { action: "author", ...request }) as Record<string, unknown>;
    expect(calls.map((c) => c.method)).toEqual([
      "create_blueprint", "add_component", "add_variable", "create_function", "compile_blueprint",
    ]);
    expect(out).toMatchObject({ assetPath: "/Game/BP_Door", created: true, stepCount: 5, failedCount: 1, ok: false });
    const steps = out.steps as Array<Record<string, unknown>>;
    expect(steps.map((s) => [s.step, s.ok])).toEqual([
      ["create", true], ["add_component", true], ["add_variable", false], ["create_function", true], ["compile", true],
    ]);
    expect(steps[2]).toMatchObject({ target: "Open", error: expect.stringContaining("already exists") });
  });

  it("sends each step the action's own parameter names", async () => {
    const { ctx, calls } = context(() => ({ success: true }));
    await blueprintTool.handler(ctx, { action: "author", ...request });
    expect(calls[0].params).toEqual({ assetPath: "/Game/BP_Door", parentClass: "Actor" });
    expect(calls[1].params).toEqual({ assetPath: "/Game/BP_Door", componentClass: "StaticMeshComponent", componentName: "StaticMeshComponent" });
    expect(calls[2].params).toEqual({ assetPath: "/Game/BP_Door", name: "Open", varType: "bool" });
    expect(calls[3].params).toEqual({ assetPath: "/Game/BP_Door", functionName: "Toggle" });
    expect(calls[4].params).toEqual({ assetPath: "/Game/BP_Door" });
  });

  it("runs the steps through the registry on a live call", async () => {
    const { ctx, calls } = context(refuseVariable);
    const registry = buildFlowRegistry([blueprintTool]);
    const task = await createLiveTask({ registry }, ctx, "blueprint.author", { ...request, compile: false });
    const result = await task.run();
    expect(result.success).toBe(true);
    expect(result.data).toMatchObject({ stepCount: 4, failedCount: 1, ok: false });
    expect(calls.map((c) => c.method)).not.toContain("compile_blueprint");
  });

  it("records a thrown step as failed without a result", async () => {
    const { ctx } = context((method) => {
      if (method === "add_component") throw new Error("socket closed");
      return { success: true };
    });
    const out = await blueprintTool.handler(ctx, { action: "author", assetPath: "/Game/BP_Door", components: [{ componentClass: "X" }], compile: false }) as Record<string, unknown>;
    expect(out.steps).toEqual([{ step: "add_component", target: "X", ok: false, error: "socket closed" }]);
    expect(out.created).toBe(false);
  });
});
