// Regression: #724 - capture_screenshot target="pie" captured the editor
// viewport in Play-in-New-Window and never included the debug canvas. It now
// captures the actual PIE game viewport with UI + on-screen debug canvas, and
// in a networked session it lands on the client rather than the server.
import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { existsSync } from "node:fs";
import { getBridge, disconnectBridge, callBridge } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;

// configure_pie writes ULevelEditorPlaySettings and saves the config, so a test
// that sets networked client mode would leave every later PIE test in the suite
// running networked. Read the settings first and put them back afterwards.
let priorPieConfig: Record<string, unknown> | null = null;

beforeAll(async () => {
  bridge = await getBridge();
  const config = await callBridge(bridge, "get_pie_config", {});
  if (config.ok) priorPieConfig = config.result as Record<string, unknown>;
});

afterAll(async () => {
  await callBridge(bridge, "pie_control", { action: "stop" }).catch(() => {});
  if (priorPieConfig) {
    await callBridge(bridge, "configure_pie", {
      numClients: priorPieConfig.numClients,
      netMode: String(priorPieConfig.netMode ?? "standalone").toLowerCase(),
      runUnderOneProcess: priorPieConfig.runUnderOneProcess,
      launchSeparateServer: priorPieConfig.launchSeparateServer,
    }).catch(() => {});
  }
  disconnectBridge();
});

describe("editor - capture_screenshot target=pie (#724)", () => {
  it("captures the client viewport in networked PIE", async ({ skip }) => {
    const configured = await callBridge(bridge, "configure_pie", {
      numClients: 1,
      netMode: "client",
      runUnderOneProcess: true,
      launchSeparateServer: true,
    });
    expect(configured.ok, configured.error).toBe(true);

    const start = await callBridge(bridge, "pie_control", { action: "start" });
    // PIE can fail to start on a cold AssetRegistry / headless config - skip then.
    if (!start.ok) skip();
    await new Promise((r) => setTimeout(r, 3000));

    const status = await callBridge(bridge, "pie_control", { action: "status" });
    const running = JSON.stringify(status.result).toLowerCase().includes("running")
      || (status.result as Record<string, unknown>)?.isPlaying === true;
    if (!running) skip();

    const r = await callBridge(bridge, "capture_screenshot", { filename: "mcp_pie_724", target: "pie" });
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    expect(result.target).toBe("pie");
    expect(result.includesDebugCanvas).toBe(true);
    expect(typeof result.pieInstance).toBe("number");
    expect(typeof result.worldPath).toBe("string");
    expect(Number(result.width)).toBeGreaterThan(0);
    expect(Number(result.height)).toBeGreaterThan(0);
    expect(existsSync(String(result.filename))).toBe(true);

    const selected = await callBridge(bridge, "capture_screenshot", {
      filename: "mcp_pie_724_selected",
      target: "pie",
      pieInstance: result.pieInstance,
    });
    expect(selected.ok, selected.error).toBe(true);
    const selectedResult = selected.result as Record<string, unknown>;
    expect(selectedResult.pieInstance).toBe(result.pieInstance);
    expect(selectedResult.worldPath).toBe(result.worldPath);

    // An explicit selector addresses that client's window. Assert on the
    // resolved instance/world, not on UE's window title text, which is not a
    // contract the bridge controls.
    const windowCapture = await callBridge(bridge, "capture_screenshot", {
      filename: "mcp_pie_724_window",
      target: "window",
      pieInstance: result.pieInstance,
    });
    expect(windowCapture.ok, windowCapture.error).toBe(true);
    const windowResult = windowCapture.result as Record<string, unknown>;
    expect(windowResult.target).toBe("window");
    expect(windowResult.pieInstance).toBe(result.pieInstance);
    expect(windowResult.worldPath).toBe(result.worldPath);
    expect(existsSync(String(windowResult.filename))).toBe(true);

    // Without a selector, target=window keeps its original meaning: whatever
    // window is active, which is what makes editor UI capturable during PIE.
    const activeWindow = await callBridge(bridge, "capture_screenshot", {
      filename: "mcp_pie_724_active_window",
      target: "window",
    });
    expect(activeWindow.ok, activeWindow.error).toBe(true);
    const activeResult = activeWindow.result as Record<string, unknown>;
    expect(activeResult.target).toBe("window");
    expect(activeResult.pieInstance).toBeUndefined();
    expect(existsSync(String(activeResult.filename))).toBe(true);
  });
});
