/**
 * `asset(reimport)`: what it reads, and what it refuses (#1008).
 *
 * Two behaviours, both found by writing this suite.
 *
 * `FReimportManager::Reimport` on an asset with no registered reimport handler
 * does not return. It wedges the game thread, so the bridge stops answering
 * entirely and EVERY later call times out, not just the reimport. Asking
 * CanReimport first turns that into an immediate answer.
 *
 * And `filePath` repoints an asset at a new source before rebuilding it. When
 * nothing on the asset could record that path it was dropped silently: the
 * reimport re-read the file the asset was originally imported from and
 * reported success, so a caller was told their file had been used when it was
 * never opened. `sourceFileUpdated` now says which of the two happened.
 *
 * Runs only against the dedicated disposable test project.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

/** Authored in the editor, so nothing can reimport it. */
const AUTHORED_ASSET = `${TEST_PREFIX}/BP_ReimportProbe`;
/** Imported from a file, so it has a reimport handler and import data. */
const IMPORTED_TEXTURE = `${TEST_PREFIX}/T_ReimportProbe`;

/** A 1x1 PNG. Content does not matter; only that it is a valid image file. */
const ONE_PIXEL_PNG = Buffer.from(
  "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==",
  "base64",
);

let bridge: EditorBridge;
let scratchDir = "";
let firstPng = "";
let secondPng = "";
/** Set when the texture fixture could not be built, so cases report why. */
let textureError = "";

beforeAll(async () => {
  bridge = await getBridge();
  scratchDir = mkdtempSync(join(tmpdir(), "ue-mcp-reimport-"));
  firstPng = join(scratchDir, "first.png");
  secondPng = join(scratchDir, "second.png");
  writeFileSync(firstPng, ONE_PIXEL_PNG);
  writeFileSync(secondPng, ONE_PIXEL_PNG);

  await callBridge(bridge, "delete_asset", { assetPath: AUTHORED_ASSET, force: true });
  await callBridge(bridge, "delete_asset", { assetPath: IMPORTED_TEXTURE, force: true });

  const created = await callBridge(bridge, "create_blueprint", {
    path: AUTHORED_ASSET,
    parentClass: "Actor",
  });
  expect(created.ok, created.error).toBe(true);

  const imported = await callBridge(bridge, "import_texture", {
    filePath: firstPng,
    packagePath: TEST_PREFIX,
    name: "T_ReimportProbe",
  });
  if (!imported.ok) textureError = `import_texture failed: ${imported.error}`;
});

afterAll(async () => {
  if (scratchDir) rmSync(scratchDir, { recursive: true, force: true });
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: AUTHORED_ASSET, force: true });
    await callBridge(bridge, "delete_asset", { assetPath: IMPORTED_TEXTURE, force: true });
    disconnectBridge();
  }
});

describe("an asset nothing can reimport (#1008)", () => {
  it("is refused promptly rather than wedging the editor", async () => {
    // The elapsed time IS the assertion. Before the guard this took the full
    // 30s bridge timeout, because the game thread never came back.
    const started = Date.now();
    const reimported = await callBridge(bridge, "reimport_asset", { assetPath: AUTHORED_ASSET });
    const elapsed = Date.now() - started;

    const refused = !reimported.ok || (reimported.result as Record<string, unknown>)?.success === false;
    expect(refused).toBe(true);
    expect(elapsed, `took ${elapsed}ms, which is a hang rather than an answer`).toBeLessThan(10000);

    const message = String(reimported.error ?? JSON.stringify(reimported.result));
    // Naming the reason matters: "reimport failed" would send a caller looking
    // at their source file rather than at the asset type.
    expect(message).toMatch(/reimport/i);
  });

  it("leaves the editor able to take the next call", async () => {
    // The real cost of the hang was never the one call. It was that nothing
    // answered afterwards, including this.
    const listed = await callBridge(bridge, "list_dialogs", {});
    expect(listed.ok, listed.error).toBe(true);
  });
});

describe("repointing an imported asset at a new source (#1008)", () => {
  it("records the new file and says that it did", async () => {
    expect(textureError, textureError).toBe("");
    const reimported = await callBridge(bridge, "reimport_asset", {
      assetPath: IMPORTED_TEXTURE,
      filePath: secondPng,
    });
    expect(reimported.ok, reimported.error).toBe(true);

    const result = reimported.result as Record<string, unknown>;
    // The field that separates "rebuilt from the same source" from "repointed
    // and rebuilt", which a caller could previously only assume.
    expect(result.sourceFileUpdated).toBe(true);
    expect(String(result.sourceFile ?? "")).toContain("second.png");
  });

  it("reports no repoint when no file was named", async () => {
    expect(textureError, textureError).toBe("");
    const reimported = await callBridge(bridge, "reimport_asset", { assetPath: IMPORTED_TEXTURE });
    expect(reimported.ok, reimported.error).toBe(true);
    expect((reimported.result as Record<string, unknown>).sourceFileUpdated).toBe(false);
  });

  it("refuses a file that is not there", async () => {
    expect(textureError, textureError).toBe("");
    const reimported = await callBridge(bridge, "reimport_asset", {
      assetPath: IMPORTED_TEXTURE,
      filePath: join(scratchDir, "no-such-file.png"),
    });
    const message = String(reimported.error ?? JSON.stringify(reimported.result));
    expect(message).toMatch(/file not found/i);
  });
});
