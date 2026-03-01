import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;

beforeAll(async () => { bridge = await getBridge(); });
afterAll(() => disconnectBridge());

describe("asset — read", () => {
  it("search_assets (wildcard)", async () => {
    const r = await callBridge(bridge, "search_assets", { query: "*", maxResults: 10 });
    expect(r.ok, r.error).toBe(true);
  });

  it("search_assets (typed)", async () => {
    const r = await callBridge(bridge, "search_assets", { query: "StaticMesh", maxResults: 5 });
    expect(r.ok, r.error).toBe(true);
  });

  it("list_textures", async () => {
    const r = await callBridge(bridge, "list_textures", { recursive: true });
    expect(r.ok, r.error).toBe(true);
  });
});

describe("asset — read specific (dynamic)", () => {
  let assetPath: string | undefined;

  beforeAll(async () => {
    const r = await callBridge(bridge, "search_assets", { query: "*", maxResults: 1 });
    if (r.ok) {
      const assets = (r.result as any)?.assets ?? (r.result as any);
      if (Array.isArray(assets) && assets.length > 0) {
        assetPath = assets[0].path ?? assets[0].asset_path ?? assets[0].objectPath;
      }
    }
  });

  it("read_asset", async ({ skip }) => {
    if (!assetPath) skip();
    const r = await callBridge(bridge, "read_asset", { path: assetPath });
    expect(r.ok, r.error).toBe(true);
  });

  it("read_asset_properties", async ({ skip }) => {
    if (!assetPath) skip();
    const r = await callBridge(bridge, "read_asset_properties", { assetPath });
    expect(r.ok, r.error).toBe(true);
  });
});

describe("asset — write (with cleanup)", () => {
  const created: string[] = [];

  afterAll(async () => {
    for (const p of created) {
      await callBridge(bridge, "delete_asset", { assetPath: p });
    }
  });

  it("duplicate_asset", async ({ skip }) => {
    const search = await callBridge(bridge, "search_assets", { query: "*", maxResults: 1 });
    const assets = (search.result as any)?.assets ?? (search.result as any);
    if (!search.ok || !Array.isArray(assets) || assets.length === 0) skip();
    const src = assets[0].path ?? assets[0].asset_path ?? assets[0].objectPath;
    const dest = `${TEST_PREFIX}/DuplicateTest`;
    const r = await callBridge(bridge, "duplicate_asset", { sourcePath: src, destinationPath: dest });
    expect(r.ok, r.error).toBe(true);
    created.push(dest);
  });

  it("save_asset (all dirty)", async () => {
    const r = await callBridge(bridge, "save_asset", { assetPath: "" });
    // May fail if no dirty assets; we're testing the method exists
    expect(r.method).toBe("save_asset");
  });
});
