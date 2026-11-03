// @live-editor render
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import { closeLiveBridges, liveBridge, liveTarget } from "./harness.js";
import { LiveServer, resultJson } from "./server.js";

type Json = Record<string, unknown>;
const cube = "/Engine/BasicShapes/Cube.Cube";
let bridge: EditorBridge;
let server: LiveServer;
let scratch: string;
let pending: string | undefined;

beforeAll(async () => {
  const target = await liveTarget();
  bridge = await liveBridge();
  server = await LiveServer.start({ projects: [target.uproject] });
  scratch = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-mesh-studio-"));
}, 120_000);

afterAll(async () => {
  if (pending) await bridge.call("cancel_mesh_studio_render", { jobId: pending }).catch(() => undefined);
  await server?.close();
  closeLiveBridges();
  if (scratch) fs.rmSync(scratch, { recursive: true, force: true });
});

describe("mesh studio renders", () => {
  it("returns final PNGs and an outcome manifest through the server with the map unchanged", async () => {
    const before = await bridge.call("get_world_state", {}) as Json;
    const result = await server.call("editor", {
      action: "render_mesh_studio", outputDir: scratch, resolution: 128, captureMode: "both", fullyLoadTextures: true,
      captures: [{ id: "cube", meshPath: cube, aspect: { width: 1, height: 2 } }, {
        id: "assembly", parts: [{ meshPath: cube }, { meshPath: cube, relativeTransform: {
          location: { x: 200, y: 0, z: 0 }, rotation: { pitch: 0, yaw: 30, roll: 0 }, scale: { x: 1, y: 1, z: 1 },
        } }],
      }], timeoutMs: 180_000,
    });
    expect(result.isError, result.text).toBe(false);
    const report = resultJson<Json>(result);
    expect(report).toMatchObject({ success: true, state: "completed", rendered: 2, cleanupComplete: true, dirtyPackagesUnchanged: true });
    const outcomes = report.outcomes as Array<Json>;
    expect(outcomes.map((row) => row.status)).toEqual(["rendered", "rendered"]);
    for (const [index, row] of outcomes.entries()) {
      const bytes = fs.readFileSync(row.file as string);
      expect(bytes.subarray(0, 8)).toEqual(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]));
      expect(bytes.readUInt32BE(16)).toBe(index === 0 ? 64 : 128);
      expect(bytes.readUInt32BE(20)).toBe(128);
      // PNG IHDR colour type 6 means RGB plus an alpha channel. Native
      // automation decodes the actual pixels and checks coverage and colour.
      expect(bytes[25]).toBe(6);
      expect(row.camera).toMatchObject({ width: index === 0 ? 64 : 128, height: 128, fov: 30 });
      expect(row.bounds).toBeDefined();
    }
    const manifest = JSON.parse(fs.readFileSync(report.manifest as string, "utf8"));
    expect(manifest).toMatchObject({ state: "completed", rendered: 2, cleanupComplete: true, dirtyPackagesUnchanged: true });
    const status = resultJson<Json>(await server.call("editor", { action: "get_mesh_studio_render", jobId: report.jobId }));
    expect(status.outcomes).toEqual(outcomes);
    const after = await bridge.call("get_world_state", {}) as Json;
    expect(after.editorWorldPackage).toBe(before.editorWorldPackage);
    expect(after.worldPackageDirty).toBe(before.worldPackageDirty);
    expect(after.dirtyPackages).toEqual(before.dirtyPackages);
  }, 180_000);

  it("reports invalid items individually and continues rendering the valid item", async () => {
    const result = await server.call("editor", {
      action: "render_mesh_studio", outputDir: scratch, resolution: 64, timeoutMs: 120_000,
      captures: [
        { id: "missing_source" },
        { id: "wrong_type", meshPath: "/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap" },
        { id: "valid_after_errors", meshPath: cube },
      ],
    });
    const report = resultJson<Json>(result);
    expect(report).toMatchObject({ success: false, state: "completed", rendered: 1, blocked: 2, cleanupComplete: true, dirtyPackagesUnchanged: true });
    const rows = report.outcomes as Array<Json>;
    expect(rows.map((row) => row.status)).toEqual(["blocked", "blocked", "rendered"]);
    expect(rows[0].reason).toMatch(/exactly one/i);
    expect(fs.statSync(rows[2].file as string).size).toBeGreaterThan(0);
  }, 120_000);

  it("cancels an active native job and verifies the cleanup without exporting a PNG", async () => {
    const start = await bridge.call("render_mesh_studio", {
      outputDir: scratch, resolution: 128, captures: [{ id: "cancelled", meshPath: cube }],
    }) as Json;
    expect(start.state).toBe("running");
    pending = start.jobId as string;
    const result = await bridge.call("cancel_mesh_studio_render", { jobId: pending }) as Json;
    expect(result).toMatchObject({ success: true, state: "cancelled", cleanupComplete: true, dirtyPackagesUnchanged: true });
    expect((result.outcomes as Json[])[0]).toMatchObject({ id: "cancelled", status: "blocked" });
    expect(fs.existsSync(path.join(scratch, "cancelled.png"))).toBe(false);
    const status = await bridge.call("get_mesh_studio_render", { jobId: pending }) as Json;
    expect(status.state).toBe("cancelled");
    pending = undefined;
  });

  it("refuses filename traversal and colliding ids before a render job starts", async () => {
    for (const items of [[{ id: "../escape", meshPath: cube }], [{ id: "Cube", meshPath: cube }, { id: "cube", meshPath: cube }]]) {
      const result = await bridge.call("render_mesh_studio", { outputDir: scratch, captures: items }) as Json;
      expect(result.success).toBe(false);
      expect(result.error).toMatch(/filename-safe id/i);
      expect(result.jobId).toBeUndefined();
    }
  });
});
