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
    const result = await stopEditor(undefined);
    expect(result.success).toBe(false);
    expect(result.message).toContain("set_project");
    expect(findInteractiveEditors).not.toHaveBeenCalled();
  });

  it("names the lockfile it checked when no port is published", async () => {
    const { projectDir } = makeProject();
    const result = await stopEditor(projectDir);
    // No lockfile and no editor process means no editor was closed, so the
    // call refuses and says which file it read. `alreadyStopped` labels the
    // reason without softening the verdict.
    expect(result.success).toBe(false);
    expect(result.alreadyStopped).toBe(true);
    expect(result.message).toContain(bridgeLockfilePath(projectDir));
    expect(result.message).not.toContain("9877");
  });

  it("does not guess a port when UE_MCP_PORT is set and the lockfile is gone", async () => {
    const previous = process.env.UE_MCP_PORT;
    process.env.UE_MCP_PORT = "9877";
    try {
      const { projectDir } = makeProject();
      const result = await stopEditor(projectDir);
      expect(result.success).toBe(false);
      expect(result.alreadyStopped).toBe(true);
      expect(result.message).toContain("Editor is not running for this project");
    } finally {
      if (previous === undefined) delete process.env.UE_MCP_PORT;
      else process.env.UE_MCP_PORT = previous;
    }
  });

  it("reports the running editor when it published no port", async () => {
    const { projectDir, projectPath } = makeProject();
    findInteractiveEditors.mockResolvedValue([editor(777, projectPath)]);

    const result = await stopEditor(projectDir);
    expect(result.success).toBe(false);
    expect(result.message).toContain("777");
    // It must say the process is only ever asked, and it must NOT offer the
    // old blanket reassurance. "ue-mcp never force-kills processes" was true
    // about signals and false about consequences: a shutdown prompt answered
    // to discard changes loses the same work a kill would have.
    expect(result.message).toContain("only ever ASKS");
    expect(result.message).toContain("unsaved work");
    expect(result.message).not.toContain("never force-kills");
  });

  it("refuses a lockfile whose process is gone rather than trusting its port", async () => {
    const { projectDir } = makeProject();
    writeLockfile(projectDir, { port: 51999, pid: 4242 });

    const result = await stopEditor(projectDir);
    // A clean exit deletes port.json, so a file naming a dead pid is what a
    // CRASH left behind. Nothing of this project's is running, which is the
    // state the message has to describe; the port is still not dialled, and
    // the call still refuses because it closed nothing.
    expect(result.success).toBe(false);
    expect(result.alreadyStopped).toBe(true);
    expect(result.message).toContain("4242");
    expect(result.message).toContain("no longer running");
    expect(findEditorByPid).toHaveBeenCalledWith(4242);
  });

  it("calls a lockfile that names somebody else's process stale, not a project mismatch", async () => {
    const { projectDir } = makeProject();
    const otherProject = path.join(os.tmpdir(), "SomeoneElse", "Other.uproject");
    writeLockfile(projectDir, { port: 51999, pid: 4242 });
    findEditorByPid.mockResolvedValue(editor(4242, otherProject));

    const result = await stopEditor(projectDir);
    expect(result.success).toBe(false);
    expect(result.alreadyStopped).toBe(true);
    expect(result.message).toContain("Stale lockfile");
    expect(result.message).toContain("Other.uproject");
    expect(result.message).toContain("no longer the editor for this project");
  });

  it("does not refuse the editor that actually has this project open (#967)", async () => {
    // The regression: the guard fired against the very editor it should match
    // and quoted that editor's own .uproject back as proof of a mismatch.
    const { projectDir, projectPath } = makeProject();
    writeLockfile(projectDir, { port: 51999, pid: 4242 });
    findEditorByPid.mockResolvedValue(editor(4242, projectPath));
    findInteractiveEditors.mockResolvedValue([editor(4242, projectPath)]);

    const result = await stopEditor(projectDir);
    // Nothing is listening on 51999 in a unit test, so the stop cannot finish.
    // What matters is that it got past ownership rather than refusing there.
    expect(result.message).not.toContain("Stale lockfile");
    expect(result.message).not.toContain("Delete that file");
    expect(result.message).toContain("bridge is unreachable");
  });

  it("self-heals to the live editor's own record instead of blaming the lockfile", async () => {
    // A stale pid in port.json while a healthy editor of this project is up:
    // telling the user to delete the file would discard the bridge's only
    // handle on it (#967/#934).
    const { projectDir, projectPath } = makeProject();
    writeLockfile(projectDir, { port: 51999, pid: 4242 });
    findEditorByPid.mockResolvedValue(null);
    findInteractiveEditors.mockResolvedValue([editor(777, projectPath)]);
    const record = path.join(projectDir, "Saved", "UE_MCP_Bridge", "instances", "777.json");
    fs.mkdirSync(path.dirname(record), { recursive: true });
    fs.writeFileSync(record, JSON.stringify({ pid: 777, port: 52222, state: "listening" }));

    const result = await stopEditor(projectDir);
    expect(result.message).not.toContain("Stale lockfile");
    expect(result.message).toContain("bridge is unreachable");
  });

  it("refuses a pidless lockfile when no editor for the project is running", async () => {
    const { projectDir } = makeProject();
    writeLockfile(projectDir, { port: 51999 });

    const result = await stopEditor(projectDir);
    expect(result.success).toBe(false);
    expect(result.alreadyStopped).toBe(true);
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

/**
 * "There was nothing to do" is a failure, and the marker is how it reads.
 *
 * A lifecycle action's verdict answers one question: did this call do the
 * thing. An editor that was already up means start_editor launched nothing,
 * and an editor that was already down means stop_editor closed nothing, so
 * both report `success: false`. That is not a nuisance to be engineered
 * around - it is the honest account of what the call did, and the alternative
 * is a handler that says yes to make somebody else's control flow shorter.
 *
 * `alreadyRunning` / `alreadyStopped` carry the part a caller genuinely needs:
 * whether the failure was "nothing to do" or "the thing broke". A flow whose
 * step expects the first absorbs it at the step, with `ignore_failure: true`,
 * which flowkit records as a failed step and walks past. See docs/flows.md.
 */
describe("a lifecycle no-op fails, and says why it did", () => {
  it("refuses a start for an editor that is already running, and spawns nothing", async () => {
    const { projectDir, projectPath } = makeProject();
    const project = new ProjectContext();
    project.setProject(projectPath);
    findInteractiveEditors.mockResolvedValue([editor(4242, projectPath)]);
    expect(projectDir).toBeTruthy();

    const result = await startEditor(project, 1);
    expect(result.success).toBe(false);
    expect(result.alreadyRunning).toBe(true);
    // Its bridge is not answering in a unit test, and that is reported as a
    // flag rather than left for a caller to read out of the sentence.
    expect(result.bridgeReady).toBe(false);
    expect(result.message).toContain("already running for this project");
  });

  it("refuses a stop for an editor that is already down, and marks the reason", async () => {
    const { projectDir } = makeProject();
    const result = await stopEditor(projectDir);
    expect(result.success).toBe(false);
    expect(result.alreadyStopped).toBe(true);
  });

  it("leaves the marker off a refusal that is not a no-op: a running editor with no port", async () => {
    // An editor IS running and cannot be reached. Nothing to absorb here: the
    // caller has a problem to fix, and no marker suggests otherwise.
    const { projectDir, projectPath } = makeProject();
    findInteractiveEditors.mockResolvedValue([editor(777, projectPath)]);

    const result = await stopEditor(projectDir);
    expect(result.success).toBe(false);
    expect(result.alreadyStopped).toBeUndefined();
    expect(result.message).toContain("777");
  });

  it("leaves it off a missing project too, which is a caller error and not a no-op", async () => {
    const stop = await stopEditor(undefined);
    expect(stop.success).toBe(false);
    expect(stop.alreadyStopped).toBeUndefined();
    const start = await startEditor(new ProjectContext());
    expect(start.success).toBe(false);
    expect(start.alreadyRunning).toBeUndefined();
  });
});

/**
 * The PIE half of the same contract, pinned at the source because the C++ is
 * built by the engine and not by this suite. A start with a session up and a
 * stop with none both fail, and both carry the marker that says the reason was
 * a no-op rather than a broken call.
 */
describe("pie_control fails a no-op and marks it", () => {
  const pieSource = fs.readFileSync(
    new URL(
      "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/EditorHandlers_PIE.cpp",
      import.meta.url,
    ),
    "utf8",
  );

  /** The body of one `if` branch inside PieControl, by the text that opens it. */
  function branchAfter(marker: string): string {
    const at = pieSource.indexOf(marker);
    expect(at, `EditorHandlers_PIE.cpp no longer contains ${marker}`).toBeGreaterThan(-1);
    return pieSource.slice(at, at + 1600);
  }

  it("answers an active session with a failure that says it is already running", () => {
    const branch = branchAfter("if (GEditor->PlayWorld != nullptr)");
    expect(branch).toContain('SetBoolField(TEXT("success"), false)');
    expect(branch).toContain('SetBoolField(TEXT("alreadyRunning"), true)');
    expect(branch).toContain("PIE session already active");
  });

  it("answers an absent session with a failure that says it is already stopped", () => {
    const branch = branchAfter("if (GEditor->PlayWorld == nullptr)");
    expect(branch).toContain('SetBoolField(TEXT("success"), false)');
    expect(branch).toContain('SetBoolField(TEXT("alreadyStopped"), true)');
    expect(branch).toContain("No PIE session active");
  });

  it("says both changed nothing, so a caller cannot read a no-op as a mutation", () => {
    // Two `changed` false fields, one per no-op branch, alongside the
    // read-only status branch that already had one.
    const changedFalse = pieSource.match(/SetBoolField\(TEXT\("changed"\), false\)/g) ?? [];
    expect(changedFalse.length).toBeGreaterThanOrEqual(3);
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
