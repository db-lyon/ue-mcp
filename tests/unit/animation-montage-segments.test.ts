import { describe, expect, it, vi } from "vitest";
import { animationTool } from "../../src/tools/animation.js";
import { handlerSpecs } from "../../src/tools/specs/animation.generated.js";
import { classifyWrite } from "../../src/flow/write-methods.js";
import type { ToolContext } from "../../src/core/types.js";

describe("animation montage segment actions (#826)", () => {
  it("exposes the three segment actions", () => {
    for (const action of ["add_montage_segment", "remove_montage_segment", "list_montage_segments"]) {
      expect(animationTool.actions[action]).toBeDefined();
      expect(animationTool.actions[action].bridge).toBe(action);
    }
  });

  it("declares every add_montage_segment param in its C++ spec, and forwards the bag as sent (#1057)", async () => {
    expect(handlerSpecs.add_montage_segment.params.map((p) => p.name).sort()).toEqual([
      "animSequencePath", "assetPath", "endPos", "insertIndex", "loopCount", "playRate", "slotIndex", "slotName", "startPos",
    ]);
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await animationTool.handler(ctx, {
      action: "add_montage_segment",
      assetPath: "/Game/Animations/AM_HitReact",
      animSequencePath: "/Game/Animations/ANIM_Hit_Left",
      slotName: "DefaultSlot",
      slotIndex: 0,
      startPos: 0.1,
      endPos: 0.9,
      playRate: 1.5,
      loopCount: 2,
      insertIndex: 1,
    });

    expect(call).toHaveBeenCalledWith(
      "add_montage_segment",
      {
        assetPath: "/Game/Animations/AM_HitReact",
        animSequencePath: "/Game/Animations/ANIM_Hit_Left",
        slotName: "DefaultSlot",
        slotIndex: 0,
        startPos: 0.1,
        endPos: 0.9,
        playRate: 1.5,
        loopCount: 2,
        insertIndex: 1,
      },
      undefined,
    );
  });

  it("declares the removal target and slot selector only (#1057)", async () => {
    expect(handlerSpecs.remove_montage_segment.params.map((p) => p.name).sort())
      .toEqual(["assetPath", "segmentIndex", "slotIndex", "slotName"]);
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await animationTool.handler(ctx, {
      action: "remove_montage_segment",
      assetPath: "/Game/Animations/AM_HitReact",
      segmentIndex: 2,
      slotName: "DefaultSlot",
    });

    expect(call).toHaveBeenCalledWith(
      "remove_montage_segment",
      { assetPath: "/Game/Animations/AM_HitReact", segmentIndex: 2, slotName: "DefaultSlot" },
      undefined,
    );
  });

  it("reads segments with an optional slot filter", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await animationTool.handler(ctx, {
      action: "list_montage_segments",
      assetPath: "/Game/Animations/AM_HitReact",
    });

    expect(call).toHaveBeenCalledWith(
      "list_montage_segments",
      { assetPath: "/Game/Animations/AM_HitReact" },
      undefined,
    );
  });

  it("passes segmentIndex through to add_montage_section so a section can anchor to a segment", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await animationTool.handler(ctx, {
      action: "add_montage_section",
      assetPath: "/Game/Animations/AM_HitReact",
      sectionName: "Left",
      segmentIndex: 1,
      slotName: "DefaultSlot",
    });

    expect(call).toHaveBeenCalledWith(
      "add_montage_section",
      {
        assetPath: "/Game/Animations/AM_HitReact",
        sectionName: "Left",
        segmentIndex: 1,
        slotName: "DefaultSlot",
      },
      undefined,
    );
  });

  it("accepts the segment authoring params in the schema", () => {
    expect(animationTool.schema.startPos.safeParse(0.25).success).toBe(true);
    expect(animationTool.schema.endPos.safeParse(1.5).success).toBe(true);
    expect(animationTool.schema.playRate.safeParse(-1).success).toBe(true);
    expect(animationTool.schema.loopCount.safeParse(3).success).toBe(true);
    expect(animationTool.schema.loopCount.safeParse(0).success).toBe(false);
    expect(animationTool.schema.insertIndex.safeParse(0).success).toBe(true);
    expect(animationTool.schema.insertIndex.safeParse(-1).success).toBe(false);
  });

  it("classifies the mutating segment calls as guardable writes", () => {
    for (const method of ["add_montage_segment", "remove_montage_segment"]) {
      const r = classifyWrite(method, { assetPath: "/Game/Animations/AM_HitReact" });
      expect(r.writes).toBe(true);
      expect(r.contentPaths).toEqual(["/Game/Animations/AM_HitReact"]);
    }
    expect(classifyWrite("list_montage_segments", { assetPath: "/Game/Animations/AM_HitReact" }).writes).toBe(false);
  });
});
