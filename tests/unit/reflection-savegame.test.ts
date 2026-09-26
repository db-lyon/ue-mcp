import { describe, expect, it, vi } from "vitest";
import { reflectionTool } from "../../src/tools/reflection.js";
import type { ToolContext } from "../../src/core/types.js";

describe("reflection.inspect_save_game", () => {
  // The action is spec'd (#1057): its bag goes to the bridge as sent, and a key
  // the handler does not declare comes back from it as paramsNotRead.
  it("forwards the slot selector to the native handler as sent", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await reflectionTool.handler(ctx, {
      action: "inspect_save_game",
      slotName: "UserSettings",
      userIndex: 2,
      className: "ignored",
    });

    expect(call).toHaveBeenCalledWith(
      "inspect_save_game",
      { slotName: "UserSettings", userIndex: 2, className: "ignored" },
      undefined,
    );
  });

  it("rejects negative and fractional user indexes in the public schema", () => {
    expect(reflectionTool.schema.userIndex.safeParse(-1).success).toBe(false);
    expect(reflectionTool.schema.userIndex.safeParse(0.5).success).toBe(false);
    expect(reflectionTool.schema.userIndex.safeParse(0).success).toBe(true);
  });
});
