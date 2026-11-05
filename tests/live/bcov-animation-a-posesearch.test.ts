// @live-editor headless
/**
 * The motion matching content pipeline on the engine tutorial skeleton: a
 * mirror table, a schema and its channels, a database with a clip, its search
 * index, and a normalization set, each read back.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/AnimA/PoseSearch";
const ENGINE = "/Engine/Tutorial/SubEditors/TutorialAssets/Character";
const SKELETON = `${ENGINE}/TutorialTPP_Skeleton`;
const WALK = `${DIR}/AS_BCovA_PsWalk`;
const MIRROR = `${DIR}/MDT_BCovA`;
const SCHEMA = `${DIR}/PSS_BCovA`;
const DATABASE = `${DIR}/PSD_BCovA`;
const NORMALIZATION = `${DIR}/PSN_BCovA`;
const ASSETS = [NORMALIZATION, DATABASE, SCHEMA, MIRROR, WALK];

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

beforeAll(async () => {
  bridge = await getBridge();
  for (const path of ASSETS) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
  await call("duplicate_asset", { sourcePath: `${ENGINE}/Tutorial_Walk_Fwd`, destinationPath: WALK });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    for (const path of ASSETS) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("pose search authoring", () => {
  it("create_mirror_data_table derives the left and right bone pairs", async () => {
    const created = await call("create_mirror_data_table", { name: "MDT_BCovA", skeletonPath: SKELETON, packagePath: DIR });
    expect(Number(created.rowCount)).toBeGreaterThan(0);
    const read = await call("read_mirror_data_table", { assetPath: MIRROR });
    expect(String(read.skeleton)).toContain("TutorialTPP_Skeleton");
    const rows = read.rows as Array<{ name: string; mirroredName: string }>;
    expect(rows).toContainEqual(expect.objectContaining({ name: "thigh_l", mirroredName: "thigh_r" }));
  }, 120_000);

  it("create_pose_search_schema binds the mirror table, and the pose and trajectory channel adds land on it", async () => {
    await call("create_pose_search_schema", {
      name: "PSS_BCovA", skeletonPath: SKELETON, packagePath: DIR, mirrorDataTablePath: MIRROR, sampleRate: 20, addDefaultChannels: false,
    });
    const empty = await call("read_pose_search_schema", { schemaPath: SCHEMA });
    expect(empty.channelCount).toBe(0);
    expect(empty.sampleRate).toBe(20);
    expect(String((empty.skeletons as Array<{ mirrorDataTable: string }>)[0].mirrorDataTable)).toContain("MDT_BCovA");

    await call("add_pose_search_schema_pose_channel", {
      schemaPath: SCHEMA, bones: ["foot_l", { bone: "foot_r", flags: ["position", "velocity"] }],
    });
    const withPose = await call("read_pose_search_schema", { schemaPath: SCHEMA });
    expect(withPose.channels).toEqual(["PoseSearchFeatureChannel_Pose"]);

    await call("add_pose_search_schema_trajectory_channel", {
      schemaPath: SCHEMA, samples: [{ offset: -0.3, flags: ["position"] }, { offset: 0, flags: ["velocity"] }, { offset: 0.5, flags: ["position"] }],
    });
    const both = await call("read_pose_search_schema", { schemaPath: SCHEMA });
    expect(both.channelCount).toBe(2);
    expect(both.channels).toContain("PoseSearchFeatureChannel_Trajectory");
  }, 120_000);

  it("create_pose_search_database uses the schema, add_pose_search_sequence adds a clip, and build_pose_search_index indexes it", async () => {
    const created = await call("create_pose_search_database", { name: "PSD_BCovA", packagePath: DIR, schemaPath: SCHEMA });
    expect(created.schemaCreated).toBe(false);
    const fresh = await call("read_pose_search_database", { assetPath: DATABASE });
    expect(String(fresh.schemaPath)).toContain("PSS_BCovA");
    expect(fresh.animationAssetCount).toBe(0);

    await call("add_pose_search_sequence", { assetPath: DATABASE, sequencePath: WALK });
    const read = await call("read_pose_search_database", { assetPath: DATABASE });
    expect(read.animationAssetCount).toBe(1);
    expect((read.animationAssets as Array<{ assetPath: string }>)[0].assetPath).toContain("AS_BCovA_PsWalk");

    const built = await call("build_pose_search_index", { assetPath: DATABASE });
    expect(built.result).toBe("Success");
    expect(built.waitedForCompletion).toBe(true);
    expect(built.animationAssetCount).toBe(1);
  }, 180_000);

  it("create_pose_search_normalization_set holds the given database", async () => {
    const created = await call("create_pose_search_normalization_set", { name: "PSN_BCovA", packagePath: DIR, databases: [DATABASE] });
    expect(created.databaseCount).toBe(1);
    const databases = await call("read_asset_properties", {
      assetPath: NORMALIZATION, propertyName: "Databases", includeValues: true, valueFormat: "json",
    });
    expect(JSON.stringify(databases.value)).toContain("PSD_BCovA");
  }, 120_000);
});
