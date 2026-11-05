/**
 * A client config ue-mcp writes starts the server on the day of a release
 * under npm's release-age gate (#1510).
 *
 * npx ignores min-release-age-exclude (npm/cli#9765), so with
 * `min-release-age=7` a launch of a release published today is refused even
 * when ue-mcp is excluded. The written launch carries `--min-release-age=0`.
 * These cases run the real npx from the pinned npm devDependency against a fake
 * registry that published ue-mcp a minute ago, with an age-gated .npmrc in the
 * sandbox home.
 */
import { spawn } from "node:child_process";
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import { createRequire } from "node:module";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { writeBridgeTree } from "../helpers/bridge-tree-fixture.js";
import { FakeHttp, PACKAGE_VERSION, plain, REPO_ROOT, runCli, Sandbox, type CliOptions, type CliResult } from "./harness.js";

const IS_WINDOWS = process.platform === "win32";
const PLATFORM = IS_WINDOWS ? "Win64" : process.platform === "darwin" ? "Mac" : "Linux";
const requireFromRepo = createRequire(path.join(REPO_ROOT, "package.json"));
const NPM_ROOT = path.dirname(requireFromRepo.resolve("npm/package.json"));
const NPM_CLI = path.join(NPM_ROOT, "bin", "npm-cli.js");
const NPX_CLI = path.join(NPM_ROOT, "bin", "npx-cli.js");
const SERVED = "fake ue-mcp served";

let sb: Sandbox;
let registry: FakeHttp;
let npmrc: string;
let fakeNpm: string;

/** The real npm, gated by the sandbox .npmrc and blind to every config outside the sandbox. */
function npmEnv(): Record<string, string> {
  return sb.env({
    npm_config_userconfig: npmrc,
    npm_config_globalconfig: sb.path("npm-global-rc"),
    npm_config_cache: sb.path("npm-cache"),
    npm_config_update_notifier: "false",
    npm_config_fund: "false",
    npm_config_audit: "false",
  });
}

function runNode(script: string, args: string[], cwd: string): Promise<CliResult> {
  return new Promise((resolve, reject) => {
    const child = spawn(process.execPath, [script, ...args], { cwd, env: npmEnv(), stdio: ["ignore", "pipe", "pipe"], windowsHide: true });
    let stdout = "";
    let stderr = "";
    child.stdout.on("data", (d) => (stdout += d));
    child.stderr.on("data", (d) => (stderr += d));
    const timer = setTimeout(() => {
      child.kill();
      reject(new Error(`${path.basename(script)} ${args.join(" ")} did not exit\n${stdout}\n${stderr}`));
    }, 120_000);
    child.on("error", reject);
    child.on("close", (code) => {
      clearTimeout(timer);
      resolve({ code, stdout, stderr });
    });
  });
}

/** Launch a client entry the way the client does, with the real npx standing in for `npx`. */
function launch(entry: { command: string; args: string[] }): Promise<CliResult> {
  expect(entry.command).toBe("npx");
  const cwd = fs.mkdtempSync(sb.path("launch-"));
  return runNode(NPX_CLI, entry.args, cwd);
}

function cli(args: string[], opts: CliOptions = {}): Promise<CliResult> {
  return runCli(sb, args, {
    ...opts,
    env: { UE_MCP_TEST_ENGINE_ROOT: sb.path("no-engine"), NPM_FAKE_SCRIPT: fakeNpm, ...opts.env },
  });
}

const readJson = (file: string) => JSON.parse(fs.readFileSync(file, "utf8"));
const ueMcpEntry = (configPath: string) => readJson(configPath).mcpServers["ue-mcp"] as { command: string; args: string[] };
const pluginDir = (uproject: string) => path.join(path.dirname(uproject), "Plugins", "UE_MCP_Bridge");

/** A prebuilt plugin tree as a release ships it, built for this platform. */
function prebuilt(dir: string): string {
  writeBridgeTree(path.join(dir, "UE_MCP_Bridge"), PACKAGE_VERSION, PLATFORM);
  return dir;
}

beforeAll(async () => {
  sb = new Sandbox("ue-mcp-age-gate-");
  fs.writeFileSync(sb.path("npm-global-rc"), "");

  // A ue-mcp package whose bin prints what it was asked to serve.
  const pkg = sb.path("fake-package");
  fs.mkdirSync(pkg, { recursive: true });
  fs.writeFileSync(path.join(pkg, "package.json"), JSON.stringify({ name: "ue-mcp", version: PACKAGE_VERSION, bin: { "ue-mcp": "cli.js" } }));
  fs.writeFileSync(path.join(pkg, "cli.js"), `#!/usr/bin/env node\nconsole.log(${JSON.stringify(SERVED)} + " " + process.argv.slice(2).join(" "));\n`);
  npmrc = sb.path("unused-npmrc");
  fs.writeFileSync(npmrc, "");
  const packed = await runNode(NPM_CLI, ["pack", "--pack-destination", sb.root], pkg);
  expect(packed.code, packed.stdout + packed.stderr).toBe(0);
  const tarball = fs.readFileSync(sb.path(`ue-mcp-${PACKAGE_VERSION}.tgz`));
  const integrity = `sha512-${crypto.createHash("sha512").update(tarball).digest("base64")}`;
  const shasum = crypto.createHash("sha1").update(tarball).digest("hex");

  // Published a minute ago, with an older release well outside the window.
  const now = new Date(Date.now() - 60_000).toISOString();
  const old = new Date(Date.now() - 30 * 86_400_000).toISOString();
  registry = await FakeHttp.start((req) => {
    const origin = registry.origin;
    if (req.url === "/ue-mcp") {
      const manifest = (version: string) => ({
        name: "ue-mcp",
        version,
        bin: { "ue-mcp": "cli.js" },
        dist: { tarball: `${origin}/ue-mcp/-/ue-mcp-${PACKAGE_VERSION}.tgz`, integrity, shasum },
      });
      return {
        json: {
          name: "ue-mcp",
          "dist-tags": { latest: PACKAGE_VERSION },
          versions: { "0.0.1": manifest("0.0.1"), [PACKAGE_VERSION]: manifest(PACKAGE_VERSION) },
          time: { created: old, modified: now, "0.0.1": old, [PACKAGE_VERSION]: now },
        },
      };
    }
    if (req.url === `/ue-mcp/-/ue-mcp-${PACKAGE_VERSION}.tgz`) return { body: tarball, headers: { "content-type": "application/octet-stream" } };
    return { status: 404, json: { error: "not found" } };
  });

  // The configuration from the report: a seven-day window with ue-mcp excluded.
  npmrc = path.join(sb.home, ".npmrc");
  fs.writeFileSync(npmrc, [`registry=${registry.origin}/`, "min-release-age=7", "min-release-age-exclude[]=ue-mcp", ""].join("\n"));

  // doctor asks npm for its version and the gate; the real npm answers from the sandbox .npmrc.
  fakeNpm = sb.path("fake-npm-script.mjs");
  fs.writeFileSync(
    fakeNpm,
    `import { execFileSync } from "node:child_process";
export default function (args) {
  const line = args.join(" ");
  if (line === "--version" || line.startsWith("config get ")) {
    process.stdout.write(execFileSync(process.execPath, [${JSON.stringify(NPM_CLI)}, ...args], { encoding: "utf8", env: { ...process.env, npm_config_userconfig: ${JSON.stringify(npmrc)}, npm_config_globalconfig: ${JSON.stringify(sb.path("npm-global-rc"))} } }));
  }
  return 0;
}
`,
  );
}, 180_000);

afterAll(async () => {
  await registry?.stop();
  sb?.cleanup();
});

describe("npx under min-release-age", () => {
  it("refuses a launch without the override, so the gate is live and the exclusion is ignored", async () => {
    covers("client.launch.age-gate");
    const r = await launch({ command: "npx", args: ["-y", `ue-mcp@${PACKAGE_VERSION}`, "Game.uproject"] });
    expect(r.code).not.toBe(0);
    expect(r.stdout + r.stderr).toMatch(/No matching version found for ue-mcp@\S+ with a date before/);
  });

  it("starts the server from a config ue-mcp update wrote, on the day of the release", async () => {
    covers("client.launch.age-gate", "cli.update.apply-only");
    const uproject = sb.makeProject("GatedGame", { engine: "5.7" });
    const cwd = path.dirname(uproject);
    const init = await cli(["init", uproject, "--json", "--install", "binary", "--binaries", prebuilt(sb.path("prebuilt-a")), "--clients", "claude-code"]);
    expect(init.code, init.stdout + init.stderr).toBe(0);

    // A config from before the fix, pinned to an older release, and a bridge that is behind.
    const mcp = path.join(cwd, ".mcp.json");
    const serves = uproject.replace(/\\/g, "/");
    fs.writeFileSync(mcp, JSON.stringify({ mcpServers: { "ue-mcp": { command: "npx", args: ["-y", "ue-mcp@0.0.1", serves] } } }, null, 2));
    const markerPath = path.join(pluginDir(uproject), ".ue-mcp-install.json");
    fs.writeFileSync(markerPath, JSON.stringify({ ...readJson(markerPath), version: "0.0.1" }));

    const update = await cli(["update", "--apply-only", "--binaries", prebuilt(sb.path("prebuilt-b"))], { cwd });
    expect(update.code, update.stdout + update.stderr).toBe(0);

    const entry = ueMcpEntry(mcp);
    expect(entry).toEqual({ command: "npx", args: ["-y", "--min-release-age=0", `ue-mcp@${PACKAGE_VERSION}`, serves] });
    const r = await launch(entry);
    expect(r.code, r.stdout + r.stderr).toBe(0);
    expect(r.stdout).toContain(`${SERVED} ${serves}`);
  });

  it("doctor reports the launch the gate would refuse, and --fix makes it start", async () => {
    covers("cli.doctor.age-gate");
    const uproject = sb.makeProject("DoctorGated");
    // A live entry: the project has its bridge, so --fix keeps the entry and lifts it.
    fs.mkdirSync(pluginDir(uproject), { recursive: true });
    fs.writeFileSync(path.join(pluginDir(uproject), "UE_MCP_Bridge.uplugin"), JSON.stringify({ VersionName: PACKAGE_VERSION }));
    const serves = uproject.replace(/\\/g, "/");
    const globalMcp = path.join(sb.home, ".claude", ".mcp.json");
    fs.mkdirSync(path.dirname(globalMcp), { recursive: true });
    fs.writeFileSync(globalMcp, JSON.stringify({ mcpServers: { "ue-mcp": { command: "npx", args: ["-y", `ue-mcp@${PACKAGE_VERSION}`, serves] } } }, null, 2));

    const report = await cli(["doctor", uproject]);
    const out = plain(report.stdout);
    expect(report.code, out + report.stderr).toBe(0);
    expect(out).toContain(`Claude Code (global) (${globalMcp}) runs \`npx ue-mcp@${PACKAGE_VERSION}\`, which npm's min-release-age=7 refuses`);
    expect(out).toContain("Run `ue-mcp doctor --fix` to add `--min-release-age=0` to the launch.");
    expect(ueMcpEntry(globalMcp).args).toEqual(["-y", `ue-mcp@${PACKAGE_VERSION}`, serves]);

    const fix = await cli(["doctor", uproject, "--fix"]);
    expect(fix.code, fix.stdout + fix.stderr).toBe(0);
    expect(plain(fix.stdout)).toContain(`Added --min-release-age=0 to the ue-mcp launch in ${globalMcp}`);
    const entry = ueMcpEntry(globalMcp);
    expect(entry.args).toEqual(["-y", "--min-release-age=0", `ue-mcp@${PACKAGE_VERSION}`, serves]);

    const again = plain((await cli(["doctor", uproject])).stdout);
    expect(again).not.toContain("min-release-age=7 refuses");
    const r = await launch(entry);
    expect(r.code, r.stdout + r.stderr).toBe(0);
    expect(r.stdout).toContain(`${SERVED} ${serves}`);
  });
});
