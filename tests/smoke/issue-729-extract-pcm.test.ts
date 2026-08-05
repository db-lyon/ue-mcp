// Regression: #729 - no way to extract a USoundWave's decoded audio in memory.
// extract_sound_wave_pcm decodes the imported data to base64 PCM without needing
// the original source file.
import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge, resultArray } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
beforeAll(async () => { bridge = await getBridge(); });
afterAll(() => disconnectBridge());

async function findSoundWavePath(): Promise<string | undefined> {
  const r = await callBridge(bridge, "list_sound_assets", { directory: "/Game", maxResults: 500 });
  if (!r.ok) return undefined;
  const assets = resultArray(r.result, "assets") ?? [];
  for (const a of assets as Array<Record<string, unknown>>) {
    if (String(a.class) === "SoundWave") return String(a.path);
  }
  // Fall back to an engine SoundWave if the project has none.
  const eng = await callBridge(bridge, "execute_python", {
    code: [
      "import unreal",
      "ar = unreal.AssetRegistryHelpers.get_asset_registry()",
      "f = unreal.ARFilter(class_names=['SoundWave'], recursive_classes=True)",
      "a = ar.get_assets(f)",
      "print('WAVE:' + (str(a[0].get_asset().get_path_name()) if a else ''))",
    ].join("\n"),
  });
  const m = JSON.stringify(eng.result).match(/WAVE:([^"\\]+)/);
  return m && m[1] ? m[1] : undefined;
}

describe("audio - extract_sound_wave_pcm (#729)", () => {
  it("returns decoded PCM metadata + base64 samples for a SoundWave", async ({ skip }) => {
    const wave = await findSoundWavePath();
    if (!wave) skip();
    const r = await callBridge(bridge, "extract_sound_wave_pcm", {
      soundPath: wave,
      maxSeconds: 1,
    });
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    expect(result.success).not.toBe(false);
    expect(Number(result.sampleRate)).toBeGreaterThan(0);
    expect(Number(result.numChannels)).toBeGreaterThan(0);
    expect(typeof result.pcmBase64).toBe("string");
    expect((result.pcmBase64 as string).length).toBeGreaterThan(0);
    expect(result.format).toBe("pcm_s16le");
  });
});
