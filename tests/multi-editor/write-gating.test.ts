/**
 * Write gating across two real bridges (#817, plan 5.2 and 5.3).
 *
 * The hard constraint this enforces: no call may mutate an editor belonging to
 * a project other than the one it explicitly targeted. Two editors answer on
 * two loopback sockets, and every assertion is made on what the OTHER bridge
 * received, because a routing bug is invisible from the bridge that was
 * supposed to be called.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { FakeBridge } from "../fake-bridge.js";
import { SessionRegistry, type EditorSession } from "../../src/session.js";
import {
  routeEditorCall,
  effectiveTaskName,
  refuseUntargetedInRegistry,
  editorAttribution,
} from "../../src/editor-gate.js";
import { injectEditorTarget, type ToolContext, type ToolDef } from "../../src/types.js";
import { assetTool } from "../../src/tools/asset.js";
import { editorTool } from "../../src/tools/editor.js";
import { cloneToolDef } from "../../src/types.js";

let root: string;
let alphaBridge: FakeBridge;
let betaBridge: FakeBridge;
let sessions: SessionRegistry;
let alpha: EditorSession;
let beta: EditorSession;

function makeProject(name: string): string {
  const dir = path.join(root, name);
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(
    path.join(dir, `${name}.uproject`),
    JSON.stringify({ FileVersion: 3, EngineAssociation: "5.6" }),
    "utf-8",
  );
  return path.join(dir, `${name}.uproject`);
}

function ctxFor(session: EditorSession): ToolContext {
  return { bridge: session.guarded, project: session.project, session, sessions };
}

/** A targetable copy of a real category tool, as the server advertises it. */
function targetable(tool: ToolDef): ToolDef {
  const copy = cloneToolDef(tool);
  injectEditorTarget(copy, sessions.list().map((s) => s.name));
  return copy;
}

/**
 * One dispatch, exactly as index.ts performs it: route, gate, then run the
 * tool's own handler against the routed session's context.
 */
async function dispatch(tool: ToolDef, params: Record<string, unknown>): Promise<
  { refused: string } | { ran: true; editor: string; attribution: string | null }
> {
  const routed = routeEditorCall(tool, params, sessions);
  const refusal = refuseUntargetedInRegistry(
    sessions,
    effectiveTaskName(tool, routed.params),
    routed.targeted,
  );
  if (refusal) return { refused: refusal };
  await tool.handler(ctxFor(routed.session), routed.params);
  return {
    ran: true,
    editor: routed.session.name,
    attribution: editorAttribution(
      { name: routed.session.name, projectPath: routed.session.project.projectPath },
      sessions.size,
    ),
  };
}

beforeEach(async () => {
  root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-gating-"));
  const alphaProject = makeProject("Alpha");
  const betaProject = makeProject("Beta");
  alphaBridge = await FakeBridge.start({ projectDir: path.dirname(alphaProject) });
  betaBridge = await FakeBridge.start({ projectDir: path.dirname(betaProject) });

  sessions = new SessionRegistry();
  alpha = sessions.register({ projectPath: alphaProject });
  beta = sessions.register({ projectPath: betaProject });
  await alpha.bridge.connect(5000);
  await beta.bridge.connect(5000);
});

afterEach(async () => {
  alpha.bridge.disconnect();
  beta.bridge.disconnect();
  await alphaBridge.stop();
  await betaBridge.stop();
  fs.rmSync(root, { recursive: true, force: true });
});

describe("untargeted calls beyond one editor", () => {
  it("refuses a mutation and reaches neither bridge", async () => {
    const tool = targetable(assetTool);
    const result = await dispatch(tool, { action: "delete", assetPath: "/Game/Doomed" });

    expect("refused" in result).toBe(true);
    expect((result as { refused: string }).refused).toContain("Alpha");
    expect((result as { refused: string }).refused).toContain("Beta");
    // The point: nothing was deleted anywhere, including in the session that
    // would have served it.
    expect(alphaBridge.methods).not.toContain("delete_asset");
    expect(betaBridge.methods).not.toContain("delete_asset");
  });

  it("refuses a lifecycle action, which is the one that closes a window", async () => {
    const tool = targetable(editorTool);
    const result = await dispatch(tool, { action: "stop_editor" });
    expect("refused" in result).toBe(true);
    expect(alphaBridge.calls).toHaveLength(0);
    expect(betaBridge.calls).toHaveLength(0);
  });

  it("still serves a read from the active session", async () => {
    const tool = targetable(assetTool);
    const result = await dispatch(tool, { action: "list", path: "/Game" });

    expect("ran" in result).toBe(true);
    expect((result as { editor: string }).editor).toBe("Alpha");
    expect(alphaBridge.methods).toContain("list_assets");
    expect(betaBridge.calls).toHaveLength(0);
  });
});

describe("targeted calls beyond one editor", () => {
  it("mutates only the editor it named", async () => {
    const tool = targetable(assetTool);
    const result = await dispatch(tool, {
      action: "delete",
      assetPath: "/Game/Doomed",
      editor: "Beta",
    });

    expect("ran" in result).toBe(true);
    expect((result as { editor: string }).editor).toBe("Beta");
    expect(betaBridge.methods).toContain("delete_asset");
    // Alpha is another user's project as far as this call is concerned.
    expect(alphaBridge.calls).toHaveLength(0);
  });

  it("never forwards the routing instruction into the bridge call", async () => {
    const tool = targetable(assetTool);
    await dispatch(tool, { action: "delete", assetPath: "/Game/Doomed", editor: "Beta" });
    const call = betaBridge.calls.find((c) => c.method === "delete_asset")!;
    expect(Object.keys(call.params)).not.toContain("editor");
  });

  it("names the serving editor on the response", async () => {
    const tool = targetable(assetTool);
    const result = await dispatch(tool, { action: "list", path: "/Game", editor: "Beta" });
    expect((result as { attribution: string }).attribution).toContain('"editor":"Beta"');
  });
});

describe("one editor", () => {
  it("gates nothing and attributes nothing", async () => {
    // Drop back to a single session: the gate must become inert, not lenient,
    // and the response must carry no extra block.
    sessions.drop("Beta");
    expect(sessions.size).toBe(1);

    const tool = cloneToolDef(assetTool);
    const result = await dispatch(tool, { action: "delete", assetPath: "/Game/Doomed" });

    expect("ran" in result).toBe(true);
    expect((result as { attribution: string | null }).attribution).toBeNull();
    expect(alphaBridge.methods).toContain("delete_asset");
  });
});
