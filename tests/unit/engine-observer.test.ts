import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { readLogState, readEngineSnapshot } from "../../src/engine-observer.js";

const temporaryRoots: string[] = [];

function makeProject(logLines: string[], snapshot?: unknown): string {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-observer-"));
  temporaryRoots.push(root);
  const projectPath = path.join(root, "Demo.uproject");
  fs.writeFileSync(projectPath, JSON.stringify({ EngineAssociation: "5.8" }));
  fs.mkdirSync(path.join(root, "Saved", "Logs"), { recursive: true });
  fs.writeFileSync(path.join(root, "Saved", "Logs", "Demo.log"), logLines.join("\r\n"));
  if (snapshot !== undefined) {
    fs.mkdirSync(path.join(root, "Saved", "UE_MCP_Bridge"), { recursive: true });
    fs.writeFileSync(path.join(root, "Saved", "UE_MCP_Bridge", "status.json"), JSON.stringify(snapshot));
  }
  return projectPath;
}

afterEach(() => {
  while (temporaryRoots.length > 0) {
    fs.rmSync(temporaryRoots.pop()!, { recursive: true, force: true });
  }
});

describe("readLogState", () => {
  it("reports the newest phase marker", () => {
    const project = makeProject([
      "LogInit: Display: Base directory: C:/UE/",
      "LogPluginManager: Mounting Engine plugin Foo",
      "LogShaderCompilers: Display: Compiling 812 shaders",
    ]);
    const state = readLogState(project);
    expect(state.phase).toBe("compiling shaders");
    expect(state.blocking).toBe(false);
  });

  it("keeps ready sticky once the bridge has announced itself", () => {
    // Asset registry and shader lines keep scrolling long after startup, so
    // newest-wins alone would report a live editor as still starting.
    const project = makeProject([
      "LogMCPBridge: [UE-MCP] Editor ready - accepting requests",
      "LogAssetRegistry: Asset registry scan of /Game finished",
      "LogAssetRegistry: Asset registry scan of /Engine finished",
    ]);
    expect(readLogState(project).phase).toBe("ready");
  });

  it("flags the out-of-date modules prompt as blocking", () => {
    const project = makeProject([
      "LogInit: Display: Base directory: C:/UE/",
      "Warning: The following modules are missing or built with a different engine version",
      "LogPluginManager: Mounting Engine plugin Foo",
    ]);
    const state = readLogState(project);
    expect(state.phase).toContain("rebuild prompt");
    expect(state.blocking).toBe(true);
  });

  it("prefers a crash over any other sticky marker", () => {
    const project = makeProject([
      "LogMCPBridge: [UE-MCP] Editor ready - accepting requests",
      "LogWindows: Error: === Critical error: ===",
    ]);
    const state = readLogState(project);
    expect(state.phase).toBe("crashed");
    expect(state.blocking).toBe(true);
    expect(state.errors.length).toBeGreaterThan(0);
  });

  it("returns an empty state without a project", () => {
    const state = readLogState(null);
    expect(state.phase).toBe("unknown");
    expect(state.logPath).toBeNull();
  });
});

describe("readEngineSnapshot", () => {
  it("stamps the snapshot with its age so stale state is obvious", () => {
    const project = makeProject(["LogInit: Display: Base directory: C:/UE/"], {
      phase: "ready",
      gameThreadStalledSeconds: 12.5,
      slowTask: { name: "Compiling Shaders (9)", fraction: 0.68 },
    });
    const snapshot = readEngineSnapshot(project);
    expect(snapshot?.phase).toBe("ready");
    expect(snapshot?.slowTask?.fraction).toBeCloseTo(0.68);
    expect(snapshot?.ageSeconds).toBeGreaterThanOrEqual(0);
  });

  it("carries the startup fields the early module publishes", () => {
    // Before the engine loop exists there is no tick to stall, so the stall
    // figure is absent by design and module count is the progress signal.
    const project = makeProject(["LogInit: Display: Base directory: C:/UE/"], {
      phase: "config init",
      gameThreadTicking: false,
      gameThreadStalledSeconds: null,
      modulesLoaded: 253,
      slowTask: { name: "Loading Default Modules for Plugin: ChaosVD", fraction: 0.73 },
    });
    const snapshot = readEngineSnapshot(project);
    expect(snapshot?.gameThreadTicking).toBe(false);
    expect(snapshot?.gameThreadStalledSeconds).toBeNull();
    expect(snapshot?.modulesLoaded).toBe(253);
  });

  it("returns null when the plugin has not written one", () => {
    const project = makeProject(["LogInit: Display: Base directory: C:/UE/"]);
    expect(readEngineSnapshot(project)).toBeNull();
  });
});
