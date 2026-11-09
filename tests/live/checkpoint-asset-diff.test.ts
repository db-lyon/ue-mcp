// @live-editor interactive
/**
 * Checkpoint asset diffs against a real editor: one turn that modifies a
 * material, a material instance, a Blueprint, a static mesh, a texture, a
 * DataTable and a level, adds one material and deletes another, then reads
 * list_changes and checks each package's assetDiff carries Unreal's own diff
 * records with both values, a picture of each version, and a summary. Then
 * open_checkpoint_diff opens Unreal's diff window, which list_windows finds
 * and close_window closes.
 *
 * Interactive: the pictures are rendered and the diff window needs Slate.
 * Checkpoints are turned on through ue-mcp.local.yml, put back afterwards.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import { thumbnailFile } from "../../src/checkpoints/asset-diff.js";
import { LiveServer, resultJson, type LiveCallResult } from "./server.js";
import { closeLiveBridges, liveTarget } from "./harness.js";

type Json = Record<string, any>;
const DIR = "/Game/MCPTest/CkAssetDiff";
const MAT = `${DIR}/M_CkA`;
const MI = `${DIR}/MI_CkA`;
const BP = `${DIR}/BP_CkA`;
const SM = `${DIR}/SM_CkA`;
const TEX = `${DIR}/T_CkA`;
const DT = `${DIR}/DT_CkA`;
const LEVEL = `${DIR}/L_CkA`;
const NEW_MAT = `${DIR}/M_CkNew`;
const DOOMED = `${DIR}/M_CkDoomed`;

const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const rel = (asset: string, ext = ".uasset") => `Content/${asset.replace(/^\/Game\//, "")}${ext}`;

let bridge: EditorBridge;
let projectDir: string;
let server: LiveServer | null = null;
let localYml: string;
let localYmlBefore: string | null = null;
let start = "";
let end = "";
let startAt = "";
let endAt = "";
let changes: Json[] = [];
let result: Json = {};

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(r.ok && body(r).success !== false, `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function tool(category: string, action: string, args: Json = {}): Promise<Json> {
  const r: LiveCallResult = await server!.call(category, { action, args });
  expect(r.isError, `${category}.${action}: ${r.text}`).toBe(false);
  return resultJson<Json>(r);
}

const diffOf = (asset: string, ext = ".uasset"): Json => {
  const c = changes.find((x) => x.path === rel(asset, ext));
  expect(c, `${rel(asset, ext)} in ${changes.map((x) => x.path).join(", ")}`).toBeTruthy();
  expect(c!.assetDiff?.state, JSON.stringify(c!.assetDiff)).toBe("ready");
  return c!.assetDiff as Json;
};

const records = (d: Json, category?: string): Json[] =>
  ((d.sections as Json[]) ?? []).filter((s) => !category || s.category === category).flatMap((s) => s.entries as Json[]);

/** Every property record, including those under graph records. */
const allProps = (d: Json): Json[] => records(d).flatMap((r) => [r, ...((r.properties as Json[]) ?? [])]);

function expectPicture(v: Json | null | undefined, label: string): void {
  expect(v, label).toBeTruthy();
  expect(String(v!.thumbnail?.id), `${label}: ${JSON.stringify(v!.thumbnail)}`).toMatch(/^[0-9a-f]{40}$/);
  const file = thumbnailFile(path.join(projectDir, "Saved", "UE_MCP_Bridge", "shadow-thumbs"), v!.thumbnail.id);
  expect(fs.statSync(file).size, file).toBeGreaterThan(100);
  expect(v!.thumbnail.url).toContain(`/v1/checkpoints/thumbnails/${v!.thumbnail.id}.png`);
}

beforeAll(async () => {
  bridge = await getBridge();
  projectDir = (await liveTarget()).projectDir;
  localYml = path.join(projectDir, "ue-mcp.local.yml");
  // Off any map under DIR so the folder can be deleted. MCP_Home is not checked in; demo_go_home creates and loads it.
  await call("demo_go_home");
  // delete_asset also removes a map file the asset registry no longer lists; delete_folder only sees what it lists.
  await callBridge(bridge, "delete_asset", { assetPath: LEVEL, force: true });
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });

  localYmlBefore = fs.existsSync(localYml) ? fs.readFileSync(localYml, "utf8") : null;
  if (localYmlBefore !== null && /checkpoints\s*:/.test(localYmlBefore)) throw new Error(`${localYml} already sets checkpoints`);
  const block = localYmlBefore?.includes("ue-mcp:") ? localYmlBefore.replace(/ue-mcp:\s*\n/, "ue-mcp:\n  checkpoints: on\n") : `${localYmlBefore ?? ""}\nue-mcp:\n  checkpoints: on\n`;
  fs.writeFileSync(localYml, block);

  // The project before the turn, saved.
  await call("create_material", { name: "M_CkA", packagePath: DIR });
  await call("add_material_expression", { materialPath: MAT, expressionType: "ScalarParameter", name: "CkRough", parameterName: "Roughness", defaultValue: 0.2 });
  await call("connect_to_material_property", { materialPath: MAT, expressionName: "CkRough", property: "Roughness" });
  await call("save_asset", { assetPath: MAT });
  await call("create_material_instance", { parentPath: MAT, name: "MI_CkA", packagePath: DIR });
  await call("save_asset", { assetPath: MI });
  await call("create_blueprint", { assetPath: BP, parentClass: "Actor" });
  // A visible mesh, so the Blueprint has a picture: the engine only renders Blueprints that show something.
  await call("add_component", { assetPath: BP, componentClass: "StaticMeshComponent", componentName: "CkBody" });
  await call("set_blueprint_component_property", { assetPath: BP, componentName: "CkBody", propertyName: "StaticMesh", value: "/Engine/BasicShapes/Cube.Cube" });
  await call("compile_blueprint", { assetPath: BP });
  await call("save_asset", { assetPath: BP });
  await call("duplicate_asset", { sourcePath: "/Engine/BasicShapes/Cube", destinationPath: SM });
  await call("save_asset", { assetPath: SM });
  await call("duplicate_asset", { sourcePath: "/Engine/EngineResources/DefaultTexture", destinationPath: TEX });
  await call("save_asset", { assetPath: TEX });
  await call("create_datatable", { name: "DT_CkA", packagePath: DIR, rowStruct: "/Script/GameplayTags.GameplayTagTableRow" });
  await call("add_datatable_row", { assetPath: DT, rowName: "RowA", row: { DevComment: "first" } });
  await call("save_asset", { assetPath: DT });
  await call("create_material", { name: "M_CkDoomed", packagePath: DIR });
  await call("save_asset", { assetPath: DOOMED });
  await call("create_new_level", { levelPath: LEVEL });
  expect((await call("get_current_level")).mapPackagePath).toBe(LEVEL);
  await call("place_actor", { actorClass: "PointLight", label: "CkMover", location: { x: 0, y: 0, z: 100 } });
  await call("place_actor", { actorClass: "PointLight", label: "CkDoomed", location: { x: 300, y: 0, z: 100 } });
  await call("save_level", {});

  server = await LiveServer.start({ projects: [(await liveTarget()).uproject] });
  await tool("editor", "clear_checkpoints");
}, 600_000);

afterAll(async () => {
  await server?.close();
  if (bridge) {
    await callBridge(bridge, "checkpoint_arm", { enabled: false });
    await callBridge(bridge, "demo_go_home", {});
    await callBridge(bridge, "delete_asset", { assetPath: LEVEL, force: true });
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
  if (localYml) {
    if (localYmlBefore === null) fs.rmSync(localYml, { force: true });
    else fs.writeFileSync(localYml, localYmlBefore);
  }
  if (projectDir) {
    for (const d of ["shadow.git", "shadow-pending", "shadow-large", "shadow-thumbs", "shadow-diffs"]) {
      fs.rmSync(path.join(projectDir, "Saved", "UE_MCP_Bridge", d), { recursive: true, force: true });
    }
    // The editor keeps the file of a native diff copy open after the diff window unloads it, until it
    // exits; the copies live under this shard's own Saved folder, so a locked one is left for the shard.
    try {
      fs.rmSync(path.join(projectDir, "Saved", "Diff", "UE_MCP_Checkpoints"), { recursive: true, force: true, maxRetries: 4, retryDelay: 250 });
    } catch (e) {
      if ((e as NodeJS.ErrnoException).code !== "EBUSY") throw e;
    }
  }
  closeLiveBridges();
}, 300_000);

describe.sequential("a turn's asset diffs", () => {
  it("list_changes gives every package of the turn an asset diff from Unreal's diff code", async () => {
    const startTaken = await tool("editor", "checkpoint", { label: "asset diff turn start" });
    start = String(startTaken.id);
    startAt = String(startTaken.createdAt);

    await tool("material", "set_expression_value", { materialPath: MAT, expressionIndex: 0, value: 0.7 });
    await tool("material", "add_expression", { materialPath: MAT, expressionType: "Constant", name: "CkAdded", value: 0.5 });
    await tool("asset", "save", { assetPath: MAT });
    await tool("material", "set_parameter", { assetPath: MI, parameterName: "Roughness", parameterType: "scalar", value: 0.4 });
    await tool("asset", "save", { assetPath: MI });
    await tool("blueprint", "add_variable", { assetPath: BP, name: "CkHealth", varType: "float" });
    await tool("blueprint", "add_component", { assetPath: BP, componentClass: "PointLightComponent", componentName: "CkLamp" });
    await tool("blueprint", "compile", { assetPath: BP });
    await tool("asset", "save", { assetPath: BP });
    await tool("editor", "set_property", { objectPath: SM, propertyName: "LightMapResolution", value: 128 });
    await tool("asset", "set_texture_settings", { assetPath: TEX, sRGB: false, save: true });
    await tool("asset", "add_datatable_row", { assetPath: DT, rowName: "RowB", row: { DevComment: "second" } });
    await tool("asset", "add_datatable_row", { assetPath: DT, rowName: "RowA", row: { DevComment: "edited" } });
    await tool("asset", "save", { assetPath: DT });
    await tool("level", "move_actor", { actorLabel: "CkMover", location: { x: 0, y: 500, z: 100 } });
    await tool("level", "delete_actor", { actorLabel: "CkDoomed" });
    await tool("level", "place_actor", { actorClass: "PointLight", label: "CkNew", location: { x: 0, y: -500, z: 100 } });
    await tool("level", "save");
    await tool("material", "create", { name: "M_CkNew", packagePath: DIR });
    await tool("asset", "save", { assetPath: NEW_MAT });
    await tool("asset", "delete", { assetPath: DOOMED, force: true });

    const endTaken = await tool("editor", "checkpoint", { label: "asset diff turn end" });
    end = String(endTaken.id);
    endAt = String(endTaken.createdAt);
    result = await tool("editor", "list_changes", { from: start, to: end });
    changes = result.changes as Json[];
    expect(result.assetDiffs).toMatchObject({ limit: 25, deferred: [] });
    expect(result.assetDiffs.diffed).toBe(changes.filter((c) => c.kind === "package").length);
  }, 600_000);

  it("a material carries the graph diff and the parameter default, with a picture of each version", () => {
    const d = diffOf(MAT);
    expect(d.engine.tool).toBe("SMaterialDiff");
    const graph = records(d, "Material Graph");
    expect(graph.some((r) => r.diffType === "NODE_ADDED" && r.displayString)).toBe(true);
    const def = allProps(d).find((r) => r.property === "DefaultValue");
    expect(def?.before).toMatch(/^0\.2/);
    expect(def?.after).toMatch(/^0\.7/);
    expectPicture(d.before, "material before");
    expectPicture(d.after, "material after");
    expect(d.summary).toMatch(/^\d+ differences?: /);
    expect(d.nativeDiff.available).toBe(true);
  });

  it("a material instance carries its overridden parameter", () => {
    const d = diffOf(MI);
    expect(d.engine.tool).toBe("SMaterialInstanceDiff");
    const scalar = records(d).find((r) => /Scalar\s*Parameter\s*Values/i.test(String(r.path)));
    expect(scalar, JSON.stringify(records(d))).toBeTruthy();
    expect(JSON.stringify(records(d))).toContain("0.4");
    expect(d.after.facts.some((f: Json) => f.name === "Scalar parameter Roughness" && f.value.startsWith("0.4"))).toBe(true);
  });

  it("a Blueprint carries My Blueprint, Components and the structural diff", () => {
    const d = diffOf(BP);
    expect(d.engine.tool).toBe("SBlueprintDiff");
    expect(records(d, "My Blueprint").some((r) => r.group === "CkHealth" && r.diffType === "PropertyAddedToB")).toBe(true);
    expect(records(d, "Components").some((r) => r.diffType === "NODE_ADDED" && r.group === "CkLamp")).toBe(true);
    expect(JSON.stringify(d.blueprint)).toContain("CkHealth");
    const change = changes.find((c) => c.path === rel(BP))!;
    expect(JSON.stringify(change.blueprintDiff)).toContain("CkHealth");
    // This Blueprint has nothing to draw (a light and a variable), so its thumbnail is one flat colour:
    // no picture, and the reason, never a blank square.
    for (const [v, label] of [[d.before, "Blueprint before"], [d.after, "Blueprint after"]] as const) {
      if (v.thumbnail && "id" in v.thumbnail) expectPicture(v, label);
      else expect(v.thumbnail?.error, label).toMatch(/drew nothing|no thumbnail renderer/);
    }
  });

  it("a static mesh carries the changed property and its LOD facts", () => {
    const d = diffOf(SM);
    const lm = records(d, "Details").find((r) => r.property === "LightMapResolution");
    expect(lm?.after).toBe("128");
    expect(lm?.before).not.toBe("128");
    expect(d.after.facts.some((f: Json) => f.name === "LOD 0" && /vertices/.test(f.value))).toBe(true);
    expectPicture(d.after, "mesh after");
  });

  it("a texture carries its sRGB change", () => {
    const d = diffOf(TEX);
    const srgb = records(d, "Details").find((r) => r.property === "SRGB");
    expect(srgb).toMatchObject({ before: "True", after: "False", diffType: "PropertyValueChanged" });
    expectPicture(d.before, "texture before");
  });

  it("a DataTable carries rows added and changed, and the engine's CSV diff", () => {
    const d = diffOf(DT);
    const rows = records(d, "Rows");
    expect(rows.some((r) => r.group === "RowB" && r.diffType === "OBJECT_ADDED")).toBe(true);
    const comment = rows.find((r) => r.group === "RowA" && /DevComment/i.test(String(r.path)));
    expect(comment).toMatchObject({ before: "first", after: "edited" });
    expect(d.textDiff.format).toBe("csv");
    expect(d.textDiff.diff).toContain("+");
  });

  it("a level carries actors added, removed and moved", () => {
    const d = diffOf(LEVEL, ".umap");
    const actors = records(d, "Actors");
    expect(actors.some((r) => r.diffType === "OBJECT_ADDED" && r.group === "CkNew")).toBe(true);
    expect(actors.some((r) => r.diffType === "OBJECT_REMOVED" && r.group === "CkDoomed")).toBe(true);
    expect(actors.some((r) => String(r.group).startsWith("CkMover") && /Relative\s*Location/i.test(String(r.path)))).toBe(true);
    // Both versions drawn from the level itself, one camera framed on the changed actors (#1568).
    for (const [v, label] of [[d.before, "level before"], [d.after, "level after"]] as const) {
      expectPicture(v, label);
      expect(v.thumbnail.source, label).toBe("rendered");
      expect(v.thumbnail.note, label).toMatch(/^Framed on the changed actors: .*CkNew/);
    }
    const thumbs = path.join(projectDir, "Saved", "UE_MCP_Bridge", "shadow-thumbs");
    expect(fs.readFileSync(thumbnailFile(thumbs, d.before.thumbnail.id)).equals(fs.readFileSync(thumbnailFile(thumbs, d.after.thumbnail.id)))).toBe(false);
  });

  it("an added asset has no before and says what it is; a deleted one has no after", () => {
    const added = diffOf(NEW_MAT);
    expect(added.status).toBe("added");
    expect(added.before).toBeNull();
    expectPicture(added.after, "added after");
    expect(added.summary).toMatch(/^New Material/);
    const gone = diffOf(DOOMED);
    expect(gone.status).toBe("deleted");
    expect(gone.after).toBeNull();
    expectPicture(gone.before, "deleted before");
    expect(gone.summary).toMatch(/^Deleted Material/);
  });

  it("diff_checkpoint_asset diffs one path on its own, the same as list_changes", async () => {
    const one = await tool("editor", "diff_checkpoint_asset", { from: start, to: end, changedPath: rel(MAT) });
    expect(one.summary).toBe(diffOf(MAT).summary);
    expect(one.before.thumbnail.id).toBe(diffOf(MAT).before.thumbnail.id);
  }, 300_000);

  it("checkpoint_diff_asset reports a version it cannot load, with the reason", async () => {
    const junk = path.join(projectDir, "Saved", "Diff", "UE_MCP_Checkpoints", "junk", "Junk.uasset");
    fs.mkdirSync(path.dirname(junk), { recursive: true });
    fs.writeFileSync(junk, "not a package");
    const r = await callBridge(bridge, "checkpoint_diff_asset", { originalFile: path.join(projectDir, rel(MAT)), fromFile: junk });
    expect(body(r).success).toBe(false);
    expect(String(body(r).error)).toContain("could not be loaded for diff");
  }, 120_000);

  it("open_checkpoint_diff opens Unreal's diff window for the material, and it closes", async () => {
    const opened = await tool("editor", "open_checkpoint_diff", { from: start, to: end, changedPath: rel(MAT) });
    expect(opened.opened).toBe("window");
    expect(opened.windowTitle).toBeTruthy();
    // Each side is dated with its checkpoint's time, not the unset date shown as "Dec 31, 1" (#1606).
    const revisions = opened.revisions as { before: Json; after: Json };
    expect(Date.parse(String(revisions.before.date))).toBe(Date.parse(startAt));
    expect(Date.parse(String(revisions.after.date))).toBe(Date.parse(endAt));
    const listed = body(await callBridge(bridge, "list_windows", { maxTexts: 1, maxButtons: 1 }));
    const win = (listed.windows as Json[]).find((w) => w.title === opened.windowTitle);
    expect(win, JSON.stringify(listed.windows?.map((w: Json) => w.title))).toBeTruthy();
    const closed = body(await callBridge(bridge, "close_window", { windowId: win!.windowId }));
    expect(closed.success).not.toBe(false);
  }, 300_000);

  it("blueprint(open_diff_window) shows the checkpoint's Blueprint against the asset now, and unloads it on close (#1483)", async () => {
    const opened = await tool("blueprint", "open_diff_window", { assetPath: BP, against: start });
    expect(opened.against).toEqual({ kind: "checkpoint", id: start });
    expect(opened.opened).toBe("window");
    expect(opened.unloadsOnClose).toBe(true);
    // The checkpoint's version on the left, dated with its checkpoint; the current asset on the right.
    const revisions = opened.revisions as { before: Json; after: Json };
    expect(Date.parse(String(revisions.before.date))).toBe(Date.parse(startAt));
    expect(revisions.after.label).toBe("Current");
    const temps = opened.tempPackages as string[];
    expect(temps).toHaveLength(1);
    expect(temps[0]).toMatch(/^\/Temp\//);
    expect(body(await callBridge(bridge, "find_object", { objectPath: temps[0] })).found).toBe(true);

    const listed = body(await callBridge(bridge, "list_windows", { maxTexts: 1, maxButtons: 1 }));
    const win = (listed.windows as Json[]).find((w) => w.title === opened.windowTitle);
    expect(win, JSON.stringify(listed.windows?.map((w: Json) => w.title))).toBeTruthy();
    expect(body(await callBridge(bridge, "close_window", { windowId: win!.windowId })).success).not.toBe(false);

    let found = true;
    for (let i = 0; i < 40 && found; i++) {
      await new Promise((r) => setTimeout(r, 250));
      found = body(await callBridge(bridge, "find_object", { objectPath: temps[0] })).found === true;
    }
    expect(found, `${temps[0]} still loaded after its window closed`).toBe(false);
  }, 300_000);

  it("checkpoint_open_native_diff opens the asset editor on an added asset", async () => {
    const r = await call("checkpoint_open_native_diff", { originalFile: path.join(projectDir, rel(NEW_MAT)), current: true });
    expect(r.opened).toBe("editor");
  }, 300_000);
});
