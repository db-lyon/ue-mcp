import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import {
  readLogState,
  readEngineSnapshot,
  withBridgeSnapshot,
  type EngineState,
} from "../../src/editor/engine-observer.js";

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

describe("readEngineSnapshot per-process files (#990)", () => {
  function writeInstanceStatus(project: string, pid: number, snapshot: unknown, mtimeMsAgo = 0): string {
    const file = path.join(path.dirname(project), "Saved", "UE_MCP_Bridge", `status.${pid}.json`);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, JSON.stringify(snapshot));
    if (mtimeMsAgo > 0) {
      const when = new Date(Date.now() - mtimeMsAgo);
      fs.utimesSync(file, when, when);
    }
    return file;
  }

  it("prefers a per-process file over the shared one", () => {
    // The shared status.json describes only whichever process wrote last, so
    // it is the fallback and not the answer.
    const project = makeProject(["LogInit: Display: Base directory: C:/UE/"], { phase: "shared" });
    writeInstanceStatus(project, 4242, { phase: "per-process", pid: 4242 });
    expect(readEngineSnapshot(project)?.phase).toBe("per-process");
    expect(readEngineSnapshot(project)?.pid).toBe(4242);
  });

  it("takes the freshest of several, which is the one still being written", () => {
    const project = makeProject(["LogInit: Display: Base directory: C:/UE/"]);
    writeInstanceStatus(project, 111, { phase: "crashed hours ago", pid: 111 }, 3_600_000);
    writeInstanceStatus(project, 222, { phase: "alive", pid: 222 });
    expect(readEngineSnapshot(project)?.phase).toBe("alive");
  });

  it("still reads an editor that writes only the shared path", () => {
    // A deployed plugin older than this change writes status.json and nothing
    // else, and must keep working.
    const project = makeProject(["LogInit: Display: Base directory: C:/UE/"], { phase: "ready" });
    expect(readEngineSnapshot(project)?.phase).toBe("ready");
  });

  it("falls through an unreadable per-process file to the shared one", () => {
    const project = makeProject(["LogInit: Display: Base directory: C:/UE/"], { phase: "shared" });
    writeInstanceStatus(project, 4242, {});
    fs.writeFileSync(
      path.join(path.dirname(project), "Saved", "UE_MCP_Bridge", "status.4242.json"),
      "{ half written",
    );
    expect(readEngineSnapshot(project)?.phase).toBe("shared");
  });

  it("ignores the pid-suffixed temp files the writer renames through", () => {
    const project = makeProject(["LogInit: Display: Base directory: C:/UE/"], { phase: "shared" });
    const dir = path.join(path.dirname(project), "Saved", "UE_MCP_Bridge");
    fs.writeFileSync(path.join(dir, "status.json.1234.tmp"), JSON.stringify({ phase: "mid-write" }));
    expect(readEngineSnapshot(project)?.phase).toBe("shared");
  });
});

describe("withBridgeSnapshot", () => {
  /** The state a failed process probe produces: nothing found, nothing known. */
  function blindState(overrides: Partial<EngineState> = {}): EngineState {
    return {
      running: false,
      processes: [],
      log: {
        logPath: null,
        secondsSinceWrite: null,
        phase: "ready",
        blocking: false,
        lastLine: null,
        tail: [],
        errors: [],
        warnings: [],
      },
      snapshot: null,
      dialogs: [],
      processProbeFailed: true,
      runningEvidence: "none",
      snapshotSource: "none",
      summary: "No editor process for this project. Log phase: ready.",
      blocked: false,
      ...overrides,
    };
  }

  it("an editor that answered the call is running, whatever the process table saw", () => {
    // #965: one response carried running:false with processes:[] AND a live
    // snapshot the editor had just served over the bridge. The report
    // contradicted itself, and an agent acting on the first half went looking
    // for an editor that was answering it.
    const merged = withBridgeSnapshot(blindState(), {
      phase: "ready",
      uptimeSeconds: 72.2,
      gameThreadTicking: true,
    });

    expect(merged.running).toBe(true);
    expect(merged.runningEvidence).toBe("bridge-snapshot");
    expect(merged.snapshotSource).toBe("bridge");
    expect(merged.summary).not.toContain("No editor process");
    expect(merged.summary).toContain("answered over the bridge");
  });

  it("keeps the failed probe visible as its own fact", () => {
    const merged = withBridgeSnapshot(blindState(), { phase: "ready" });
    expect(merged.processProbeFailed).toBe(true);
    expect(merged.processes).toEqual([]);
  });

  it("surfaces a modal the bridge reported even with no process row to attach it to", () => {
    const merged = withBridgeSnapshot(blindState(), {
      phase: "ready",
      modal: { title: "Restore Packages", message: "Restore auto-saved packages?", buttons: ["Yes", "No"] },
    });
    expect(merged.blocked).toBe(true);
    expect(merged.summary).toContain("Restore Packages");
    expect(merged.summary).toContain("Yes, No");
  });

  it("does not call the editor blocked for a prompt that parks nothing", () => {
    // #1118: an ordinary editor window is reported and is not an outage. The
    // summary still names it, because an unanswered question is worth seeing;
    // `blocked` is what callers act on and it stays false.
    const merged = withBridgeSnapshot(blindState(), {
      phase: "ready",
      modal: { title: "Find Results", message: "3 results", buttons: [], blocksGameThread: false },
    });
    expect(merged.blocked).toBe(false);
    expect(merged.summary).toContain("Find Results");
    expect(merged.summary).toContain("non-blocking prompt");
  });

  it("still calls it blocked when the prompt says it parks the game thread", () => {
    const merged = withBridgeSnapshot(blindState(), {
      phase: "ready",
      modal: { title: "Save Content", message: "m", buttons: ["Cancel"], blocksGameThread: true },
    });
    expect(merged.blocked).toBe(true);
  });

  it("says the probe found nothing when the probe itself did run", () => {
    const merged = withBridgeSnapshot(blindState({ processProbeFailed: false }), { phase: "ready" });
    expect(merged.running).toBe(true);
    expect(merged.summary).toContain("did not find it");
  });
});
