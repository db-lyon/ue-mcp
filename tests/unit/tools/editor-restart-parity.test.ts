/**
 * editor(restart_editor) is a declared flow over stop_editor, an editor-state
 * probe and start_editor, and answers exactly as the composition it replaced.
 * That composition, as it stood in editor-control.ts, is kept below as the
 * oracle, and both run against the same scripted stop, start and processes.
 */
import { beforeEach, describe, expect, it, vi } from "vitest";
import type { TaskResult } from "@db-lyon/flowkit";
import type { EditorProcess } from "../../../src/editor/engine-observer.js";

const script = vi.hoisted(() => ({
  stop: async (): Promise<Record<string, unknown>> => ({ success: true, message: "stopped" }),
  start: async (): Promise<Record<string, unknown>> => ({ success: true, message: "started" }),
  interactive: [] as unknown[],
  stopCalls: 0,
  startCalls: [] as unknown[][],
}));

vi.mock("../../../src/editor/editor-control.js", async (importOriginal) => ({
  ...(await importOriginal<typeof import("../../../src/editor/editor-control.js")>()),
  stopEditor: vi.fn(async () => { script.stopCalls++; return script.stop(); }),
  startEditor: vi.fn(async (...args: unknown[]) => { script.startCalls.push(args); return script.start(); }),
}));

vi.mock("../../../src/editor/engine-observer.js", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../../../src/editor/engine-observer.js")>();
  return {
    ...actual,
    findInteractiveEditors: vi.fn(async () => script.interactive),
    readEngineState: vi.fn(async () => ({
      running: script.interactive.length > 0,
      processes: script.interactive,
      log: { logPath: null, secondsSinceWrite: null, phase: "unknown", blocking: false, lastLine: null, tail: [], errors: [], warnings: [] },
      snapshot: null,
      dialogs: [],
      summary: "scripted",
      blocked: false,
    })),
  };
});

const { editorTool } = await import("../../../src/tools/editor.js");
const { stopEditor, startEditor, connectedEditorOf } = await import("../../../src/editor/editor-control.js");
const { findInteractiveEditors } = await import("../../../src/editor/engine-observer.js");
const { buildFlowRegistry } = await import("../../../src/flow/registry.js");
const { createLiveTask } = await import("../../../src/flow/live-task.js");
const { startProgress } = await import("../../../src/cli/ui/progress.js");
import type { ToolContext, ToolDef } from "../../../src/core/types.js";
import type { FlowContext } from "../../../src/flow/context.js";

/** restartEditor, verbatim from editor-control.ts before the move. */
async function legacyRestart(ctx: ToolContext): Promise<unknown> {
  const project = ctx.project;
  const bridge = ctx.bridge;
  if (!project.projectPath) {
    return { success: false, message: "No project loaded. Use project(action='set_project') first." };
  }
  const stopResult = await stopEditor(project.projectDir ?? undefined, { connected: connectedEditorOf(bridge) });
  if (!stopResult.success && (await findInteractiveEditors(project.projectPath)).length > 0) {
    return { success: false, message: `Failed to stop editor: ${stopResult.message}` };
  }
  const startResult = await startEditor(project, undefined, undefined, { openDisplay: startProgress });
  if (!startResult.success) {
    return startResult;
  }
  if (bridge) {
    try {
      await bridge.connect(5000);
    } catch {
      // Bridge reconnect timer will handle it
    }
  }
  return startResult;
}

const legacyTool: ToolDef = editorTool.rebuild!({
  ...editorTool.actions,
  restart_editor: { kind: "handler", effect: "mutate", description: editorTool.actions.restart_editor.description, handler: legacyRestart },
});

function context(projectPath: string | null) {
  const bridgeLog = { connects: [] as unknown[], connected: false, failConnect: false };
  const bridge = {
    get isConnected() { return bridgeLog.connected; },
    call: async () => ({ success: true }),
    connect: async (timeoutMs?: number) => {
      bridgeLog.connects.push(timeoutMs);
      if (bridgeLog.failConnect) throw new Error("no editor yet");
      bridgeLog.connected = true;
    },
    getTarget: () => ({ projectPath, port: 0, portSource: "default", verified: false }),
  };
  const project = {
    projectPath,
    projectDir: projectPath ? "C:/Projects/Demo" : null,
    projectName: projectPath ? "Demo" : null,
    ensureLoaded: () => {},
  };
  return { bridge, project, bridgeLog };
}

function settled(result: TaskResult): Record<string, unknown> {
  return { success: result.success, data: result.data, error: result.error?.message };
}

async function run(tool: ToolDef, projectPath: string | null, failConnect = false) {
  script.stopCalls = 0;
  script.startCalls = [];
  const { bridge, project, bridgeLog } = context(projectPath);
  bridgeLog.failConnect = failConnect;
  const ctx = { project, bridge, getToolGraph: () => [tool] } as unknown as FlowContext;
  let result: Record<string, unknown>;
  try {
    const task = await createLiveTask({ registry: buildFlowRegistry([tool]) }, ctx, "editor.restart_editor", {});
    result = settled(await task.run());
  } catch (e) {
    result = { thrown: (e as Error).message };
  }
  // The category tool's own handler, the route a direct call takes.
  const directCtx = { ...ctx, bridge: context(projectPath).bridge } as unknown as ToolContext;
  let direct: string;
  try {
    direct = JSON.stringify(await tool.handler(directCtx, { action: "restart_editor" }));
  } catch (e) {
    direct = `thrown: ${(e as Error).message}`;
  }
  return {
    result,
    direct,
    json: JSON.stringify(result),
    stops: script.stopCalls,
    // What startEditor acts on: 300s is its own default, and an unset policy or echo launches the same editor.
    starts: script.startCalls.map(([, timeout, onProgress, opts]) => {
      const o = (opts ?? {}) as { openDisplay?: unknown; dialogPolicy?: unknown; paramEcho?: unknown };
      return { timeout: timeout ?? 300, onProgress, openDisplay: o.openDisplay, dialogPolicy: o.dialogPolicy, paramEcho: !!o.paramEcho };
    }),
    connects: bridgeLog.connects,
  };
}

async function expectParity(projectPath: string | null, failConnect = false) {
  const before = await run(legacyTool, projectPath, failConnect);
  const after = await run(editorTool, projectPath, failConnect);
  expect(after).toEqual(before);
  return after;
}

const PROJECT = "C:/Projects/Demo/Demo.uproject";
const RUNNING = [{ pid: 7, commandLine: "UnrealEditor.exe Demo.uproject" } as unknown as EditorProcess];

beforeEach(() => {
  script.stop = async () => ({ success: true, message: "stopped" });
  script.start = async () => ({ success: true, message: "started", port: 1 });
  script.interactive = [];
});

describe("restart_editor, declared as a flow", () => {
  it("is a flow action now", () => {
    expect(editorTool.actions.restart_editor.kind).toBe("flow");
  });

  it("asks for a project instead of scanning the machine", async () => {
    const out = await expectParity(null);
    expect(out.result.data).toEqual({ success: false, message: "No project loaded. Use project(action='set_project') first." });
    expect(out.stops).toBe(0);
    expect(findInteractiveEditors).not.toHaveBeenCalled();
  });

  it("stops, starts and reconnects", async () => {
    const out = await expectParity(PROJECT);
    expect(out.result.data).toEqual({ success: true, message: "started", port: 1 });
    expect(out.connects).toEqual([5000]);
  });

  it("aborts when the stop fails and an interactive editor of this project is still up", async () => {
    script.stop = async () => ({ success: false, message: "unsaved packages: /Game/Map" });
    script.interactive = RUNNING;
    const out = await expectParity(PROJECT);
    expect(out.result.data).toEqual({ success: false, message: "Failed to stop editor: unsaved packages: /Game/Map" });
    expect(out.starts).toEqual([]);
  });

  it("starts anyway when the failed stop left nothing of this project running", async () => {
    script.stop = async () => ({ success: false, message: "already stopped", alreadyStopped: true });
    const out = await expectParity(PROJECT);
    expect(out.starts).toHaveLength(2); // one per route
  });

  it("returns a failed start as it came, without reconnecting", async () => {
    script.start = async () => ({ success: false, message: "already running", alreadyRunning: true });
    const out = await expectParity(PROJECT);
    expect(out.connects).toEqual([]);
  });

  it("leaves a reconnect that does not land to the reconnect timer", async () => {
    await expectParity(PROJECT, true);
  });

  it("fails as the composition did when the stop throws, starting nothing", async () => {
    script.stop = async () => { throw new Error("lockfile unreadable"); };
    const out = await expectParity(PROJECT);
    expect(out.starts).toEqual([]);
  });

  it("fails as the composition did when the start throws", async () => {
    script.start = async () => { throw new Error("engine not found"); };
    await expectParity(PROJECT);
  });
});
