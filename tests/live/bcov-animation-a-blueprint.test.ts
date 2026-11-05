// @live-editor headless
/**
 * AnimBlueprint and blendspace authoring on the engine tutorial skeleton:
 * blueprint creation, state machine states and transitions, motion matching
 * and pose history nodes, node function binding, and both blendspace kinds.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/AnimA/Blueprint";
const ENGINE = "/Engine/Tutorial/SubEditors/TutorialAssets/Character";
const SKELETON = `${ENGINE}/TutorialTPP_Skeleton`;
const WALK = `${DIR}/AS_BCovA_BpWalk`;
const ABP = `${DIR}/ABP_BCovA`;
const DATABASE = `${DIR}/PSD_BCovA_Bp`;
const DATABASE_SCHEMA = `${DIR}/PSD_BCovA_Bp_Schema`;
const BLENDSPACE = `${DIR}/BS_BCovA`;
const BLENDSPACE_1D = `${DIR}/BS1D_BCovA`;
const ASSETS = [ABP, BLENDSPACE, BLENDSPACE_1D, DATABASE, DATABASE_SCHEMA, WALK];

type Json = Record<string, unknown>;
type AnimNode = { id: string; class: string; animNodeStruct?: string; node?: Json };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function animNode(id: string): Promise<AnimNode | undefined> {
  const inspected = await call("inspect_anim_nodes", { assetPath: ABP });
  return (inspected.nodes as AnimNode[]).find((n) => n.id === id);
}

beforeAll(async () => {
  bridge = await getBridge();
  for (const path of ASSETS) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
  await call("duplicate_asset", { sourcePath: `${ENGINE}/Tutorial_Walk_Fwd`, destinationPath: WALK });
  await call("create_pose_search_database", { name: "PSD_BCovA_Bp", packagePath: DIR, skeletonPath: SKELETON });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    for (const path of ASSETS) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("anim blueprint authoring", () => {
  it("create_anim_blueprint targets the skeleton, and add_state plus add_transition build a readable state machine", async () => {
    const created = await call("create_anim_blueprint", { name: "ABP_BCovA", skeletonPath: SKELETON, packagePath: DIR });
    expect(created.class).toBe("AnimBlueprint");
    const blueprint = await call("read_anim_blueprint", { assetPath: ABP });
    expect(String(blueprint.targetSkeleton)).toContain("TutorialTPP_Skeleton");

    await call("create_state_machine", { assetPath: ABP, name: "BCovLoco" });
    await call("add_state", { assetPath: ABP, stateMachineName: "BCovLoco", stateName: "BCovIdle" });
    await call("add_state", { assetPath: ABP, stateMachineName: "BCovLoco", stateName: "BCovMove" });
    const transition = await call("add_transition", {
      assetPath: ABP, stateMachineName: "BCovLoco", fromState: "BCovIdle", toState: "BCovMove", blendDuration: 0.35,
    });

    const machine = await call("read_state_machine", { assetPath: ABP, stateMachineName: "BCovLoco" });
    const states = (machine.states as Array<{ name: string }>).map((s) => s.name);
    expect(states).toEqual(expect.arrayContaining(["BCovIdle", "BCovMove"]));
    const transitions = machine.transitions as Array<{ fromState: string; toState: string; transitionGuid: string; blendDuration: number }>;
    const edge = transitions.find((t) => t.fromState === "BCovIdle" && t.toState === "BCovMove");
    expect(edge?.blendDuration).toBeCloseTo(0.35, 3);
    expect(edge?.transitionGuid).toBe(transition.transitionGuid);
  }, 180_000);

  it("add_motion_matching_node and add_pose_history_node place nodes inspect_anim_nodes reads back", async () => {
    const mm = await call("add_motion_matching_node", { assetPath: ABP, databasePath: DATABASE });
    const mmNode = await animNode(String(mm.nodeGuid));
    expect(mmNode?.animNodeStruct).toContain("MotionMatching");
    expect(JSON.stringify(mmNode?.node)).toContain("PSD_BCovA_Bp");

    const history = await call("add_pose_history_node", { assetPath: ABP, poseCount: 7 });
    expect(history.insertedBeforeOutput).toBe(true);
    const historyNode = await animNode(String(history.nodeGuid));
    expect(historyNode?.animNodeStruct).toContain("HistoryCollector");
    expect(String(historyNode?.node?.PoseCount)).toBe("7");

    const filtered = await call("inspect_anim_nodes", { assetPath: ABP, nodeClass: "MotionMatching" });
    expect((filtered.nodes as AnimNode[]).map((n) => n.id)).toEqual([String(mm.nodeGuid)]);
  }, 180_000);

  it("bind_anim_node_function binds a blueprint function into the node's update slot", async () => {
    const history = await call("inspect_anim_nodes", { assetPath: ABP, nodeClass: "HistoryCollector" });
    const nodeGuid = (history.nodes as AnimNode[])[0].id;
    await call("create_function", { assetPath: ABP, functionName: "BCovOnUpdate" });

    const bound = await call("bind_anim_node_function", { assetPath: ABP, nodeGuid, functionName: "BCovOnUpdate" });
    expect(bound.binding).toBe("update");
    expect(bound.previousFunctionName).toBe("None");

    const graph = await call("read_anim_graph", { assetPath: ABP });
    const node = (graph.nodes as Array<{ id: string; properties: Array<{ name: string; value: string }> }>).find((n) => n.id === nodeGuid);
    expect(node?.properties.find((p) => p.name === "UpdateFunction")?.value).toContain("BCovOnUpdate");

    const again = await call("bind_anim_node_function", { assetPath: ABP, nodeGuid, functionName: "BCovOnUpdate" });
    expect(again.previousFunctionName).toBe("BCovOnUpdate");
    expect(again.unchanged).toBe(true);
  }, 180_000);
});

describe("blendspace authoring", () => {
  it("create_blendspace sets both axes and add_blend_sample places a sample at its position", async () => {
    await call("create_blendspace", {
      name: "BS_BCovA", skeletonPath: SKELETON, packagePath: DIR,
      axisHorizontal: "BCovSpeed", horizontalMin: 0, horizontalMax: 600, axisVertical: "BCovLean", verticalMin: -90, verticalMax: 90,
    });
    const axes = (await call("read_blendspace", { assetPath: BLENDSPACE })).axes as Array<{ displayName: string; min: number; max: number }>;
    expect(axes[0]).toMatchObject({ displayName: "BCovSpeed", min: 0, max: 600 });
    expect(axes[1]).toMatchObject({ displayName: "BCovLean", min: -90, max: 90 });

    const added = await call("add_blend_sample", { assetPath: BLENDSPACE, animation: WALK, position: { x: 300, y: 45 } });
    const read = await call("read_blendspace", { assetPath: BLENDSPACE });
    const samples = read.samples as Array<{ animation: string; sampleValue: { x: number; y: number } }>;
    const sample = samples[Number(added.sampleIndex)];
    expect(sample.animation).toContain("AS_BCovA_BpWalk");
    expect(sample.sampleValue.x).toBeCloseTo(300, 3);
    expect(sample.sampleValue.y).toBeCloseTo(45, 3);
  }, 120_000);

  it("create_blendspace_1d sets its one axis and takes a sample on it", async () => {
    const created = await call("create_blendspace_1d", {
      name: "BS1D_BCovA", skeletonPath: SKELETON, packagePath: DIR, axisName: "BCovRate", axisMin: 0, axisMax: 250,
    });
    expect(created.class).toBe("BlendSpace1D");
    const axes = (await call("read_blendspace", { assetPath: BLENDSPACE_1D })).axes as Array<{ displayName: string; max: number }>;
    expect(axes[0]).toMatchObject({ displayName: "BCovRate", max: 250 });

    await call("add_blend_sample", { assetPath: BLENDSPACE_1D, animation: WALK, x: 125 });
    const read = await call("read_blendspace", { assetPath: BLENDSPACE_1D });
    expect(read.sampleCount).toBe(1);
    expect((read.samples as Array<{ sampleValue: { x: number } }>)[0].sampleValue.x).toBeCloseTo(125, 3);
  }, 120_000);
});
