/**
 * The Unreal Canvas Bridge (UE 5.9) claims 127.0.0.1:9877 for its webcam
 * server and 9880 for its viewport stream, through the real CLI and server
 * (#1824): doctor reports the plugin and a pin that lands on either port, and
 * a server started on such a project targets the derived port instead and
 * publishes no pin for the editor to bind.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { deriveProjectPort } from "../../src/bridge/port.js";
import { requestedPortPath } from "../../src/bridge/requested-port.js";
import { McpServer, plain, runCli, Sandbox } from "./harness.js";

let sb: Sandbox;
beforeAll(() => {
  sb = new Sandbox("ue-mcp-canvas-");
});
afterAll(() => sb.cleanup());

/** A UE 5.9 project that enables the Canvas Bridge, optionally pinning bridge.port in ue-mcp.yml. */
function canvasProject(name: string, pin?: number): string {
  const uproject = sb.makeProject(name, { engine: "5.9" });
  const descriptor = JSON.parse(fs.readFileSync(uproject, "utf8"));
  fs.writeFileSync(uproject, JSON.stringify({ ...descriptor, Plugins: [{ Name: "UnrealCanvasBridge", Enabled: true }] }));
  if (pin !== undefined) fs.writeFileSync(path.join(path.dirname(uproject), "ue-mcp.yml"), `ue-mcp:\n  bridge:\n    port: ${pin}\n`);
  return uproject;
}

describe("Unreal Canvas Bridge ports", () => {
  it("doctor names the plugin, and fails the verdict on a pin it claims", async () => {
    covers("cli.doctor.unreal-canvas");
    const quiet = canvasProject("QuietCanvas");
    const q = plain((await runCli(sb, ["doctor", quiet])).stdout);
    expect(q).toMatch(/epic canvas:\s+UnrealCanvasBridge enabled; it claims 9877 and 9880, which ue-mcp keeps off/);
    expect(q).not.toContain("This project enables UnrealCanvasBridge");

    const pinned = canvasProject("PinnedCanvas", 9877);
    const r = await runCli(sb, ["doctor", pinned]);
    const out = plain(r.stdout);
    expect(out).toContain(
      "This project enables UnrealCanvasBridge, and ue-mcp's configured bridge port for this project is 9877, which the Canvas webcam server (webcam_server.py) uses",
    );
    expect(out).not.toContain("Everything aligned");

    // Without the plugin, the same pin is nobody's business.
    const plain9877 = sb.makeProject("NoCanvas", { engine: "5.9" });
    fs.writeFileSync(path.join(path.dirname(plain9877), "ue-mcp.yml"), "ue-mcp:\n  bridge:\n    port: 9877\n");
    const none = plain((await runCli(sb, ["doctor", plain9877])).stdout);
    expect(none).not.toContain("epic canvas:");
    expect(none).not.toContain("UnrealCanvasBridge");
  });

  it("a server pinned to 9880 targets the derived port and asks the editor for nothing", async () => {
    covers("server.bridge-port.unreal-canvas");
    const uproject = canvasProject("ServedCanvas");
    const projectDir = path.dirname(uproject);
    const server = await McpServer.start(sb, { projects: [uproject], port: 9880 });
    try {
      const r = await server.call("tools", { action: "call", category: "project", method: "get_status" });
      expect(r.isError, r.text).toBe(false);
      const status = JSON.parse(r.text);
      expect(status.editorTarget).toMatchObject({ port: deriveProjectPort(projectDir), portSource: "derived" });
    } finally {
      await server.close();
    }
    expect(fs.existsSync(requestedPortPath(projectDir))).toBe(false);
  });
});
