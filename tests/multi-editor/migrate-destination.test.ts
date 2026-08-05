/**
 * asset(migrate) into a second editor (#817, plan 6.5).
 *
 * Two bridges on two loopback sockets: the migrate has to leave from one and
 * the rescan has to land in the other, which is not observable from either
 * side alone.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { FakeBridge } from "../fake-bridge.js";
import { SessionRegistry, type EditorSession } from "../../src/session.js";
import { assetTool } from "../../src/tools/asset.js";
import {
  cloneToolDef,
  injectEditorTarget,
  injectMigrateTarget,
  removeEditorTarget,
  removeMigrateTarget,
  MIGRATE_TARGET_PARAM,
  type ToolContext,
} from "../../src/types.js";

let root: string;
let alphaBridge: FakeBridge;
let betaBridge: FakeBridge;
let sessions: SessionRegistry;
let alpha: EditorSession;
let beta: EditorSession;

function makeProject(name: string): string {
  const dir = path.join(root, name);
  fs.mkdirSync(path.join(dir, "Content"), { recursive: true });
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

function run(params: Record<string, unknown>, from: EditorSession = alpha): Promise<unknown> {
  return assetTool.handler(ctxFor(from), { action: "migrate", ...params });
}

beforeEach(async () => {
  root = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-migrate-")));
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

describe("asset(migrate) toEditor", () => {
  it("resolves the destination Content directory from the target session", async () => {
    await run({ assetPaths: ["/Game/Meshes/SM_Rock"], toEditor: "Beta" });

    const call = alphaBridge.calls.find((c) => c.method === "migrate")!;
    expect(call).toBeTruthy();
    expect(String(call.params.destinationContentDir).replace(/\\/g, "/")).toBe(
      path.join(root, "Beta", "Content").replace(/\\/g, "/"),
    );
    // The routing instruction is not a migrate parameter.
    expect(Object.keys(call.params)).not.toContain("toEditor");
  });

  it("rescans in the destination editor, not the source", async () => {
    await run({ assetPaths: ["/Game/Meshes/SM_Rock", "/Game/Meshes/SM_Tree"], toEditor: "Beta" });

    const rescans = betaBridge.calls.filter((c) => c.method === "diagnose_registry");
    expect(rescans).toHaveLength(1);
    expect(rescans[0].params).toMatchObject({ path: "/Game/Meshes", reconcile: true, recursive: true });
    // Alpha holds the source assets; rescanning it would prove nothing.
    expect(alphaBridge.methods).not.toContain("diagnose_registry");
  });

  it("reports the destination and the rescan on the response", async () => {
    const result = (await run({ assetPath: "/Game/Meshes/SM_Rock", toEditor: "Beta" })) as {
      destination: { editor: string; contentDir: string };
      rescan: { attempted: boolean; scanned: string[] };
    };
    expect(result.destination.editor).toBe("Beta");
    expect(result.rescan).toMatchObject({ attempted: true, scanned: ["/Game/Meshes"] });
  });

  it("copies nothing and rescans nothing on a dry run", async () => {
    const result = (await run({ assetPath: "/Game/Meshes/SM_Rock", toEditor: "Beta", dryRun: true })) as {
      rescan: { attempted: boolean };
    };
    expect(result.rescan.attempted).toBe(false);
    expect(betaBridge.methods).not.toContain("diagnose_registry");
  });

  it("refuses a destination that is the editor the call runs in", async () => {
    await expect(run({ assetPath: "/Game/X", toEditor: "Alpha" })).rejects.toThrow(/nothing to migrate between/);
    expect(alphaBridge.methods).not.toContain("migrate");
  });

  it("refuses a destination and a directory that could disagree", async () => {
    await expect(
      run({ assetPath: "/Game/X", toEditor: "Beta", destinationContentDir: "D:/Somewhere/Content" }),
    ).rejects.toThrow(/not both/);
    expect(alphaBridge.methods).not.toContain("migrate");
  });

  it("names the registered editors when the destination is unknown", async () => {
    await expect(run({ assetPath: "/Game/X", toEditor: "Gamma" })).rejects.toThrow(/Alpha, Beta/);
  });

  it("still takes a plain destination directory, unchanged", async () => {
    const result = await run({ assetPath: "/Game/X", destinationContentDir: "D:/Other/Content" });
    const call = alphaBridge.calls.find((c) => c.method === "migrate")!;
    expect(call.params.destinationContentDir).toBe("D:/Other/Content");
    // No session was addressed, so the response is the bridge's own, untouched.
    expect(result).not.toHaveProperty("destination");
    expect(betaBridge.calls).toHaveLength(0);
  });

  it("reports a closed destination instead of failing a copy that worked", async () => {
    beta.bridge.disconnect();
    const result = (await run({ assetPath: "/Game/X", toEditor: "Beta" })) as {
      rescan: { attempted: boolean; reason: string };
    };
    expect(result.rescan.attempted).toBe(false);
    expect(result.rescan.reason).toContain("not connected");
    expect(alphaBridge.methods).toContain("migrate");
  });
});

describe("the toEditor parameter itself", () => {
  it("is absent at one editor and present beyond one", () => {
    const tool = cloneToolDef(assetTool);
    expect(MIGRATE_TARGET_PARAM in tool.schema).toBe(false);

    injectEditorTarget(tool, ["Alpha", "Beta"]);
    injectMigrateTarget(tool, ["Alpha", "Beta"]);
    expect(MIGRATE_TARGET_PARAM in tool.schema).toBe(true);

    removeEditorTarget(tool);
    removeMigrateTarget(tool);
    expect(MIGRATE_TARGET_PARAM in tool.schema).toBe(false);
  });

  it("is not added to a category with nothing to migrate", () => {
    const tool = cloneToolDef(assetTool);
    delete tool.actions.migrate;
    expect(injectMigrateTarget(tool, ["Alpha", "Beta"]).injected).toBe(false);
    expect(MIGRATE_TARGET_PARAM in tool.schema).toBe(false);
  });
});
