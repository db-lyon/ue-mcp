/** The activity feed's reading of an MCP tools/call, through the gateway and the flow tool. */
import { describe, expect, it } from "vitest";
import { describeToolCall, resultError } from "../../src/daemon/activity.js";

describe("describeToolCall", () => {
  it("reads a category tool call", () => {
    expect(describeToolCall({ name: "level", arguments: { action: "get_outliner", args: {} } })).toEqual({ category: "level", action: "get_outliner" });
  });

  it("reads through the micro gateway", () => {
    expect(describeToolCall({ name: "tools", arguments: { action: "call", category: "blueprint", method: "create", args: {} } }))
      .toEqual({ category: "blueprint", action: "create" });
    expect(describeToolCall({ name: "tools", arguments: { action: "search", query: "x" } })).toEqual({ category: "tools", action: "search" });
  });

  it("names the flow a flow call runs", () => {
    expect(describeToolCall({ name: "flow", arguments: { action: "run", flowName: "cpp_iterate" } }))
      .toEqual({ category: "flow", action: "run", flowName: "cpp_iterate" });
  });

  it("survives a malformed call", () => {
    expect(describeToolCall(undefined)).toEqual({ category: "unknown", action: "unknown" });
  });
});

describe("resultError", () => {
  it("is the first text of a failed result, and nothing for a success", () => {
    expect(resultError({ isError: true, content: [{ type: "text", text: "Bridge error: nope" }] })).toBe("Bridge error: nope");
    expect(resultError({ content: [{ type: "text", text: "fine" }] })).toBeUndefined();
  });
});
