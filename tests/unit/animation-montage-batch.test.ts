import { describe, expect, it, vi } from "vitest";
import { animationTool } from "../../src/tools/animation.js";
import type { ToolContext } from "../../src/types.js";

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
    expect(animationTool.schema.items.safeParse([{
      name: "AM_Invalid",
      animSequencePath: "/Game/Animations/ANIM_Attack",
      notifies: [{ notifyName: "Damage", triggerTime: -1 }],
    }]).success).toBe(false);
  });

  it("forwards only the items array to the native batch handler", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;
    const items = [{
      name: "AM_Attack",
      animSequencePath: "/Game/Animations/ANIM_Attack",
    }];

    await animationTool.handler(ctx, {
      action: "author_montages_batch",
      items,
      assetPath: "/Game/ShouldNotLeak",
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
