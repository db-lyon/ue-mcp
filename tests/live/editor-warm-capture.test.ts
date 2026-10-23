/** Native rendering validation, restricted by the harness to tests/ue_mcp. */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { setTimeout as delay } from "node:timers/promises";
import { LiveServer, resultJson } from "./server.js";
import { closeLiveBridges, liveTarget } from "./harness.js";

const target = await liveTarget();
let server: LiveServer;
let activeId: string | undefined;
const folder = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-warm-capture-"));
beforeAll(async () => { server = await LiveServer.start({ projects: [target.uproject] }); }, 240_000);
afterAll(async () => {
  if (activeId) await server.call("editor", { action: "capture_scene_png", captureId: activeId, cancel: true });
  await server?.close();
  closeLiveBridges();
  fs.rmSync(folder, { recursive: true, force: true });
});
async function capture(args: Record<string, unknown>) {
  return resultJson<any>(await server.call("editor", { action: "capture_scene_png", ...args }));
}

describe("warm capture lifecycle", () => {
  it("writes the final PNG after exactly the requested warm frames and reports its camera", async () => {
    const outputPath = path.join(folder, "warmed.png");
    const location = { x: 123, y: -456, z: 789 };
    let result = await capture({ outputPath, location, width: 64, height: 48, fov: 60, warmupFrames: 2 });
    activeId = result.captureId;
    expect(result).toMatchObject({ success: true, status: "warming", complete: false, fileWritten: false, warmupFramesRendered: 0 });
    const deadline = Date.now() + 70_000;
    while (!result.complete && Date.now() < deadline) {
      await delay(100);
      result = await capture({ captureId: activeId });
    }
    expect(result).toMatchObject({ success: true, status: "completed", fileWritten: true,
      warmupFramesRendered: 2, warmupFramesRequested: 2, captureActorRemoved: true,
      streamingMode: "normal_capture_view", streamingViewBoost: 1, textureResidencyVerified: false });
    expect(result.captureMetadata).toMatchObject({ location, width: 64, height: 48, fovDegrees: 60, fullyLoadTextures: false });
    expect(fs.readFileSync(outputPath).subarray(0, 8)).toEqual(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]));
    activeId = undefined;
  }, 90_000);

  it("refuses overlapping jobs and cancels without exporting; repeated cancellation is stable", async () => {
    const outputPath = path.join(folder, "cancelled.png");
    const started = await capture({ outputPath, width: 64, height: 48, warmupFrames: 240 });
    activeId = started.captureId;
    expect(started.status).toBe("warming");
    const overlapping = await capture({ outputPath: path.join(folder, "overlap.png"), warmupFrames: 1 });
    expect(overlapping.success).toBe(false);
    const cancelled = await capture({ captureId: activeId, cancel: true });
    expect(cancelled).toMatchObject({ status: "cancelled", complete: true, fileWritten: false, captureActorRemoved: true });
    expect(await capture({ captureId: activeId, cancel: true })).toEqual(cancelled);
    expect(fs.existsSync(outputPath)).toBe(false);
    activeId = undefined;
  });

  it("refuses invalid bounds and incompatible force-loading before starting", async () => {
    for (const args of [{ warmupFrames: -1 }, { warmupFrames: 1.5 }, { warmupFrames: 241 },
      { warmupFrames: 1, warmupTimeoutSeconds: 0 }, { warmupFrames: 1, fullyLoadTextures: true }]) {
      expect((await capture({ outputPath: path.join(folder, "invalid.png"), ...args })).success).toBe(false);
    }
    expect(fs.existsSync(path.join(folder, "invalid.png"))).toBe(false);
  });
});
