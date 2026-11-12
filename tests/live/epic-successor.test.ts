// @live-editor headless
/**
 * A wrapped Epic action whose tool a newer engine renamed reaches that
 * successor under the same action name, with its arguments renamed.
 *
 * asset.epic_get_asset_class wraps editor_toolset.toolsets.asset.AssetTools.
 * get_asset_class (5.8, asset_path) and, on 5.9, EditorToolset.AssetToolset.
 * GetAssetClass (assetPath). Whichever engine is attached, the call made with
 * either spelling has to answer with the class of the asset it named.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { LiveServer } from "./server.js";
import { closeLiveBridges, liveTarget } from "./harness.js";

const target = await liveTarget();

let server: LiveServer;

beforeAll(async () => {
  server = await LiveServer.start({ projects: [target.uproject] });
}, 240_000);

afterAll(async () => {
  await server?.close();
  closeLiveBridges();
});

const CUBE = "/Engine/BasicShapes/Cube.Cube";

async function assetClass(args: Record<string, unknown>): Promise<string> {
  const result = await server.call("asset", { action: "epic_get_asset_class", ...args });
  expect(result.isError, result.text).toBe(false);
  return result.text;
}

describe("a wrapped Epic action across an engine's rename of its tool", () => {
  it("answers with the asset's class when called with the older argument name", async () => {
    expect(await assetClass({ asset_path: CUBE })).toMatch(/StaticMesh/);
  }, 60_000);

  it("answers the same when called with the newer argument name", async () => {
    expect(await assetClass({ assetPath: CUBE })).toMatch(/StaticMesh/);
  }, 60_000);
});
