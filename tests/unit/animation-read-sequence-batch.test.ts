import { describe, expect, it, vi } from "vitest";
import { animationTool } from "../../src/tools/animation.js";
import type { ToolContext } from "../../src/core/types.js";
import { readHandlerFile } from "../../scripts/lib/cpp-registrations.mjs";

const source = readHandlerFile("AnimationHandlers_Sequence.cpp");
const body = source.slice(source.indexOf("// read_anim_sequence"), source.indexOf("// scan_animation_tracks"));

describe("animation(read_sequence) batch form (#1163)", () => {
  it("forwards every batch parameter under the name the handler reads", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;

    await animationTool.handler(ctx, {
      action: "read_sequence",
      directory: "/Game/Anims",
      recursive: false,
      nameFilter: "strafe*",
      fields: ["sequenceLength", "rateScale"],
      cursor: "abc",
      limit: 20,
    });

    expect(call).toHaveBeenCalledWith("read_anim_sequence", {
      directory: "/Game/Anims",
      recursive: false,
      nameFilter: "strafe*",
      fields: ["sequenceLength", "rateScale"],
      cursor: "abc",
      limit: 20,
    }, undefined);
  });

  it("forwards assetPaths", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;
    await animationTool.handler(ctx, { action: "read_sequence", assetPaths: ["/Game/A", "/Game/B"] });
    expect(call).toHaveBeenCalledWith("read_anim_sequence", { assetPaths: ["/Game/A", "/Game/B"] }, undefined);
  });

  it("declares the new parameters on the category schema", () => {
    expect(animationTool.schema.nameFilter).toBeDefined();
    expect(animationTool.schema.fields.safeParse(["rateScale"]).success).toBe(true);
    expect(animationTool.schema.assetPaths.description).toContain("read_anim_sequence");
  });

  it("documents the batch form, its paging and its per-row failures", () => {
    const description = animationTool.actions.read_sequence.description ?? "";
    for (const name of ["assetPaths", "directory", "nameFilter", "fields", "cursor", "limit", "failedCount"]) {
      expect(description).toContain(name);
    }
  });

  it("reads every parameter the tool forwards, through the recorded helpers", () => {
    for (const key of ["fields", "assetPaths", "directory", "recursive", "nameFilter"]) {
      expect(body).toContain(`TEXT("${key}")`);
    }
    expect(body).toContain("MCPPagination::ReadPageRequest(");
    expect(body).toContain("MCPPagination::EmitPage(");
    expect(body).not.toMatch(/Params->TryGet/);
  });

  it("reports a failed sequence on its own row instead of failing the call", () => {
    expect(body).toContain('Row->SetBoolField(TEXT("success"), false)');
    expect(body).toContain('SetNumberField(TEXT("failedCount")');
  });

  it("validates fields against the keys the read actually writes", () => {
    const names = body.slice(body.indexOf("AnimReadSeqFieldNames()"), body.indexOf("AnimReadSeqDefaultBatchFields()"));
    const listed = [...names.matchAll(/TEXT\("([A-Za-z]+)"\)/g)].map((m) => m[1]);
    const fill = body.slice(body.indexOf("void AnimReadSeqFill("), body.indexOf("void AnimReadSeqSelect("));
    const written = [...fill.matchAll(/Result->Set\w+Field\(TEXT\("([A-Za-z]+)"\)/g)].map((m) => m[1]);
    expect(new Set(listed)).toEqual(new Set(written));
  });
});
