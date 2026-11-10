// @live-editor headless
/**
 * Learning Agents observations: the observation types read from the engine,
 * then an interactor Blueprint whose SpecifyAgentObservation override is built
 * here (a set observation over a float element, with a cross-attention query
 * where the engine has one) and read back node by node. Needs the Learning
 * Agents plugin, which the test project enables.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/LearningAgents";
const BP = `${DIR}/BP_BCovInteractor`;
const NOT_INTERACTOR = `${DIR}/BP_BCovNotInteractor`;

type Json = Record<string, unknown>;
type ObservationType = { function: string; supportsCrossAttention: boolean; parameters?: Array<{ name: string; type: string; default?: string }> };
type Link = { nodeId: string; pin: string };
type Input = { name: string; default?: string; linkedTo?: Link[] };
type Node = { nodeId: string; function: string; inputs: Input[] };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;
let crossAttention = false;
let setId = "";
let elementId = "";
let queryId = "";

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function read(): Promise<{ res: Json; nodes: Map<string, Node> }> {
  const res = await call("learning_agents_read_observations", { blueprintPath: BP });
  const nodes = new Map((res.observations as Node[]).map((n) => [n.nodeId, n]));
  return { res, nodes };
}

const input = (node: Node | undefined, name: string): Input | undefined => node?.inputs.find((i) => i.name === name);

beforeAll(async () => {
  bridge = await getBridge();
  for (const asset of [BP, NOT_INTERACTOR]) await callBridge(bridge, "delete_asset", { assetPath: asset, force: true });
  await call("create_blueprint", { assetPath: BP, parentClass: "/Script/LearningAgents.LearningAgentsInteractor" });
  await call("create_blueprint", { assetPath: NOT_INTERACTOR, parentClass: "Actor" });
}, 120_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    for (const asset of [BP, NOT_INTERACTOR]) await callBridge(bridge, "delete_asset", { assetPath: asset, force: true });
  } finally {
    disconnectBridge();
  }
}, 120_000);

describe("Learning Agents observation types", () => {
  it("lists the Specify functions with their parameters, read from reflection", async () => {
    const res = await call("learning_agents_list_observation_types");
    const types = res.observationTypes as ObservationType[];
    expect(Number(res.count)).toBeGreaterThan(20);
    const float = types.find((t) => t.function === "SpecifyFloatObservation");
    expect(float, JSON.stringify(types.map((t) => t.function))).toBeDefined();
    expect(float!.parameters!.some((p) => p.name === "Tag")).toBe(true);
    expect(float!.parameters!.some((p) => p.name === "Schema")).toBe(true);
  }, 60_000);

  it("marks the cross-attention observations on an engine that has them", async () => {
    const res = await call("learning_agents_list_observation_types", { includeParameters: false });
    const [major, minor] = String(res.engineVersion).split(".").map(Number);
    crossAttention = res.crossAttention === true;
    expect(crossAttention).toBe(major > 5 || (major === 5 && minor >= 9));
    const marked = res.crossAttentionTypes as string[];
    if (crossAttention) {
      for (const name of ["SpecifySetObservation", "SpecifyArrayObservation", "SpecifyMapObservation", "SpecifyInclusiveUnionObservation"]) {
        expect(marked).toContain(name);
      }
      expect(marked).not.toContain("SpecifyFloatObservation");
    } else {
      expect(marked).toEqual([]);
    }
  }, 60_000);

  it("nameFilter narrows the list", async () => {
    const res = await call("learning_agents_list_observation_types", { nameFilter: "Lidar" });
    expect((res.observationTypes as ObservationType[]).map((t) => t.function)).toEqual(["SpecifyLidarObservation"]);
  }, 60_000);
});

describe("Learning Agents interactor observations", () => {
  it("an interactor with no override reads as an empty schema", async () => {
    const { res } = await read();
    expect(res.hasOverride).toBe(false);
    expect(res.count).toBe(0);
  }, 60_000);

  it("learning_agents_add_observation creates the override and returns a set observation from it", async () => {
    const res = await call("learning_agents_add_observation", {
      blueprintPath: BP, observationType: "Set", settings: { MaxNum: 8, Tag: "BCovSet" }, connectTo: "result",
    });
    expect(res.created).toBe(true);
    expect(res.createdOverride).toBe(true);
    expect(res.compiled).toBe(true);
    expect((res.rollback as Json).method).toBe("delete_node");
    setId = String(res.nodeId);
  }, 120_000);

  it("feeds a float observation into the set's Element, and a query on engines with cross-attention", async () => {
    const element = await call("learning_agents_add_observation", {
      blueprintPath: BP, observationType: "FloatObservation", settings: { Tag: "BCovElement" }, connectTo: setId, connectPin: "Element",
    });
    expect(element.createdOverride).toBe(false);
    expect(element.compiled).toBe(true);
    elementId = String(element.nodeId);
    if (crossAttention) {
      const query = await call("learning_agents_add_observation", {
        blueprintPath: BP, observationType: "SpecifyFloatObservation", settings: { Tag: "BCovQuery" }, connectTo: setId, connectPin: "QueryElement",
      });
      expect(query.compiled).toBe(true);
      queryId = String(query.nodeId);
    }
  }, 120_000);

  it("learning_agents_read_observations reads the tree back with its settings and links", async () => {
    const { res, nodes } = await read();
    expect(res.hasOverride).toBe(true);
    expect(res.rootNodeId).toBe(setId);
    const set = nodes.get(setId);
    expect(set?.function).toBe("SpecifySetObservation");
    expect(input(set, "MaxNum")?.default).toBe("8");
    expect(input(set, "Tag")?.default).toBe("BCovSet");
    expect(input(set, "Element")?.linkedTo?.[0]?.nodeId).toBe(elementId);
    expect(input(nodes.get(elementId), "Tag")?.default).toBe("BCovElement");
    if (crossAttention) expect(input(set, "QueryElement")?.linkedTo?.[0]?.nodeId).toBe(queryId);
    expect(res.count).toBe(crossAttention ? 3 : 2);
  }, 60_000);

  it("refuses a setting the function does not take and adds nothing", async () => {
    const before = (await read()).res.count;
    const bad = await callBridge(bridge, "learning_agents_add_observation", { blueprintPath: BP, observationType: "Float", settings: { NoSuchInput: 1 } });
    expect(succeeded(bad)).toBe(false);
    expect((await read()).res.count).toBe(before);
  }, 60_000);

  it("refuses a Blueprint that is not an interactor", async () => {
    const r = await callBridge(bridge, "learning_agents_read_observations", { blueprintPath: NOT_INTERACTOR });
    expect(succeeded(r)).toBe(false);
  }, 60_000);
});
