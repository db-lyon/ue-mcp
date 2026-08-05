import { describe, expect, it, vi } from "vitest";
import { editorTool } from "../../src/tools/editor.js";
import type { ToolContext } from "../../src/types.js";

describe("editor.read_bone_transforms", () => {
  it("accepts and forwards relativeTo to the native handler", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    expect(editorTool.schema.relativeTo.safeParse("hand_r").success).toBe(true);
    expect(editorTool.schema.relativeTo.safeParse(42).success).toBe(false);

    await editorTool.handler(ctx, {
      action: "read_bone_transforms",
      actorLabel: "Character",
      componentName: "CharacterMesh0",
      bones: ["weapon_socket"],
      relativeTo: "hand_r",
      space: "world",
      world: "pie",
      pieInstance: 1,
    });

    expect(call).toHaveBeenCalledWith("read_bone_transforms", {
      actorLabel: "Character",
      componentName: "CharacterMesh0",
      bones: ["weapon_socket"],
      relativeTo: "hand_r",
      space: "world",
      limit: undefined,
      world: "pie",
      pieInstance: 1,
    }, undefined);
  });
});
