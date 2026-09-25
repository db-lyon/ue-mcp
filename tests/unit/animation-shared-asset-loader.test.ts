import { readdirSync, readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { animationTool } from "../../src/tools/animation.js";

const dir = new URL("../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/", import.meta.url);
const animationSources = readdirSync(dir)
  .filter((name) => name.startsWith("AnimationHandlers") && name.endsWith(".cpp"))
  .map((name) => ({ name, source: readFileSync(new URL(name, dir), "utf8") }));

describe("animation handlers load assets through the shared resolver (#1108)", () => {
  it("never calls the editor asset library loader directly", () => {
    for (const { name, source } of animationSources) {
      expect(source.includes("UEditorAssetLibrary::LoadAsset("), name).toBe(false);
    }
  });

  it("reports notify duration and track on read_sequence", () => {
    const source = animationSources.find((s) => s.name === "AnimationHandlers_Sequence.cpp")!.source;
    const body = source.slice(
      source.indexOf("FAnimationHandlers::ReadAnimSequence("),
      source.indexOf("// scan_animation_tracks"),
    );
    expect(body).toContain("MCPLoadAssetObject(AssetPath)");
    expect(body).toContain("MCPAssetNotFoundError(AssetPath)");
    expect(body).toContain('SetNumberField(TEXT("duration"), NotifyEvent.GetDuration())');
    expect(body).toContain('SetStringField(TEXT("trackName")');
    const description = animationTool.actions.read_sequence.description ?? "";
    expect(description).toContain("duration");
    expect(description).toContain("trackName");
  });
});
