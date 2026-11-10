/**
 * The Unreal Canvas Bridge (UE 5.9) claims 127.0.0.1:9877 for its webcam
 * server and 9880 for its viewport stream. ue-mcp keeps its bridge off both
 * when a project enables that plugin, on the client and in the C++ bridge, and
 * doctor names a clash it finds (#1824).
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import {
  CANVAS_RESERVED_PORTS,
  canvasBridgeEnabled,
  canvasPortReplacement,
  readUnrealCanvas,
} from "../../src/integrations/epic/unreal-canvas.js";
import { deriveProjectPort } from "../../src/bridge/port.js";
import { EditorBridge } from "../../src/bridge/bridge.js";
import { requestedPortPath } from "../../src/bridge/requested-port.js";
import { ProjectContext } from "../../src/config/project.js";
import { bridgePort } from "../../src/cli/init-core.js";
import { formatDoctor, type DoctorReport } from "../../src/cli/doctor.js";
import { ProjectFixture } from "../helpers/project-fixture.js";

const stripAnsi = (s: string) => s.replace(/\x1b\[[0-9;]*m/g, "");

let fixture: ProjectFixture;
const savedPort = process.env.UE_MCP_PORT;
beforeEach(() => {
  fixture = new ProjectFixture("ue-mcp-canvas-ports-");
  delete process.env.UE_MCP_PORT;
});
afterEach(() => {
  fixture.cleanup();
  if (savedPort === undefined) delete process.env.UE_MCP_PORT;
  else process.env.UE_MCP_PORT = savedPort;
});

/** A project whose .uproject lists the given plugins. */
function project(name: string, plugins: Array<{ Name: string; Enabled: boolean }> = []): string {
  const uproject = fixture.makeProject(name, { engine: "5.9" });
  fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, EngineAssociation: "5.9", Plugins: plugins }));
  return uproject;
}

const CANVAS_ON = [{ Name: "UnrealCanvasBridge", Enabled: true }];

describe("canvasBridgeEnabled", () => {
  it("reads the .uproject's own entry", () => {
    expect(canvasBridgeEnabled(project("On", CANVAS_ON))).toEqual({ enabled: true, by: "uproject" });
    expect(canvasBridgeEnabled(project("Off", [{ Name: "UnrealCanvasBridge", Enabled: false }]))).toEqual({ enabled: false, by: null });
    // Not EnabledByDefault, so an unlisted plugin is off.
    expect(canvasBridgeEnabled(project("Unlisted")).enabled).toBe(false);
  });

  it("counts a project plugin that enables it through its dependency list", () => {
    const uproject = project("ViaPlugin");
    const pluginDir = path.join(path.dirname(uproject), "Plugins", "CanvasGlue");
    fs.mkdirSync(pluginDir, { recursive: true });
    fs.writeFileSync(path.join(pluginDir, "CanvasGlue.uplugin"), JSON.stringify({ Plugins: CANVAS_ON }));
    expect(canvasBridgeEnabled(uproject)).toEqual({ enabled: true, by: "CanvasGlue" });
  });

  it("lets the .uproject turn it off over a plugin that depends on it", () => {
    const uproject = project("Overridden", [{ Name: "UnrealCanvasBridge", Enabled: false }]);
    const pluginDir = path.join(path.dirname(uproject), "Plugins", "CanvasGlue");
    fs.mkdirSync(pluginDir, { recursive: true });
    fs.writeFileSync(path.join(pluginDir, "CanvasGlue.uplugin"), JSON.stringify({ Plugins: CANVAS_ON }));
    expect(canvasBridgeEnabled(uproject).enabled).toBe(false);
  });
});

describe("canvasPortReplacement", () => {
  it("moves a reserved pin to the derived port only when Canvas is on", () => {
    const on = project("On", CANVAS_ON);
    const off = project("Off");
    for (const { port } of CANVAS_RESERVED_PORTS) {
      const swap = canvasPortReplacement(on, port, 50123);
      expect(swap?.port).toBe(50123);
      expect(swap?.reason).toContain(`port ${port} is used by`);
      expect(canvasPortReplacement(off, port, 50123)).toBeNull();
    }
    expect(canvasPortReplacement(on, 9878, 50123)).toBeNull();
    expect(canvasPortReplacement(null, 9877, 50123)).toBeNull();
  });

  it("never needs to move a derived port", () => {
    // Derived ports are in the ephemeral range, which holds neither reserved port.
    for (const dir of ["C:/a", "D:/Projects/Game", "/home/u/proj", "C:/Users/x/Unreal Projects/Canvas"]) {
      const derived = deriveProjectPort(dir);
      expect(derived).toBeGreaterThanOrEqual(49152);
      expect(CANVAS_RESERVED_PORTS.some((r) => r.port === derived)).toBe(false);
    }
  });
});

describe("the client's bridge port", () => {
  it("bridgePort skips a reserved config pin and says why", () => {
    const uproject = project("Pinned", CANVAS_ON);
    fs.writeFileSync(path.join(path.dirname(uproject), "ue-mcp.yml"), "ue-mcp:\n  bridge:\n    port: 9877\n");
    const ctx = new ProjectContext();
    ctx.setProject(uproject);
    const bp = bridgePort(ctx);
    expect(bp.port).toBe(deriveProjectPort(path.dirname(uproject)));
    expect(bp.pinnedBy).toBe("config");
    expect(bp.avoided?.port).toBe(9877);
  });

  it("bridgePort keeps a reserved pin when Canvas is off", () => {
    const uproject = project("PinnedNoCanvas");
    process.env.UE_MCP_PORT = "9880";
    const ctx = new ProjectContext();
    ctx.setProject(uproject);
    const bp = bridgePort(ctx);
    expect(bp).toMatchObject({ port: 9880, pinnedBy: "env" });
    expect(bp.avoided).toBeUndefined();
  });

  it("EditorBridge drops a reserved pin for the derived port and clears the published pin", () => {
    const uproject = project("Bridge", CANVAS_ON);
    const projectDir = path.dirname(uproject);
    const bridge = new EditorBridge("127.0.0.1", 9877);
    bridge.setProjectContext(uproject);
    expect(bridge.port).toBe(deriveProjectPort(projectDir));
    expect(bridge.getTarget().portSource).toBe("derived");
    expect(bridge.canvasPortAvoided?.port).toBe(9877);
    // A derived port is not a pin, so nothing tells the editor to bind 9877.
    expect(fs.existsSync(requestedPortPath(projectDir))).toBe(false);
  });

  it("EditorBridge keeps an ordinary pin and publishes it", () => {
    const uproject = project("BridgeOrdinary", CANVAS_ON);
    const bridge = new EditorBridge("127.0.0.1", 9878);
    bridge.setProjectContext(uproject);
    expect(bridge.port).toBe(9878);
    expect(bridge.canvasPortAvoided).toBeNull();
    expect(JSON.parse(fs.readFileSync(requestedPortPath(path.dirname(uproject)), "utf8")).port).toBe(9878);
  });

  it("EditorBridge applies the rule when retargeted to a Canvas project", () => {
    const first = project("First");
    const second = project("Second", CANVAS_ON);
    const bridge = new EditorBridge("127.0.0.1", 9880);
    bridge.setProjectContext(first);
    expect(bridge.port).toBe(9880);
    const target = bridge.retargetProject(second);
    expect(target.port).toBe(deriveProjectPort(path.dirname(second)));
    expect(target.verified).toBe(true);
  });
});

describe("readUnrealCanvas and doctor", () => {
  it("reports each ue-mcp port that lands on a reserved one", () => {
    const status = readUnrealCanvas(project("Clash", CANVAS_ON), { bridge: 9877, liveBridge: 9880, flow: 7723 });
    expect(status.bridgeEnabled).toBe(true);
    expect(status.portConflicts).toEqual([
      "ue-mcp's configured bridge port for this project is 9877, which the Canvas webcam server (webcam_server.py) uses",
      "the port this project's running editor bound for the ue-mcp bridge is 9880, which the Canvas viewport stream uses",
    ]);
    expect(readUnrealCanvas(project("NoClash"), { bridge: 9877 }).portConflicts).toEqual([]);
  });

  const base: DoctorReport = {
    selfVersion: "1.0.78",
    registryLatest: "1.0.78",
    npmGlobal: { version: "1.0.78", dir: "/g/ue-mcp" },
    localShadow: null,
    effectiveNpx: "1.0.78",
    runningServers: [],
    targetProjectDir: "C:/proj/Vale",
    bridgePlugin: { version: "0.3.0", project: "C:/proj/Vale/Vale.uproject" },
    bareNpxConfigs: [],
  };
  const canvas = { bridgeEnabled: true, enabledBy: "uproject", reservedPorts: CANVAS_RESERVED_PORTS, portConflicts: [] as string[] };

  it("shows the plugin and spoils the verdict on a clash", () => {
    const quiet = stripAnsi(formatDoctor({ ...base, unrealCanvas: canvas }));
    expect(quiet).toMatch(/epic canvas:\s+UnrealCanvasBridge enabled; it claims 9877 and 9880, which ue-mcp keeps off/);
    expect(quiet).toContain("Everything aligned");

    const clash = stripAnsi(formatDoctor({
      ...base,
      unrealCanvas: { ...canvas, portConflicts: ["ue-mcp's configured bridge port for this project is 9877, which the Canvas webcam server (webcam_server.py) uses"] },
    }));
    expect(clash).not.toContain("Everything aligned");
    expect(clash).toContain("This project enables UnrealCanvasBridge, and ue-mcp's configured bridge port for this project is 9877");
  });

  it("says nothing when the plugin is off", () => {
    const out = stripAnsi(formatDoctor({ ...base, unrealCanvas: { ...canvas, bridgeEnabled: false } }));
    expect(out).not.toContain("epic canvas:");
  });
});

describe("C++ parity", () => {
  it("BridgePortConfig.cpp reserves the same ports and the walk steps over them", () => {
    const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
    const priv = path.join(repo, "plugin", "ue_mcp_bridge", "Source", "UE_MCP_Bridge", "Private");
    const config = fs.readFileSync(path.join(priv, "BridgePortConfig.cpp"), "utf8");
    const list = /kCanvasReservedPorts\[\]\s*=\s*\{([^}]*)\}/.exec(config)?.[1];
    expect(list?.split(",").map((s) => Number(s.trim()))).toEqual(CANVAS_RESERVED_PORTS.map((r) => r.port));
    expect(config).toContain('FindPlugin(TEXT("UnrealCanvasBridge"))');
    // Every pinned return goes through the check; only the derived one does not.
    const resolve = config.slice(config.indexOf("FMCPBridgePortChoice FMCPBridgeServer::ResolveConfiguredPort()"));
    expect((resolve.match(/return KeepOffCanvasPorts\(Choice\);/g) ?? []).length).toBe(4);
    const server = fs.readFileSync(path.join(priv, "BridgeServer.cpp"), "utf8");
    expect(server).toMatch(/if \(IsCanvasReservedPort\(RequestedPort \+ Offset\)\)\s*\{\s*continue;/);
  });
});
