/**
 * The bridge upgrade matrix: every released UE 5.8 Win64 bridge, installed as
 * a binary plugin, updated by the real applyUpdate to the newest release and
 * to the layout the plugin source in this tree builds, with the editor down
 * and with it running. The installed tree must then be one the editor loads
 * without offering a rebuild (verifyBridgeTree) and hold exactly the target's
 * files.
 *
 * The trees come from tests/unit/fixtures/bridge-upgrade, one JSON per release
 * recorded by `node scripts/bridge-upgrade-fixtures.mjs`. Text files keep their
 * content; DLLs become stub PE images and other binaries tagged placeholders.
 * UE_MCP_UPGRADE_MATRIX=zips uses the release zips cached by that script
 * instead, end to end through zip extraction.
 *
 * On Windows a running editor is played by another process mapping every old
 * DLL as an image (tests/helpers/dll-holder.ts): renamable, not deletable or
 * writable. Elsewhere the same swap runs with nothing held.
 */
import { afterEach, beforeAll, describe, expect, it } from "vitest";
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as path from "node:path";
import { execFileSync } from "node:child_process";
import { applyUpdate, readPendingUpdate, settlePendingUpdate } from "../../src/editor/bridge-update.js";
import { binaryArchiveName } from "../../src/editor/bridge-binaries.js";
import { verifyBridgeTree, treeFiles, ASIDE_FILE } from "../../src/editor/bridge-tree.js";
import { INSTALL_MARKER_NAME, readInstallMarker, writeInstallMarker } from "../../src/editor/install-marker.js";
import { isNewer } from "../../src/core/version-check.js";
import { ProjectFixture } from "../helpers/project-fixture.js";
import { DllHolder, stubModule, stubTag } from "../helpers/dll-holder.js";

const ROOT = path.resolve(__dirname, "..", "..");
const FIXTURES = path.join(ROOT, "tests", "unit", "fixtures", "bridge-upgrade");
const ZIP_CACHE = path.join(ROOT, "node_modules", ".cache", "ue-mcp", "bridge-releases");
const ENGINE = "5.8";
const PLATFORM = "Win64";
const USE_ZIPS = process.env.UE_MCP_UPGRADE_MATRIX === "zips";
const HOLD = process.platform === "win32";

interface FixtureFile { path: string; size: number; sha256: string; text?: string }
interface Release { version: string; zip: string; zipSha256: string; files: FixtureFile[] }

function loadReleases(): Release[] {
  return fs.readdirSync(FIXTURES)
    .filter((f) => f.endsWith(".json"))
    .map((f) => JSON.parse(fs.readFileSync(path.join(FIXTURES, f), "utf-8")) as Release)
    .sort((a, b) => (isNewer(a.version, b.version) ? 1 : isNewer(b.version, a.version) ? -1 : 0));
}

/** The layout the plugin source in this tree ships: the newest release's files, re-derived from the descriptor here. */
function currentLayout(newest: Release): Release {
  const src = path.join(ROOT, "plugin", "ue_mcp_bridge");
  const descriptor = JSON.parse(fs.readFileSync(path.join(src, "UE_MCP_Bridge.uplugin"), "utf-8"));
  const version = `${newest.version}-next`;
  descriptor.VersionName = version;
  descriptor.Installed = true;
  descriptor.EngineVersion = `${ENGINE}.0`;
  delete descriptor.EnabledByDefault;
  const modules: string[] = descriptor.Modules.map((m: { Name: string }) => m.Name);
  const buildId = JSON.parse(newest.files.find((f) => f.path.endsWith("UnrealEditor.modules"))!.text!).BuildId;
  const files: FixtureFile[] = [
    { path: "UE_MCP_Bridge.uplugin", size: 0, sha256: "", text: JSON.stringify(descriptor, null, "\t") },
    {
      path: `Binaries/${PLATFORM}/UnrealEditor.modules`, size: 0, sha256: "",
      text: JSON.stringify({ BuildId: buildId, Modules: Object.fromEntries(modules.map((m) => [m, `UnrealEditor-${m}.dll`])) }, null, "\t"),
    },
    ...modules.flatMap((m) => ["dll", "pdb"].map((ext) => ({ path: `Binaries/${PLATFORM}/UnrealEditor-${m}.${ext}`, size: 0, sha256: "" }))),
    ...fs.readdirSync(path.join(src, "Resources")).map((f) => ({ path: `Resources/${f}`, size: 0, sha256: "" })),
  ];
  return { version, zip: "", zipSha256: "", files };
}

/** The bytes a fixture file stands for. */
function contentOf(release: Release, f: FixtureFile): Buffer {
  if (f.text !== undefined) return Buffer.from(f.text);
  const tag = `${release.version}:${f.path}`;
  return f.path.endsWith(".dll") ? stubModule(tag) : Buffer.from(`\nue-mcp-stub:${tag}\n`);
}

const tar = process.platform === "win32"
  ? path.join(process.env.SystemRoot ?? process.env.SYSTEMROOT ?? "C:\\Windows", "System32", "tar.exe")
  : "tar";

/** Lay `release` out as a plugin tree at `dir`, from its cached zip or its fixture. */
function layOut(release: Release, dir: string): void {
  fs.mkdirSync(dir, { recursive: true });
  if (USE_ZIPS && release.zip) {
    const x = `${dir}.x`;
    fs.mkdirSync(x, { recursive: true });
    execFileSync(tar, ["-xf", path.join(ZIP_CACHE, release.zip), "-C", x]);
    fs.cpSync(path.join(x, "UE_MCP_Bridge"), dir, { recursive: true });
    fs.rmSync(x, { recursive: true, force: true });
    return;
  }
  for (const f of release.files) {
    fs.mkdirSync(path.dirname(path.join(dir, f.path)), { recursive: true });
    fs.writeFileSync(path.join(dir, f.path), contentOf(release, f));
  }
}

/** True when the file at `full` is the one `release` ships at `rel`. */
function isFrom(release: Release, rel: string, full: string): boolean {
  const f = release.files.find((x) => x.path === rel);
  if (!f) return false;
  const bytes = fs.readFileSync(full);
  if (USE_ZIPS && release.zip) return crypto.createHash("sha256").update(bytes).digest("hex") === f.sha256;
  if (f.text !== undefined) return bytes.toString() === f.text;
  return (stubTag(bytes) ?? bytes.toString().trim().replace(/^ue-mcp-stub:/, "")) === `${release.version}:${f.path}`;
}

const releases = loadReleases();
const newest = releases[releases.length - 1];
const targets: Release[] = [newest, currentLayout(newest)];

let fixture: ProjectFixture;
let holder: DllHolder | null = null;
const saved = { ...process.env };

beforeAll(() => {
  if (!USE_ZIPS) return;
  for (const r of releases) {
    const zip = path.join(ZIP_CACHE, r.zip);
    if (!fs.existsSync(zip)) throw new Error(`${r.zip} is not cached; run node scripts/bridge-upgrade-fixtures.mjs`);
    expect(crypto.createHash("sha256").update(fs.readFileSync(zip)).digest("hex")).toBe(r.zipSha256);
  }
});

afterEach(async () => {
  await holder?.release();
  holder = null;
  for (const k of Object.keys(process.env)) if (!(k in saved)) delete process.env[k];
  Object.assign(process.env, saved);
  fixture?.cleanup();
});

/** A project with `from` installed as a binary bridge, the way `ue-mcp init` leaves it. */
function installed(from: Release): { uproject: string; pluginDir: string } {
  fixture = new ProjectFixture("ue-mcp-upgrade-");
  for (const k of ["HOME", "USERPROFILE"]) process.env[k] = fixture.root;
  process.env.APPDATA = path.join(fixture.root, "AppData");
  delete process.env.UE_MCP_BINARIES;
  const uproject = fixture.makeProject("Game", { engine: ENGINE });
  const pluginDir = path.join(path.dirname(uproject), "Plugins", "UE_MCP_Bridge");
  layOut(from, pluginDir);
  writeInstallMarker(pluginDir, { kind: "binary", engine: ENGINE, platform: PLATFORM, version: from.version });
  return { uproject, pluginDir };
}

/** Where applyUpdate fetches `to` from: its cached release zip, or a plugin tree. */
function binariesFor(to: Release): string {
  if (USE_ZIPS && to.zip) return path.join(ZIP_CACHE, to.zip);
  const dir = path.join(fixture.root, "release", binaryArchiveName({ version: to.version, engine: ENGINE, platform: PLATFORM }), "UE_MCP_Bridge");
  layOut(to, dir);
  return dir;
}

function targetFiles(to: Release): string[] {
  return [...to.files.map((f) => f.path), INSTALL_MARKER_NAME];
}

/** Every active file is the target's, byte for byte. */
function expectTargetContent(to: Release, pluginDir: string): void {
  for (const f of to.files) expect(isFrom(to, f.path, path.join(pluginDir, f.path)), f.path).toBe(true);
}

for (const to of targets) {
  const starts = releases.filter((r) => r.version !== to.version);
  describe(`upgrade to ${to.version}`, () => {
    it.each(starts.map((r) => [r.version, r] as const))("from %s with the editor down", async (_v, from) => {
      const { uproject, pluginDir } = installed(from);
      const result = await applyUpdate(uproject, { version: to.version, editorRunning: false, binaries: binariesFor(to) });

      expect(result, result.error).toMatchObject({ ok: true, state: "applied", kind: "binary" });
      expect(verifyBridgeTree(pluginDir, { version: to.version, platform: PLATFORM, files: targetFiles(to) })).toEqual([]);
      expect(treeFiles(pluginDir).filter((f) => ASIDE_FILE.test(f))).toEqual([]);
      expect(readPendingUpdate(pluginDir)).toBeNull();
      expectTargetContent(to, pluginDir);
    }, 120_000);

    it.each(starts.map((r) => [r.version, r] as const))("from %s with the editor running", async (_v, from) => {
      const { uproject, pluginDir } = installed(from);
      const oldDlls = treeFiles(pluginDir).filter((f) => f.endsWith(".dll")).map((f) => path.join(pluginDir, f));
      if (HOLD) {
        holder = new DllHolder();
        await holder.hold(oldDlls);
        // The precondition the in-place swap exists for.
        expect(() => fs.writeFileSync(oldDlls[0], "x")).toThrow();
        expect(() => fs.rmSync(oldDlls[0])).toThrow();
      }

      const result = await applyUpdate(uproject, { version: to.version, editorRunning: true, platform: "win32", binaries: binariesFor(to) });

      expect(result, result.error).toMatchObject({ ok: true, state: "applied", kind: "binary" });
      const pending = readPendingUpdate(pluginDir);
      expect(pending?.state).toBe("swapped");
      expect(verifyBridgeTree(pluginDir, { version: to.version, platform: PLATFORM, files: targetFiles(to), asideFiles: pending!.oldFiles })).toEqual([]);
      expect(readInstallMarker(pluginDir)).toEqual({ kind: "binary", engine: ENGINE, platform: PLATFORM, version: to.version });
      expectTargetContent(to, pluginDir);
      // Every aside file is one of the old release's files, renamed.
      for (const aside of pending!.oldFiles) {
        const original = path.relative(pluginDir, aside.replace(ASIDE_FILE, "")).replace(/\\/g, "/");
        expect(original === INSTALL_MARKER_NAME || isFrom(from, original, aside), aside).toBe(true);
      }
      // Every old DLL the editor held went aside rather than being lost.
      for (const dll of oldDlls) {
        expect(pending!.oldFiles.some((a) => a.replace(ASIDE_FILE, "") === dll), dll).toBe(true);
      }

      // While held, settling keeps the aside files; once the editor exits it removes them.
      if (HOLD) {
        expect(settlePendingUpdate(pluginDir, true)).toBe(false);
        await holder!.release();
      }
      expect(settlePendingUpdate(pluginDir, false)).toBe(true);
      expect(treeFiles(pluginDir).filter((f) => ASIDE_FILE.test(f))).toEqual([]);
      expect(verifyBridgeTree(pluginDir, { version: to.version, platform: PLATFORM, files: targetFiles(to) })).toEqual([]);
    }, 120_000);
  });
}

/** Every directory under Plugins/ holding a UE_MCP_Bridge.uplugin: what the editor would discover. */
function discoverable(uproject: string): string[] {
  const plugins = path.join(path.dirname(uproject), "Plugins");
  return treeFiles(plugins).filter((f) => /(^|\/)UE_MCP_Bridge\.uplugin$/i.test(f)).map((f) => path.posix.dirname(f));
}

function holdDlls(dir: string): Promise<void> {
  holder = new DllHolder();
  return holder.hold(treeFiles(dir).filter((f) => f.endsWith(".dll")).map((f) => path.join(dir, f)));
}

async function releaseHeld(): Promise<void> {
  await holder?.release();
  holder = null;
}

describe("an editor the updater missed (editorRunning false while it maps the old DLLs)", () => {
  it("installs the new tree and moves the old one out of Plugins/, where the editor cannot find it", async () => {
    const from = releases[0];
    const { uproject, pluginDir } = installed(from);
    if (HOLD) await holdDlls(pluginDir);

    const result = await applyUpdate(uproject, { version: newest.version, editorRunning: false, binaries: binariesFor(newest) });

    expect(result, result.error).toMatchObject({ ok: true, state: "applied" });
    expect(verifyBridgeTree(pluginDir, { version: newest.version, platform: PLATFORM, files: targetFiles(newest) })).toEqual([]);
    expectTargetContent(newest, pluginDir);
    expect(discoverable(uproject)).toEqual(["UE_MCP_Bridge"]);

    await releaseHeld();
    expect(settlePendingUpdate(pluginDir, false)).toBe(true);
    expect(fs.existsSync(path.join(path.dirname(uproject), "Intermediate", "UE_MCP_Update"))).toBe(false);
  }, 120_000);
});

describe("copies earlier releases left under Plugins/", () => {
  it.each(["UE_MCP_Bridge.old-4242", "UE_MCP_Bridge.staging-4242", "UE_MCP_Bridge.pending-update"])(
    "takes %s out of the editor's sight on the next update, even while it is mapped",
    async (name) => {
      const from = releases[0];
      const { uproject, pluginDir } = installed(from);
      const spare = path.join(path.dirname(pluginDir), name);
      layOut(from, spare);
      if (HOLD) await holdDlls(spare);
      expect(discoverable(uproject).sort()).toEqual(["UE_MCP_Bridge", name].sort());

      const result = await applyUpdate(uproject, { version: newest.version, editorRunning: true, platform: "win32", binaries: binariesFor(newest) });

      expect(result, result.error).toMatchObject({ ok: true, state: "applied" });
      expect(discoverable(uproject)).toEqual(["UE_MCP_Bridge"]);
    },
    120_000,
  );
});

describe("two updates while one editor stays up", () => {
  it("keeps tracking the first update's aside files, so the second leaves none behind", async () => {
    const from = releases.find((r) => r.version === "1.4.10") ?? releases[0];
    const mid = releases[releases.length - 2];
    const { uproject, pluginDir } = installed(from);
    if (HOLD) await holdDlls(pluginDir);

    const first = await applyUpdate(uproject, { version: mid.version, editorRunning: true, platform: "win32", binaries: binariesFor(mid) });
    expect(first, first.error).toMatchObject({ ok: true, state: "applied" });
    const second = await applyUpdate(uproject, { version: newest.version, editorRunning: true, platform: "win32", binaries: binariesFor(newest) });
    expect(second, second.error).toMatchObject({ ok: true, state: "applied" });

    const pending = readPendingUpdate(pluginDir)!;
    expect(verifyBridgeTree(pluginDir, { version: newest.version, platform: PLATFORM, files: targetFiles(newest), asideFiles: pending.oldFiles })).toEqual([]);
    await releaseHeld();
    expect(settlePendingUpdate(pluginDir, false)).toBe(true);
    expect(treeFiles(pluginDir).filter((f) => ASIDE_FILE.test(f))).toEqual([]);
  }, 120_000);
});

describe("a release that drops files", () => {
  it("retires every file the old release had and the new one does not, with the editor running", async () => {
    const { uproject, pluginDir } = installed(newest);
    // An older layout's leftovers: a retired module and the Python bridge's content (#1273).
    const retired = path.join(pluginDir, "Binaries", PLATFORM, "UnrealEditor-UE_MCP_Retired.dll");
    fs.writeFileSync(retired, stubModule("retired"));
    fs.mkdirSync(path.join(pluginDir, "Content", "Python"), { recursive: true });
    fs.writeFileSync(path.join(pluginDir, "Content", "Python", "init_unreal.py"), "print()");
    if (HOLD) {
      holder = new DllHolder();
      await holder.hold([retired]);
    }
    const to = targets[1];

    const result = await applyUpdate(uproject, { version: to.version, editorRunning: true, platform: "win32", binaries: binariesFor(to) });

    expect(result, result.error).toMatchObject({ ok: true, state: "applied" });
    const pending = readPendingUpdate(pluginDir)!;
    expect(verifyBridgeTree(pluginDir, { version: to.version, platform: PLATFORM, files: targetFiles(to), asideFiles: pending.oldFiles })).toEqual([]);
    await releaseHeld();
    expect(settlePendingUpdate(pluginDir, false)).toBe(true);
    expect(fs.existsSync(path.join(pluginDir, "Content"))).toBe(false);
    expect(fs.existsSync(retired)).toBe(false);
  }, 120_000);
});
