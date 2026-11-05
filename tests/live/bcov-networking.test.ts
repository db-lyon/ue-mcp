// @live-editor headless
/**
 * Networking: every CDO replication setter on a throwaway Actor Blueprint, each
 * read back through get_networking_info, get_cdo_properties or reflect_class.
 * Refs #1223.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Networking";
const BP = `${DIR}/BP_BCov_Networking`;
const BP_CLASS = `${BP}.BP_BCov_Networking_C`;
const VAR = "BCovReplicatedHealth";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function info(): Promise<Json> {
  return call("get_networking_info", { blueprintPath: BP });
}

/** One CDO property as Unreal export text. */
async function cdoText(propertyName: string): Promise<string> {
  const res = await call("get_cdo_properties", { className: BP_CLASS, propertyNames: [propertyName] });
  return String((res.properties as Json)[propertyName]);
}

async function varFlags(): Promise<string[]> {
  const res = await call("reflect_class", { className: BP_CLASS });
  const prop = (res.properties as Json[]).find((p) => p.name === VAR);
  expect(prop, `${VAR} on ${BP_CLASS}`).toBeDefined();
  return (prop!.flags as string[] | undefined) ?? [];
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: BP, force: true });
  await call("create_blueprint", { assetPath: BP, parentClass: "Actor" });
  await call("add_variable", { assetPath: BP, name: VAR, varType: "float" });
  await call("compile_blueprint", { assetPath: BP });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: BP, force: true });
    disconnectBridge();
  }
});

describe("networking CDO settings", () => {
  it("get_networking_info reports the defaults of a fresh Actor Blueprint", async () => {
    const before = await info();
    expect(before.blueprintPath).toBe(BP);
    expect(before.replicates).toBe(false);
    expect(before.alwaysRelevant).toBe(false);
    expect(before.replicateMovement).toBe(false);
    expect(Number(before.netPriority)).toBeCloseTo(1, 3);
  }, 120_000);

  it("set_replicates turns replication on and get_networking_info reads it back", async () => {
    const set = await call("set_replicates", { blueprintPath: BP, replicates: true });
    expect(set.previousReplicates).toBe(false);
    expect(set.unchanged).toBe(false);
    expect((await info()).replicates).toBe(true);
    const again = await call("set_replicates", { blueprintPath: BP, replicates: true });
    expect(again.unchanged).toBe(true);
  }, 120_000);

  it("configure_net_update_frequency writes both frequencies", async () => {
    const set = await call("configure_net_update_frequency", { blueprintPath: BP, netUpdateFrequency: 12, minNetUpdateFrequency: 3 });
    expect(set.unchanged).toBe(false);
    const after = await info();
    expect(Number(after.netUpdateFrequency)).toBeCloseTo(12, 3);
    expect(Number(after.minNetUpdateFrequency)).toBeCloseTo(3, 3);
  }, 120_000);

  it("set_net_dormancy stores the named dormancy", async () => {
    const set = await call("set_net_dormancy", { blueprintPath: BP, dormancy: "DORM_DormantAll" });
    expect(set.dormancy).toBe("DORM_DormantAll");
    // ENetDormancy: DORM_DormantAll is 2.
    expect(Number((await info()).netDormancy)).toBe(2);
  }, 120_000);

  it("set_always_relevant flips bAlwaysRelevant", async () => {
    const set = await call("set_always_relevant", { blueprintPath: BP, alwaysRelevant: true });
    expect(set.previousAlwaysRelevant).toBe(false);
    expect((await info()).alwaysRelevant).toBe(true);
  }, 120_000);

  it("set_net_priority stores the new priority", async () => {
    await call("set_net_priority", { blueprintPath: BP, netPriority: 3.5 });
    expect(Number((await info()).netPriority)).toBeCloseTo(3.5, 3);
  }, 120_000);

  it("set_replicate_movement turns movement replication on", async () => {
    await call("set_replicate_movement", { blueprintPath: BP, replicateMovement: true });
    expect((await info()).replicateMovement).toBe(true);
  }, 120_000);

  it("set_only_relevant_to_owner writes bOnlyRelevantToOwner on the CDO", async () => {
    expect(await cdoText("bOnlyRelevantToOwner")).toBe("False");
    const set = await call("set_only_relevant_to_owner", { blueprintPath: BP, onlyRelevantToOwner: true });
    expect(set.previousOnlyRelevantToOwner).toBe(false);
    expect(await cdoText("bOnlyRelevantToOwner")).toBe("True");
  }, 120_000);

  it("set_net_load_on_client clears bNetLoadOnClient on the CDO", async () => {
    expect(await cdoText("bNetLoadOnClient")).toBe("True");
    const set = await call("set_net_load_on_client", { blueprintPath: BP, loadOnClient: false });
    expect(set.previousLoadOnClient).toBe(true);
    expect(await cdoText("bNetLoadOnClient")).toBe("False");
  }, 120_000);

  it("configure_net_cull_distance stores the squared distance", async () => {
    const set = await call("configure_net_cull_distance", { blueprintPath: BP, netCullDistanceSquared: 4_000_000 });
    expect(set.unchanged).toBe(false);
    expect(Number(set.storedNetCullDistanceSquared)).toBeCloseTo(4_000_000, 0);
    expect(Number(await cdoText("NetCullDistanceSquared"))).toBeCloseTo(4_000_000, 0);
  }, 120_000);

  it("set_property_replicated marks a Blueprint variable Replicated and clears it again", async () => {
    expect(await varFlags()).not.toContain("Replicated");

    const on = await call("set_property_replicated", { blueprintPath: BP, variableName: VAR, replicationType: "Replicated" });
    expect(on.previousReplicationType).toBe("None");
    expect(await varFlags()).toContain("Replicated");

    const off = await call("set_property_replicated", { blueprintPath: BP, variableName: VAR, replicationType: "None" });
    expect(off.previousReplicationType).toBe("Replicated");
    expect(await varFlags()).not.toContain("Replicated");
  }, 120_000);
});
