import { readFileSync } from "node:fs";
import { describe, expect, it, vi } from "vitest";
import { animationTool } from "../../src/tools/animation.js";
import type { ToolContext } from "../../src/core/types.js";

describe("animation.author_montages_batch", () => {
  it("exposes a structured montage batch schema", () => {
    const valid = animationTool.schema.items.safeParse([{
      name: "AM_Attack",
      animSequencePath: "/Game/Animations/ANIM_Attack",
      packagePath: "/Game/Animations/Montages",
      onConflict: "skip",
      slotName: "FullBody",
      blendIn: 0.1,
      blendOut: 0.15,
      sections: [{ sectionName: "Attack", startTime: 0 }],
      notifies: [{
        notifyName: "Damage",
        triggerTime: 0.35,
        notifyClass: "/Script/Game.DamageNotify",
        properties: { TraceGroup: "Bite" },
      }],
    }]);

    expect(valid.success).toBe(true);
    // Each item's fields come from the C++ spec (#1057): name and
    // animSequencePath are required, and a nested section or notify is
    // validated by the handler, which fails that item's stage by name.
    expect(animationTool.schema.items.safeParse([{
      name: "AM_Invalid",
      notifies: [{ notifyName: "Damage", triggerTime: 0.35 }],
    }]).success).toBe(false);

    // create_montage treats every onConflict but "error" as skip, so the batch
    // refuses anything else per item rather than skipping silently.
    const source = readFileSync(new URL(
      "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/AnimationHandlers.cpp",
      import.meta.url,
    ), "utf8");
    expect(source).toContain('if (ItemOnConflict != TEXT("skip") && ItemOnConflict != TEXT("error"))');
  });

  it("forwards the bag as sent, under its C++ spec (#1057)", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const items = [{
      name: "AM_Attack",
      animSequencePath: "/Game/Animations/ANIM_Attack",
    }];

    expect(animationTool.actions.author_montages_batch.mapParams).toBeUndefined();
    await animationTool.handler(ctx, {
      action: "author_montages_batch",
      items,
    });

    expect(call).toHaveBeenCalledWith(
      "author_montages_batch",
      { items },
      undefined,
    );
  });
});

describe("animation.add_notify", () => {
  it("carries notifyProperties through to the native handler", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await animationTool.handler(ctx, {
      action: "add_notify",
      assetPath: "/Game/Animations/AM_Attack",
      notifyName: "Damage",
      triggerTime: 0.35,
      notifyClass: "/Script/Game.DamageNotify",
      notifyProperties: { TraceGroup: "Bite" },
    });

    const [method, params] = call.mock.calls[0];
    expect(method).toBe("add_anim_notify");
    expect(params).toMatchObject({
      notifyClass: "/Script/Game.DamageNotify",
      notifyProperties: { TraceGroup: "Bite" },
    });
  });
});
