// @live-editor headless
/**
 * The Fab actions that work without an account: the local cache readers,
 * cross-checked against each other, and importing a local file into a content
 * folder. Login, logout, library sync and cache clearing are not run. Refs #1223.
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
    expect(status.pluginLoaded, JSON.stringify(status)).toBe(true);
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
});
