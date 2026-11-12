// @live-editor headless
/**
 * The Fab actions that work without an account: the local cache readers,
 * cross-checked against each other, and importing a local file into a content
 * folder, and building the fab.com search page URL. Login, logout, library sync
 * and cache clearing are not run. Without the Fab plugin (a UE 5.9 install
 * the launcher did not add the "Fab UE Plugin" item to), the actions that need
 * it refuse with PLUGIN_NOT_AVAILABLE. Refs #1223, #1485, #1889.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = `${TEST_PREFIX}/BCov/Fab`;
const BASE = `bcov_fab_${process.pid}`;
const SOURCE = path.join(os.tmpdir(), `${BASE}.wav`);

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

/** A short mono 16-bit PCM WAV of silence: a file any generic importer accepts. */
function writeWav(file: string): void {
  const frames = 4410;
  const data = Buffer.alloc(frames * 2);
  const header = Buffer.alloc(44);
  header.write("RIFF", 0, "ascii");
  header.writeUInt32LE(36 + data.length, 4);
  header.write("WAVE", 8, "ascii");
  header.write("fmt ", 12, "ascii");
  header.writeUInt32LE(16, 16);
  header.writeUInt16LE(1, 20);
  header.writeUInt16LE(1, 22);
  header.writeUInt32LE(44100, 24);
  header.writeUInt32LE(88200, 28);
  header.writeUInt16LE(2, 32);
  header.writeUInt16LE(16, 34);
  header.write("data", 36, "ascii");
  header.writeUInt32LE(data.length, 40);
  fs.writeFileSync(file, Buffer.concat([header, data]));
}

async function assetsUnder(): Promise<Array<{ path: string; name: string; className: string }>> {
  const listed = await call("list_assets", { directory: DIR, recursive: true });
  return (listed.assets as Array<{ path: string; name: string; className: string }>) ?? [];
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  writeWav(SOURCE);
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
  try { fs.unlinkSync(SOURCE); } catch { /* already gone */ }
}, 120_000);

describe("fab local state", () => {
  it("fab_status, fab_cache_info and fab_list_cached agree on the local download cache", async () => {
    const status = await call("fab_status", {});
    expect(typeof status.pluginInstalled).toBe("boolean");
    expect(typeof status.pluginEnabled).toBe("boolean");
    // The launcher installs Fab as its own item ("Fab UE Plugin"), and a UE 5.9
    // engine can come without it (#1889). There the status says what is missing
    // and how to get it instead of reading a cache.
    if (status.pluginLoaded === false) {
      expect(status.errorCode).toBe("PLUGIN_NOT_AVAILABLE");
      expect(String(status.note)).toContain("Fab plugin module not loaded");
      if (status.pluginInstalled === false) {
        expect(status.nativeApiLinked).toBe(false);
        expect(status.pluginEnabled).toBe(false);
        expect(String(status.note)).toContain("Fab UE Plugin");
        expect(String(status.note)).toContain("Install to Engine");
      }
      return;
    }
    expect(status.pluginLoaded, JSON.stringify(status)).toBe(true);
    expect(status.pluginInstalled).toBe(true);
    expect(status.pluginEnabled).toBe(true);
    expect(String(status.pluginDir)).toMatch(/Fab/);
    expect(status.nativeApiLinked).toBe(true);
    expect(typeof status.fabWindowOpened).toBe("boolean");

    const info = await call("fab_cache_info", {});
    expect(String(info.cacheLocation).length).toBeGreaterThan(0);
    expect(info.cacheLocation).toBe(status.cacheLocation);
    expect(info.cacheSizeBytes).toBe(status.cacheSizeBytes);
    expect(info.cacheSize).toBe(status.cacheSize);

    const listed = await call("fab_list_cached", {});
    expect(listed.cacheLocation).toBe(info.cacheLocation);
    expect(listed.count).toBe(info.count);
    expect((listed.cachedAssets as unknown[]).length).toBe(Number(listed.count));
  }, 60_000);

  it("fab_import_file imports a local file into the destination folder", async () => {
    if ((await call("fab_status", {})).pluginLoaded === false) {
      const refused = await callBridge(bridge, "fab_import_file", { source: SOURCE, destination: DIR });
      expect(succeeded(refused)).toBe(false);
      expect(body(refused).errorCode).toBe("PLUGIN_NOT_AVAILABLE");
      expect(String(body(refused).error)).toContain("Fab plugin");
      expect(await assetsUnder()).toEqual([]);
      return;
    }
    const imported = await call("fab_import_file", { source: SOURCE, destination: DIR });
    expect(imported.source).toBe(SOURCE);
    expect(imported.destination).toBe(DIR);
    if (imported.async !== true) {
      expect(Number(imported.count)).toBeGreaterThan(0);
      for (const p of imported.importedAssets as string[]) expect(p.startsWith(`${DIR}/`), p).toBe(true);
    }

    let assets = await assetsUnder();
    for (let i = 0; i < 60 && assets.length === 0; i++) {
      await new Promise((r) => setTimeout(r, 500));
      assets = await assetsUnder();
    }
    expect(assets.length, `nothing landed under ${DIR}`).toBeGreaterThan(0);
    expect(assets.some((a) => a.name.toLowerCase().includes(BASE.toLowerCase())), JSON.stringify(assets)).toBe(true);
  }, 120_000);

  it("without the Fab plugin the Fab actions refuse with PLUGIN_NOT_AVAILABLE and the install step", async () => {
    const status = await call("fab_status", {});
    // With Fab loaded the readers and the import are asserted above; the live
    // suite skips no test, so this one has nothing left to check there.
    if (status.pluginLoaded !== false) return;

    const refusals = [
      await callBridge(bridge, "fab_list_cached", {}),
      await callBridge(bridge, "fab_cache_info", {}),
      await callBridge(bridge, "fab_import_file", { source: SOURCE, destination: DIR }),
    ];
    for (const r of refusals) {
      expect(succeeded(r), JSON.stringify(r.result)).toBe(false);
      const b = body(r);
      expect(b.errorCode).toBe("PLUGIN_NOT_AVAILABLE");
      expect(b.pluginInstalled).toBe(status.pluginInstalled);
      expect(b.pluginEnabled).toBe(status.pluginEnabled);
      // The refusal is the same sentence status reports, naming what is missing and how to get it.
      expect(String(status.note)).toContain(String(b.error));
      if (status.pluginInstalled === false) expect(String(b.error)).toMatch(/not installed in this engine.*Fab UE Plugin/s);
    }
    // Nothing was imported by the refused call.
    expect(await assetsUnder()).toEqual([]);
  }, 60_000);

  it("fab_open_search builds the fab.com search URL from each filter without opening it", async () => {
    // open:false only: the live suite runs unattended, so nothing here launches
    // a browser. The open path is UE.MCP.Fab.OpenSearch.LaunchesTheSearchUrl.
    const base = "https://www.fab.com/search";
    const bare = await call("fab_open_search", { query: "stone wall", open: false });
    expect(bare.url).toBe(`${base}?q=stone%20wall&asset_formats=unreal-engine`);
    expect(bare.opened).toBe(false);

    const filtered = await call("fab_open_search", {
      query: "rock & moss/1",
      listingType: "3d-model",
      assetFormat: "unreal-engine",
      free: true,
      open: false,
    });
    expect(filtered.url).toBe(`${base}?q=rock%20%26%20moss%2F1&listing_types=3d-model&asset_formats=unreal-engine&is_free=1`);
    expect(filtered.opened).toBe(false);

    const unfiltered = await call("fab_open_search", { query: "tree", assetFormat: "", free: false, open: false });
    expect(unfiltered.url).toBe(`${base}?q=tree`);

    const missing = await callBridge(bridge, "fab_open_search", { open: false });
    expect(succeeded(missing)).toBe(false);
  }, 60_000);
});
