/**
 * One test process driving two bridges (#817, plan item 7.1).
 *
 * The plan's blocker was the module-global bridge in tests/setup.ts, shared by
 * every smoke file, which made "two editors" unexpressible in a test. Two real
 * loopback sockets remove that: each session discovers its own bridge through
 * its own project's lockfile, and every assertion below is about which of the
 * two actually received a call.
 *
 * No engine is involved, which is deliberate: CI runners have no Unreal
 * install, so a tier that needs one cannot gate merges. Everything that a
 * second engine would add is behind the handler; everything routing depends on
 * (port discovery, socket identity, per-session dispatch) is real here.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { FakeBridge } from "../fake-bridge.js";
import { SessionRegistry } from "../../src/session.js";
import { getBridgeFor, resetTestBridges } from "../setup.js";

let root: string;
let alpha: FakeBridge;
let beta: FakeBridge;
let alphaDir: string;
let betaDir: string;

function makeProject(name: string): { dir: string; uproject: string } {
  const dir = path.join(root, name);
  fs.mkdirSync(dir, { recursive: true });
  const uproject = path.join(dir, `${name}.uproject`);
  fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, EngineAssociation: "5.6" }), "utf-8");
  return { dir, uproject };
}

beforeEach(async () => {
  root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-two-bridge-"));
  alphaDir = makeProject("Alpha").dir;
  betaDir = makeProject("Beta").dir;
  alpha = await FakeBridge.start({ projectDir: alphaDir });
  beta = await FakeBridge.start({ projectDir: betaDir });
});

afterEach(async () => {
  resetTestBridges();
  await alpha.stop();
  await beta.stop();
  fs.rmSync(root, { recursive: true, force: true });
});

describe("two bridges in one process", () => {
  it("binds two distinct ports and publishes a lockfile each", () => {
    expect(alpha.port).not.toBe(beta.port);
    for (const dir of [alphaDir, betaDir]) {
      const lockfile = path.join(dir, "Saved", "UE_MCP_Bridge", "port.json");
      expect(fs.existsSync(lockfile)).toBe(true);
    }
  });

  it("routes each session's calls to its own editor and to no other", async () => {
    const sessions = new SessionRegistry();
    const a = sessions.register({ projectPath: path.join(alphaDir, "Alpha.uproject") });
    const b = sessions.register({ projectPath: path.join(betaDir, "Beta.uproject") });

    await a.bridge.connect(5000);
    await b.bridge.connect(5000);

    await a.guarded.call("alpha_only_call", { marker: "a" });
    await b.guarded.call("beta_only_call", { marker: "b" });

    expect(alpha.methods).toEqual(["alpha_only_call"]);
    expect(beta.methods).toEqual(["beta_only_call"]);

    a.bridge.disconnect();
    b.bridge.disconnect();
  });

  it("resolves a target by name and reaches that editor's socket", async () => {
    const sessions = new SessionRegistry();
    sessions.register({ projectPath: path.join(alphaDir, "Alpha.uproject") });
    sessions.register({ projectPath: path.join(betaDir, "Beta.uproject") });

    for (const s of sessions.list()) await s.bridge.connect(5000);

    // The routing decision the MCP dispatch layer makes, made here directly.
    const target = sessions.resolve("Beta");
    await target.guarded.call("place_actor", {});

    expect(beta.methods).toEqual(["place_actor"]);
    expect(alpha.methods).toEqual([]);

    for (const s of sessions.list()) s.bridge.disconnect();
  });

  it("keeps the untargeted default on the active session and moves it with use_editor", async () => {
    const sessions = new SessionRegistry();
    sessions.register({ projectPath: path.join(alphaDir, "Alpha.uproject") });
    sessions.register({ projectPath: path.join(betaDir, "Beta.uproject") });
    for (const s of sessions.list()) await s.bridge.connect(5000);

    await sessions.resolve(undefined).guarded.call("first", {});
    expect(alpha.methods).toEqual(["first"]);

    sessions.use("Beta");
    await sessions.resolve(undefined).guarded.call("second", {});
    expect(beta.methods).toEqual(["second"]);
    expect(alpha.methods).toEqual(["first"]);

    for (const s of sessions.list()) s.bridge.disconnect();
  });

  it("leaves one editor answering when the other one goes away", async () => {
    const sessions = new SessionRegistry();
    const a = sessions.register({ projectPath: path.join(alphaDir, "Alpha.uproject") });
    const b = sessions.register({ projectPath: path.join(betaDir, "Beta.uproject") });
    await a.bridge.connect(5000);
    await b.bridge.connect(5000);

    await beta.stop();

    const result = await a.guarded.call("still_here", {});
    expect((result as { servedBy?: string }).servedBy).toBe(alphaDir);

    a.bridge.disconnect();
    b.bridge.disconnect();
  });
});

describe("the smoke harness accessor", () => {
  it("hands out one bridge per project instead of one per process", async () => {
    const one = await getBridgeFor({ projectDir: alphaDir, verifyTarget: false });
    const two = await getBridgeFor({ projectDir: betaDir, verifyTarget: false });

    expect(one).not.toBe(two);
    expect(one.port).toBe(alpha.port);
    expect(two.port).toBe(beta.port);

    await one.call("from_one", {});
    await two.call("from_two", {});
    expect(alpha.methods).toEqual(["from_one"]);
    expect(beta.methods).toEqual(["from_two"]);
  });

  it("reuses the connection for a project it already reached", async () => {
    const first = await getBridgeFor({ projectDir: alphaDir, verifyTarget: false });
    const again = await getBridgeFor({ projectDir: alphaDir, verifyTarget: false });
    expect(again).toBe(first);
  });
});
