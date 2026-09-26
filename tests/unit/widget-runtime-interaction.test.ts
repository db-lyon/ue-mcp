import { describe, expect, it, vi } from "vitest";
import { widgetTool } from "../../src/tools/widget.js";
import type { ToolContext } from "../../src/core/types.js";

describe("widget.invoke_runtime_function", () => {
  it("forwards the child interaction payload the C++ handler reads", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await widgetTool.handler(ctx, {
      action: "invoke_runtime_function",
      className: "WBP_Settings",
      childName: "InvertYCheck",
      value: true,
    });

    expect(call).toHaveBeenCalledWith(
      "invoke_runtime_function",
      {
        widgetName: undefined,
        className: "WBP_Settings",
        functionName: undefined,
        childName: "InvertYCheck",
        value: true,
        commitMethod: undefined,
      },
      undefined,
    );
  });

  it("carries the text commit selector through to the bridge", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await widgetTool.handler(ctx, {
      action: "invoke_runtime_function",
      widgetName: "WBP_Settings_C_0",
      childName: "PlayerName",
      functionName: "OnTextCommitted",
      value: "Ada",
      commitMethod: "OnUserMovedFocus",
    });

    expect(call).toHaveBeenCalledWith(
      "invoke_runtime_function",
      {
        widgetName: "WBP_Settings_C_0",
        className: undefined,
        functionName: "OnTextCommitted",
        childName: "PlayerName",
        value: "Ada",
        commitMethod: "OnUserMovedFocus",
      },
      undefined,
    );
  });

  it("accepts a bare button click with no value", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await widgetTool.handler(ctx, {
      action: "invoke_runtime_function",
      widgetName: "WBP_Settings_C_0",
      childName: "ApplyButton",
    });

    expect(call).toHaveBeenCalledWith(
      "invoke_runtime_function",
      {
        widgetName: "WBP_Settings_C_0",
        className: undefined,
        functionName: undefined,
        childName: "ApplyButton",
        value: undefined,
        commitMethod: undefined,
      },
      undefined,
    );
  });
});
