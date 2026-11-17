import { describe, expect, it } from "vitest";
import { editorTool } from "../../src/tools/editor.js";
import { REGISTERED_HANDLER_TIMEOUT_SECONDS } from "../../src/bridge/bridge-timeouts.js";
import { readHandlerFile } from "../../scripts/lib/cpp-registrations.mjs";

describe("editor.invoke_object_functions", () => {
  const calls = [
    { target: "playerpawn", functionName: "StartAction" },
    { objectPath: "/Game/Test.Pawn:Inventory", functionName: "StopAction", args: { immediate: true } },
  ];

  it("accepts an ordered call list whose entries each name a function", () => {
    expect(editorTool.schema.calls.safeParse(calls).success).toBe(true);
    expect(editorTool.schema.calls.safeParse([{ target: "playerpawn" }]).success).toBe(false);
    expect(editorTool.schema.calls.safeParse([{ functionName: "F", args: 42 }]).success).toBe(false);
    expect(editorTool.schema.calls.safeParse(Array.from({ length: 64 }, () => calls[0])).success).toBe(true);
  });

  it("bounds the list in the handler, before any call runs (#1057)", () => {
    const source = readHandlerFile("EditorHandlers_PIERuntime.cpp");
    expect(source).toContain("Missing required non-empty array parameter 'calls'");
    expect(source).toContain("'calls' accepts at most 64 entries");
  });

  it("is spec'd: it forwards its bag as sent, with its long timeout", () => {
    const action = editorTool.actions.invoke_object_functions;
    expect(action.bridge).toBe("invoke_object_functions");
    expect(action.timeoutMs).toBeUndefined();
    expect(REGISTERED_HANDLER_TIMEOUT_SECONDS.invoke_object_functions).toBe(300);
    expect(action.mapParams).toBeUndefined();
  });
});
