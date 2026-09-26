import { describe, expect, it, vi } from "vitest";
import { editorTool } from "../../src/tools/editor.js";
import type { ToolContext } from "../../src/core/types.js";

/**
 * #973: a scripted call runs inside FEditorScriptExecutionGuard, which sets
 * GAllowActorScriptExecutionInEditor, and AActor::GetFunctionCallspace answers
 * Local unconditionally on that global. deferToNextTick is the opt-in that
 * moves the send outside the guard's scope. If the flag is dropped anywhere
 * between the schema and the bridge, the call silently keeps the wrong
 * routing, so the passthrough is asserted rather than assumed.
 */
describe("editor deferToNextTick", () => {
  it("is a boolean in the public schema", () => {
    expect(editorTool.schema.deferToNextTick.safeParse(true).success).toBe(true);
    expect(editorTool.schema.deferToNextTick.safeParse("yes").success).toBe(false);
  });

  it("forwards the flag from invoke_function", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await editorTool.handler(ctx, {
      action: "invoke_function",
      actorLabel: "BP_PlayerController_C_0",
      functionName: "Server_Commit",
      world: "pie",
      deferToNextTick: true,
    });

    expect(call).toHaveBeenCalledWith("invoke_function", {
      actorLabel: "BP_PlayerController_C_0",
      functionName: "Server_Commit",
      component: undefined,
      args: undefined,
      actorArgs: undefined,
      world: "pie",
      pieInstance: undefined,
      deferToNextTick: true,
    }, undefined);
  });

  it("forwards the flag from invoke_object_function", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await editorTool.handler(ctx, {
      action: "invoke_object_function",
      target: "playercontroller",
      functionName: "Server_Commit",
      deferToNextTick: true,
    });

    expect(call).toHaveBeenCalledWith("invoke_object_function", {
      functionName: "Server_Commit",
      objectPath: undefined,
      target: "playercontroller",
      subsystemClass: undefined,
      playerIndex: undefined,
      args: undefined,
      world: undefined,
      pieInstance: undefined,
      deferToNextTick: true,
    }, undefined);
  });

  it("leaves the flag off when it was never asked for", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await editorTool.handler(ctx, {
      action: "invoke_function",
      actorLabel: "BP_PlayerController_C_0",
      functionName: "Server_Commit",
    });

    const forwarded = call.mock.calls[0][1] as Record<string, unknown>;
    expect(forwarded.deferToNextTick).toBeUndefined();
  });
});
