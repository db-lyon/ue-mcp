// @live-editor scm
/**
 * Source control category with a provider configured (#1357), against the test
 * project's editor.
 *
 * Every action here goes through the editor's own provider, and every
 * assertion reads the effect back: the provider's state, the file on disk, or
 * the repository history. It submits to whatever repository the test project
 * is under, so it runs only when UE_MCP_LIVE_SCM=1 says that repository is a
 * throwaway one. tests/live/source-control.test.ts covers the same actions
 * with no provider.
 *
 * The sync case needs a change upstream of the workspace. Staging one is the
 * only provider-specific step, and it is done here, in the test, with the
 * provider's own command line; where no stager applies the case skips and
 * says why.
 */
import { execFileSync } from "node:child_process";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import { liveTarget } from "./harness.js";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const norm = (p: string): string => path.resolve(p).replace(/\\/g, "/").toLowerCase();

const OPTED_IN = process.env.UE_MCP_LIVE_SCM === "1";
const PROBE_REL = "ScmProbe/probe.txt";

let bridge: EditorBridge;
let probe = "";
let provider: Json = {};
let ready = false;
let skipReason = "";

beforeAll(async () => {
  if (!OPTED_IN) {
    skipReason = "UE_MCP_LIVE_SCM=1 is not set; this suite submits to the test project's repository";
    return;
  }
  const target = await liveTarget();
  bridge = await getBridge();
  const answered = await callBridge(bridge, "source_control_get_provider", {});
  expect(answered.ok, answered.error).toBe(true);
  provider = body(answered);
  if (provider.enabled !== true) {
    skipReason = "no source control provider is enabled in this editor";
    return;
  }
  probe = path.join(target.projectDir, PROBE_REL);
  fs.mkdirSync(path.dirname(probe), { recursive: true });
  fs.writeFileSync(probe, `probe created ${new Date().toISOString()}\n`);
  ready = true;
}, 120_000);

afterAll(async () => {
  if (bridge) disconnectBridge();
});

async function call(method: string, params: Json): Promise<Json> {
  const answered = await callBridge(bridge, method, params);
  expect(answered.ok, answered.error).toBe(true);
  return body(answered);
}

async function probeState(): Promise<Json> {
  const status = await call("source_control_get_status", { paths: [PROBE_REL] });
  expect(status.success, String(status.error)).toBe(true);
  const files = status.files as Json[];
  expect(files).toHaveLength(1);
  return files[0];
}

const includesProbe = (list: unknown): boolean =>
  Array.isArray(list) && list.some((f) => typeof f === "string" && norm(f) === norm(probe));

/** Commit a change to the probe upstream of the workspace, where the provider's repository allows it. */
function stageUpstreamChange(content: string): string | null {
  const info = (provider.info ?? {}) as Json;
  const remote = typeof info.remote === "string" ? info.remote : "";
  const repository = typeof info.repository === "string" ? info.repository : "";
  if (!remote || !repository || !fs.existsSync(path.join(repository, ".git"))) return null;
  // A submit on a distributed provider commits locally and pushes nothing, so the probe the earlier
  // cases recorded reaches the remote first; the upstream edit is then a plain fast-forward to sync.
  execFileSync("git", ["push", "-q", "origin", "HEAD"], { cwd: repository, stdio: "pipe" });
  const clone = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-scm-upstream-"));
  const git = (...args: string[]) => execFileSync("git", args, { cwd: clone, stdio: "pipe" }).toString();
  execFileSync("git", ["clone", "-q", remote, clone], { stdio: "pipe" });
  const relInRepo = path.relative(repository, probe);
  fs.mkdirSync(path.dirname(path.join(clone, relInRepo)), { recursive: true });
  fs.writeFileSync(path.join(clone, relInRepo), content);
  git("add", relInRepo);
  git("-c", "user.name=ue-mcp-live", "-c", "user.email=live@ue-mcp.invalid", "commit", "-q", "-m", "upstream probe change");
  git("push", "-q", "origin", "HEAD");
  fs.rmSync(clone, { recursive: true, force: true });
  return content;
}

describe("source control with a provider (#1357)", () => {
  it("get_provider reports the operations the provider can run", (ctx) => {
    if (!ready) ctx.skip(skipReason);
    expect(provider.success).toBe(true);
    const operations = provider.operations as Json;
    for (const action of ["checkout", "add", "mark_for_delete", "revert", "sync", "submit", "history", "connect"]) {
      expect(typeof operations[action], action).toBe("boolean");
    }
    // Every provider the editor ships reads status, which history runs on.
    expect(operations.history).toBe(true);
  });

  it("connect leaves the provider connected and never echoes the password", async (ctx) => {
    if (!ready) ctx.skip(skipReason);
    const connected = await call("source_control_connect", { password: "not-a-real-secret" });
    expect(connected.success, String(connected.error)).toBe(true);
    expect(connected.connected).toBe(true);
    expect(connected.passwordSent).toBe(true);
    expect(JSON.stringify(connected)).not.toContain("not-a-real-secret");
    const after = await call("source_control_get_provider", {});
    expect(after.connected).toBe(true);
  });

  it("history lists the revision a submit just recorded", async (ctx) => {
    if (!ready) ctx.skip(skipReason);
    const added = await call("source_control_add", { paths: [PROBE_REL] });
    expect(added.success, String(added.error)).toBe(true);
    const description = `ue-mcp live history probe ${Date.now()}`;
    const submitted = await call("source_control_submit", { paths: [PROBE_REL], description });
    expect(submitted.success, String(submitted.error)).toBe(true);
    expect(includesProbe(submitted.submitted)).toBe(true);

    const history = await call("source_control_history", { path: PROBE_REL, limit: 5 });
    expect(history.success, String(history.error)).toBe(true);
    const revisions = history.revisions as Json[];
    expect(revisions.length).toBeGreaterThanOrEqual(1);
    expect(String(revisions[0].description)).toContain(description);
    expect(history.total as number).toBeGreaterThanOrEqual(revisions.length);
  });

  it("mark_for_delete marks the file deleted, and its rollback restores it", async (ctx) => {
    if (!ready) ctx.skip(skipReason);
    const before = fs.readFileSync(probe, "utf8");
    const marked = await call("source_control_mark_for_delete", { paths: [PROBE_REL] });
    expect(marked.success, String(marked.error)).toBe(true);
    expect(includesProbe(marked.markedForDelete)).toBe(true);
    expect((await probeState()).deleted).toBe(true);

    const rollback = marked.rollback as { method: string; payload: Json };
    expect(rollback.method).toBe("source_control_revert");
    const reverted = await call(rollback.method, rollback.payload);
    expect(reverted.success, String(reverted.error)).toBe(true);
    expect((await probeState()).deleted).toBe(false);
    // Restored from the repository, so line endings follow its checkout settings.
    expect(fs.readFileSync(probe, "utf8").replace(/\r\n/g, "\n")).toBe(before.replace(/\r\n/g, "\n"));
  });

  it("sync brings a change made upstream into the workspace file", async (ctx) => {
    if (!ready) ctx.skip(skipReason);
    const content = `upstream change ${Date.now()}\n`;
    if (!stageUpstreamChange(content)) ctx.skip("no local repository and remote to stage an upstream change in");
    expect(fs.readFileSync(probe, "utf8")).not.toBe(content);

    const synced = await call("source_control_sync", { paths: [PROBE_REL] });
    expect(synced.success, String(synced.error)).toBe(true);
    expect(synced.synced).toBe(true);
    expect(includesProbe(synced.changedFiles)).toBe(true);
    expect(fs.readFileSync(probe, "utf8").replace(/\r\n/g, "\n")).toBe(content);

    const history = await call("source_control_history", { path: PROBE_REL, limit: 1 });
    expect(String((history.revisions as Json[])[0].description)).toContain("upstream probe change");
  });
});
