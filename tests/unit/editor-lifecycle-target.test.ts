/**
 * Lifecycle actions must act on the editor for the loaded project and on no
 * other one (#819). These cover the paths where the port lockfile cannot vouch
 * for a listener, which is where the old resolver fell through to port 9877 and
 * a stop request could reach a different project's editor.
 */

import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterEach, describe, expect, it, vi } from "vitest";
import type { EditorProcess } from "../../src/engine-observer.js";

vi.mock("../../src/engine-observer.js", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../../src/engine-observer.js")>();
  return {
    ...actual,
    listEditorProcesses: vi.fn(async () => []),
    findInteractiveEditors: vi.fn(async () => []),
    findEditorByPid: vi.fn(async () => null),
    readEngineState: vi.fn(async () => ({
      running: true,
      processes: [],
      log: { logPath: null, secondsSinceWrite: null, phase: "unknown", blocking: false, lastLine: null, tail: [], errors: [], warnings: [] },
      snapshot: null,
      dialogs: [],
      summary: "stubbed engine state.",
      blocked: false,
    })),
  };
});

const observer = await import("../../src/engine-observer.js");
const { startEditor, stopEditor, restartEditor } = await import("../../src/editor-control.js");
const { bridgeLockfilePath } = await import("../../src/editor-target.js");
const { ProjectContext } = await import("../../src/project.js");

const findInteractiveEditors = vi.mocked(observer.findInteractiveEditors);
const findEditorByPid = vi.mocked(observer.findEditorByPid);

const temporaryRoots: string[] = [];

function makeProject(): { projectDir: string; projectPath: string } {
  const projectDir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-lifecycle-"));
  temporaryRoots.push(projectDir);
  const projectPath = path.join(projectDir, "Demo.uproject");
  fs.writeFileSync(projectPath, JSON.stringify({ EngineAssociation: "5.8" }));
  return { projectDir, projectPath };
}

function writeLockfile(projectDir: string, contents: Record<string, unknown>): void {
  const file = bridgeLockfilePath(projectDir);
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, JSON.stringify(contents));
}

function editor(pid: number, projectPath: string | null): EditorProcess {
  return { pid, commandLine: "", projectPath, headless: false, responding: true, windowTitle: null };
}

afterEach(() => {
  vi.clearAllMocks();
  findInteractiveEditors.mockResolvedValue([]);
  findEditorByPid.mockResolvedValue(null);
  while (temporaryRoots.length > 0) {
    fs.rmSync(temporaryRoots.pop()!, { recursive: true, force: true });
  }
});

describe("stopEditor targeting", () => {
  it("refuses without a loaded project instead of hunting for an editor", async () => {
    const result = await stopEditor(false, undefined);
    expect(result.success).toBe(false);
    expect(result.message).toContain("set_project");
    expect(findInteractiveEditors).not.toHaveBeenCalled();
  });

  it("names the lockfile it checked when no port is published", async () => {
    const { projectDir } = makeProject();
    const result = await stopEditor(false, projectDir);
    expect(result.success).toBe(false);
    expect(result.message).toContain(bridgeLockfilePath(projectDir));
    expect(result.message).not.toContain("9877");
  });

  it("does not guess a port when UE_MCP_PORT is set and the lockfile is gone", async () => {
    const previous = process.env.UE_MCP_PORT;
    process.env.UE_MCP_PORT = "9877";
    try {
      const { projectDir } = makeProject();
      const result = await stopEditor(false, projectDir);
      expect(result.success).toBe(false);
      expect(result.message).toContain("Editor is not running for this project");
    } finally {
      if (previous === undefined) delete process.env.UE_MCP_PORT;
      else process.env.UE_MCP_PORT = previous;
    }
  });

  it("reports the running editor when it published no port", async () => {
    const { projectDir, projectPath } = makeProject();
    findInteractiveEditors.mockResolvedValue([editor(777, projectPath)]);

    const result = await stopEditor(false, projectDir);
    expect(result.success).toBe(false);
    expect(result.message).toContain("777");
    expect(result.message).toContain("never force-kills");
  });

  it("refuses a lockfile whose process is gone rather than trusting its port", async () => {
    const { projectDir } = makeProject();
    writeLockfile(projectDir, { port: 51999, pid: 4242 });

    const result = await stopEditor(false, projectDir);
    expect(result.success).toBe(false);
    expect(result.message).toContain("4242");
    expect(result.message).toContain("no longer running");
    expect(findEditorByPid).toHaveBeenCalledWith(4242);
  });

  it("refuses when the recorded pid now belongs to another project", async () => {
    const { projectDir } = makeProject();
    const otherProject = path.join(os.tmpdir(), "SomeoneElse", "Other.uproject");
    writeLockfile(projectDir, { port: 51999, pid: 4242 });
    findEditorByPid.mockResolvedValue(editor(4242, otherProject));

    const result = await stopEditor(false, projectDir);
    expect(result.success).toBe(false);
    expect(result.message).toContain("Other.uproject");
    expect(result.message).toContain("not this project");
  });

  it("refuses a pidless lockfile when no editor for the project is running", async () => {
    const { projectDir } = makeProject();
    writeLockfile(projectDir, { port: 51999 });

    const result = await stopEditor(false, projectDir);
    expect(result.success).toBe(false);
    expect(result.message).toContain("no pid");
  });
});

describe("start and restart without a loaded project", () => {
  it("startEditor asks for a project instead of scanning the machine", async () => {
    const result = await startEditor(new ProjectContext());
    expect(result.success).toBe(false);
    expect(result.message).toContain("set_project");
    expect(findInteractiveEditors).not.toHaveBeenCalled();
  });

  it("restartEditor asks for a project instead of scanning the machine", async () => {
    const result = await restartEditor(new ProjectContext());
    expect(result.success).toBe(false);
    expect(result.message).toContain("set_project");
    expect(findInteractiveEditors).not.toHaveBeenCalled();
  });
});

describe("native editor shutdown", () => {
  it("uses MainFrame so standalone asset editors close before subsystem teardown", () => {
    const source = fs.readFileSync(
      new URL(
        "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/EditorHandlers.cpp",
        import.meta.url,
      ),
      "utf8",
    );
    const buildRules = fs.readFileSync(
      new URL(
        "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/UE_MCP_Bridge.Build.cs",
        import.meta.url,
      ),
      "utf8",
    );
    const shutdownHandler = source.slice(
      source.indexOf("FEditorHandlers::RequestEditorShutdown"),
      source.indexOf("FEditorHandlers::FocusViewportOnActor"),
    );

    expect(source).toContain('#include "Interfaces/IMainFrameModule.h"');
    expect(buildRules).toContain('"MainFrame",');
    expect(shutdownHandler).toContain("FModuleManager::LoadModuleChecked<IMainFrameModule>");
    expect(shutdownHandler).toContain("MainFrameModule.RequestCloseEditor()");
    expect(shutdownHandler).not.toContain("UKismetSystemLibrary::QuitEditor()");
  });
});
