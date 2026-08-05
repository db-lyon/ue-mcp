import { describe, expect, it } from "vitest";
import { editorTool } from "../../src/tools/editor.js";

describe("editor.invoke_object_functions", () => {
  const calls = [
    { target: "playerpawn", functionName: "StartAction" },
    { objectPath: "/Game/Test.Pawn:Inventory", functionName: "StopAction", args: { immediate: true } },
  ];

  it("accepts a bounded ordered call list", () => {
    expect(editorTool.schema.calls.safeParse(calls).success).toBe(true);
    expect(editorTool.schema.calls.safeParse([]).success).toBe(false);
    expect(editorTool.schema.calls.safeParse([{ target: "playerpawn" }]).success).toBe(false);
    expect(editorTool.schema.calls.safeParse(Array.from({ length: 64 }, () => calls[0])).success).toBe(true);
    expect(editorTool.schema.calls.safeParse(Array.from({ length: 65 }, () => calls[0])).success).toBe(false);
  });

  it("forwards only the call list and shared world selector", () => {
    const action = editorTool.actions.invoke_object_functions;
    expect(action.bridge).toBe("invoke_object_functions");
    expect(action.timeoutMs).toBe(300_000);
    expect(action.mapParams?.({
      calls,
      world: "pie",
      pieInstance: 1,
      objectPath: "must-not-leak",
    })).toEqual({ calls, world: "pie", pieInstance: 1 });
  });
});
