/**
 * Checkpoints through a spawned daemon (#1482): the toggle from ue-mcp.yml and
 * the per-session override, the /v1/checkpoints routes, their events, the
 * bridge recording contract (checkpoint_arm hands the editor a pending
 * generation; the editor copies a package there before saving it), a subset
 * restore with the editor told what to reload, and a project .git that no
 * action touches. The editor is a fake bridge that writes what the C++
 * recorder writes.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeBridge } from "../fake-bridge.js";
import { McpServer, Sandbox, json, stopSandboxDaemons, waitFor } from "./harness.js";
import { base, bearer, call, readSse, startDaemonCli, type Discovery } from "./daemon-fakes.js";

type Json = Record<string, any>;

let sb: Sandbox;
let bridge: FakeBridge;
let d: Discovery;
let project: string;
let dir: string;
/** The generation the fake editor was last armed with, or null when disarmed. */
let armed: string | null = null;
const prepared: string[][] = [];
const finished: Json[][] = [];
/** What the fake editor read from each version it was handed to diff, and the native diffs it was asked to open. */
const diffed: Array<{ original: string; from: string | null; to: string | null }> = [];
const opened: Json[] = [];
const PNG = Buffer.from("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+ip1sAAAAASUVORK5CYII=", "base64");

/** What checkpoint_diff_asset answers: the versions it was handed, read back, and a picture written for each one asked for. */
function fakeAssetDiff(p: Json): Json {
  const readSide = (f: unknown) => (typeof f === "string" && fs.existsSync(f) ? fs.readFileSync(f, "utf8") : null);
  const from = readSide(p.fromFile);
  const to = readSide(p.toFile);
  diffed.push({ original: path.basename(String(p.originalFile)), from, to });
  const side = (content: string | null, thumb: unknown) => {
    if (content === null) return null;
    if (typeof thumb === "string") fs.writeFileSync(thumb, PNG);
    return { class: "Blueprint", facts: [{ name: "Content", value: content }], ...(typeof thumb === "string" ? { thumbnail: { file: thumb, source: "rendered" } } : {}) };
  };
  return {
    success: true,
    packageName: `/Game/${path.basename(String(p.originalFile), ".uasset")}`,
    status: from === null ? "added" : to === null ? "deleted" : "modified",
    assetClass: "Blueprint",
    engine: { tool: "SBlueprintDiff", note: "fake", native: "window", nativeAvailable: true },
    before: side(from, p.fromThumbnail),
    after: side(to, p.toThumbnail),
    sections: from && to ? [{
      category: "Defaults",
      kind: "properties",
      engineSource: "DiffUtils::CompareUnrelatedObjects",
      entries: [{ displayString: "Content changed value in Door", diffType: "PropertyValueChanged", path: "Content", property: "Content", before: from, after: to }],
    }] : [],
    recordCount: from && to ? 1 : 0,
  };
}

const file = (rel: string) => path.join(dir, rel);
const write = (rel: string, content: string) => {
  fs.mkdirSync(path.dirname(file(rel)), { recursive: true });
  fs.writeFileSync(file(rel), content);
};
const read = (rel: string) => fs.readFileSync(file(rel), "utf8");

/** What the bridge's PreSavePackageWithContextEvent hook does, then the save itself. */
function editorSave(rel: string, content: string): void {
  if (!armed) throw new Error("the fake editor was never armed");
  const manifest = path.join(armed, "manifest.jsonl");
  const n = fs.existsSync(manifest) ? fs.readFileSync(manifest, "utf8").split("\n").filter(Boolean).length : 0;
  const existed = fs.existsSync(file(rel));
  if (existed) fs.copyFileSync(file(rel), path.join(armed, `${n}.bin`));
  fs.appendFileSync(manifest, `${JSON.stringify({ path: file(rel), existed, ...(existed ? { copy: `${n}.bin` } : {}) })}\n`);
  write(rel, content);
}

const gitSnapshot = () => {
  const out: Record<string, string> = {};
  for (const f of fs.readdirSync(file(".git"), { recursive: true }).map(String)) {
    const abs = path.join(file(".git"), f);
    if (fs.statSync(abs).isFile()) out[f] = fs.readFileSync(abs, "utf8");
  }
  return out;
};

beforeAll(async () => {
  sb = new Sandbox();
  project = sb.makeProject("Turns");
  dir = path.dirname(project);
  write(".git/HEAD", "ref: refs/heads/main\n");
  write(".git/config", "[core]\n\tbare = false\n");
  write("Config/DefaultGame.ini", "[/Script/EngineSettings.GeneralProjectSettings]\nProjectName=Turns\n");
  write("Content/BP_Door.uasset", "door v1");
  write("Content/M_Wall.uasset", "wall v1");
  bridge = await FakeBridge.start({
    projectDir: dir,
    handlers: {
      checkpoint_arm: (p) => {
        armed = p.enabled === true ? String(p.pendingDir) : null;
        return { success: true, armed: p.enabled === true, dirtyPackages: ["/Game/Maps/Unsaved"] };
      },
      checkpoint_prepare_restore: (p) => {
        prepared.push((p.files as string[]) ?? []);
        return { success: true, closedEditors: [] };
      },
      checkpoint_finish_restore: (p) => {
        const files = (p.files as Json[]) ?? [];
        finished.push(files);
        return {
          success: true,
          reloaded: files.filter((f) => f.kind === "package" && f.exists).map((f) => path.basename(String(f.file))),
          unloaded: files.filter((f) => f.kind === "package" && !f.exists).map((f) => path.basename(String(f.file))),
          configsReloaded: files.filter((f) => f.kind === "config").map((f) => path.basename(String(f.file), ".ini")),
          reopened: [],
        };
      },
      checkpoint_diff_blueprint: () => ({ success: true, isBlueprint: false }),
      checkpoint_diff_asset: (p) => (Array.isArray(p.items) ? { success: true, results: (p.items as Json[]).map(fakeAssetDiff) } : fakeAssetDiff(p)),
      checkpoint_open_native_diff: (p) => {
        opened.push({ ...p, fromContent: typeof p.fromFile === "string" && fs.existsSync(p.fromFile) ? fs.readFileSync(p.fromFile, "utf8") : null });
        return { success: true, opened: "window", windowTitle: `Diff of ${path.basename(String(p.originalFile))}` };
      },
    },
  });
  d = await startDaemonCli(sb, project, { UE_MCP_PORT: undefined });
  await waitFor(async () => (await call(d, "GET", "/v1/health")).body.editors[0]?.connected === true, 15_000, "the daemon to reach the fake editor");
}, 60_000);

afterAll(async () => {
  await stopSandboxDaemons(sb);
  await bridge?.stop();
  sb.cleanup();
});

describe("checkpoints through the daemon", () => {
  let start: string;
  let end: string;
  let gitBefore: Record<string, string>;

  it("are off by default: no repository, no armed editor, and creating one is refused", async () => {
    covers("config.checkpoints");
    gitBefore = gitSnapshot();
    const health = await call(d, "GET", "/v1/health");
    expect(health.body.features).toContain("checkpoints");

    const state = await call(d, "GET", "/v1/checkpoints");
    expect(state.status).toBe(200);
    expect(state.body).toMatchObject({ enabled: false, source: "config", configured: false, repository: null, checkpoints: [] });
    const refused = await call(d, "POST", "/v1/checkpoints", { label: "nope" });
    expect(refused.status).toBe(409);
    expect(fs.existsSync(file("Saved/UE_MCP_Bridge/shadow.git"))).toBe(false);
    expect(bridge.calls.some((c) => c.method === "checkpoint_arm" && c.params.enabled === true)).toBe(false);
  });

  it("turn on per session through the daemon, which arms the editor", async () => {
    covers("config.checkpoints", "daemon.checkpoints");
    const before = (await call(d, "GET", "/v1/health")).body.lastEventId as number;
    const stream = readSse(`${base(d)}/v1/events`, { ...bearer(d), "last-event-id": String(before) }, (es) => es.some((e) => e.type === "checkpoints.toggled"));
    const on = await call(d, "PUT", "/v1/checkpoints/enabled", { enabled: true });
    expect(on.status).toBe(200);
    expect(on.body).toMatchObject({ enabled: true, source: "session", configured: false });
    const toggled = (await stream).find((e) => e.type === "checkpoints.toggled")!;
    expect(toggled.data).toMatchObject({ enabled: true, source: "session" });
    expect(armed).not.toBeNull();
    expect(path.relative(dir, armed!).split(path.sep).slice(0, 3)).toEqual(["Saved", "UE_MCP_Bridge", "shadow-pending"]);
    expect((await call(d, "PUT", "/v1/checkpoints/enabled", { enabled: "yes" })).status).toBe(400);
  });

  it("record a turn's editor saves and server writes and list what changed", async () => {
    covers("daemon.checkpoints");
    const a = await call(d, "POST", "/v1/checkpoints", { label: "turn start" });
    expect(a.status, JSON.stringify(a.body)).toBe(200);
    expect(a.body).toMatchObject({ label: "turn start", trigger: "client", editorConnected: true, dirtyPackages: ["/Game/Maps/Unsaved"] });
    start = a.body.id;
    expect(start).toMatch(/^[0-9a-f]{40}$/);

    // The turn: two package saves in the editor and a config and a source write through the server.
    editorSave("Content/BP_Door.uasset", "door v2");
    editorSave("Content/M_Wall.uasset", "wall v2");
    editorSave("Config/DefaultGame.ini", "[/Script/EngineSettings.GeneralProjectSettings]\nProjectName=Turns2\n");
    const server = await McpServer.start(sb, { projects: [project], mode: "daemon", port: null });
    try {
      const wrote = await server.call("tools", { action: "call", category: "project", method: "write_cpp_file", args: { path: "Turns/Door.h", content: "#pragma once\n" } });
      expect(wrote.isError, wrote.text).toBe(false);
    } finally {
      await server.client.close();
    }

    const before = (await call(d, "GET", "/v1/health")).body.lastEventId as number;
    const stream = readSse(`${base(d)}/v1/events`, { ...bearer(d), "last-event-id": String(before) }, (es) => es.some((e) => e.type === "checkpoint.created"));
    const b = await call(d, "POST", "/v1/checkpoints", { label: "turn end" });
    expect(b.status, JSON.stringify(b.body)).toBe(200);
    end = b.body.id;
    expect((b.body.changes as Json[]).map((c) => [c.path, c.status, c.kind])).toEqual([
      ["Config/DefaultGame.ini", "modified", "config"],
      ["Content/BP_Door.uasset", "modified", "package"],
      ["Content/M_Wall.uasset", "modified", "package"],
      ["Source/Turns/Door.h", "added", "source"],
    ]);
    const created = (await stream).find((e) => e.type === "checkpoint.created")!;
    expect(created.data).toMatchObject({ id: end, label: "turn end", trigger: "client", files: 4 });

    const changes = await call(d, "GET", `/v1/checkpoints/changes?from=${start.slice(0, 8)}&to=${end}`);
    expect(changes.status).toBe(200);
    const ini = (changes.body.changes as Json[]).find((c) => c.path === "Config/DefaultGame.ini")!;
    expect(ini.diff).toContain("-ProjectName=Turns\n");
    expect(ini.diff).toContain("+ProjectName=Turns2\n");

    const list = await call(d, "GET", "/v1/checkpoints?limit=10");
    expect((list.body.checkpoints as Json[]).map((c) => c.label)).toEqual(["turn end", "turn start"]);
    expect(list.body.repository).toBe(file("Saved/UE_MCP_Bridge/shadow.git"));
  });

  it("give each package an asset diff from the editor, with a picture of each version the daemon serves", async () => {
    covers("daemon.checkpoints.asset-diff");
    const health = await call(d, "GET", "/v1/health");
    expect(health.body.features).toContain("checkpoints.assetDiff");
    // The changes listed in the turn above already asked the editor once per package.
    const changes = await call(d, "GET", `/v1/checkpoints/changes?from=${start}&to=${end}`);
    expect(changes.status).toBe(200);
    expect(changes.body.assetDiffs).toEqual({ limit: 25, diffed: 2, deferred: [] });
    // The editor was handed each version's bytes, exported from the shadow history.
    expect(diffed.map((x) => [x.original, x.from, x.to]).sort()).toEqual([["BP_Door.uasset", "door v1", "door v2"], ["M_Wall.uasset", "wall v1", "wall v2"]]);
    const door = (changes.body.changes as Json[]).find((c) => c.path === "Content/BP_Door.uasset")!;
    expect(door.assetDiff).toMatchObject({ state: "ready", status: "modified", assetClass: "Blueprint", nativeDiff: { available: true } });
    expect(door.assetDiff.summary).toBe("1 difference: Content door v1 to door v2");
    const ini = (changes.body.changes as Json[]).find((c) => c.path === "Config/DefaultGame.ini")!;
    expect(ini.assetDiff).toBeUndefined();

    // The pictures, by blob id, served as PNG.
    const thumb = door.assetDiff.before.thumbnail;
    expect(thumb).toMatchObject({ source: "rendered" });
    expect(thumb.id).toMatch(/^[0-9a-f]{40}$/);
    expect(door.assetDiff.after.thumbnail.id).not.toBe(thumb.id);
    const res = await fetch(`${base(d)}${thumb.url}`, { headers: bearer(d) });
    expect(res.status).toBe(200);
    expect(res.headers.get("content-type")).toBe("image/png");
    expect(Buffer.from(await res.arrayBuffer()).equals(PNG)).toBe(true);
    expect((await fetch(`${base(d)}/v1/checkpoints/thumbnails/${"0".repeat(40)}.png`, { headers: bearer(d) })).status).toBe(404);

    // The same two versions again: answered from the cache, the same answer, the editor not asked.
    const again = await call(d, "GET", `/v1/checkpoints/asset-diff?from=${start}&to=${end}&path=Content/BP_Door.uasset`);
    expect(again.status).toBe(200);
    expect(again.body).toEqual(door.assetDiff);
    expect(diffed).toHaveLength(2);
    expect((await call(d, "GET", `/v1/checkpoints/asset-diff?from=${start}&to=${end}&path=Config/DefaultGame.ini`)).status).toBe(400);
    expect((await call(d, "GET", `/v1/checkpoints/asset-diff?from=${start}&to=${end}`)).status).toBe(400);

    // Unreal's own diff: both versions exported, labelled with the checkpoints.
    const native = await call(d, "POST", "/v1/checkpoints/native-diff", { path: "Content/BP_Door.uasset", from: start, to: end });
    expect(native.status, JSON.stringify(native.body)).toBe(200);
    expect(native.body).toMatchObject({ opened: "window", windowTitle: "Diff of BP_Door.uasset", path: "Content/BP_Door.uasset" });
    expect(opened.at(-1)).toMatchObject({ fromContent: "door v1", fromLabel: `Checkpoint ${start.slice(0, 7)}: turn start`, toLabel: `Checkpoint ${end.slice(0, 7)}: turn end` });
    // The diff window dates each side with its checkpoint's time, not an unset date (#1606).
    const taken = new Map(((await call(d, "GET", "/v1/checkpoints")).body.checkpoints as Json[]).map((c) => [c.id, c.createdAt]));
    expect(opened.at(-1)).toMatchObject({ fromTime: taken.get(start), toTime: taken.get(end) });
    expect(Number.isNaN(Date.parse(String(opened.at(-1)!.fromTime)))).toBe(false);
    const current = await call(d, "POST", "/v1/checkpoints/native-diff", { path: "Content/BP_Door.uasset", from: start });
    expect(current.status).toBe(200);
    expect(opened.at(-1)).toMatchObject({ current: true, toLabel: "Current", fromTime: taken.get(start) });
    expect(opened.at(-1)!.toTime).toBeUndefined();
    expect(opened.at(-1)!.toFile).toBeUndefined();
  });

  it("restore a subset to the turn's start, tell the editor what to reload, and keep the rest", async () => {
    covers("daemon.checkpoints");
    const before = (await call(d, "GET", "/v1/health")).body.lastEventId as number;
    const stream = readSse(`${base(d)}/v1/events`, { ...bearer(d), "last-event-id": String(before) }, (es) => es.some((e) => e.type === "checkpoint.restored"));
    const r = await call(d, "POST", "/v1/checkpoints/restore", {
      checkpoint: start,
      paths: ["Content/BP_Door.uasset", "Config/DefaultGame.ini", "Source/Turns/Door.h"],
    });
    expect(r.status, JSON.stringify(r.body)).toBe(200);
    expect(r.body.refused).toEqual([]);
    expect(r.body.restored).toEqual([
      { path: "Config/DefaultGame.ini", action: "written" },
      { path: "Content/BP_Door.uasset", action: "written" },
      { path: "Source/Turns/Door.h", action: "deleted" },
    ]);
    expect(read("Content/BP_Door.uasset")).toBe("door v1");
    expect(read("Config/DefaultGame.ini")).toContain("ProjectName=Turns\n");
    expect(fs.existsSync(file("Source/Turns/Door.h"))).toBe(false);
    expect(read("Content/M_Wall.uasset")).toBe("wall v2");
    expect(r.body.reloaded).toEqual(["BP_Door.uasset"]);
    expect(r.body.configsReloaded).toEqual(["DefaultGame"]);

    // The editor was told before the write and after it, with the same files.
    expect(prepared.at(-1)!.map((f) => path.basename(f)).sort()).toEqual(["BP_Door.uasset", "DefaultGame.ini", "Door.h"]);
    expect(finished.at(-1)!.map((f) => [path.basename(String(f.file)), f.exists])).toEqual([
      ["DefaultGame.ini", true],
      ["BP_Door.uasset", true],
      ["Door.h", false],
    ]);
    const restored = (await stream).find((e) => e.type === "checkpoint.restored")!;
    expect(restored.data).toMatchObject({ checkpoint: start, before: r.body.before, after: r.body.after });

    // The restore is checkpointed, so restoring to `before` undoes it.
    const undo = await call(d, "POST", "/v1/checkpoints/restore", { checkpoint: r.body.before });
    expect(undo.status, JSON.stringify(undo.body)).toBe(200);
    expect(read("Content/BP_Door.uasset")).toBe("door v2");
    expect(read("Source/Turns/Door.h")).toBe("#pragma once\n");
  });

  it("refuse a path edited outside the agent unless forced", async () => {
    covers("daemon.checkpoints");
    write("Content/M_Wall.uasset", "wall edited by hand");
    const now = await call(d, "GET", `/v1/checkpoints/changes?from=${start}`);
    expect((now.body.changes as Json[]).find((c) => c.path === "Content/M_Wall.uasset")).toMatchObject({ untracked: true });
    const r = await call(d, "POST", "/v1/checkpoints/restore", { checkpoint: start, paths: ["Content/M_Wall.uasset"] });
    expect(r.body.refused).toEqual([expect.objectContaining({ path: "Content/M_Wall.uasset", reason: "edited-outside" })]);
    expect(read("Content/M_Wall.uasset")).toBe("wall edited by hand");
    const forced = await call(d, "POST", "/v1/checkpoints/restore", { checkpoint: start, paths: ["Content/M_Wall.uasset"], force: true });
    expect(forced.body.restored).toEqual([{ path: "Content/M_Wall.uasset", action: "written" }]);
    expect(read("Content/M_Wall.uasset")).toBe("wall v1");
  });

  it("diff the first 25 packages of a large turn and name the rest, each diffed on request", async () => {
    covers("daemon.checkpoints.asset-diff");
    const a = (await call(d, "POST", "/v1/checkpoints", { label: "big start" })).body.id as string;
    for (let i = 0; i < 27; i++) editorSave(`Content/Bulk/B_${String(i).padStart(2, "0")}.uasset`, `bulk ${i}`);
    const b = (await call(d, "POST", "/v1/checkpoints", { label: "big end" })).body.id as string;
    const sent = bridge.calls.length;
    const r = await call(d, "GET", `/v1/checkpoints/changes?from=${a}&to=${b}`);
    expect(r.body.assetDiffs.diffed).toBe(25);
    // One editor call for all 25, so the editor unloads the versions, a full garbage collection, once.
    const diffCalls = bridge.calls.slice(sent).filter((c) => c.method === "checkpoint_diff_asset");
    expect(diffCalls).toHaveLength(1);
    expect(diffCalls[0].params.items).toHaveLength(25);
    expect(r.body.assetDiffs.deferred).toEqual(["Content/Bulk/B_25.uasset", "Content/Bulk/B_26.uasset"]);
    const late = (r.body.changes as Json[]).find((c) => c.path === "Content/Bulk/B_26.uasset")!;
    expect(late.assetDiff).toMatchObject({ state: "deferred" });
    expect(late.assetDiff.summary).toMatch(/Not diffed yet/);
    const one = await call(d, "GET", `/v1/checkpoints/asset-diff?from=${a}&to=${b}&path=Content/Bulk/B_26.uasset`);
    expect(one.body).toMatchObject({ state: "ready", status: "added", before: null });
    expect(one.body.after.facts).toEqual([{ name: "Content", value: "bulk 26" }]);
    expect(one.body.summary).toMatch(/^New Blueprint/);
  });

  it("answer bad input with 400 and an unknown editor with 404", async () => {
    covers("daemon.checkpoints");
    expect((await call(d, "GET", "/v1/checkpoints/changes")).status).toBe(400);
    expect((await call(d, "GET", "/v1/checkpoints/changes?from=deadbeef")).status).toBe(400);
    expect((await call(d, "POST", "/v1/checkpoints/restore", {})).status).toBe(400);
    expect((await call(d, "GET", "/v1/checkpoints?editor=nobody")).status).toBe(404);
  });

  it("never touched the project's own .git", () => {
    covers("daemon.checkpoints");
    expect(gitSnapshot()).toEqual(gitBefore);
  });

  it("turn off again, disarming the editor and keeping the history, and clear deletes it", async () => {
    covers("config.checkpoints", "daemon.checkpoints");
    const off = await call(d, "PUT", "/v1/checkpoints/enabled", { enabled: null });
    expect(off.body).toMatchObject({ enabled: false, source: "config" });
    expect(armed).toBeNull();
    expect(bridge.calls.filter((c) => c.method === "checkpoint_arm").at(-1)!.params).toEqual({ enabled: false });
    expect(off.body.checkpoints.length).toBeGreaterThan(0);

    const cleared = await call(d, "DELETE", "/v1/checkpoints");
    expect(cleared.body).toMatchObject({ cleared: true });
    expect(fs.existsSync(file("Saved/UE_MCP_Bridge/shadow.git"))).toBe(false);
    expect((await call(d, "GET", "/v1/checkpoints")).body.repository).toBeNull();
  });
});

describe("checkpoints: on in ue-mcp.yml", () => {
  it("turns them on for the project, and editor(checkpoint) records with no editor running", async () => {
    covers("config.checkpoints", "server.checkpoints");
    const other = sb.makeProject("Configured", { config: "ue-mcp:\n  checkpoints: on\n" });
    const otherDir = path.dirname(other);
    fs.mkdirSync(path.join(otherDir, "Source"), { recursive: true });
    const server = await McpServer.start(sb, { projects: [other], mode: "in-process" });
    try {
      const listed = await server.call("tools", { action: "call", category: "editor", method: "list_checkpoints", args: {} });
      expect(listed.isError, listed.text).toBe(false);
      expect(json(listed)).toMatchObject({ enabled: true, source: "config", configured: true });
      const a = await server.call("tools", { action: "call", category: "editor", method: "checkpoint", args: { label: "offline start" } });
      expect(a.isError, a.text).toBe(false);
      const id = json<Json>(a).id as string;
      expect(json<Json>(a).editorConnected).toBe(false);
      const wrote = await server.call("tools", { action: "call", category: "project", method: "write_cpp_file", args: { path: "Offline.h", content: "// one\n" } });
      expect(wrote.isError, wrote.text).toBe(false);
      const changes = await server.call("tools", { action: "call", category: "editor", method: "list_changes", args: { from: id } });
      expect(json<Json>(changes).changes).toEqual([expect.objectContaining({ path: "Source/Offline.h", status: "added", kind: "source" })]);
      const restored = await server.call("tools", { action: "call", category: "editor", method: "restore", args: { checkpoint: id } });
      expect(restored.isError, restored.text).toBe(false);
      expect(json<Json>(restored).restored).toEqual([{ path: "Source/Offline.h", action: "deleted" }]);
      expect(fs.existsSync(path.join(otherDir, "Source", "Offline.h"))).toBe(false);
      expect(fs.existsSync(path.join(otherDir, "Saved", "UE_MCP_Bridge", "shadow.git", "HEAD"))).toBe(true);
    } finally {
      await server.close();
    }
  }, 60_000);
});
