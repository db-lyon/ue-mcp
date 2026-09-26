import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { animationTool } from "../../src/tools/animation.js";
import { listHandlerFiles } from "../../scripts/lib/cpp-registrations.mjs";

const animationSources = listHandlerFiles()
  .filter(({ name }) => name.startsWith("AnimationHandlers"))
  .map(({ name, path }) => ({ name, source: readFileSync(path, "utf8") }));

describe("animation handlers load assets through the shared resolver (#1108)", () => {
  it("never calls the editor asset library loader directly", () => {
    for (const { name, source } of animationSources) {
      expect(source.includes("UEditorAssetLibrary::LoadAsset("), name).toBe(false);
    }
  });

  it("reports notify duration and track on read_sequence", () => {
    const source = animationSources.find((s) => s.name === "AnimationHandlers_Sequence.cpp")!.source;
    const body = source.slice(
      source.indexOf("// read_anim_sequence"),
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
