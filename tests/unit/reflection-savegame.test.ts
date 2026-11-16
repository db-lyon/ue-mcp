import { describe, expect, it, vi } from "vitest";
import { reflectionTool } from "../../src/tools/reflection.js";
import type { ToolContext } from "../../src/surface/tool-types.js";

describe("reflection.inspect_save_game", () => {
  // The action is spec'd (#1057): its bag goes to the bridge as sent, and a key
  // the handler does not declare is refused before it is sent (#1336).
  it("forwards the slot selector to the native handler as sent", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await reflectionTool.handler(ctx, { action: "inspect_save_game", slotName: "UserSettings", userIndex: 2 });

    expect(call).toHaveBeenCalledWith("inspect_save_game", { slotName: "UserSettings", userIndex: 2 }, undefined);
  });

  it("refuses a key the action does not declare, naming what it takes (#1336)", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;
    await expect(reflectionTool.handler(ctx, { action: "inspect_save_game", slotName: "UserSettings", className: "ignored" }))
      .rejects.toThrow(/inspect_save_game does not take className; it takes slotName, userIndex/);
    expect(call).not.toHaveBeenCalled();
  });

  it("rejects negative and fractional user indexes in the public schema", () => {
    expect(reflectionTool.schema.userIndex.safeParse(-1).success).toBe(false);
    expect(reflectionTool.schema.userIndex.safeParse(0.5).success).toBe(false);
    expect(reflectionTool.schema.userIndex.safeParse(0).success).toBe(true);
  });
});
