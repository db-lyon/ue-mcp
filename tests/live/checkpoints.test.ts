// @live-editor headless
/**
 * Checkpoints against a real editor (#1482).
 *
 * The bridge half first, method by method: checkpoint_arm makes the editor
 * copy a package before its first save, checkpoint_prepare_restore and
 * checkpoint_finish_restore put the copy back and reload it, and
 * checkpoint_diff_blueprint diffs a checkpoint's copy against the loaded
 * asset. Then the whole turn through the shipped server: a Blueprint, a
 * material, a level and a config file edited in one group, checkpointed, and
 * a subset restored, with the restored files byte-identical to the checkpoint,
 * the rest keeping the agent's edits, and the editor showing both without a
 * restart.
 *
 * Checkpoints are turned on for the server through ue-mcp.local.yml in the
 * target project, put back as it was afterwards.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import { LiveServer, resultJson, type LiveCallResult } from "./server.js";
import { closeLiveBridges, liveTarget } from "./harness.js";

type Json = Record<string, any>;
const DIR = "/Game/MCPTest/Checkpoints";
const BP = `${DIR}/BP_Ck`;
const BP_TURN = `${DIR}/BP_CkTurn`;
const MAT = `${DIR}/M_CkTurn`;
const LEVEL = `${DIR}/L_CkTurn`;
const INI_SECTION = "/Script/UEMCP.CheckpointLiveTest";

const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});

let bridge: EditorBridge;
let projectDir: string;
let server: LiveServer | null = null;
let localYml: string;
let localYmlBefore: string | null = null;

const fileOf = (assetPath: string, ext = ".uasset") => path.join(projectDir, "Content", `${assetPath.replace(/^\/Game\//, "")}${ext}`);

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(r.ok && body(r).success !== false, `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

function manifest(dir: string): Json[] {
  const file = path.join(dir, "manifest.jsonl");
  if (!fs.existsSync(file)) return [];
  return fs.readFileSync(file, "utf8").split("\n").filter(Boolean).map((l) => JSON.parse(l) as Json);
}

async function variables(assetPath: string): Promise<string[]> {
  const listed = await call("list_blueprint_variables", { assetPath });
  return ((listed.variables as Json[]) ?? []).map((v) => String(v.name));
}

async function tool(category: string, action: string, args: Json = {}): Promise<Json> {
  const r: LiveCallResult = await server!.call(category, { action, args });
  expect(r.isError, `${category}.${action}: ${r.text}`).toBe(false);
  return resultJson<Json>(r);
}

beforeAll(async () => {
  bridge = await getBridge();
  projectDir = (await liveTarget()).projectDir;
  localYml = path.join(projectDir, "ue-mcp.local.yml");
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
}, 120_000);

afterAll(async () => {
  await server?.close();
  if (bridge) {
    await callBridge(bridge, "checkpoint_arm", { enabled: false });
    await callBridge(bridge, "load_level", { levelPath: "/Game/MCP_Home", discardUnsaved: true });
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
  if (localYml) {
    if (localYmlBefore === null) fs.rmSync(localYml, { force: true });
    else fs.writeFileSync(localYml, localYmlBefore);
  }
  if (projectDir) fs.rmSync(path.join(projectDir, "Saved", "UE_MCP_Bridge", "shadow-live"), { recursive: true, force: true });
  closeLiveBridges();
}, 120_000);

describe("the bridge recorder", () => {
  const gen = (name: string) => path.join(projectDir, "Saved", "UE_MCP_Bridge", "shadow-live", name);
  let stateOne: Buffer;
  let copyOfStateOne: string;

  it("checkpoint_arm records a new package as absent and an existing one as a copy, once per generation", async () => {
    const first = gen("g1");
    const armed = await call("checkpoint_arm", { enabled: true, pendingDir: first });
    expect(armed.armed).toBe(true);
    expect(Array.isArray(armed.dirtyPackages)).toBe(true);

    await call("create_blueprint", { assetPath: BP, parentClass: "Actor" });
    await call("save_asset", { assetPath: BP });
    await call("save_asset", { assetPath: BP, force: true });
    const recorded = manifest(first).filter((r) => String(r.path).endsWith("BP_Ck.uasset"));
    expect(recorded).toHaveLength(1);
    expect(recorded[0].existed).toBe(false);
    stateOne = fs.readFileSync(fileOf(BP));

    const second = gen("g2");
    const rearmed = await call("checkpoint_arm", { enabled: true, pendingDir: second });
    expect(rearmed.previousDir).toBeTruthy();
    await call("add_variable", { assetPath: BP, name: "CkVar", varType: "float" });
    await call("save_asset", { assetPath: BP, force: true });
    const copied = manifest(second).filter((r) => String(r.path).endsWith("BP_Ck.uasset"));
    expect(copied).toHaveLength(1);
    expect(copied[0].existed).toBe(true);
    copyOfStateOne = path.join(second, String(copied[0].copy));
    expect(fs.readFileSync(copyOfStateOne).equals(stateOne)).toBe(true);
    expect(fs.readFileSync(fileOf(BP)).equals(stateOne)).toBe(false);
  }, 120_000);

  it("checkpoint_diff_blueprint diffs a checkpoint's copy against the loaded Blueprint", async () => {
    const fromFile = path.join(projectDir, "Saved", "Diff", "UE_MCP_Checkpoints", "BP_Ck-live.uasset");
    fs.mkdirSync(path.dirname(fromFile), { recursive: true });
    fs.copyFileSync(copyOfStateOne, fromFile);
    try {
      const diff = await call("checkpoint_diff_blueprint", { originalFile: fileOf(BP), fromFile });
      expect(diff.isBlueprint).toBe(true);
      expect(JSON.stringify(diff)).toContain("CkVar");
      const other = await call("checkpoint_diff_blueprint", { originalFile: fileOf(BP), fromFile: path.join(path.dirname(fromFile), "missing.uasset") });
      expect(other.isBlueprint).toBe(false);
    } finally {
      fs.rmSync(fromFile, { force: true });
    }
  }, 120_000);

  it("checkpoint_prepare_restore and checkpoint_finish_restore put a package back and reload it in place", async () => {
    expect(await variables(BP)).toContain("CkVar");
    const prepared = await call("checkpoint_prepare_restore", { files: [fileOf(BP)] });
    expect(prepared.detached).toContain(BP);
    fs.copyFileSync(copyOfStateOne, fileOf(BP));
    const finished = await call("checkpoint_finish_restore", { files: [{ file: fileOf(BP), exists: true, kind: "package" }], reopen: prepared.closedEditors ?? [] });
    expect(finished.reloaded).toContain(BP);
    expect(await variables(BP)).not.toContain("CkVar");
    expect(fs.readFileSync(fileOf(BP)).equals(stateOne)).toBe(true);
  }, 120_000);

  it("checkpoint_finish_restore unloads a package whose file a restore deleted", async () => {
    const prepared = await call("checkpoint_prepare_restore", { files: [fileOf(BP)] });
    expect(prepared.detached).toContain(BP);
    fs.rmSync(fileOf(BP), { force: true });
    const finished = await call("checkpoint_finish_restore", { files: [{ file: fileOf(BP), exists: false, kind: "package" }] });
    expect(finished.unloaded).toContain(BP);
    const found = await callBridge(bridge, "list_blueprint_variables", { assetPath: BP });
    expect(found.ok && body(found).success !== false).toBe(false);
  }, 120_000);

  it("checkpoint_arm with enabled false stops recording", async () => {
    const third = gen("g3");
    await call("checkpoint_arm", { enabled: true, pendingDir: third });
    expect((await call("checkpoint_arm", { enabled: false })).armed).toBe(false);
    await call("create_blueprint", { assetPath: `${DIR}/BP_CkUnarmed`, parentClass: "Actor" });
    await call("save_asset", { assetPath: `${DIR}/BP_CkUnarmed` });
    expect(manifest(third)).toEqual([]);
  }, 120_000);

  it("checkpoint_prepare_restore refuses during PIE", async () => {
    await call("pie_control", { pieAction: "start" });
    try {
      const refused = await callBridge(bridge, "checkpoint_prepare_restore", { files: [], checkOnly: true });
      expect(body(refused).success).toBe(false);
      expect(body(refused).errorCode).toBe("pie_running");
    } finally {
      await callBridge(bridge, "pie_control", { pieAction: "stop" });
    }
  }, 180_000);
});

describe("a turn through the server", () => {
  const ini = () => path.join(projectDir, "Config", "DefaultGame.ini");
  let start: string;
  const before: Record<string, Buffer | null> = {};
  const read = (file: string) => (fs.existsSync(file) ? fs.readFileSync(file) : null);

  beforeAll(async () => {
    localYmlBefore = fs.existsSync(localYml) ? fs.readFileSync(localYml, "utf8") : null;
    if (localYmlBefore !== null && /checkpoints\s*:/.test(localYmlBefore)) throw new Error(`${localYml} already sets checkpoints`);
    const block = localYmlBefore?.includes("ue-mcp:") ? localYmlBefore.replace(/ue-mcp:\s*\n/, "ue-mcp:\n  checkpoints: on\n") : `${localYmlBefore ?? ""}\nue-mcp:\n  checkpoints: on\n`;
    fs.writeFileSync(localYml, block);
    // Fixtures that exist before the turn, saved, so the turn modifies them.
    await call("create_blueprint", { assetPath: BP_TURN, parentClass: "Actor" });
    await call("save_asset", { assetPath: BP_TURN });
    await call("create_material", { name: "M_CkTurn", packagePath: DIR });
    await call("save_asset", { assetPath: MAT });
    await call("create_new_level", { levelPath: LEVEL });
    await call("save_level", {});
    server = await LiveServer.start({ projects: [(await liveTarget()).uproject] });
    await tool("editor", "clear_checkpoints");
  }, 300_000);

  it("records a Blueprint, a material, a level and a config edit in one checkpoint", async () => {
    const state = await tool("editor", "list_checkpoints");
    expect(state).toMatchObject({ enabled: true, source: "config", configured: true });
    start = (await tool("editor", "checkpoint", { label: "live turn start" })).id;
    for (const f of [fileOf(BP_TURN), fileOf(MAT), fileOf(LEVEL, ".umap"), ini()]) before[f] = read(f);

    await tool("blueprint", "add_variable", { assetPath: BP_TURN, name: "TurnVar", varType: "int" });
    await tool("asset", "save", { assetPath: BP_TURN });
    await tool("material", "add_expression", { materialPath: MAT, expressionType: "Constant", name: "CkConst" });
    await tool("asset", "save", { assetPath: MAT });
    await tool("level", "place_actor", { actorClass: "PointLight", label: "CkLight" });
    await tool("level", "save");
    await tool("project", "set_config", { configName: "DefaultGame.ini", section: INI_SECTION, key: "Turn", value: "1" });

    const end = await tool("editor", "checkpoint", { label: "live turn end" });
    const changed = (end.changes as Json[]).map((c) => c.path as string);
    expect(changed).toEqual(expect.arrayContaining([
      "Config/DefaultGame.ini",
      `Content/${BP_TURN.slice(6)}.uasset`,
      `Content/${MAT.slice(6)}.uasset`,
      `Content/${LEVEL.slice(6)}.umap`,
    ]));
    const changes = await tool("editor", "list_changes", { from: start, to: end.id });
    const iniChange = (changes.changes as Json[]).find((c) => c.path === "Config/DefaultGame.ini")!;
    expect(iniChange.diff).toContain("+Turn=1");
    const bpChange = (changes.changes as Json[]).find((c) => c.path === `Content/${BP_TURN.slice(6)}.uasset`)!;
    expect(JSON.stringify(bpChange.blueprintDiff)).toContain("TurnVar");
  }, 300_000);

  it("restores the Blueprint and the config, keeps the material and the level, and the editor shows both", async () => {
    const restored = await tool("editor", "restore", {
      checkpoint: start,
      paths: [`Content/${BP_TURN.slice(6)}.uasset`, "Config/DefaultGame.ini"],
    });
    expect(restored.refused).toEqual([]);
    expect((restored.restored as Json[]).map((r) => r.path).sort()).toEqual(["Config/DefaultGame.ini", `Content/${BP_TURN.slice(6)}.uasset`]);
    expect(read(fileOf(BP_TURN))!.equals(before[fileOf(BP_TURN)]!)).toBe(true);
    // Byte-identical to the checkpoint, which includes not existing when the project had no DefaultGame.ini.
    expect(read(ini())).toEqual(before[ini()]);
    expect(read(fileOf(MAT))!.equals(before[fileOf(MAT)]!)).toBe(false);
    expect(read(fileOf(LEVEL, ".umap"))!.equals(before[fileOf(LEVEL, ".umap")]!)).toBe(false);

    // The editor, without a restart: the Blueprint lost its variable, the level kept its light.
    expect(await variables(BP_TURN)).not.toContain("TurnVar");
    const outliner = await call("get_world_outliner", {});
    expect(JSON.stringify(outliner)).toContain("CkLight");
    expect(restored.configsReloaded).toContain("Game");

    // Undo the restore.
    await tool("editor", "restore", { checkpoint: restored.before });
    expect(await variables(BP_TURN)).toContain("TurnVar");
  }, 300_000);

  it("leaves the project's own .git alone", () => {
    const git = path.join(projectDir, ".git");
    if (!fs.existsSync(git)) return;
    expect(fs.existsSync(path.join(projectDir, "Saved", "UE_MCP_Bridge", "shadow.git", "HEAD"))).toBe(true);
    expect(fs.readFileSync(path.join(git, "HEAD"), "utf8")).toMatch(/^ref: |^[0-9a-f]{40}/);
  });
});
