// @live-editor headless
/**
 * AnimBlueprint and state machine authoring read back through read_anim_blueprint,
 * read_anim_graph and read_state_machine: state animation, transition blend and
 * condition, entry wiring, removals and the target skeleton. Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const ENGINE = "/Engine/Tutorial/SubEditors/TutorialAssets/Character";
const ENGINE_SKELETON = `${ENGINE}/TutorialTPP_Skeleton`;
const DIR = `${TEST_PREFIX}/BCov/AnimB/Graph`;
const IDLE = `${DIR}/AS_BCovIdle`;
const ABP = `${DIR}/ABP_BCovGraph`;
const ABP_SKEL = `${DIR}/ABP_BCovSkel`;
const SKELETON = `${DIR}/SK_BCovGraph`;
const SM = "BCovSM";
const ASSETS = [ABP, ABP_SKEL, IDLE, SKELETON];

type Json = Record<string, unknown>;
type State = { name: string; animAsset?: string };
type Transition = { fromState: string; toState: string; transitionGuid: string; boundGraph?: string; blendDuration: number; logicType: string };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function machine(name = SM): Promise<{ states: State[]; transitions: Transition[] }> {
  const read = await call("read_state_machine", { assetPath: ABP, stateMachineName: name });
  return { states: read.states as State[], transitions: read.transitions as Transition[] };
}

function between(transitions: Transition[], from: string, to: string): Transition | undefined {
  return transitions.find((t) => t.fromState === from && t.toState === to);
}

beforeAll(async () => {
  bridge = await getBridge();
  for (const path of ASSETS) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
  await call("duplicate_asset", { sourcePath: `${ENGINE}/Tutorial_Idle`, destinationPath: IDLE });
  await call("duplicate_asset", { sourcePath: ENGINE_SKELETON, destinationPath: SKELETON });
  await call("create_anim_blueprint", { name: "ABP_BCovGraph", skeletonPath: ENGINE_SKELETON, packagePath: DIR });
  await call("create_anim_blueprint", { name: "ABP_BCovSkel", skeletonPath: ENGINE_SKELETON, packagePath: DIR });
  await call("add_variable", { assetPath: ABP, name: "bBCovGo", varType: "bool" });
  await call("add_variable", { assetPath: ABP, name: "bBCovAlt", varType: "bool" });
  await call("create_state_machine", { assetPath: ABP, name: SM });
  for (const stateName of ["Idle", "Walk", "Extra"]) {
    await call("add_state", { assetPath: ABP, stateMachineName: SM, stateName });
  }
  await call("add_transition", { assetPath: ABP, stateMachineName: SM, fromState: "Idle", toState: "Walk" });
  await call("add_transition", { assetPath: ABP, stateMachineName: SM, fromState: "Walk", toState: "Idle" });
  await call("add_transition", { assetPath: ABP, stateMachineName: SM, fromState: "Idle", toState: "Extra" });
  await call("compile_blueprint", { assetPath: ABP });
}, 300_000);

afterAll(async () => {
  if (bridge) {
    for (const path of ASSETS) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
    disconnectBridge();
  }
});

describe("anim blueprint reads", () => {
  it("read_anim_blueprint reports the skeleton and the variables added", async () => {
    const read = await call("read_anim_blueprint", { assetPath: ABP });
    expect(String(read.targetSkeleton)).toContain("TutorialTPP_Skeleton");
    const vars = (read.variables as Array<{ name: string }>).map((v) => v.name);
    expect(vars).toContain("bBCovGo");
    expect(vars).toContain("bBCovAlt");
  }, 120_000);

  it("read_anim_graph lists the state machine node in the AnimGraph", async () => {
    const graph = await call("read_anim_graph", { assetPath: ABP });
    const nodes = graph.nodes as Array<{ class: string; title: string }>;
    expect(graph.nodeCount).toBe(nodes.length);
    const smNode = nodes.find((n) => n.class.includes("StateMachine"));
    expect(smNode, JSON.stringify(nodes.map((n) => n.class))).toBeDefined();
    expect(smNode?.title).toContain(SM);
  }, 120_000);

  it("read_state_machine lists the states and transitions authored", async () => {
    const { states, transitions } = await machine();
    expect(states.map((s) => s.name).sort()).toEqual(["Extra", "Idle", "Walk"]);
    expect(between(transitions, "Idle", "Walk")).toBeDefined();
    expect(between(transitions, "Walk", "Idle")).toBeDefined();
  }, 120_000);
});

describe("state machine authoring", () => {
  it("set_state_animation puts the clip in the state", async () => {
    await call("set_state_animation", { assetPath: ABP, stateMachineName: SM, stateName: "Idle", animAssetPath: IDLE });
    const idle = (await machine()).states.find((s) => s.name === "Idle");
    expect(idle?.animAsset).toContain("AS_BCovIdle");
  }, 120_000);

  it("set_transition_blend writes the crossfade and logic", async () => {
    await call("set_transition_blend", {
      assetPath: ABP, stateMachineName: SM, fromState: "Idle", toState: "Walk", blendDuration: 0.45, blendLogic: "Inertialization",
    });
    const t = between((await machine()).transitions, "Idle", "Walk");
    expect(t?.blendDuration).toBeCloseTo(0.45, 4);
    expect(t?.logicType).toBe("Inertialization");
  }, 120_000);

  it("set_transition_condition wires the variable into the rule graph", async () => {
    const t = between((await machine()).transitions, "Idle", "Walk");
    expect(t?.boundGraph).toBeTruthy();
    await call("set_transition_condition", { assetPath: ABP, stateMachineName: SM, fromState: "Idle", toState: "Walk", variableName: "bBCovGo" });
    const rule = await call("read_anim_graph", { assetPath: ABP, graphName: t!.boundGraph });
    expect(JSON.stringify(rule.nodes)).toContain("bBCovGo");

    const second = await call("set_transition_condition", {
      assetPath: ABP, stateMachineName: SM, transitionGuid: t!.transitionGuid, variableName: "bBCovAlt", negate: true,
    });
    expect(second.previousVariableName).toBe("bBCovGo");
    const after = JSON.stringify((await call("read_anim_graph", { assetPath: ABP, graphName: t!.boundGraph })).nodes);
    expect(after).toContain("bBCovAlt");
    expect(after).not.toContain("bBCovGo");
  }, 120_000);

  it("set_state_machine_entry points the entry at a state and keeps it", async () => {
    const first = await call("set_state_machine_entry", { assetPath: ABP, stateMachineName: SM, stateName: "Idle" });
    expect(first.entryState).toBe("Idle");
    const moved = await call("set_state_machine_entry", { assetPath: ABP, stateMachineName: SM, stateName: "Walk" });
    expect(moved.previousEntryState).toBe("Idle");
    const replay = await call("set_state_machine_entry", { assetPath: ABP, stateMachineName: SM, stateName: "Walk" });
    expect(replay.unchanged).toBe(true);
    expect(replay.entryState).toBe("Walk");
  }, 120_000);

  it("remove_transition removes the transition between two states", async () => {
    expect(between((await machine()).transitions, "Idle", "Extra")).toBeDefined();
    const removed = await call("remove_transition", { assetPath: ABP, stateMachineName: SM, fromState: "Idle", toState: "Extra" });
    expect(removed.removedTransitionCount).toBe(1);
    const { transitions } = await machine();
    expect(between(transitions, "Idle", "Extra")).toBeUndefined();
    expect(between(transitions, "Idle", "Walk")).toBeDefined();
  }, 120_000);

  it("remove_state removes the state and its transitions", async () => {
    await call("add_transition", { assetPath: ABP, stateMachineName: SM, fromState: "Extra", toState: "Idle" });
    const removed = await call("remove_state", { assetPath: ABP, stateMachineName: SM, stateName: "Extra" });
    expect(removed.removedTransitionCount).toBe(1);
    const { states, transitions } = await machine();
    expect(states.map((s) => s.name)).not.toContain("Extra");
    expect(transitions.some((t) => t.fromState === "Extra" || t.toState === "Extra")).toBe(false);
  }, 120_000);

  it("remove_state_machine removes a second machine and leaves the first", async () => {
    await call("create_state_machine", { assetPath: ABP, name: "BCovSM2" });
    await call("add_state", { assetPath: ABP, stateMachineName: "BCovSM2", stateName: "Only" });
    expect((await machine("BCovSM2")).states.map((s) => s.name)).toContain("Only");

    const removed = await call("remove_state_machine", { assetPath: ABP, stateMachineName: "BCovSM2" });
    expect(removed.removedStates).toBe(1);
    const gone = await callBridge(bridge, "read_state_machine", { assetPath: ABP, stateMachineName: "BCovSM2" });
    expect(succeeded(gone)).toBe(false);
    expect((await machine()).states.length).toBeGreaterThan(0);
  }, 120_000);

  it("set_anim_blueprint_skeleton retargets the blueprint to another skeleton", async () => {
    const set = await call("set_anim_blueprint_skeleton", { assetPath: ABP_SKEL, skeletonPath: SKELETON });
    expect(String(set.previousSkeletonPath)).toContain("TutorialTPP_Skeleton");
    const read = await call("read_anim_blueprint", { assetPath: ABP_SKEL });
    expect(String(read.targetSkeleton)).toContain("SK_BCovGraph");
  }, 120_000);
});
