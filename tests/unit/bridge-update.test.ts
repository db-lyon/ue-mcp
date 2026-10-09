/**
 * Updating an installed bridge (spec 5.4, 7.3): what a release offers, and
 * applying it with the editor down, with it running on Windows (a loaded DLL
 * is renamed aside, never overwritten), and with it running elsewhere (staged
 * until the editor exits). Pinned client configs follow the bridge.
 */
import { afterAll, afterEach, beforeAll, beforeEach, describe, expect, it } from "vitest";
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as http from "node:http";
import * as path from "node:path";
import { execFileSync, spawn, type ChildProcess } from "node:child_process";
import {
  applyUpdate,
  checkForUpdate,
  readPendingUpdate,
  settlePendingUpdate,
  swapInPlace,
  type ReleaseFetcher,
} from "../../src/editor/bridge-update.js";
import { binaryArchiveName, binaryManifestName } from "../../src/editor/bridge-binaries.js";
import { readInstallMarker, writeInstallMarker } from "../../src/editor/install-marker.js";
import { ProjectFixture } from "../helpers/project-fixture.js";

const tar = process.platform === "win32"
  ? path.join(process.env.SystemRoot ?? process.env.SYSTEMROOT ?? "C:\\Windows", "System32", "tar.exe")
  : "tar";

let fixture: ProjectFixture;
const saved = { ...process.env };

beforeEach(() => {
  fixture = new ProjectFixture("ue-mcp-update-");
  for (const k of ["HOME", "USERPROFILE"]) process.env[k] = fixture.root;
  process.env.APPDATA = path.join(fixture.root, "AppData");
  delete process.env.UE_MCP_BINARIES;
});

afterEach(() => {
  for (const k of Object.keys(process.env)) if (!(k in saved)) delete process.env[k];
  Object.assign(process.env, saved);
  fixture.cleanup();
});

/** A project with a binary bridge of `version` for UE 5.7 on this platform. */
function binaryProject(version: string): { uproject: string; pluginDir: string } {
  const uproject = fixture.makeProject("Game", { engine: "5.7" });
  const pluginDir = path.join(path.dirname(uproject), "Plugins", "UE_MCP_Bridge");
  const bin = path.join(pluginDir, "Binaries", "Win64");
  fs.mkdirSync(bin, { recursive: true });
  fs.writeFileSync(path.join(pluginDir, "UE_MCP_Bridge.uplugin"), JSON.stringify({ VersionName: version }));
  fs.writeFileSync(path.join(bin, "UnrealEditor-UE_MCP_Bridge.dll"), `old ${version}`);
  writeInstallMarker(pluginDir, { kind: "binary", engine: "5.7", platform: "Win64", version });
  return { uproject, pluginDir };
}

function json(body: unknown) {
  return { ok: true, status: 200, json: async () => body };
}

describe("checkForUpdate", () => {
  const releases = (tags: Record<string, string>, manifestFiles: string[]): ReleaseFetcher => async (url) => {
    if (url.includes("registry.npmjs.org")) return json({ "dist-tags": tags });
    if (url.endsWith("-manifest.json")) return json({ version: "x", assets: manifestFiles.map((file) => ({ file })) });
    return { ok: false, status: 404, json: async () => ({}) };
  };

  it("offers the newest stable to a stable install, with binaries for its engine", async () => {
    const { uproject } = binaryProject("1.0.0");
    const file = binaryArchiveName({ version: "1.1.0", engine: "5.7", platform: "Win64" });
    const check = await checkForUpdate(uproject, releases({ latest: "1.1.0", beta: "1.2.0-beta.1" }, [file]));
    expect(check).toMatchObject({ channel: "latest", latest: "1.1.0", target: "1.1.0", binaries: { available: true, file } });
    expect(check.installed).toMatchObject({ kind: "binary", version: "1.0.0", engine: "5.7" });
  });

  it("keeps a beta install on beta, and moves it to a newer stable", async () => {
    const { uproject } = binaryProject("1.2.0-beta.0");
    expect((await checkForUpdate(uproject, releases({ latest: "1.1.0", beta: "1.2.0-beta.1" }, []))).target).toBe("1.2.0-beta.1");
    expect((await checkForUpdate(uproject, releases({ latest: "1.2.0", beta: "1.2.0-beta.1" }, []))).target).toBe("1.2.0");
  });

  it("says when a release has no binaries for this engine, and nothing when up to date", async () => {
    const { uproject } = binaryProject("1.0.0");
    expect((await checkForUpdate(uproject, releases({ latest: "1.1.0" }, ["other.zip"]))).binaries?.available).toBe(false);
    expect((await checkForUpdate(uproject, releases({ latest: "1.0.0" }, []))).target).toBeNull();
  });

  it("reports a registry it cannot read instead of throwing", async () => {
    const { uproject } = binaryProject("1.0.0");
    const check = await checkForUpdate(uproject, async () => ({ ok: false, status: 503, json: async () => ({}) }));
    expect(check.target).toBeNull();
    expect(check.error).toMatch(/503/);
  });
});

describe("applyUpdate from a release", () => {
  let server: http.Server;
  let base: string;
  const files = new Map<string, Buffer>();

  beforeAll(async () => {
    server = http.createServer((req, res) => {
      const body = files.get((req.url ?? "").split("/").pop() ?? "");
      if (!body) return void res.writeHead(404).end();
      res.writeHead(200).end(body);
    });
    await new Promise<void>((r) => server.listen(0, "127.0.0.1", () => r()));
    base = `http://127.0.0.1:${(server.address() as { port: number }).port}`;
  });
  afterAll(async () => {
    await new Promise<void>((r) => server.close(() => r()));
  });

  /** Publish a release of `version` for UE 5.7 Win64 on the fixture server. */
  function publish(version: string): void {
    const t = { version, engine: "5.7", platform: "Win64" };
    const src = path.join(fixture.root, `release-${version}`, "UE_MCP_Bridge");
    fs.mkdirSync(path.join(src, "Binaries", "Win64"), { recursive: true });
    fs.writeFileSync(path.join(src, "UE_MCP_Bridge.uplugin"), JSON.stringify({ VersionName: version }));
    fs.writeFileSync(path.join(src, "Binaries", "Win64", "UnrealEditor-UE_MCP_Bridge.dll"), `new ${version}`);
    const zip = path.join(fixture.root, binaryArchiveName(t));
    execFileSync(tar, ["-a", "-c", "-f", zip, "-C", path.dirname(src), "UE_MCP_Bridge"]);
    const bytes = fs.readFileSync(zip);
    files.set(binaryArchiveName(t), bytes);
    const sha256 = crypto.createHash("sha256").update(bytes).digest("hex");
    files.set(binaryManifestName(version), Buffer.from(JSON.stringify({ version, assets: [{ ...t, file: binaryArchiveName(t), sha256, size: bytes.length }] })));
  }

  it("swaps a binary install in at once with the editor down, and repins the client configs", async () => {
    const { uproject, pluginDir } = binaryProject("1.0.0");
    const mcp = path.join(path.dirname(uproject), ".mcp.json");
    fs.writeFileSync(mcp, JSON.stringify({ mcpServers: { "ue-mcp": { command: "C:/node/npx.cmd", args: ["-y", "ue-mcp@1.0.0", uproject.replace(/\\/g, "/")] } } }));
    publish("1.1.0");

    const result = await applyUpdate(uproject, { version: "1.1.0", editorRunning: false, binaries: base });

    expect(result).toMatchObject({ ok: true, state: "applied", kind: "binary" });
    expect(readInstallMarker(pluginDir)?.version).toBe("1.1.0");
    expect(fs.readFileSync(path.join(pluginDir, "Binaries", "Win64", "UnrealEditor-UE_MCP_Bridge.dll"), "utf-8")).toBe("new 1.1.0");
    expect(result.repinned).toEqual([mcp]);
    expect(JSON.parse(fs.readFileSync(mcp, "utf-8")).mcpServers["ue-mcp"]).toEqual({
      command: "C:/node/npx.cmd",
      args: ["-y", "ue-mcp@1.1.0", uproject.replace(/\\/g, "/")],
    });
  });

  it("stages beside the plugin while the editor runs off Windows, and swaps when it exits", async () => {
    const { uproject, pluginDir } = binaryProject("1.0.0");
    publish("1.1.0");

    const result = await applyUpdate(uproject, { version: "1.1.0", editorRunning: true, platform: "linux", binaries: base });

    expect(result.state).toBe("staged");
    expect(readInstallMarker(pluginDir)?.version).toBe("1.0.0");
    expect(readPendingUpdate(pluginDir)?.state).toBe("staged");
    expect(settlePendingUpdate(pluginDir, true)).toBe(false);
    expect(settlePendingUpdate(pluginDir, false)).toBe(true);
    expect(readInstallMarker(pluginDir)?.version).toBe("1.1.0");
    expect(readPendingUpdate(pluginDir)).toBeNull();
  });

  it("refuses a release whose archive does not match its manifest, and leaves the install alone", async () => {
    const { uproject, pluginDir } = binaryProject("1.0.0");
    publish("1.1.0");
    const name = binaryArchiveName({ version: "1.1.0", engine: "5.7", platform: "Win64" });
    files.set(name, Buffer.concat([files.get(name)!, Buffer.from("x")]));

    const result = await applyUpdate(uproject, { version: "1.1.0", editorRunning: false, binaries: base });

    expect(result.ok).toBe(false);
    expect(result.error).toMatch(/manifest hash/);
    expect(readInstallMarker(pluginDir)?.version).toBe("1.0.0");
  });

  it("moves a source install only to the version of the package deploying it", async () => {
    const uproject = fixture.makeProject("Src", { engine: "5.7" });
    const pluginDir = path.join(path.dirname(uproject), "Plugins", "UE_MCP_Bridge");
    fs.mkdirSync(path.join(pluginDir, "Source"), { recursive: true });
    fs.writeFileSync(path.join(pluginDir, "UE_MCP_Bridge.uplugin"), "{}");
    writeInstallMarker(pluginDir, { kind: "source", engine: "5.7", platform: "Win64", version: "0.0.1" });

    const result = await applyUpdate(uproject, { version: "99.0.0", editorRunning: false });
    expect(result.ok).toBe(false);
    expect(result.error).toMatch(/source install updates to the version of the package/);
  });
});

describe.runIf(process.platform === "win32")("swapInPlace against a loaded DLL", () => {
  let holder: ChildProcess | null = null;

  afterEach(() => {
    holder?.kill();
    holder = null;
  });

  /** Load a real DLL in another process, the way the editor holds the bridge. */
  async function loadInAnotherProcess(dll: string): Promise<void> {
    const ps = [
      `Add-Type -Name K -Namespace W -MemberDefinition '[DllImport("kernel32.dll")] public static extern IntPtr LoadLibrary(string p);'`,
      `if ([W.K]::LoadLibrary('${dll.replace(/'/g, "''")}') -eq [IntPtr]::Zero) { exit 3 }`,
      `Write-Output loaded`,
      `Start-Sleep -Seconds 60`,
    ].join("; ");
    holder = spawn("powershell", ["-NoProfile", "-Command", ps], { stdio: ["ignore", "pipe", "inherit"] });
    await new Promise<void>((resolve, reject) => {
      holder!.stdout!.on("data", (d: Buffer) => d.toString().includes("loaded") && resolve());
      holder!.on("exit", (code) => reject(new Error(`holder exited ${code}`)));
    });
  }

  it("renames the loaded DLL aside, puts the new one in place, and cleans up once it is released", async () => {
    const pluginDir = path.join(fixture.root, "Plugins", "UE_MCP_Bridge");
    const dll = path.join(pluginDir, "Binaries", "Win64", "UnrealEditor-UE_MCP_Bridge.dll");
    fs.mkdirSync(path.dirname(dll), { recursive: true });
    // Any small system DLL stands in for the bridge module.
    fs.copyFileSync(path.join(process.env.SystemRoot ?? process.env.SYSTEMROOT ?? "C:\\Windows", "System32", "version.dll"), dll);
    await loadInAnotherProcess(dll);

    // The precondition this path exists for: a loaded DLL cannot be overwritten.
    expect(() => fs.writeFileSync(dll, "overwrite")).toThrow();

    const staging = path.join(fixture.root, "staging");
    fs.mkdirSync(path.join(staging, "Binaries", "Win64"), { recursive: true });
    fs.writeFileSync(path.join(staging, "Binaries", "Win64", "UnrealEditor-UE_MCP_Bridge.dll"), "new module");
    fs.writeFileSync(path.join(staging, "UE_MCP_Bridge.uplugin"), "{}");

    const old = swapInPlace(staging, pluginDir, "1");

    expect(fs.readFileSync(dll, "utf-8")).toBe("new module");
    expect(old).toEqual([`${dll}.old-1`]);
    expect(fs.existsSync(`${dll}.old-1`)).toBe(true);
    expect(fs.existsSync(staging)).toBe(false);

    fs.writeFileSync(path.join(pluginDir, ".ue-mcp-update-pending.json"), JSON.stringify({ version: "x", state: "swapped", oldFiles: old, at: "" }));
    expect(settlePendingUpdate(pluginDir, true)).toBe(false);
    expect(fs.existsSync(`${dll}.old-1`)).toBe(true);

    holder!.kill();
    await new Promise((r) => holder!.once("exit", r));
    holder = null;
    expect(settlePendingUpdate(pluginDir, false)).toBe(true);
    expect(fs.existsSync(`${dll}.old-1`)).toBe(false);
  }, 60_000);
});
