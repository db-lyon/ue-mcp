import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { animationTool } from "../../src/tools/animation.js";

const handlers = "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/";
const read = (rel: string) => readFileSync(new URL(handlers + rel, import.meta.url), "utf8");

describe("animation.read_montage notify objects (#1117)", () => {
  it("advertises objectPath and properties for editing in place", () => {
    const description = animationTool.actions.read_montage.description ?? "";
    expect(description).toContain("objectPath");
    expect(description).toContain("editor(set_property");
    expect(description).toContain("notifyStates");
  });

  it("reports each notify's instance path and editable properties", () => {
    const source = read("Handlers/AnimationHandlers.cpp");
    const body = source.slice(
      source.indexOf("FAnimationHandlers::ReadAnimMontage("),
      source.indexOf("FAnimationHandlers::CreateAnimBlueprint("),
    );
    expect(body).toContain("NotifyEvent.NotifyStateClass->GetPathName()");
    expect(body).toContain("MCPAnimNotify::EditableProperties(NotifyEvent.NotifyStateClass)");
    expect(body).toContain("MCPAnimNotify::ListNotifyStates(Montage)");
  });

  it("shares one notify-state lister through a header", () => {
    expect(read("HandlerAnimNotify.h")).toContain("inline TArray<TSharedPtr<FJsonValue>> ListNotifyStates(");
    const depth = read("Handlers/AnimationHandlers_Depth.cpp");
    expect(depth).not.toContain("static TArray<TSharedPtr<FJsonValue>> MCPAnimDepthListNotifyStates(");
    expect(depth).toContain("MCPAnimNotify::ListNotifyStates(AnimAsset)");
  });
});
