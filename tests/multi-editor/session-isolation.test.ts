/**
 * State that must not be shared between editors (#817, plan item 4.1).
 *
 * Asset locks live in the bridge, which is per editor, and guards wrap the
 * bridge, which is per editor. Both were process-level: one lock owner id for
 * the whole server, and one guard pipeline every session shared. The first
 * makes a lock taken in one editor read as re-entrant in another; the second
 * lets a guard declared by one project's plugins veto another project's calls.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { FakeBridge } from "../fake-bridge.js";
import { SessionRegistry, type EditorSession } from "../../src/session.js";
import { withAssetLocks, SESSION_ID } from "../../src/locking.js";
import { assetTool } from "../../src/tools/asset.js";
import type { ToolContext } from "../../src/types.js";

let root: string;
let alpha: FakeBridge;
let beta: FakeBridge;
let sessions: SessionRegistry;
let a: EditorSession;
let b: EditorSession;

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

beforeEach(async () => {
  root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-isolation-"));
  const alphaProject = makeProject("Alpha");
  const betaProject = makeProject("Beta");
  alpha = await FakeBridge.start({ projectDir: path.dirname(alphaProject) });
  beta = await FakeBridge.start({ projectDir: path.dirname(betaProject) });

  sessions = new SessionRegistry();
  a = sessions.register({ projectPath: alphaProject });
  b = sessions.register({ projectPath: betaProject });
  await a.bridge.connect(5000);
  await b.bridge.connect(5000);
});

afterEach(async () => {
  a.bridge.disconnect();
  b.bridge.disconnect();
  await alpha.stop();
  await beta.stop();
  fs.rmSync(root, { recursive: true, force: true });
});

describe("lock ownership", () => {
  it("gives each editor its own owner id, distinct from the process id", () => {
    expect(a.lockOwnerId).not.toBe(b.lockOwnerId);
    expect(a.lockOwnerId).not.toBe(SESSION_ID);
  });

  it("acquires and releases under the addressed editor's id", async () => {
    await withAssetLocks(
      a.bridge,
      { enabled: true, ttlSeconds: 60 },
      "asset.create_data_asset",
      { assetPath: "/Game/Thing" },
      async () => "done",
      a.lockOwnerId,
    );

    const acquire = alpha.calls.find((c) => c.method === "acquire_lock");
    const release = alpha.calls.find((c) => c.method === "release_lock");
    expect(acquire?.params.sessionId).toBe(a.lockOwnerId);
    expect(release?.params.sessionId).toBe(a.lockOwnerId);
    // The other editor was not involved at all.
    expect(beta.calls).toEqual([]);
  });

  it("sends the addressed editor's id from the explicit lock actions too", async () => {
    await assetTool.actions.lock.handler!(ctxFor(a), { action: "lock", assetPath: "/Game/Thing" });
    await assetTool.actions.unlock.handler!(ctxFor(b), { action: "unlock", assetPath: "/Game/Thing" });
    await assetTool.actions.unlock_all.handler!(ctxFor(b), { action: "unlock_all" });

    expect(alpha.calls.find((c) => c.method === "acquire_lock")!.params.sessionId).toBe(a.lockOwnerId);
    expect(beta.calls.find((c) => c.method === "release_lock")!.params.sessionId).toBe(b.lockOwnerId);
    expect(beta.calls.find((c) => c.method === "release_session_locks")!.params.sessionId).toBe(b.lockOwnerId);
  });

  it("round-trips: what unlock releases is what lock acquired", async () => {
    await assetTool.actions.lock.handler!(ctxFor(a), { action: "lock", assetPath: "/Game/Thing" });
    await assetTool.actions.unlock.handler!(ctxFor(a), { action: "unlock", assetPath: "/Game/Thing" });

    const acquired = alpha.calls.find((c) => c.method === "acquire_lock")!.params.sessionId;
    const released = alpha.calls.find((c) => c.method === "release_lock")!.params.sessionId;
    expect(released).toBe(acquired);
  });

  it("still honours an explicitly passed sessionId, for clearing a crashed one", async () => {
    await assetTool.actions.unlock_all.handler!(ctxFor(a), {
      action: "unlock_all",
      sessionId: "some-crashed-session",
    });
    expect(alpha.calls.at(-1)!.params.sessionId).toBe("some-crashed-session");
  });
});

describe("guard pipelines", () => {
  it("gives each editor its own registry", () => {
    expect(a.guards).not.toBe(b.guards);
  });

  it("keeps one editor's guard off the other editor's calls", async () => {
    a.guards.register({
      name: "alpha-veto",
      before: async () => {
        throw new Error("vetoed by alpha's guard");
      },
    });

    await expect(a.guarded.call("place_actor", {})).rejects.toThrow(/vetoed by alpha's guard/);
    await expect(b.guarded.call("place_actor", {})).resolves.toBeDefined();
    expect(beta.methods).toEqual(["place_actor"]);
    expect(alpha.methods).toEqual([]);
  });
});
