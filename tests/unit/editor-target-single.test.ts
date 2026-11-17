/**
 * An editor name at one registered editor (#1278).
 *
 * The `editor` parameter is only advertised beyond one editor, but a caller can
 * still hold a name from before a reconnect. It must resolve strictly: an
 * unknown name refuses, and never falls through to the active editor.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { SessionRegistry } from "../../src/sessions/session.js";
import { resolveEditorTarget, takeEditorTarget } from "../../src/dispatch/editor-gate.js";
import type { ToolDef } from "../../src/surface/tool-types.js";

/** What the call boundary does with `editor`: take it off, then resolve it. */
function routeEditorCall(tool: ToolDef, params: Record<string, unknown>, registry: SessionRegistry) {
  const taken = takeEditorTarget(tool, params);
  return { ...resolveEditorTarget(registry, taken.target), params: taken.params };
}
import { editorTool } from "../../src/tools/editor.js";
import { buildMicroGateway } from "../../src/surface/context/micro-context.js";
import { ProjectFixture } from "../helpers/project-fixture.js";

let fixture: ProjectFixture;
let sessions: SessionRegistry;

beforeEach(() => {
  fixture = new ProjectFixture("ue-mcp-single-target-");
  sessions = new SessionRegistry();
  sessions.register({ projectPath: fixture.makeProject("Alpha") });
});

afterEach(() => {
  for (const s of sessions.list()) s.bridge.disconnect();
  fixture.cleanup();
});

describe("editor targeting at one editor (#1278)", () => {
  it("refuses an unknown editor name instead of using the active editor", () => {
    expect(() => routeEditorCall(editorTool, { action: "stop_editor", editor: "voxeltest" }, sessions))
      .toThrow(/No editor session named 'voxeltest'\. Registered: Alpha/);
  });

  it("refuses an unknown name inside a gateway call's args", () => {
    const gateway = buildMicroGateway([editorTool]);
    expect(() => routeEditorCall(gateway, {
      action: "call", category: "editor", method: "stop_editor", args: { editor: "voxeltest" },
    }, sessions)).toThrow(/voxeltest/);
  });

  it("accepts the registered name and strips it from the parameters", () => {
    const params = { action: "stop_editor", editor: "Alpha" };
    const routed = routeEditorCall(editorTool, params, sessions);
    expect(routed.session.name).toBe("Alpha");
    expect(routed.params).toEqual({ action: "stop_editor" });
    expect(params.editor).toBe("Alpha");
  });

  it("leaves an untargeted call on the active editor", () => {
    const routed = routeEditorCall(editorTool, { action: "stop_editor" }, sessions);
    expect(routed.session.name).toBe("Alpha");
    expect(routed.targeted).toBe(false);
  });
});
