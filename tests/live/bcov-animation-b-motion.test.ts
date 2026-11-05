// @live-editor headless
/**
 * Motion matching content read back: mirror data table, pose search schema and
 * database (schema swap, clips, cost settings), and a chooser wired into a
 * Motion Matching node's database pin. Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const ENGINE = "/Engine/Tutorial/SubEditors/TutorialAssets/Character";
const ENGINE_SKELETON = `${ENGINE}/TutorialTPP_Skeleton`;
const DIR = `${TEST_PREFIX}/BCov/AnimB/Motion`;
const IDLE = `${DIR}/AS_BCovIdle`;
const WALK = `${DIR}/AS_BCovWalk`;
const MIRROR = `${DIR}/MDT_BCov`;
const SCHEMA = `${DIR}/PSS_BCov`;
const SCHEMA_ALT = `${DIR}/PSS_BCovAlt`;
const DATABASE = `${DIR}/PSD_BCov`;
const CHOOSER = `${DIR}/CT_BCov`;
const ABP = `${DIR}/ABP_BCovMM`;
const ASSETS = [ABP, CHOOSER, DATABASE, SCHEMA, SCHEMA_ALT, MIRROR, IDLE, WALK];

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function readDatabase(): Promise<Json> {
  return call("read_pose_search_database", { assetPath: DATABASE });
}

beforeAll(async () => {
  bridge = await getBridge();
  for (const path of ASSETS) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
  await call("duplicate_asset", { sourcePath: `${ENGINE}/Tutorial_Idle`, destinationPath: IDLE });
  await call("duplicate_asset", { sourcePath: `${ENGINE}/Tutorial_Walk_Fwd`, destinationPath: WALK });
  await call("create_mirror_data_table", { name: "MDT_BCov", skeletonPath: ENGINE_SKELETON, packagePath: DIR });
  await call("create_pose_search_schema", {
    name: "PSS_BCov", skeletonPath: ENGINE_SKELETON, packagePath: DIR, mirrorDataTablePath: MIRROR, sampleRate: 60,
  });
  await call("create_pose_search_schema", { name: "PSS_BCovAlt", skeletonPath: ENGINE_SKELETON, packagePath: DIR });
  await call("create_pose_search_database", { name: "PSD_BCov", packagePath: DIR, schemaPath: SCHEMA });
}, 300_000);

afterAll(async () => {
  if (bridge) {
    for (const path of ASSETS) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
    disconnectBridge();
  }
});

describe("motion matching content", () => {
  it("read_mirror_data_table lists the left and right bone pairs", async () => {
    const read = await call("read_mirror_data_table", { assetPath: MIRROR });
    expect(String(read.skeleton)).toContain("TutorialTPP_Skeleton");
    const rows = read.rows as Array<{ name: string; mirroredName: string }>;
    expect(read.rowCount).toBe(rows.length);
    const pair = rows.find((r) => /_l$/i.test(r.name));
    expect(pair, JSON.stringify(rows.slice(0, 10))).toBeDefined();
    expect(pair!.mirroredName).toBe(pair!.name.replace(/_l$/i, "_r"));
  }, 120_000);

  it("read_pose_search_schema reports the sample rate, skeleton and mirror table", async () => {
    const read = await call("read_pose_search_schema", { schemaPath: SCHEMA });
    expect(read.sampleRate).toBe(60);
    const skeletons = read.skeletons as Array<{ skeleton: string; mirrorDataTable: string }>;
    expect(skeletons[0].skeleton).toContain("TutorialTPP_Skeleton");
    expect(skeletons[0].mirrorDataTable).toContain("MDT_BCov");
    expect(Number(read.channelCount)).toBeGreaterThan(0);
  }, 120_000);

  it("set_pose_search_schema swaps the database's schema", async () => {
    expect(String((await readDatabase()).schemaPath)).toContain("PSS_BCov.");
    const set = await call("set_pose_search_schema", { assetPath: DATABASE, schemaPath: SCHEMA_ALT });
    expect(String(set.previousSchemaPath)).toContain("PSS_BCov");
    const read = await readDatabase();
    expect(String(read.schemaPath)).toContain("PSS_BCovAlt");
    expect((read.schema as { path: string }).path).toContain("PSS_BCovAlt");
  }, 120_000);

  it("set_pose_search_clips replaces the clip list", async () => {
    const set = await call("set_pose_search_clips", {
      assetPath: DATABASE, clips: [IDLE, { sequencePath: WALK, enabled: true }], clearExisting: true,
    });
    expect(set.newCount).toBe(2);
    const read = await readDatabase();
    expect(read.animationAssetCount).toBe(2);
    const paths = (read.animationAssets as Array<{ assetPath: string }>).map((a) => a.assetPath);
    expect(paths.some((p) => p.includes("AS_BCovIdle"))).toBe(true);
    expect(paths.some((p) => p.includes("AS_BCovWalk"))).toBe(true);
  }, 120_000);

  it("set_pose_search_database_settings writes the cost biases", async () => {
    await call("set_pose_search_database_settings", {
      assetPath: DATABASE, continuingPoseCostBias: -0.25, baseCostBias: 0.5, loopingCostBias: -0.125, kdTreeQueryNumNeighbors: 48,
    });
    const read = await readDatabase();
    expect(Number(read.continuingPoseCostBias)).toBeCloseTo(-0.25, 4);
    expect(Number(read.baseCostBias)).toBeCloseTo(0.5, 4);
    expect(Number(read.loopingCostBias)).toBeCloseTo(-0.125, 4);
    expect(read.kdTreeQueryNumNeighbors).toBe(48);
  }, 120_000);

  it("set_motion_matching_chooser drives the node's database pin from a chooser", async () => {
    await call("create_anim_blueprint", { name: "ABP_BCovMM", skeletonPath: ENGINE_SKELETON, packagePath: DIR });
    const node = await call("add_motion_matching_node", { assetPath: ABP, databasePath: DATABASE });
    await call("chooser_create", { name: "CT_BCov", packagePath: DIR });

    const set = await call("set_motion_matching_chooser", { assetPath: ABP, chooserPath: CHOOSER });
    expect(set.motionMatchingNodeGuid).toBe(node.nodeGuid);
    expect(set.databasePinDriven).toBe(true);

    type Node = { id: string; class: string; pins?: Array<{ name: string; connected: boolean }> };
    const nodes = (await call("read_anim_graph", { assetPath: ABP })).nodes as Node[];
    expect(nodes.some((n) => n.id === set.evaluateChooserNodeGuid)).toBe(true);
    const mm = nodes.find((n) => n.id === node.nodeGuid);
    expect(mm, JSON.stringify(nodes.map((n) => n.class))).toBeDefined();
    expect(mm!.pins?.find((p) => p.name === "Database")?.connected).toBe(true);
  }, 180_000);
});
