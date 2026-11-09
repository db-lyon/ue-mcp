/**
 * `ue-mcp install <name>` spawned against a fake release: the name resolves
 * through the install index, the bare project is set up with prebuilt
 * binaries, and the extension and the editor plugin its manifest declares
 * both land. `extension install <manifest>` takes the same path.
 */
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as path from "node:path";
import { execFileSync } from "node:child_process";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { writeBridgeTree } from "../helpers/bridge-tree-fixture.js";
import { FakeHttp, PACKAGE_VERSION, plain, runCli, Sandbox } from "./harness.js";
import { zipExtension } from "./daemon-fakes.js";

const PLATFORM = process.platform === "win32" ? "Win64" : process.platform === "darwin" ? "Mac" : "Linux";
const TAR = process.platform === "win32" ? path.join(process.env.SystemRoot ?? "C:\\Windows", "System32", "tar.exe") : "tar";
const sha = (file: string) => crypto.createHash("sha256").update(fs.readFileSync(file)).digest("hex");
const readJson = (file: string) => JSON.parse(fs.readFileSync(file, "utf8"));

let sb: Sandbox;
let fake: FakeHttp;
let index: string;
let binaries: string;
let extZip: { zip: string; sha256: string };
let pluginZip: string;
let pluginSha: string;
let pluginVersion = "0.2.0";

/** An editor plugin release zip: one folder holding its descriptor and this platform's binaries. */
function zipPlugin(work: string, version: string): string {
  const root = path.join(work, `plugin-${version}`, "Probe_Studio");
  fs.mkdirSync(path.join(root, "Binaries", PLATFORM), { recursive: true });
  fs.writeFileSync(path.join(root, "Probe_Studio.uplugin"), JSON.stringify({ FileVersion: 3, VersionName: version, FriendlyName: "Probe Studio" }));
  fs.writeFileSync(path.join(root, "Binaries", PLATFORM, "UnrealEditor-Probe_Studio.dll"), version);
  const zip = path.join(work, `Probe_Studio-${version}.zip`);
  execFileSync(TAR, ["-a", "-c", "-f", zip, "-C", path.dirname(root), "Probe_Studio"], { windowsHide: true });
  return zip;
}

beforeAll(async () => {
  sb = new Sandbox("ue-mcp-named-");
  const work = sb.path("work");
  fs.mkdirSync(work, { recursive: true });
  extZip = zipExtension(work, { name: "probe-studio", version: "0.1.0" });
  pluginZip = zipPlugin(work, pluginVersion);
  pluginSha = sha(pluginZip);
  binaries = sb.path("prebuilt");
  writeBridgeTree(path.join(binaries, "UE_MCP_Bridge"), PACKAGE_VERSION, PLATFORM);
  fake = await FakeHttp.start((req) => {
    if (req.url === "/latest.json") {
      return { json: { version: "0.1.0", url: "ext.zip", sha256: extZip.sha256, pluginChannel: "plugin/plugin.json", pluginMin: "0.1.0" } };
    }
    if (req.url === "/ext.zip") return { body: fs.readFileSync(extZip.zip) };
    if (req.url === "/plugin/plugin.json") {
      return { json: { version: pluginVersion, files: { "5.7": { url: "p.zip", sha256: pluginSha } } } };
    }
    if (req.url === "/plugin/p.zip") return { body: fs.readFileSync(pluginZip) };
    return { status: 404 };
  });
  index = sb.path("install-index.json");
  fs.writeFileSync(index, JSON.stringify({ studio: { title: "Probe Studio", manifest: `${fake.origin}/latest.json` } }));
});
afterAll(async () => {
  await fake.stop();
  sb.cleanup();
});

const env = () => ({ UE_MCP_INSTALL_INDEX: index, UE_MCP_BINARIES: binaries });

describe("ue-mcp install <name>", () => {
  let uproject: string;
  let dir: string;

  beforeAll(() => {
    uproject = sb.makeProject("Named", { engine: "5.7" });
    dir = path.dirname(uproject);
  });

  it("sets up a bare project, installs the extension and its editor plugin, and says to open the project", async () => {
    covers("cli.install");
    const r = await runCli(sb, ["install", "studio"], { cwd: dir, env: env() });
    expect(r.code, r.stdout + r.stderr).toBe(0);
    const out = plain(r.stdout);
    expect(out).toContain("set up ue-mcp in Named");
    expect(out).toContain("installed Probe Studio 0.1.0");
    expect(out).toContain("installed the Probe Studio editor plugin 0.2.0");
    expect(out).toContain("Open Named in Unreal to finish.");
    expect(out).not.toMatch(/extension/i);

    expect(readJson(path.join(dir, "Plugins", "UE_MCP_Bridge", ".ue-mcp-install.json"))).toMatchObject({ kind: "binary", engine: "5.7", version: PACKAGE_VERSION });
    expect(fs.existsSync(path.join(dir, "ue-mcp.yml"))).toBe(true);
    expect(fs.readFileSync(path.join(dir, "Plugins", "Probe_Studio", "Binaries", PLATFORM, "UnrealEditor-Probe_Studio.dll"), "utf8")).toBe("0.2.0");
    const enabled = (readJson(uproject).Plugins as Array<{ Name: string; Enabled: boolean }>).filter((p) => p.Enabled).map((p) => p.Name);
    expect(enabled).toEqual(expect.arrayContaining(["UE_MCP_Bridge", "PythonScriptPlugin", "Probe_Studio"]));
    expect(fs.existsSync(path.join(sb.home, ".ue-mcp", "extensions", "probe-studio@0.1.0", "package.json"))).toBe(true);
  });

  it("is a no-op for what is already in place, and moves the editor plugin to a newer build", async () => {
    covers("cli.install");
    const again = await runCli(sb, ["install", "studio", uproject, "--json"], { env: env() });
    expect(again.code, again.stdout + again.stderr).toBe(0);
    const result = JSON.parse(again.stdout);
    expect(result).toMatchObject({ ok: true, name: "probe-studio", version: "0.1.0", editorPlugin: { name: "Probe_Studio", version: "0.2.0", changed: false } });
    expect(result.setup).toBeUndefined();

    pluginVersion = "0.3.0";
    pluginZip = zipPlugin(sb.path("work"), pluginVersion);
    pluginSha = sha(pluginZip);
    const up = await runCli(sb, ["extension", "install", `${fake.origin}/latest.json`, uproject, "--json"], { env: env() });
    covers("cli.extension.install");
    expect(up.code, up.stdout + up.stderr).toBe(0);
    expect(JSON.parse(up.stdout).editorPlugin).toMatchObject({ version: "0.3.0", changed: true });
    expect(fs.readFileSync(path.join(dir, "Plugins", "Probe_Studio", "Binaries", PLATFORM, "UnrealEditor-Probe_Studio.dll"), "utf8")).toBe("0.3.0");
    expect(fs.readdirSync(path.join(dir, "Plugins")).sort()).toEqual(["Probe_Studio", "UE_MCP_Bridge"]);
  });

  it("refuses an editor plugin build whose digest does not match, and keeps the one installed", async () => {
    covers("cli.install");
    const good = pluginSha;
    pluginSha = "0".repeat(64);
    try {
      const r = await runCli(sb, ["install", "studio", uproject, "--json"], { env: env() });
      expect(r.code).toBe(1);
      expect(JSON.parse(r.stdout)).toMatchObject({ ok: false, error: expect.stringMatching(/does not match the expected SHA-256/) });
    } finally {
      pluginSha = good;
    }
    expect(fs.readFileSync(path.join(sb.path("Named"), "Plugins", "Probe_Studio", "Binaries", PLATFORM, "UnrealEditor-Probe_Studio.dll"), "utf8")).toBe("0.3.0");
  });

  it("names the engines a channel has builds for when the project's is not one", async () => {
    covers("cli.install");
    const old = sb.makeProject("OldEngine", { engine: "5.6", config: "ue-mcp:\n  version: 1\n" });
    writeBridgeTree(path.join(path.dirname(old), "Plugins", "UE_MCP_Bridge"), PACKAGE_VERSION, PLATFORM);
    const r = await runCli(sb, ["install", "studio", old, "--json"], { env: env() });
    expect(r.code).toBe(1);
    expect(JSON.parse(r.stdout).error).toMatch(/no editor plugin build for UE 5\.6 \(builds exist for 5\.7\)/);
  });

  it("lists the names it knows when given one it does not, and reaches nothing", async () => {
    covers("cli.install");
    const before = fake.requests.length;
    const r = await runCli(sb, ["install", "nonesuch", uproject], { env: env() });
    expect(r.code).toBe(1);
    expect(plain(r.stdout)).toContain("'nonesuch' is not something ue-mcp can install by name (known: studio)");
    expect(fake.requests.length).toBe(before);
  });
});
