/**
 * A source bridge update builds the bridge (#1593), driven through the real CLI
 * and a spawned daemon. The engine is a fake whose build script records its
 * arguments and exits with the code in a file beside it, so one daemon sees a
 * build fail and then pass. No editor is launched: every start here is refused
 * by a failed build before anything is spawned.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { PACKAGE_VERSION, plain, runCli, Sandbox, stopSandboxDaemons } from "./harness.js";
import { call, startDaemonCli, type Discovery } from "./daemon-fakes.js";

const IS_WINDOWS = process.platform === "win32";

let sb: Sandbox;
let fakeNpm: string;

beforeAll(() => {
  sb = new Sandbox();
  // Answers the version query update makes with this package's own version, so no install runs.
  fakeNpm = sb.path("fake-npm-script.mjs");
  fs.writeFileSync(fakeNpm, `export default function (args) {
  if (args.join(" ") === "view ue-mcp version") process.stdout.write(${JSON.stringify(PACKAGE_VERSION)} + "\\n");
  return 0;
}
`);
});

afterAll(async () => {
  await stopSandboxDaemons(sb);
  sb.cleanup();
});

interface FakeEngine {
  root: string;
  log: string;
  /** The exit code the next build returns. */
  exitWith(code: number): void;
  calls(): string[];
}

/** An engine root whose build script logs each call and exits with the code in exit-code.txt. */
function fakeEngine(name: string): FakeEngine {
  const root = sb.path(name);
  const log = path.join(root, "build-calls.log");
  const codeFile = path.join(root, "exit-code.txt");
  const batch = path.join(root, "Engine", "Build", "BatchFiles");
  if (IS_WINDOWS) {
    fs.mkdirSync(batch, { recursive: true });
    fs.writeFileSync(path.join(batch, "Build.bat"), `@echo off\r\n>>"${log}" echo %*\r\nset /p CODE=<"${codeFile}"\r\nexit /b %CODE%\r\n`);
  } else {
    const dir = path.join(batch, process.platform === "darwin" ? "Mac" : "Linux");
    fs.mkdirSync(dir, { recursive: true });
    const script = path.join(dir, "Build.sh");
    fs.writeFileSync(script, `#!/bin/sh\necho "$@" >> "${log}"\nexit $(cat "${codeFile}")\n`);
    fs.chmodSync(script, 0o755);
  }
  fs.writeFileSync(codeFile, "0");
  return {
    root,
    log,
    exitWith: (code) => fs.writeFileSync(codeFile, String(code)),
    calls: () => (fs.existsSync(log) ? fs.readFileSync(log, "utf8").split(/\r?\n/).filter(Boolean) : []),
  };
}

function pendingBuildFile(uproject: string): string {
  return path.join(path.dirname(uproject), "Plugins", "UE_MCP_Bridge", "Intermediate", "ue-mcp-build-pending.json");
}

async function deployed(name: string): Promise<string> {
  const uproject = sb.makeProject(name);
  const r = await runCli(sb, ["deploy", uproject]);
  expect(r.code, r.stdout + r.stderr).toBe(0);
  return uproject;
}

describe("update --apply-only on a source bridge with the editor down", () => {
  it("builds the bridge before reporting success", async () => {
    covers("cli.update.apply-only");
    const uproject = await deployed("SrcBuilds");
    const engine = fakeEngine("SrcBuildsEngine");

    const r = await runCli(sb, ["update", uproject, "--apply-only"], { env: { UE_MCP_TEST_ENGINE_ROOT: engine.root } });

    expect(r.code, r.stdout + r.stderr).toBe(0);
    expect(plain(r.stdout)).toContain(`Bridge ${PACKAGE_VERSION} source deployed and built`);
    expect(engine.calls()).toHaveLength(1);
    expect(engine.calls()[0]).toMatch(/^SrcBuildsEditor /);
    expect(engine.calls()[0]).toContain("-NoHotReloadFromIDE");
    expect(fs.existsSync(pendingBuildFile(uproject))).toBe(false);
  });

  it("reports a failed build and exits non-zero, and a second run finishes it", async () => {
    covers("cli.update.apply-only");
    const uproject = await deployed("SrcFails");
    const engine = fakeEngine("SrcFailsEngine");
    engine.exitWith(6);

    const failed = await runCli(sb, ["update", uproject, "--apply-only"], { env: { UE_MCP_TEST_ENGINE_ROOT: engine.root } });

    expect(failed.code).toBe(1);
    expect(plain(failed.stdout + failed.stderr)).toContain(`the bridge ${PACKAGE_VERSION} source is deployed, but building it failed`);
    expect(plain(failed.stdout + failed.stderr)).toContain("exit code 6");
    expect(JSON.parse(fs.readFileSync(pendingBuildFile(uproject), "utf8"))).toMatchObject({ version: PACKAGE_VERSION });

    engine.exitWith(0);
    const again = await runCli(sb, ["update", uproject, "--apply-only"], { env: { UE_MCP_TEST_ENGINE_ROOT: engine.root } });
    expect(again.code, again.stdout + again.stderr).toBe(0);
    expect(plain(again.stdout)).toContain("source deployed and built");
    expect(engine.calls()).toHaveLength(2);
    expect(fs.existsSync(pendingBuildFile(uproject))).toBe(false);
  });
});

describe("the daemon and a source bridge that owes a build", () => {
  let uproject: string;
  let engine: FakeEngine;
  let d: Discovery;

  beforeAll(async () => {
    uproject = await deployed("SrcDaemon");
    engine = fakeEngine("SrcDaemonEngine");
    engine.exitWith(3);
    // An update that deployed and did not build, as one with the editor running does.
    const deferred = await runCli(sb, ["update", uproject, "--no-build"], { env: { UE_MCP_TEST_ENGINE_ROOT: engine.root, NPM_FAKE_SCRIPT: fakeNpm } });
    expect(deferred.code, deferred.stdout + deferred.stderr).toBe(0);
    expect(plain(deferred.stdout)).toContain("start the editor through ue-mcp, which builds first");
    expect(engine.calls()).toHaveLength(0);
    d = await startDaemonCli(sb, uproject, {
      UE_MCP_TEST_ENGINE_ROOT: engine.root,
      // Named so the start reaches the build; the failed build refuses before anything is spawned.
      UE_EDITOR_PATH: sb.path("SrcDaemonEngine", "no-editor-here.exe"),
    });
  });

  it("names the owed build in /v1/install and offers rebuild in /v1/health", async () => {
    covers("daemon.editor-rebuild", "cli.update");
    const health = await call(d, "GET", "/v1/health");
    expect(health.body.features).toEqual(expect.arrayContaining(["install.pendingBuild", "editor.rebuild"]));

    const install = await call(d, "GET", "/v1/install");
    expect(install.status).toBe(200);
    expect(install.body.plugin.pendingBuild).toMatchObject({ version: PACKAGE_VERSION, reason: "--no-build" });
    // Never compiled here; a stale binary reports bridge_build_stale instead.
    expect(install.body.problems.map((p: { code: string }) => p.code)).toContain("bridge_never_compiled");
  });

  it("builds before a start, and refuses the launch on a failed build with its reason", async () => {
    covers("daemon.editor-rebuild");
    const started = await call(d, "POST", "/v1/editor/start");
    expect(started.status, started.text).toBe(200);
    const [step] = started.body.steps;
    expect(step).toMatchObject({ step: "editor.start_editor", ok: false });
    expect(step.reason).toContain("Not launching");
    expect(step.reason).toContain("building it failed");
    expect(engine.calls()).toHaveLength(1);
    expect(JSON.parse(fs.readFileSync(pendingBuildFile(uproject), "utf8")).failed.message).toContain("exit code 3");
  });

  it("rebuild stops, builds, names a failed build's reason and starts nothing", async () => {
    covers("daemon.editor-rebuild");
    const rebuilt = await call(d, "POST", "/v1/editor/rebuild");
    expect(rebuilt.status, rebuilt.text).toBe(200);
    const steps = rebuilt.body.steps as Array<{ step: string; ok: boolean; reason?: string }>;
    expect(steps.map((s) => s.step)).toEqual(["editor.stop_editor", "project.build"]);
    expect(steps[0].ok).toBe(true);
    expect(steps[1].ok).toBe(false);
    expect(steps[1].reason).toContain("exit code 3");
    expect(engine.calls()).toHaveLength(2);
    expect(fs.existsSync(pendingBuildFile(uproject))).toBe(true);
  });

  it("applying the update again with the editor down builds it, and the owed build is gone", async () => {
    covers("daemon.update", "daemon.editor-rebuild");
    engine.exitWith(0);
    const applied = await call(d, "POST", "/v1/update/apply", {});
    expect(applied.status, applied.text).toBe(200);
    expect(applied.body).toMatchObject({ ok: true, kind: "source", state: "built", build: { success: true } });
    expect(engine.calls()).toHaveLength(3);
    expect(fs.existsSync(pendingBuildFile(uproject))).toBe(false);
    const install = await call(d, "GET", "/v1/install");
    expect(install.body.plugin.pendingBuild).toBeNull();
  });
});
