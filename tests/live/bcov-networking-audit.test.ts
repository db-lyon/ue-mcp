// @live-editor headless
/**
 * networking(audit_replication): a throwaway Actor Blueprint built with one of
 * each replication mistake the audit reports, then audited, fixed and audited
 * again, plus a native engine class whose lifetime conditions are known.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/NetAudit";
const BP = `${DIR}/BP_BCov_NetAudit`;
const VAR = "BCovAuditHealth";

type Json = Record<string, unknown>;
type Finding = { kind: string; member?: string };
type Row = {
  class: string; kind: string; assetPath?: string;
  replication?: Json;
  properties: Array<{ name: string; condition?: string; registered: boolean; repNotify?: string; pushBased?: boolean }>;
  rpcs: Array<{ name: string; type: string; reliable: boolean; validated: boolean }>;
  findings: Finding[];
};
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let scratch = "";

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function auditRow(params: Json = {}): Promise<{ res: Json; row: Row }> {
  const res = await call("audit_replication", { path: DIR, includeNative: false, ...params });
  const row = (res.classes as Row[]).find((c) => c.assetPath === BP);
  expect(row, JSON.stringify(res.classes)).toBeDefined();
  return { res, row: row! };
}

const kinds = (row: Row): string[] => row.findings.map((f) => f.kind);

beforeAll(async () => {
  bridge = await getBridge();
  scratch = fs.mkdtempSync(path.join(os.tmpdir(), "bcov-netaudit-"));
  await callBridge(bridge, "delete_asset", { assetPath: BP, force: true });
  await call("create_blueprint", { assetPath: BP, parentClass: "Actor" });
  await call("add_variable", { assetPath: BP, name: VAR, varType: "float" });
  await call("set_property_replicated", { blueprintPath: BP, variableName: VAR, replicationType: "Replicated" });
  await call("add_custom_event", { assetPath: BP, eventName: "BCovMulticastBoom", netMode: "multicast", reliable: true });
  await call("add_custom_event", { assetPath: BP, eventName: "BCovServerDo", netMode: "server", reliable: false });
  await call("create_function", { assetPath: BP, functionName: "OnRep_BCovOrphan" });
  await call("compile_blueprint", { assetPath: BP });
}, 180_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    await callBridge(bridge, "delete_asset", { assetPath: BP, force: true });
  } finally {
    if (scratch) fs.rmSync(scratch, { recursive: true, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("audit_replication on a Blueprint", () => {
  it("lists the replicated property, both RPCs and the actor settings", async () => {
    const { res, row } = await auditRow({ requireServerValidation: true });
    expect(Number(res.blueprintsScanned)).toBeGreaterThanOrEqual(1);
    expect(row.kind).toBe("blueprint");
    expect(row.replication?.replicates).toBe(false);
    expect(typeof row.replication?.netDormancy).toBe("string");

    const prop = row.properties.find((p) => p.name === VAR);
    expect(prop, JSON.stringify(row.properties)).toBeDefined();
    expect(prop!.registered).toBe(true);
    expect(prop!.condition).toBe("COND_None");

    const multicast = row.rpcs.find((r) => r.name === "BCovMulticastBoom");
    expect(multicast).toMatchObject({ type: "NetMulticast", reliable: true });
    const server = row.rpcs.find((r) => r.name === "BCovServerDo");
    expect(server).toMatchObject({ type: "Server", reliable: false, validated: false });
  }, 120_000);

  it("reports each mistake the Blueprint carries", async () => {
    const { res, row } = await auditRow({ requireServerValidation: true });
    const found = kinds(row);
    expect(found).toContain("replicatedPropertyOnNonReplicatingClass");
    expect(found).toContain("reliableMulticast");
    expect(found).toContain("serverRpcWithoutValidation");
    expect(found).toContain("onRepWithoutReplicatedProperty");
    expect(row.findings.find((f) => f.kind === "onRepWithoutReplicatedProperty")?.member).toBe("OnRep_BCovOrphan");
    const totals = res.findingsByKind as Record<string, number>;
    expect(totals.reliableMulticast).toBeGreaterThanOrEqual(1);
  }, 120_000);

  it("drops the findings that no longer hold once replication is on and validation is not required", async () => {
    await call("set_replicates", { blueprintPath: BP, replicates: true });
    const { row } = await auditRow({ requireServerValidation: false });
    expect(row.replication?.replicates).toBe(true);
    const found = kinds(row);
    expect(found).not.toContain("replicatedPropertyOnNonReplicatingClass");
    expect(found).not.toContain("serverRpcWithoutValidation");
    expect(found).toContain("reliableMulticast");
  }, 120_000);

  it("className narrows the audit to the class and its subclasses", async () => {
    const res = await call("audit_replication", { path: DIR, className: BP, includeNative: false });
    const rows = res.classes as Row[];
    expect(rows.length).toBe(1);
    expect(rows[0].assetPath).toBe(BP);
  }, 120_000);

  it("dumpToFile writes every row to the requested file instead of inline", async () => {
    const out = path.join(scratch, "replication.json");
    const res = await call("audit_replication", { path: DIR, includeNative: false, dumpToFile: true, outputPath: out });
    expect(res.dumpedToFile).toBe(true);
    expect(res.classes).toBeUndefined();
    const dumped = JSON.parse(fs.readFileSync(String(res.outputPath), "utf8")) as { classes: Row[] };
    expect(dumped.classes.some((c) => c.assetPath === BP)).toBe(true);
  }, 120_000);
});

describe("audit_replication on native classes", () => {
  it("reads lifetime conditions from GetLifetimeReplicatedProps", async () => {
    const res = await call("audit_replication", {
      path: "/Script/Engine", className: "PlayerState", includeEngine: true, includeBlueprints: false, limit: 50,
    });
    const row = (res.classes as Row[]).find((c) => c.class === "PlayerState");
    expect(row, JSON.stringify((res.classes as Row[]).map((c) => c.class))).toBeDefined();
    expect(row!.kind).toBe("native");
    const byName = new Map(row!.properties.map((p) => [p.name, p]));
    expect(byName.get("CompressedPing")?.condition).toBe("COND_SkipOwner");
    expect(byName.get("PlayerId")?.condition).toBe("COND_InitialOnly");
    expect(byName.get("PlayerId")?.pushBased).toBe(true);
  }, 120_000);

  it("refuses a limit outside 1..5000", async () => {
    const r = await callBridge(bridge, "audit_replication", { limit: 0 });
    expect(succeeded(r)).toBe(false);
  }, 60_000);
});
