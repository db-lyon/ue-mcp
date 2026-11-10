// @live-editor headless
/**
 * World Partition setup through UWorldPartitionSubsystem (#1828), in a scratch World Partition map under
 * /Game/MCPTest/BCov/WPSubsystem: streaming on and off, the runtime hash class, runtime partitions and their
 * HLOD setups, HLOD actor builds and stats, the minimap actor, and the World Partition check and repair.
 *
 * Most of these functions arrived in UE 5.9. On an older editor each 5.9-only action is asserted to refuse
 * with unsupported_engine_version; streaming, HLOD actor builds and the minimap actor have an older
 * equivalent and run on every engine. The map open before the file is reopened afterwards.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const HOME = "/Game/MCP_Home";
const DIR = `${TEST_PREFIX}/BCov/WPSubsystem`;
const MAP = `${DIR}/L_BCovWPSub`;
const CUBE = "/Engine/BasicShapes/Cube.Cube";
const CUBE_LABEL = "BCov_WPSub_Cube";
const HLOD_LAYER = "/Engine/Maps/Templates/HLODs/HLODLayer_Instanced.HLODLayer_Instanced";
const HASH_SET = "/Script/Engine.WorldPartitionRuntimeHashSet";
const SPATIAL_HASH = "/Script/Engine.WorldPartitionRuntimeSpatialHash";
const PARTITION = "BCovPartition";
const RENAMED = "BCovPartitionRenamed";
const SETUP = "BCovSetup";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let originalMap: string | undefined;
let is59 = false;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function refused(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method} should refuse: ${JSON.stringify(r.result)}`).toBe(false);
  return body(r);
}

/** Before 5.9 a 5.9-only action refuses with the subsystem function it needed. */
async function expectUnsupported(method: string, params: Json, fn: string): Promise<void> {
  const out = await refused(method, params);
  expect(out.errorCode).toBe("unsupported_engine_version");
  expect(String(out.error)).toContain(`${fn} requires Unreal Engine 5.9 or newer`);
}

async function currentMap(): Promise<string | undefined> {
  return body(await callBridge(bridge, "get_current_level", {})).mapPackagePath as string | undefined;
}

const partitionNames = async (): Promise<string[]> =>
  ((await call("list_runtime_partitions")).partitions as Json[]).map((p) => String(p.name));
const setupRows = async (partitionName: string): Promise<Json[]> =>
  (await call("list_hlod_setups", { partitionName })).setups as Json[];

beforeAll(async () => {
  bridge = await getBridge();
  const caps = body(await callBridge(bridge, "get_bridge_capabilities", {}));
  const [major, minor] = String(caps.engineVersion).split(".").map(Number);
  is59 = major > 5 || (major === 5 && minor >= 9);
  originalMap = await currentMap();
  await call("demo_go_home");
  await callBridge(bridge, "delete_asset", { assetPath: MAP, force: true });
  const created = await call("create_new_level", { levelPath: MAP, worldPartition: true });
  expect(created.worldPartition).toBe(true);
  await call("place_actor", { actorClass: "/Script/Engine.StaticMeshActor", label: CUBE_LABEL, staticMesh: CUBE, location: { x: 0, y: 0, z: 100 } });
  await call("save_level");
  await call("load_level", { levelPath: HOME });
  await call("load_level", { levelPath: MAP });
  expect(await currentMap()).toBe(MAP);
}, 300_000);

afterAll(async () => {
  if (!bridge) return;
  const back = originalMap && originalMap.startsWith("/Game/") && !originalMap.startsWith(DIR) ? originalMap : HOME;
  const restored = await callBridge(bridge, "load_level", { levelPath: back, discardUnsaved: true });
  if (!succeeded(restored)) await callBridge(bridge, "load_level", { levelPath: HOME, discardUnsaved: true });
  await callBridge(bridge, "delete_asset", { assetPath: MAP, force: true });
  disconnectBridge();
}, 300_000);

describe("streaming", () => {
  it("set_world_partition_streaming turns streaming off and on, and reports a repeat as unchanged", async () => {
    if ((await call("get_world_partition_settings")).enableStreaming === false) {
      expect((await call("set_world_partition_streaming", { enabled: true })).enabled).toBe(true);
    }
    const off = await call("set_world_partition_streaming", { enabled: false });
    expect(off.enabled).toBe(false);
    expect(off.previousEnabled).toBe(true);
    expect(off.changed).toBe(true);
    expect((off.rollback as Json).method).toBe("set_world_partition_streaming");
    expect(((off.rollback as Json).payload as Json).enabled).toBe(true);
    expect((await call("get_world_partition_settings")).enableStreaming).toBe(false);
    expect((await call("set_world_partition_streaming", { enabled: false })).unchanged).toBe(true);

    const on = await call("set_world_partition_streaming", { enabled: true });
    expect(on.enabled).toBe(true);
    expect((await call("get_world_partition_settings")).enableStreaming).toBe(true);
  }, 60_000);
});

describe("runtime hash and runtime partitions", () => {
  it("set_runtime_hash_class converts the runtime hash, leaving it a runtime hash set", async () => {
    if (!is59) return expectUnsupported("set_runtime_hash_class", { runtimeHashClass: HASH_SET }, "UWorldPartitionSubsystem::SetRuntimeHashClass");
    const before = String((await call("get_world_partition_settings")).runtimeHashClass);
    if (before === HASH_SET) {
      const spatial = await call("set_runtime_hash_class", { runtimeHashClass: SPATIAL_HASH });
      expect(spatial.changed).toBe(true);
      expect(spatial.previousClass).toBe(HASH_SET);
      expect((await call("get_world_partition_settings")).runtimeHashClass).toBe(SPATIAL_HASH);
    }
    const set = await call("set_runtime_hash_class", { runtimeHashClass: HASH_SET });
    expect(set.runtimeHashClass).toBe(HASH_SET);
    expect((set.rollback as Json).method).toBe("set_runtime_hash_class");
    expect((await call("get_world_partition_settings")).runtimeHashClass).toBe(HASH_SET);
    expect((await call("set_runtime_hash_class", { runtimeHashClass: HASH_SET })).unchanged).toBe(true);
  }, 60_000);

  it("list_runtime_partitions lists the runtime hash set's partitions with their class", async () => {
    if (!is59) return expectUnsupported("list_runtime_partitions", {}, "UWorldPartitionSubsystem::GetRuntimePartitionNames");
    const out = await call("list_runtime_partitions");
    expect(out.runtimeHashClass).toBe(HASH_SET);
    const rows = out.partitions as Json[];
    expect(rows.length).toBeGreaterThan(0);
    expect(out.count).toBe(rows.length);
    expect(String(rows[0].class)).toContain("RuntimePartition");
  }, 60_000);

  it("add_runtime_partition adds a partition once, and get_runtime_partition reads its properties", async () => {
    if (!is59) return expectUnsupported("add_runtime_partition", { partitionName: PARTITION }, "UWorldPartitionSubsystem::AddRuntimePartition");
    const added = await call("add_runtime_partition", { partitionName: PARTITION });
    expect(added.created).toBe(true);
    expect(added.class).toBe("/Script/Engine.RuntimePartitionLHGrid");
    expect((added.rollback as Json).method).toBe("remove_runtime_partition");
    expect(await partitionNames()).toContain(PARTITION);
    expect((await call("add_runtime_partition", { partitionName: PARTITION })).existed).toBe(true);

    const read = await call("get_runtime_partition", { partitionName: PARTITION });
    expect(read.hasPartition).toBe(true);
    expect(read.class).toBe("/Script/Engine.RuntimePartitionLHGrid");
    expect((read.properties as Json).CellSize).toBeDefined();
    expect((read.properties as Json).LoadingRange).toBeDefined();
  }, 60_000);

  it("set_runtime_partition_properties writes a partition's loading range and rolls back to the old one", async () => {
    if (!is59) return expectUnsupported("set_runtime_partition_properties", { partitionName: PARTITION, properties: { LoadingRange: 1 } }, "UWorldPartitionSubsystem::GetRuntimePartition");
    const before = String(((await call("get_runtime_partition", { partitionName: PARTITION })).properties as Json).LoadingRange);
    const out = await call("set_runtime_partition_properties", { partitionName: PARTITION, properties: { LoadingRange: 51200 } });
    expect((out.readBack as Json).LoadingRange).toBe("51200");
    expect((out.previous as Json).LoadingRange).toBe(before);
    expect((out.rollback as Json).method).toBe("set_runtime_partition_properties");
    expect(((await call("get_runtime_partition", { partitionName: PARTITION })).properties as Json).LoadingRange).toBe("51200");

    const typo = await refused("set_runtime_partition_properties", { partitionName: PARTITION, properties: { NoSuchProperty: 1 } });
    expect(String(typo.error)).toContain("NoSuchProperty");
  }, 60_000);

  it("rename_runtime_partition renames a partition and offers the rename back", async () => {
    if (!is59) return expectUnsupported("rename_runtime_partition", { partitionName: PARTITION, newName: RENAMED }, "UWorldPartitionSubsystem::RenameRuntimePartition");
    const out = await call("rename_runtime_partition", { partitionName: PARTITION, newName: RENAMED });
    expect(out.previousName).toBe(PARTITION);
    expect(((out.rollback as Json).payload as Json).newName).toBe(PARTITION);
    const names = await partitionNames();
    expect(names).toContain(RENAMED);
    expect(names).not.toContain(PARTITION);
  }, 60_000);
});

describe("HLOD setups", () => {
  it("add_hlod_setup adds a spatially loaded setup list_hlod_setups lists", async () => {
    if (!is59) return expectUnsupported("add_hlod_setup", { partitionName: RENAMED, setupName: SETUP }, "UWorldPartitionSubsystem::AddHLODSetup");
    const out = await call("add_hlod_setup", { partitionName: RENAMED, setupName: SETUP });
    expect(out.created).toBe(true);
    expect((out.rollback as Json).method).toBe("remove_hlod_setup");
    const row = (await setupRows(RENAMED)).find((s) => s.setupName === SETUP);
    expect(row?.spatiallyLoaded).toBe(true);
    expect(row?.hlodLayers).toEqual([]);
    expect((await call("add_hlod_setup", { partitionName: RENAMED, setupName: SETUP })).existed).toBe(true);
  }, 60_000);

  it("get_runtime_partition reads an HLOD setup's partition", async () => {
    if (!is59) return expectUnsupported("get_runtime_partition", { partitionName: RENAMED, setupName: SETUP }, "UWorldPartitionSubsystem::GetHLODSetupPartition");
    const out = await call("get_runtime_partition", { partitionName: RENAMED, setupName: SETUP });
    expect(out.setupName).toBe(SETUP);
    if (out.hasPartition) expect(String(out.class)).toContain("RuntimePartition");
    else expect(out.hasPartition).toBe(false);
    const listed = (await setupRows(RENAMED)).find((s) => s.setupName === SETUP);
    expect(Boolean(listed?.partitionClass)).toBe(Boolean(out.hasPartition));
  }, 60_000);

  it("set_hlod_setup_layers assigns an HLOD layer, reports a repeat as unchanged and clears it", async () => {
    if (!is59) return expectUnsupported("set_hlod_setup_layers", { partitionName: RENAMED, setupName: SETUP, hlodLayers: [HLOD_LAYER] }, "UWorldPartitionSubsystem::SetHLODSetupLayers");
    const set = await call("set_hlod_setup_layers", { partitionName: RENAMED, setupName: SETUP, hlodLayers: [HLOD_LAYER] });
    expect(set.hlodLayers).toEqual([HLOD_LAYER]);
    expect(set.previousHlodLayers).toEqual([]);
    expect((set.rollback as Json).method).toBe("set_hlod_setup_layers");
    expect((await setupRows(RENAMED)).find((s) => s.setupName === SETUP)?.hlodLayers).toEqual([HLOD_LAYER]);
    expect((await call("set_hlod_setup_layers", { partitionName: RENAMED, setupName: SETUP, hlodLayers: [HLOD_LAYER] })).unchanged).toBe(true);

    const cleared = await call("set_hlod_setup_layers", { partitionName: RENAMED, setupName: SETUP, hlodLayers: [] });
    expect(cleared.hlodLayers).toEqual([]);
    const missing = await refused("set_hlod_setup_layers", { partitionName: RENAMED, setupName: SETUP, hlodLayers: ["/Game/NoSuch/HLODLayer_Missing"] });
    expect(String(missing.error)).toContain("HLODLayer_Missing");
  }, 60_000);

  it("remove_hlod_setup removes the setup, then reports it already removed", async () => {
    if (!is59) return expectUnsupported("remove_hlod_setup", { partitionName: RENAMED, setupName: SETUP }, "UWorldPartitionSubsystem::RemoveHLODSetup");
    const out = await call("remove_hlod_setup", { partitionName: RENAMED, setupName: SETUP });
    expect(out.updated).toBe(true);
    expect((out.rollback as Json).method).toBe("add_hlod_setup");
    expect((await setupRows(RENAMED)).map((s) => s.setupName)).not.toContain(SETUP);
    expect((await call("remove_hlod_setup", { partitionName: RENAMED, setupName: SETUP })).alreadyRemoved).toBe(true);
  }, 60_000);

  it("remove_runtime_partition removes the partition, then reports it already removed", async () => {
    if (!is59) return expectUnsupported("remove_runtime_partition", { partitionName: RENAMED }, "UWorldPartitionSubsystem::RemoveRuntimePartition");
    const out = await call("remove_runtime_partition", { partitionName: RENAMED });
    expect(out.previousClass).toBe("/Script/Engine.RuntimePartitionLHGrid");
    expect((out.rollback as Json).method).toBe("add_runtime_partition");
    expect(await partitionNames()).not.toContain(RENAMED);
    expect((await call("remove_runtime_partition", { partitionName: RENAMED })).alreadyRemoved).toBe(true);
  }, 60_000);
});

describe("HLOD actors and stats", () => {
  it("build_hlod_actors builds nothing in a map without HLOD actors and refuses an actor that is not one", async () => {
    const none = await call("build_hlod_actors", {});
    expect(none.requested).toBe(0);
    expect(none.unchanged).toBe(true);
    const wrong = await refused("build_hlod_actors", { actorLabels: [CUBE_LABEL] });
    expect(String(wrong.error)).toContain("not an HLOD actor");
  }, 60_000);

  it("get_hlod_stats lists no HLOD actors in a map that has never been built", async () => {
    if (!is59) return expectUnsupported("get_hlod_stats", {}, "UWorldPartitionSubsystem::GetHLODStats");
    const out = await call("get_hlod_stats");
    expect(out.hlodActors).toEqual([]);
    expect(out.count).toBe(0);
  }, 60_000);
});

describe("minimap", () => {
  it("create_world_partition_minimap spawns the minimap actor once and get_world_partition_minimap reads it", async () => {
    const before = await call("get_world_partition_minimap");
    if (before.exists) {
      await call("delete_actor", { actorPath: (before.minimap as Json).actorPath });
      expect((await call("get_world_partition_minimap")).exists).toBe(false);
    }
    const created = await call("create_world_partition_minimap");
    expect(created.created).toBe(true);
    expect((created.rollback as Json).method).toBe("delete_actor");
    const path = String((created.minimap as Json).actorPath);
    expect(path).toContain("WorldPartitionMiniMap");
    expect((await call("create_world_partition_minimap")).existed).toBe(true);

    const read = await call("get_world_partition_minimap");
    expect(read.exists).toBe(true);
    expect((read.minimap as Json).actorPath).toBe(path);
    expect(((read.minimap as Json).properties as Json).WorldUnitsPerPixel).toBeDefined();
    if (is59) {
      expect(read.worldBounds).toBeDefined();
      expect(read.resolution !== undefined || read.resolutionError !== undefined).toBe(true);
    }
  }, 60_000);
});

describe("World Partition validation", () => {
  it("check_world_partition_errors reports the map's problems with consistent counts", async () => {
    if (!is59) return expectUnsupported("check_world_partition_errors", {}, "UWorldPartitionSubsystem::CheckForErrors");
    const out = await call("check_world_partition_errors");
    const rows = out.messages as Json[];
    expect(out.count).toBe(rows.length);
    expect(Number(out.errors) + Number(out.warnings)).toBeLessThanOrEqual(rows.length);
    for (const row of rows) {
      expect(["Error", "Warning", "Info"]).toContain(row.severity);
      expect(typeof row.errorType).toBe("string");
    }
  }, 60_000);

  it("repair_world_partition_errors finds nothing to repair in a saved map with external actors", async () => {
    if (!is59) return expectUnsupported("repair_world_partition_errors", {}, "UWorldPartitionSubsystem::RepairErrors");
    await call("save_level");
    const out = await call("repair_world_partition_errors");
    expect(out.repairableBefore).toBe(0);
    expect(out.repairableAfter).toBe(0);
    expect(out.changed).toBe(false);
    expect(out.rollbackPossible).toBe(false);
  }, 60_000);
});
