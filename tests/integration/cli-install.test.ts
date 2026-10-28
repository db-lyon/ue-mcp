/**
 * The install path, spawned: init, deploy, update, build, doctor and
 * uninstall-hooks against sandbox projects, with npm and the engine's build
 * script replaced by recording fakes.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { writeBridgeTree } from "../helpers/bridge-tree-fixture.js";
import { PACKAGE_VERSION, plain, REPO_ROOT, runCli, Sandbox, snapshotTree, type CliOptions, type CliResult } from "./harness.js";

const IS_WINDOWS = process.platform === "win32";
const PLATFORM = IS_WINDOWS ? "Win64" : process.platform === "darwin" ? "Mac" : "Linux";
const HOOK_MATCHER = "mcp__ue-mcp__editor";
const ALL_CLIENTS = "claude-code,claude-code-global,claude-desktop,cursor,codex";

let sb: Sandbox;
let fakeNpm: string;

beforeAll(() => {
  sb = new Sandbox("ue-mcp-install-");
  // Answers the two npm queries update and doctor make; every other call just succeeds.
  fakeNpm = sb.path("fake-npm-script.mjs");
  fs.writeFileSync(
    fakeNpm,
    `export default function (args) {
  const line = args.join(" ");
  if (line === "view ue-mcp version") process.stdout.write((process.env.FAKE_NPM_LATEST ?? "") + "\\n");
  if (line === "root -g") process.stdout.write((process.env.FAKE_NPM_ROOT ?? "") + "\\n");
  return 0;
}
`,
  );
});
afterAll(() => sb.cleanup());

function cli(args: string[], opts: CliOptions = {}): Promise<CliResult> {
  return runCli(sb, args, {
    ...opts,
    // A missing explicit engine pin fails outright, so no case can reach an installed engine.
    env: { UE_MCP_TEST_ENGINE_ROOT: sb.path("no-engine"), NPM_FAKE_SCRIPT: fakeNpm, FAKE_NPM_LATEST: PACKAGE_VERSION, FAKE_NPM_ROOT: sb.path("npm-global"), ...opts.env },
  });
}

const readJson = (file: string) => JSON.parse(fs.readFileSync(file, "utf8"));
const pluginDir = (uproject: string) => path.join(path.dirname(uproject), "Plugins", "UE_MCP_Bridge");
const marker = (uproject: string) => readJson(path.join(pluginDir(uproject), ".ue-mcp-install.json"));
const enabledPlugins = (uproject: string): string[] =>
  (readJson(uproject).Plugins ?? []).filter((p: { Enabled?: boolean }) => p.Enabled).map((p: { Name: string }) => p.Name);
const ueMcpEntry = (configPath: string) => readJson(configPath).mcpServers?.["ue-mcp"];
const state = () => readJson(path.join(sb.home, ".ue-mcp", "state.json"));

function hookRecorded(settingsPath: string): boolean {
  if (!fs.existsSync(path.join(sb.home, ".ue-mcp", "state.json"))) return false;
  const projects = (state().projects ?? {}) as Record<string, { installedHooks?: string[] }>;
  return Object.values(projects).some((p) => (p.installedHooks ?? []).some((h) => path.resolve(h) === path.resolve(settingsPath)));
}

function hasHookMatcher(settingsPath: string): boolean {
  if (!fs.existsSync(settingsPath)) return false;
  const post = (readJson(settingsPath).hooks?.PostToolUse ?? []) as Array<{ matcher: string }>;
  return post.some((h) => h.matcher === HOOK_MATCHER);
}

/** A prebuilt plugin tree as a release ships it, built for this platform. */
function prebuilt(dir: string, dll: string): string {
  writeBridgeTree(path.join(dir, "UE_MCP_Bridge"), PACKAGE_VERSION, PLATFORM, dll);
  return dir;
}

/** An engine root whose build script records its arguments and exits with FAKE_BUILD_EXIT. */
function fakeEngine(name: string): { root: string; log: string } {
  const root = sb.path(name);
  const log = path.join(root, "build-calls.log");
  const batch = path.join(root, "Engine", "Build", "BatchFiles");
  if (IS_WINDOWS) {
    fs.mkdirSync(batch, { recursive: true });
    // Redirect first: a trailing digit in the arguments would otherwise name a handle.
    fs.writeFileSync(path.join(batch, "Build.bat"), `@echo off\r\n>>"${log}" echo %*\r\nexit /b %FAKE_BUILD_EXIT%\r\n`);
  } else {
    const dir = path.join(batch, process.platform === "darwin" ? "Mac" : "Linux");
    fs.mkdirSync(dir, { recursive: true });
    const script = path.join(dir, "Build.sh");
    fs.writeFileSync(script, `#!/bin/sh\necho "$@" >> "${log}"\nexit \${FAKE_BUILD_EXIT:-0}\n`);
    fs.chmodSync(script, 0o755);
  }
  return { root, log };
}

describe("init --yes", () => {
  let uproject: string;
  let first: CliResult;
  const initArgs = () => [
    "init", uproject, "--yes", "--install", "source", "--clients", ALL_CLIENTS,
    "--feedback", "--hook", "--skills", "--context", "lean",
  ];

  beforeAll(async () => {
    uproject = sb.makeProject("Fresh");
    first = await cli(initArgs());
  });

  it("deploys the bridge, enables it, and writes config, client entries, the hook and skills", () => {
    covers("cli.init.yes");
    expect(first.code, first.stdout + first.stderr).toBe(0);
    const dir = path.dirname(uproject);

    expect(fs.existsSync(path.join(pluginDir(uproject), "UE_MCP_Bridge.uplugin"))).toBe(true);
    expect(fs.existsSync(path.join(pluginDir(uproject), "Source"))).toBe(true);
    expect(marker(uproject)).toMatchObject({ kind: "source", version: PACKAGE_VERSION, engine: "5.6", platform: PLATFORM });
    expect(enabledPlugins(uproject)).toEqual(expect.arrayContaining(["UE_MCP_Bridge", "PythonScriptPlugin"]));

    const yml = fs.readFileSync(path.join(dir, "ue-mcp.yml"), "utf8");
    expect(yml).toMatch(/strategy: lean/);
    expect(yml).not.toMatch(/- feedback/);
    expect(fs.readFileSync(path.join(dir, ".gitignore"), "utf8").split(/\r?\n/)).toContain("ue-mcp.local.yml");

    const serves = uproject.replace(/\\/g, "/");
    for (const config of [
      path.join(dir, ".mcp.json"),
      path.join(sb.home, ".claude", ".mcp.json"),
      path.join(sb.home, "AppData", "Roaming", "Claude", "claude_desktop_config.json"),
      path.join(dir, ".cursor", "mcp.json"),
    ]) {
      const entry = ueMcpEntry(config);
      expect(entry, config).toBeDefined();
      // The machine path pins the exact version so server and bridge move together.
      expect(entry.args).toEqual(["-y", "--min-release-age=0", `ue-mcp@${PACKAGE_VERSION}`, serves]);
    }
    const codex = fs.readFileSync(path.join(sb.home, ".codex", "config.toml"), "utf8");
    expect(codex).toContain("[mcp_servers.ue-mcp]");
    expect(codex).toContain(`ue-mcp@${PACKAGE_VERSION}`);

    const settings = path.join(dir, ".claude", "settings.json");
    expect(hasHookMatcher(settings)).toBe(true);
    expect(hookRecorded(settings)).toBe(true);
    const skills = fs.readdirSync(path.join(dir, ".claude", "skills"));
    expect(skills).toContain("ue-mcp-workflow");
    expect(fs.existsSync(path.join(dir, ".claude", "skills", "ue-mcp-workflow", "SKILL.md"))).toBe(true);
  });

  it("is idempotent: a second run with the same flags changes no file", async () => {
    covers("cli.init.rerun");
    const dir = path.dirname(uproject);
    const before = { project: snapshotTree(dir), home: snapshotTree(sb.home) };
    const again = await cli([...initArgs(), "--json"]);
    expect(again.code, again.stderr).toBe(0);
    const result = JSON.parse(again.stdout);
    expect(result.ok).toBe(true);
    expect(result.plan.isReInit).toBe(true);
    const byId = Object.fromEntries((result.steps as Array<{ id: string; status: string }>).map((s) => [s.id, s.status]));
    expect(byId).toMatchObject({ plugin: "unchanged", uproject: "unchanged", config: "unchanged", "client:cursor": "unchanged", "client:codex": "unchanged" });
    expect(byId.gitignore).toBeUndefined();
    expect(snapshotTree(dir)).toEqual(before.project);
    expect(snapshotTree(sb.home)).toEqual(before.home);
  });
});

describe("init --json", () => {
  it("prints exactly one JSON result on stdout, success or failure", async () => {
    covers("cli.init.json");
    const uproject = sb.makeProject("JsonGame");
    const empty = sb.path("no-project-here");
    fs.mkdirSync(empty, { recursive: true });
    const [good, missing, badFlag] = await Promise.all([
      cli(["init", uproject, "--json", "--install", "source", "--clients", "cursor", "--context", "full"]),
      cli(["init", "--json"], { cwd: empty }),
      cli(["init", uproject, "--json", "--install", "teleport"]),
    ]);

    expect(good.code, good.stderr).toBe(0);
    const result = JSON.parse(good.stdout);
    expect(result.ok).toBe(true);
    expect(result.plan.project.name).toBe("JsonGame");
    expect(result.plan.contextStrategy).toBe("full");
    expect(result.steps.map((s: { id: string }) => s.id)).toEqual(expect.arrayContaining(["plugin", "uproject", "client:cursor", "config"]));
    expect(ueMcpEntry(path.join(path.dirname(uproject), ".cursor", "mcp.json"))).toBeDefined();

    expect(missing.code).toBe(2);
    expect(JSON.parse(missing.stdout)).toEqual({ ok: false, error: expect.stringContaining("No .uproject found") });
    expect(badFlag.code).toBe(2);
    expect(JSON.parse(badFlag.stdout)).toEqual({ ok: false, error: "--install must be auto, source or binary, not 'teleport'" });
  });
});

describe("init --dry-run", () => {
  it("prints the plan and writes nothing, and a blocked plan exits 1", async () => {
    covers("cli.init.dry-run");
    const uproject = sb.makeProject("DryGame");
    const dir = path.dirname(uproject);
    const before = { project: snapshotTree(dir), home: snapshotTree(sb.home) };
    const [planned, blocked] = await Promise.all([
      cli(["init", uproject, "--yes", "--dry-run", "--install", "source", "--clients", "cursor", "--disable", "niagara"]),
      cli(["init", uproject, "--yes", "--dry-run", "--install", "source", "--clients", "vim"]),
    ]);

    expect(planned.code, planned.stderr).toBe(0);
    const plan = JSON.parse(planned.stdout);
    expect(plan.install).toMatchObject({ kind: "source", existing: null, writes: true });
    expect(plan.disable).toEqual(["niagara"]);
    expect(plan.clients.find((c: { id: string }) => c.id === "cursor").selected).toBe(true);
    expect(plan.blockers).toEqual([]);
    // What a setup page needs to offer every choice the interactive wizard does.
    expect(plan.categories.map((c: { name: string }) => c.name)).toEqual(
      ["level", "blueprint", "material", "asset", "animation", "niagara", "landscape", "pcg", "foliage", "audio", "widget", "gameplay", "gas", "networking", "demo"],
    );
    expect(plan.categories.find((c: { name: string }) => c.name === "niagara")).toEqual(
      { name: "niagara", label: "vfx (niagara)", description: "VFX systems, emitters, modules", requiredPlugins: ["Niagara"] },
    );
    expect(plan.nativeCategories.length).toBeGreaterThan(0);
    expect(plan.bridge).toEqual({ port: expect.any(Number), derivedPort: plan.bridge.port, pinnedBy: null });
    expect(plan.feedbackAuth).toEqual({ login: null });

    expect(blocked.code).toBe(1);
    expect(JSON.parse(blocked.stdout).blockers).toEqual([expect.stringContaining("unknown client 'vim'")]);

    expect(snapshotTree(dir)).toEqual(before.project);
    expect(snapshotTree(sb.home)).toEqual(before.home);
  });
});

describe("init --feedback-mode", () => {
  it("stores the project's feedback mode as feedback mode --editor does, and the plan reports it", async () => {
    covers("cli.init.feedback-mode");
    const uproject = sb.makeProject("ModeGame");
    const dryRun = (extra: string[] = []) => cli(["init", uproject, "--json", "--dry-run", "--install", "source", "--clients", "cursor", ...extra]);

    const fresh = await dryRun();
    expect(fresh.code, fresh.stderr).toBe(0);
    expect(JSON.parse(fresh.stdout).plan.feedbackMode).toEqual({ mode: "interactive", scope: "default", set: null });
    const previewed = JSON.parse((await dryRun(["--feedback-mode", "defer"])).stdout).plan.feedbackMode;
    expect(previewed).toEqual({ mode: "defer", scope: "project", set: "defer" });
    const stored = () => fs.existsSync(path.join(sb.home, ".ue-mcp", "state.json"))
      ? Object.values((state().projects ?? {}) as Record<string, { feedback?: { mode?: string } }>).map((p) => p.feedback?.mode).filter(Boolean)
      : [];
    expect(stored()).toEqual([]);

    const applied = await cli(["init", uproject, "--json", "--install", "source", "--clients", "cursor", "--feedback-mode", "defer"]);
    expect(applied.code, applied.stdout + applied.stderr).toBe(0);
    const result = JSON.parse(applied.stdout);
    expect(result.steps.find((s: { id: string }) => s.id === "feedback-mode")).toMatchObject({ status: "changed", detail: "feedback mode defer" });

    const shown = await runCli(sb, ["feedback", "mode", "--editor", uproject]);
    expect(shown.code, shown.stderr).toBe(0);
    expect(plain(shown.stdout)).toMatch(/effective: defer/);
    expect(plain(shown.stdout)).toContain("editor 'ModeGame': defer");
    expect(stored()).toEqual(["defer"]);
    expect(JSON.parse((await dryRun()).stdout).plan.feedbackMode).toEqual({ mode: "defer", scope: "project", set: null });

    const overridden = await cli(["init", uproject, "--json", "--dry-run", "--install", "source", "--clients", "cursor"], { env: { UE_MCP_FEEDBACK_MODE: "auto-approve" } });
    expect(JSON.parse(overridden.stdout).plan.feedbackMode).toEqual({ mode: "auto-approve", scope: "env", set: null });

    const bad = await cli(["init", uproject, "--json", "--feedback-mode", "sometimes"]);
    expect(bad.code).toBe(2);
    expect(JSON.parse(bad.stdout)).toEqual({ ok: false, error: "--feedback-mode must be interactive, auto-approve or defer, not 'sometimes'" });
  });
});

describe("init --install binary", () => {
  it("installs prebuilt binaries from a local directory with a binary marker and no source", async () => {
    covers("cli.init.binary");
    const uproject = sb.makeProject("BinGame", { engine: "5.7" });
    const binaries = prebuilt(sb.path("prebuilt-1"), "first");
    const r = await cli(["init", uproject, "--json", "--install", "binary", "--binaries", binaries, "--clients", "cursor"]);
    expect(r.code, r.stdout + r.stderr).toBe(0);
    const result = JSON.parse(r.stdout);
    expect(result.steps.find((s: { id: string }) => s.id === "plugin").detail).toContain("installed prebuilt binaries");

    const dir = pluginDir(uproject);
    expect(marker(uproject)).toEqual({ kind: "binary", engine: "5.7", platform: PLATFORM, version: PACKAGE_VERSION });
    expect(fs.readFileSync(path.join(dir, "Binaries", PLATFORM, "UnrealEditor-UE_MCP_Bridge.dll"), "utf8")).toBe("first");
    // deploy() runs after the install and must not have written source over it.
    expect(fs.existsSync(path.join(dir, "Source"))).toBe(false);
    expect(enabledPlugins(uproject)).toContain("UE_MCP_Bridge");
  });
});

// Each case owns its project and touches nothing in the home directory, so they run side by side.
describe.concurrent("project commands", () => {
  describe("deploy", () => {
    it("copies the plugin source, enables it, and reports a second run as current", async () => {
      covers("cli.deploy");
      const uproject = sb.makeProject("DeployGame");
      const first = await cli(["deploy", uproject]);
      expect(first.code, first.stderr).toBe(0);
      expect(plain(first.stdout)).toContain("Plugin sources updated");
      expect(plain(first.stdout)).toContain("Enabled UE_MCP_Bridge");
      expect(enabledPlugins(uproject)).toEqual(expect.arrayContaining(["UE_MCP_Bridge", "PythonScriptPlugin"]));
      const shipped = fs.readFileSync(path.join(REPO_ROOT, "plugin", "ue_mcp_bridge", "UE_MCP_Bridge.uplugin"), "utf8");
      expect(fs.readFileSync(path.join(pluginDir(uproject), "UE_MCP_Bridge.uplugin"), "utf8")).toBe(shipped);
      expect(marker(uproject).kind).toBe("source");

      const [second, nowhere] = await Promise.all([
        cli(["deploy"], { cwd: path.dirname(uproject) }),
        cli(["deploy"], { cwd: sb.home }),
      ]);
      expect(second.code).toBe(0);
      expect(plain(second.stdout)).toContain("Plugin already up to date");
      expect(nowhere.code).toBe(1);
      expect(plain(nowhere.stdout + nowhere.stderr)).toContain("No .uproject found");
    });
  });

  describe("update --apply-only", () => {
    it("redeploys a source install from this package", async () => {
      covers("cli.update.apply-only");
      const uproject = sb.makeProject("ApplySource");
      expect((await cli(["deploy", uproject])).code).toBe(0);
      const uplugin = path.join(pluginDir(uproject), "UE_MCP_Bridge.uplugin");
      fs.writeFileSync(uplugin, "stale");
      const engine = fakeEngine("ApplySourceEngine");

      const r = await cli(["update", uproject, "--apply-only"], { env: { UE_MCP_TEST_ENGINE_ROOT: engine.root, FAKE_BUILD_EXIT: "0" } });
      expect(r.code, r.stdout + r.stderr).toBe(0);
      expect(plain(r.stdout)).toContain(`Bridge ${PACKAGE_VERSION} source deployed and built`);
      expect(fs.readFileSync(uplugin, "utf8")).not.toBe("stale");
    });

    it("installs binaries named by --binaries in either spelling, run from the project directory", async () => {
      covers("cli.update.apply-only");
      const uproject = sb.makeProject("ApplyBinary", { engine: "5.7" });
      const cwd = path.dirname(uproject);
      expect((await cli(["init", uproject, "--json", "--install", "binary", "--binaries", prebuilt(sb.path("prebuilt-a"), "old"), "--clients", "cursor"])).code).toBe(0);
      const dll = path.join(pluginDir(uproject), "Binaries", PLATFORM, "UnrealEditor-UE_MCP_Bridge.dll");
      const markerPath = path.join(pluginDir(uproject), ".ue-mcp-install.json");
      const age = () => fs.writeFileSync(markerPath, JSON.stringify({ ...marker(uproject), version: "0.0.1" }));

      // The value of --binaries is not the project.
      age();
      const spaced = await cli(["update", "--apply-only", "--binaries", prebuilt(sb.path("prebuilt-b"), "spaced")], { cwd });
      expect(spaced.code, spaced.stdout + spaced.stderr).toBe(0);
      expect(plain(spaced.stdout)).toContain(`Bridge ${PACKAGE_VERSION} installed`);
      expect(fs.readFileSync(dll, "utf8")).toBe("spaced");
      expect(marker(uproject).version).toBe(PACKAGE_VERSION);

      age();
      const joined = await cli(["update", "--apply-only", `--binaries=${prebuilt(sb.path("prebuilt-c"), "joined")}`], { cwd });
      expect(joined.code, joined.stdout + joined.stderr).toBe(0);
      expect(fs.readFileSync(dll, "utf8")).toBe("joined");
    });
  });

  describe("update", () => {
    it("installs the newest package through npm, then redeploys the bridge", async () => {
      covers("cli.update");
      const uproject = sb.makeProject("UpdateGame");
      expect((await cli(["deploy", uproject])).code).toBe(0);
      const uplugin = path.join(pluginDir(uproject), "UE_MCP_Bridge.uplugin");
      fs.writeFileSync(uplugin, "stale");
      const engine = fakeEngine("UpdateEngine");
      const callsBefore = sb.npmCalls().length;

      const r = await cli(["update", uproject, "--no-build"], {
        // The repo sits under this "global root", so update takes itself for a global install.
        env: { FAKE_NPM_LATEST: "99.0.0", FAKE_NPM_ROOT: path.dirname(REPO_ROOT), UE_MCP_TEST_ENGINE_ROOT: engine.root },
      });
      const out = plain(r.stdout);
      expect(r.code, out + r.stderr).toBe(0);
      expect(out).toContain(`Updating ue-mcp ${PACKAGE_VERSION} -> 99.0.0`);
      expect(out).toContain("Plugin deployed but not rebuilt (--no-build)");

      const calls = sb.npmCalls().slice(callsBefore).map((c) => c.args.join(" "));
      expect(calls).toContain("view ue-mcp version");
      expect(calls).toContain("root -g");
      expect(calls).toContain("install -g ue-mcp@99.0.0");
      expect(calls.filter((c) => c.startsWith("install"))).toEqual(["install -g ue-mcp@99.0.0"]);

      // The redeploy ran as its own process from this package and restored the plugin.
      expect(fs.readFileSync(uplugin, "utf8")).not.toBe("stale");
      expect(fs.existsSync(engine.log)).toBe(false);
    });

    it("stops at the deploy with --no-build and leaves the engine alone", async () => {
      covers("cli.update");
      const uproject = sb.makeProject("UpdateNoBuild");
      const engine = fakeEngine("NoBuildEngine");
      const r = await cli(["update", uproject, "--no-build"], { env: { UE_MCP_TEST_ENGINE_ROOT: engine.root } });
      const out = plain(r.stdout);
      expect(r.code, out + r.stderr).toBe(0);
      expect(out).toContain("Package already up to date");
      expect(out).toContain("Plugin deployed but not rebuilt (--no-build)");
      expect(fs.existsSync(path.join(pluginDir(uproject), "UE_MCP_Bridge.uplugin"))).toBe(true);
      expect(fs.existsSync(engine.log)).toBe(false);
    });
  });

  describe("build", () => {
    it("runs the engine build script for the editor target and propagates its exit", async () => {
      covers("cli.build");
      const uproject = sb.makeProject("BuildGame");
      // One engine per concurrent build: two cmd appends to one log race on Windows.
      const engine = fakeEngine("BuildEngine");
      const failing = fakeEngine("FailingEngine");
      const missing = sb.path("NoEngineHere");
      fs.mkdirSync(missing, { recursive: true });
      const [ok, failed, noEngine] = await Promise.all([
        cli(["build", uproject], { env: { UE_MCP_TEST_ENGINE_ROOT: engine.root, FAKE_BUILD_EXIT: "0" } }),
        cli(["build"], { cwd: path.dirname(uproject), env: { UE_MCP_TEST_ENGINE_ROOT: failing.root, FAKE_BUILD_EXIT: "3" } }),
        cli(["build", uproject], { env: { UE_MCP_TEST_ENGINE_ROOT: missing } }),
      ]);

      expect(ok.code, ok.stdout + ok.stderr).toBe(0);
      expect(plain(ok.stdout)).toContain("Build succeeded");
      const calls = [engine.log, failing.log].map((log) => fs.readFileSync(log, "utf8").trim());
      for (const call of calls) {
        expect(call).toMatch(new RegExp(`^BuildGameEditor ${PLATFORM} Development -Project=`));
        expect(call).toContain(path.basename(uproject));
        expect(call).toContain("-WaitMutex");
        expect(call).toContain("-NoHotReloadFromIDE");
      }

      expect(failed.code).toBe(1);
      expect(plain(failed.stdout)).toContain("Build failed with exit code 3");

      expect(noEngine.code).toBe(1);
      expect(plain(noEngine.stdout)).toContain("does not point at a usable Unreal engine");
    });
  });
});

describe("doctor", () => {
  let uproject: string;
  let globalConfig: string;
  const gone = () => sb.path("Gone", "Gone.uproject").replace(/\\/g, "/");

  beforeAll(() => {
    uproject = sb.makeProject("DoctorGame");
    const globalRoot = sb.path("npm-global");
    fs.mkdirSync(path.join(globalRoot, "ue-mcp"), { recursive: true });
    fs.writeFileSync(path.join(globalRoot, "ue-mcp", "package.json"), JSON.stringify({ name: "ue-mcp", version: "1.0.0" }));
    globalConfig = path.join(sb.home, ".claude", ".mcp.json");
    fs.mkdirSync(path.dirname(globalConfig), { recursive: true });
    fs.writeFileSync(
      globalConfig,
      JSON.stringify({
        mcpServers: {
          "ue-mcp": { command: "npx", args: ["-y", "ue-mcp@latest", gone()] },
          other: { command: "other-server", args: [] },
        },
      }, null, 2),
    );
  });

  it("reports versions and dead client entries without writing anything", async () => {
    covers("cli.doctor");
    const before = { project: snapshotTree(path.dirname(uproject)), home: snapshotTree(sb.home) };
    const r = await cli(["doctor", uproject], { env: { FAKE_NPM_LATEST: "99.0.0" } });
    const out = plain(r.stdout);
    expect(r.code, out + r.stderr).toBe(0);
    expect(out).toMatch(/registry latest:\s*99\.0\.0/);
    expect(out).toMatch(/npm global:\s*1\.0\.0/);
    expect(out).toMatch(/bridge plugin:\s*\(not deployed\)\s+DoctorGame\.uproject/);
    expect(out).toContain("npm global is 1.0.0, latest is 99.0.0");
    expect(out).toContain(`serves ${path.resolve(gone())}, which no longer exists`);
    expect(snapshotTree(path.dirname(uproject))).toEqual(before.project);
    expect(snapshotTree(sb.home)).toEqual(before.home);
  });

  it("--fix removes only the ue-mcp entry that points at a missing project", async () => {
    covers("cli.doctor.fix");
    const r = await cli(["doctor", uproject, "--fix"]);
    expect(r.code, r.stdout + r.stderr).toBe(0);
    expect(plain(r.stdout)).toContain(`Removed the ue-mcp entry from ${globalConfig}`);
    const servers = readJson(globalConfig).mcpServers;
    expect(servers["ue-mcp"]).toBeUndefined();
    expect(servers.other).toEqual({ command: "other-server", args: [] });

    const again = await cli(["doctor", uproject, "--fix"]);
    expect(again.code).toBe(0);
    expect(plain(again.stdout)).not.toContain("Removed the ue-mcp entry");
  });
});

describe("uninstall-hooks", () => {
  it("removes the hook init installed and forgets it in the user state", async () => {
    covers("cli.uninstall-hooks");
    const uproject = sb.makeProject("HookGame");
    const dir = path.dirname(uproject);
    const settings = path.join(dir, ".claude", "settings.json");
    const init = await cli(["init", uproject, "--json", "--install", "source", "--clients", "claude-code", "--feedback", "--hook"]);
    expect(init.code, init.stderr).toBe(0);
    // A setting the user owns must survive the removal.
    fs.writeFileSync(settings, JSON.stringify({ ...readJson(settings), model: "kept" }, null, 2));
    expect(hasHookMatcher(settings)).toBe(true);
    expect(hookRecorded(settings)).toBe(true);

    const r = await cli(["uninstall-hooks", dir]);
    expect(r.code, r.stdout + r.stderr).toBe(0);
    expect(plain(r.stdout)).toContain(`Removed hook from ${path.resolve(settings)}`);
    expect(hasHookMatcher(settings)).toBe(false);
    expect(readJson(settings).model).toBe("kept");
    expect(hookRecorded(settings)).toBe(false);

    const again = await cli(["uninstall-hooks"], { cwd: dir });
    expect(again.code).toBe(0);
    expect(plain(again.stdout)).toContain("Nothing to remove");
  });
});
