/**
 * The offline surface, and the guard that keeps it honest (T16).
 *
 * `src/offline.ts` answers "which of these 900-odd actions can run with no
 * editor". Most of that answer is mechanical, because an action carrying a
 * bridge method cannot run without one. The rest is a hand-written table, and a
 * hand-written table over a surface this size is exactly the thing that rots.
 *
 * So the first two cases here are the anti-drift guard: the table's key set and
 * the graph's handler-backed action set must be the same set, in both
 * directions. Add a local action without classifying it and the first case
 * names it. Rename or delete one and the second names the stale entry. Nothing
 * defaults quietly to "works offline", which is the answer that would cost an
 * agent a session to disprove.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { describe, it, expect } from "vitest";
import { ALL_TOOLS } from "../../src/tools.js";
import { projectTool } from "../../src/tools/project.js";
import { ProjectContext } from "../../src/project.js";
import { McpError, ErrorCode } from "../../src/errors.js";
import {
  LOCAL_ACTIONS,
  EDITOR_BOUND_LOCAL_ACTIONS,
  availabilityReport,
  classifyAction,
  classifyGraph,
  editorDownMessage,
  explainEditorDown,
  offlineActionNames,
} from "../../src/offline.js";
import type { ToolDef } from "../../src/types.js";

/** Every `tool.action` in this package whose work starts in a local handler. */
function handlerBackedActions(): string[] {
  const out: string[] = [];
  for (const tool of ALL_TOOLS) {
    for (const [action, spec] of Object.entries(tool.actions)) {
      if (!spec.bridge && spec.handler) out.push(`${tool.name}.${action}`);
    }
  }
  return out;
}

function classified(): Set<string> {
  return new Set([...Object.keys(LOCAL_ACTIONS), ...Object.keys(EDITOR_BOUND_LOCAL_ACTIONS)]);
}

describe("offline classification stays level with the dispatched graph", () => {
  it("classifies every action that runs in this process", () => {
    const known = classified();
    const missing = handlerBackedActions().filter((name) => !known.has(name));
    if (missing.length > 0) {
      throw new Error(
        `${missing.length} action(s) run in the server process and nothing says whether they ` +
          `then call the editor:\n` +
          missing.map((n) => `  ${n}`).join("\n") +
          `\n\nAdd each to LOCAL_ACTIONS in src/offline.ts (with the reason it needs no editor) ` +
          `or to EDITOR_BOUND_LOCAL_ACTIONS (with the bridge work it does). ` +
          `project(list_available_actions) reports this answer to callers, so an unclassified ` +
          `action is reported as unknown and treated as needing an editor.`,
      );
    }
    expect(missing).toEqual([]);
  });

  it("carries no entry for an action that no longer exists", () => {
    const live = new Set(handlerBackedActions());
    const stale = [...classified()].filter((name) => !live.has(name));
    if (stale.length > 0) {
      throw new Error(
        `${stale.length} classified action(s) are not in the graph any more:\n` +
          stale.map((n) => `  ${n}`).join("\n") +
          `\n\nThey were renamed, deleted, or turned into bridge actions. Remove them from ` +
          `src/offline.ts.`,
      );
    }
    expect(stale).toEqual([]);
  });

  it("never calls an action offline while it still carries a bridge method", () => {
    // The structural half of the guard. An action classified `always` must not
    // have the one field that makes dispatch send it to the editor, whatever
    // the table says about it.
    const contradictions: string[] = [];
    for (const tool of ALL_TOOLS) {
      for (const [action, spec] of Object.entries(tool.actions)) {
        const verdict = classifyAction(tool.name, action, spec);
        if (verdict.availability === "always" && spec.bridge) {
          contradictions.push(`${tool.name}.${action} -> ${spec.bridge}`);
        }
      }
    }
    expect(contradictions).toEqual([]);
  });

  it("classifies a bridge action from its own declaration, with the method named", () => {
    const verdict = classifyAction("asset", "list", { kind: "bridge", effect: "read", bridge: "list_assets" });
    expect(verdict.availability).toBe("editor");
    expect(verdict.bridgeMethod).toBe("list_assets");
    expect(verdict.reason).toContain("list_assets");
  });

  it("reports an unclassified local action as unknown rather than as offline", () => {
    const verdict = classifyAction("someplugin", "do_a_thing", { kind: "handler", effect: "read", handler: async () => ({}) });
    expect(verdict.availability).toBe("unknown");
    expect(verdict.reason).toContain("plugin");
  });

  it("keeps asset.migrate editor-bound, which a source scan gets wrong", () => {
    // migrate's handler is one line that names a module-level helper, and the
    // bridge call lives in the helper. Anything deriving this answer by
    // scanning the closure's source reads it as editor-free, and an agent is
    // then told a migrate will work with the editor down.
    const migrate = ALL_TOOLS.find((t) => t.name === "asset")!.actions.migrate;
    expect(migrate.bridge).toBeUndefined();
    expect(classifyAction("asset", "migrate", migrate).availability).toBe("editor");
  });
});

describe("the offline surface itself", () => {
  it("includes the C++ correctness primitives and the engine index", () => {
    const offline = new Set(offlineActionNames(ALL_TOOLS));
    for (const action of [
      "project.verify_symbols",
      "project.suggest_build_deps",
      "project.lint_cpp_header",
      "project.find_example_usage",
      "project.build_engine_index",
    ]) {
      expect(offline.has(action), `${action} should run with no editor`).toBe(true);
    }
  });

  it("includes the way back to a running editor and the way to a fresh build", () => {
    const offline = new Set(offlineActionNames(ALL_TOOLS));
    // An offline surface with no way to start an editor is a dead end.
    expect(offline.has("editor.start_editor")).toBe(true);
    expect(offline.has("project.get_status")).toBe(true);
    // UnrealBuildTool cannot link while an editor holds the module DLLs, so
    // the build is only usable while the editor is down.
    expect(offline.has("project.build")).toBe(true);
    expect(offline.has("editor.build_project")).toBe(true);
  });

  it("excludes everything that dispatches into the editor", () => {
    const offline = new Set(offlineActionNames(ALL_TOOLS));
    for (const action of [
      "asset.list",
      "level.get_outliner",
      "editor.execute_python",
      "blueprint.author",
      "asset.migrate",
    ]) {
      expect(offline.has(action), `${action} needs an editor`).toBe(false);
    }
  });

  it("is a real fraction of the surface, and a small one", () => {
    const total = classifyGraph(ALL_TOOLS).length;
    const offline = offlineActionNames(ALL_TOOLS).length;
    expect(total).toBeGreaterThan(700);
    expect(offline).toBeGreaterThan(30);
    // If this ever approaches the whole surface, the classifier has broken
    // rather than the bridge having become unnecessary.
    expect(offline).toBeLessThan(total / 2);
  });
});

describe("availabilityReport", () => {
  const graph: ToolDef[] = ALL_TOOLS;

  it("blocks the bridge half while the editor is down", () => {
    const report = availabilityReport(graph, { editorConnected: false });
    expect(report.availableNow).toBe(offlineActionNames(graph).length);
    expect(report.availableNow + report.blocked).toBe(report.total);
    expect(report.reason).toContain("No editor is connected");
  });

  it("blocks nothing while the editor is up, and still classifies", () => {
    const report = availabilityReport(graph, { editorConnected: true });
    expect(report.blocked).toBe(0);
    expect(report.availableNow).toBe(report.total);
    expect(report.reason).toBeUndefined();
  });

  it("narrows to one category", () => {
    const report = availabilityReport(graph, { editorConnected: false, category: "project" });
    expect(report.byCategory).toHaveLength(1);
    expect(report.byCategory[0].category).toBe("project");
    expect(report.total).toBe(Object.keys(graph.find((t) => t.name === "project")!.actions).length);
  });

  it("lists names only when asked, and only the side that was asked for", () => {
    expect(availabilityReport(graph, { editorConnected: false }).actions).toBeUndefined();

    const available = availabilityReport(graph, {
      editorConnected: false,
      category: "project",
      names: true,
      state: "available",
    });
    expect(available.actions!.every((a) => a.availableNow)).toBe(true);

    const blocked = availabilityReport(graph, {
      editorConnected: false,
      category: "project",
      names: true,
      state: "blocked",
    });
    expect(blocked.actions!.every((a) => !a.availableNow)).toBe(true);
    expect(blocked.actions!.every((a) => typeof a.bridgeMethod === "string")).toBe(true);

    const all = availabilityReport(graph, {
      editorConnected: false,
      category: "project",
      names: true,
      state: "all",
    });
    expect(all.actions!.length).toBe(available.actions!.length + blocked.actions!.length);
  });

  it("keeps the per-category rows adding up to the totals", () => {
    const report = availabilityReport(graph, { editorConnected: false });
    const summed = report.byCategory.reduce(
      (acc, row) => ({
        total: acc.total + row.total,
        availableNow: acc.availableNow + row.availableNow,
        blocked: acc.blocked + row.blocked,
      }),
      { total: 0, availableNow: 0, blocked: 0 },
    );
    expect(summed).toEqual({
      total: report.total,
      availableNow: report.availableNow,
      blocked: report.blocked,
    });
  });
});

describe("the message when the editor is not there", () => {
  const base = {
    method: "list_assets",
    projectPath: "C:/games/Vale/Vale.uproject",
    port: 51234,
    portSource: "derived",
    cause: "connect ECONNREFUSED 127.0.0.1:51234",
  };

  it("names the call, the project, the port and the way back", () => {
    const message = editorDownMessage({ ...base, offlineActionCount: 47 });
    expect(message).toContain("list_assets");
    expect(message).toContain("Vale.uproject");
    expect(message).toContain("51234");
    expect(message).toContain("connect ECONNREFUSED");
    expect(message).toContain("editor(action='start_editor')");
    expect(message).toContain("47 actions do not need an editor");
    expect(message).toContain("project(action='list_available_actions')");
  });

  it("says the editor is coming up rather than absent when its log says so", () => {
    const message = editorDownMessage({ ...base, phase: "compiling shaders" });
    expect(message).toContain("compiling shaders");
    expect(message).toContain("coming up rather than absent");
  });

  it("stops offering to wait when the log ends in a crash", () => {
    const message = editorDownMessage({ ...base, phase: "crashed", blocking: true });
    expect(message).toContain("check_for_crashes");
    expect(message).toContain("start_editor");
    expect(message).not.toContain("coming up rather than absent");
  });

  it("sends the modules-out-of-date prompt at the install check, which is its diagnosis", () => {
    const message = editorDownMessage({
      ...base,
      phase: "waiting on rebuild prompt (modules out of date)",
      blocking: true,
    });
    expect(message).toContain("project(action='check_install')");
  });

  it("points at the dialog when one is holding the editor", () => {
    const message = editorDownMessage({ ...base, phase: "waiting", blocking: true, modal: "Save Content" });
    expect(message).toContain("Save Content");
    expect(message).toContain("respond_to_dialog");
    // Restarting an editor that is merely waiting on a dialog throws away
    // whatever it was doing, so the advice must not be "start one".
    expect(message).not.toContain("start_editor");
  });

  it("rewrites a connection failure and leaves everything else alone", () => {
    const connection = new McpError(ErrorCode.NOT_CONNECTED, "Failed to connect to editor bridge at ws://127.0.0.1:51234");
    const rewritten = explainEditorDown(connection, base) as McpError;
    expect(rewritten).toBeInstanceOf(McpError);
    expect(rewritten.code).toBe(ErrorCode.NOT_CONNECTED);
    expect(rewritten.message).toContain("start_editor");
    // The call never reached a socket, so a retry is safe. A bridge timeout
    // says "unknown" for the opposite reason and must not be touched here.
    expect(rewritten.details?.outcome).toBe("failed");
    expect(rewritten.details?.method).toBe("list_assets");

    const timeout = new McpError(ErrorCode.BRIDGE_TIMEOUT, "timed out", { outcome: "unknown" });
    expect(explainEditorDown(timeout, base)).toBe(timeout);

    const plain = new Error("something else");
    expect(explainEditorDown(plain, base)).toBe(plain);
  });
});

describe("project(list_available_actions) over the real dispatcher", () => {
  /** A context whose bridge reports the connection state and nothing else. */
  function contextWith(connected: boolean) {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-offline-"));
    const uproject = path.join(root, "Probe.uproject");
    fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, EngineAssociation: "5.8" }));
    fs.mkdirSync(path.join(root, "Content"), { recursive: true });
    const project = new ProjectContext();
    project.setProject(uproject);
    return {
      project,
      bridge: {
        isConnected: connected,
        connect: async () => {},
        retargetProject: () => ({ projectPath: uproject, port: 9999, portSource: "derived" as const, verified: true }),
        getTarget: () => ({ projectPath: uproject, port: 9999, portSource: "derived" as const, verified: true }),
        call: async () => {
          throw new Error("this context must never reach the editor");
        },
      },
    };
  }

  it("answers with the editor down, without reaching an editor", async () => {
    const ctx = contextWith(false);
    const result = (await projectTool.handler(ctx as never, {
      action: "list_available_actions",
    })) as Record<string, unknown>;

    expect(result.editorConnected).toBe(false);
    expect(result.availableNow).toBeGreaterThan(30);
    expect(result.blocked).toBeGreaterThan(500);
    expect(result.hint).toContain("start_editor");
    expect((result.editorTarget as Record<string, unknown>).port).toBe(9999);
  });

  it("lists names for one category when asked", async () => {
    const ctx = contextWith(false);
    const result = (await projectTool.handler(ctx as never, {
      action: "list_available_actions",
      category: "project",
      includeNames: true,
    })) as Record<string, unknown>;

    const actions = result.actions as Array<{ action: string; availableNow: boolean }>;
    expect(actions.length).toBeGreaterThan(20);
    expect(actions.every((a) => a.availableNow)).toBe(true);
    expect(actions.some((a) => a.action === "verify_symbols")).toBe(true);
  });

  it("refuses an unknown category and an unknown state by listing the real ones", async () => {
    const ctx = contextWith(false);
    await expect(
      projectTool.handler(ctx as never, { action: "list_available_actions", category: "nope" }),
    ).rejects.toThrow(/Unknown category 'nope'\. Available: project/);
    await expect(
      projectTool.handler(ctx as never, { action: "list_available_actions", state: "sideways" }),
    ).rejects.toThrow(/'state' must be 'available', 'blocked' or 'all'/);
  });
});
