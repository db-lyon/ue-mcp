/**
 * The package as npm ships it: src/ compiled to dist/ with the repo's own
 * tsconfig, laid out like an installed ue-mcp (package.json, dist/, plugin/,
 * skills/), and run with plain node. A plugin that is not beside it imports
 * `ue-mcp/task` and `ue-mcp/guard` the way a published plugin does, and a
 * daemon extension's code imports `ue-mcp/daemon`.
 *
 * Slow: one full tsc emit, about 30 seconds.
 */
import { spawnSync } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { FakeBridge } from "../fake-bridge.js";
import { covers } from "../helpers/covers.js";
import { json, McpServer, REPO_ROOT, Sandbox } from "./harness.js";
import { pluginsYml, writePluginPackage } from "./plugin-fixtures.js";

let sb: Sandbox;
/** The installed package root: <sandbox>/global/node_modules/ue-mcp. */
let pkg: string;
let distEntry: string;

function link(target: string, at: string): void {
  fs.symlinkSync(target, at, process.platform === "win32" ? "junction" : "dir");
}

beforeAll(() => {
  sb = new Sandbox("ue-mcp-pkg-");
  pkg = sb.path("global", "node_modules", "ue-mcp");
  fs.mkdirSync(pkg, { recursive: true });
  const tsc = spawnSync(
    process.execPath,
    [path.join(REPO_ROOT, "node_modules", "typescript", "bin", "tsc"), "-p", path.join(REPO_ROOT, "tsconfig.json"), "--outDir", path.join(pkg, "dist")],
    { cwd: REPO_ROOT, encoding: "utf8", windowsHide: true },
  );
  if (tsc.status !== 0) throw new Error(`tsc failed:\n${tsc.stdout}\n${tsc.stderr}`);
  fs.copyFileSync(path.join(REPO_ROOT, "package.json"), path.join(pkg, "package.json"));
  link(path.join(REPO_ROOT, "plugin"), path.join(pkg, "plugin"));
  link(path.join(REPO_ROOT, "skills"), path.join(pkg, "skills"));
  link(path.join(REPO_ROOT, "schemas"), path.join(pkg, "schemas"));
  // The package's own dependencies, where an install would have put them.
  link(path.join(REPO_ROOT, "node_modules"), path.join(pkg, "node_modules"));
  distEntry = path.join(pkg, "dist", "index.js");
}, 240_000);

afterAll(() => {
  // Unlink the links first, so removing the sandbox can never reach into the repo.
  for (const name of ["plugin", "skills", "schemas", "node_modules"]) {
    try { fs.unlinkSync(path.join(pkg, name)); } catch { /* never made */ }
  }
  sb.cleanup();
});

const PLUGIN_MANIFEST = `actionPrefix: pkgx
tasks:
  pkgx_hello:
    class_path: tasks/hello
  pkgx_block:
    class_path: tasks/block
provides:
  pkgxcat:
    description: A category served by a task built on ue-mcp/task
    actions:
      greet:
        task: pkgx_hello
        effect: read
        schema:
          who: { type: string }
guards:
  blocklist:
    before:
      class_path: tasks/block
      options:
        blocked: [get_world_outliner_blocked_probe]
`;

const HELLO = `import { UeMcpTask } from "ue-mcp/task";
export default class Hello extends UeMcpTask {
  get taskName() { return "pkgx.hello"; }
  async execute() {
    return { success: true, data: { greeting: "hello, " + (this.options.who ?? "world"), base: Object.getPrototypeOf(Hello).name } };
  }
}
`;

const BLOCK = `import { UeMcpGuard } from "ue-mcp/guard";
export default class Block extends UeMcpGuard {
  get taskName() { return "pkgx.block"; }
  before(call) {
    const blocked = this.config.blocked ?? [];
    return blocked.includes(call.method) ? this.deny("pkgx blocklist refused " + call.method) : this.allow();
  }
}
`;

describe("the built package", () => {
  it("starts from dist/index.js and serves a plugin task written against ue-mcp/task", async () => {
    covers("exports.task");
    const uproject = sb.makeProject("Game", { config: pluginsYml(["pkgx-plugin"]) });
    const projectDir = path.dirname(uproject);
    // Installed beside the project, with no ue-mcp next to it: the server's own copy answers the import.
    writePluginPackage(path.join(projectDir, "node_modules", "pkgx-plugin"), {
      name: "pkgx-plugin",
      version: "1.0.0",
      manifest: PLUGIN_MANIFEST,
      files: { "tasks/hello.js": HELLO, "tasks/block.js": BLOCK },
    });
    expect(fs.existsSync(path.join(projectDir, "node_modules", "ue-mcp"))).toBe(false);

    const server = await McpServer.start(sb, { projects: [uproject], strategy: "full", entry: [distEntry] });
    try {
      expect(await server.toolNames()).toContain("pkgxcat");
      const r = await server.call("pkgxcat", { action: "greet", who: "dist" });
      expect(r.isError, r.text).toBe(false);
      expect(r.text).toContain("hello, dist");
      expect(r.text).toContain("UeMcpTask");
    } finally {
      await server.close();
    }
  });

  it("runs a plugin guard written against ue-mcp/guard on real bridge calls", async () => {
    covers("exports.guard");
    const uproject = sb.makeProject("Guarded", { config: pluginsYml(["pkgx-plugin"]) });
    const projectDir = path.dirname(uproject);
    writePluginPackage(path.join(projectDir, "node_modules", "pkgx-plugin"), {
      name: "pkgx-plugin",
      version: "1.0.0",
      manifest: PLUGIN_MANIFEST.replace("get_world_outliner_blocked_probe", "get_world_outliner"),
      files: { "tasks/hello.js": HELLO, "tasks/block.js": BLOCK },
    });
    const bridge = await FakeBridge.start({ projectDir });
    const server = await McpServer.start(sb, { projects: [uproject], strategy: "full", port: null, entry: [distEntry] });
    try {
      const refused = await server.call("level", { action: "get_outliner" });
      expect(refused.isError).toBe(true);
      expect(refused.text).toContain("pkgx blocklist refused get_world_outliner");
      expect(bridge.methods).not.toContain("get_world_outliner");

      const allowed = await server.call("project", { action: "get_status" });
      expect(allowed.isError, allowed.text).toBe(false);
    } finally {
      await server.close();
      await bridge.stop();
    }
  });

  it("resolves ue-mcp/daemon for extension code, and its bundle signing round-trips", () => {
    covers("exports.daemon");
    const consumer = sb.path("global", "consumer.mjs");
    fs.writeFileSync(
      consumer,
      `import * as daemon from "ue-mcp/daemon";
import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
const dir = ${JSON.stringify(sb.path("bundle"))};
fs.mkdirSync(path.join(dir, "assets"), { recursive: true });
fs.writeFileSync(path.join(dir, "index.html"), "<html></html>");
fs.writeFileSync(path.join(dir, "assets", "app.js"), "1");
const { privateKey, publicKey } = crypto.generateKeyPairSync("ed25519");
const api = { min: daemon.DAEMON_API_VERSION, max: daemon.DAEMON_API_VERSION };
daemon.signBundle(dir, { name: "probe", version: "1.0.0", api }, privateKey.export({ type: "pkcs8", format: "pem" }));
const good = daemon.verifyBundle(dir, [publicKey]);
fs.writeFileSync(path.join(dir, "assets", "app.js"), "2");
let tampered = null;
try { daemon.verifyBundle(dir, [publicKey]); } catch (e) { tampered = e.message; }
console.log(JSON.stringify({ exports: Object.keys(daemon).sort(), api: daemon.EXTENSION_API_VERSION, good: good.name, tampered }));
`,
    );
    const r = spawnSync(process.execPath, [consumer], { cwd: sb.path("global"), encoding: "utf8", windowsHide: true });
    expect(r.status, r.stderr).toBe(0);
    const out = JSON.parse(r.stdout.trim()) as { exports: string[]; api: number; good: string; tampered: string | null };
    expect(out.exports).toEqual(expect.arrayContaining(["DAEMON_API_VERSION", "EXTENSION_API_VERSION", "readDiscovery", "signBundle", "verifyBundle"]));
    expect(typeof out.api).toBe("number");
    expect(out.good).toBe("probe");
    expect(out.tampered).toMatch(/does not match its hash/);
  });
});

describe("the context measurement format", () => {
  it("ships its validator in ue-mcp/daemon and its JSON Schema as a package file", () => {
    covers("exports.context-measurement");
    const files = (JSON.parse(fs.readFileSync(path.join(REPO_ROOT, "package.json"), "utf8")) as { files: string[] }).files;
    expect(files).toContain("schemas");
    const consumer = sb.path("global", "measurement.mjs");
    fs.writeFileSync(
      consumer,
      `import { parseContextMeasurement, scopeBucket } from "ue-mcp/daemon";
import schema from "ue-mcp/schemas/context-measurement.schema.json" with { type: "json" };
const doc = { version: 1, measuredAt: "2026-10-25T00:00:00Z", ueMcpVersion: "1.0.0", runs: [
  { model: { vendor: "v", model: "m" }, scope: scopeBucket(5), strategy: "lean", seedTokens: 10, tasks: [] },
] };
console.log(JSON.stringify({ ok: parseContextMeasurement(doc).ok, bad: parseContextMeasurement({ version: 2 }).ok, title: schema.title, scope: doc.runs[0].scope }));
`,
    );
    const r = spawnSync(process.execPath, [consumer], { cwd: sb.path("global"), encoding: "utf8", windowsHide: true });
    expect(r.status, r.stderr).toBe(0);
    expect(JSON.parse(r.stdout.trim())).toEqual({ ok: true, bad: false, title: "ue-mcp context measurement", scope: "4-8" });
  });
});

describe("the built server's surface", () => {
  it("advertises exactly what the source server does", async () => {
    covers("package.dist-server");
    const uproject = sb.makeProject("Plain");
    const built = await McpServer.start(sb, { projects: [uproject], strategy: "full", entry: [distEntry] });
    const fromSource = await McpServer.start(sb, { projects: [uproject], strategy: "full" });
    try {
      expect(built.instructions).toBe(fromSource.instructions);
      expect(await built.listTools()).toEqual(await fromSource.listTools());
      const status = json(await built.call("project", { action: "get_status" }));
      expect(JSON.stringify(status)).toContain("Plain");
    } finally {
      await built.close();
      await fromSource.close();
    }
  });
});
