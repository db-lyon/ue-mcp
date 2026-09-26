import { describe, expect, it, vi } from "vitest";
import { editorTool } from "../../src/tools/editor.js";
import { declaredActionEffect } from "../../src/action-effects.js";
import { RECORDED_HANDLER_SPECS } from "../../src/tools/specs/index.js";
import type { ToolContext } from "../../src/core/types.js";

/**
 * #881: the playhead has to be movable to an exact frame, or a capture of the
 * evaluated world races realtime playback and lands wherever the tick left it.
 *
 * scrub_sequence is a separate action rather than a fourth sequenceAction verb:
 * the transport verbs are play|pause|stop and a scrub carries a time they have
 * no use for. play_sequence still refuses a fourth verb, but it refuses it in
 * the handler, which names the three it takes, rather than in the schema. The
 * MCP SDK validates arguments before the tool callback runs, so a strict enum
 * spent that refusal on a transport schema dump the caller cannot act on.
 */
describe("editor.scrub_sequence", () => {
  it("forwards the sequence and the requested time", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await editorTool.handler(ctx, {
      action: "scrub_sequence",
      sequencePath: "/Game/Cinematics/LS_Opening",
      frame: 48,
      timeUnit: "display",
    });

    expect(call).toHaveBeenCalledWith("scrub_sequence", {
      sequencePath: "/Game/Cinematics/LS_Opening",
      seconds: undefined,
      frame: 48,
      timeUnit: "display",
    }, undefined);
  });

  it("accepts assetPath, the way the other sequencer actions do", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await editorTool.handler(ctx, {
      action: "scrub_sequence",
      assetPath: "/Game/Cinematics/LS_Opening",
      seconds: 2.5,
    });

    // #1057: the bag goes to the bridge as sent, and the registry renames the
    // alias to sequencePath before the handler runs.
    expect(call).toHaveBeenCalledWith("scrub_sequence", {
      assetPath: "/Game/Cinematics/LS_Opening",
      seconds: 2.5,
    }, undefined);
    const sequencePath = RECORDED_HANDLER_SPECS.scrub_sequence.params.find((p) => p.name === "sequencePath");
    expect(sequencePath?.aliases).toContain("assetPath");
  });

  it("advertises the two time units and leaves the refusal to the handler", () => {
    expect(editorTool.schema.timeUnit.safeParse("display").success).toBe(true);
    expect(editorTool.schema.timeUnit.safeParse("tick").success).toBe(true);
    // scrub_sequence names both units when it rejects a third, and that message
    // is worth more than a schema dump, so the parse lets the value through.
    expect(editorTool.schema.timeUnit.safeParse("frames").success).toBe(true);
    expect(editorTool.schema.timeUnit.description).toContain("display");
    expect(editorTool.schema.timeUnit.description).toContain("tick");
  });

  it("documents the three play_sequence verbs and lets the handler refuse a fourth", () => {
    expect(editorTool.schema.sequenceAction.safeParse("play").success).toBe(true);
    expect(editorTool.schema.sequenceAction.safeParse("scrub").success).toBe(true);
    const described = editorTool.schema.sequenceAction.description ?? "";
    for (const verb of ["play", "pause", "stop"]) {
      expect(described).toContain(verb);
    }
  });

  it("classifies as a mutation, so it cannot land in an unnamed editor", () => {
    expect(declaredActionEffect("editor", "scrub_sequence")).toBe("mutate");
  });
});
