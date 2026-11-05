// @live-editor headless
/**
 * StateTree runtime actions against a tree running on a component in PIE:
 * reading the active state, sending an event a transition consumes, and
 * requesting a transition. PIE is started here and stopped in afterAll.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/StateTreeRuntime";
const ST = `${DIR}/ST_BCovRuntime`;
const BP = `${DIR}/BP_BCovRuntimeActor`;
const ACTOR = "BCov_StateTree_Runtime";
const EVENT = "BCov.StateTree.Go";
const DELAY = "/Script/StateTreeModule.StateTreeDelayTask";
const SCHEMA = "/Script/GameplayStateTreeModule.StateTreeComponentSchema";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

let bridge: EditorBridge;
let playing = false;
const ids: Record<string, string> = {};

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function pieRunning(): Promise<boolean> {
  return body(await callBridge(bridge, "pie_control", { pieAction: "status" })).isPlaying === true;
}

async function answerPiePrompt(): Promise<void> {
  const listed = body(await callBridge(bridge, "list_dialogs"));
  for (const dialog of (listed.dialogs as Json[] | undefined) ?? []) {
    const proceed = ((dialog.buttons as string[] | undefined) ?? []).find((b) => /play in editor/i.test(b));
    if (proceed) await callBridge(bridge, "respond_to_dialog", { buttonLabel: proceed });
  }
}

async function runtime(): Promise<Json> {
  return call("read_state_tree_runtime", { actorLabel: ACTOR });
}

/** Poll the running tree until `state` is active; the tree applies events and requests on a later tick. */
async function waitForActive(state: string): Promise<string[]> {
  let active: string[] = [];
  for (let i = 0; i < 40; i++) {
    active = ((await runtime()).activeStates as string[]) ?? [];
    if (active.includes(state)) break;
    await sleep(250);
  }
  return active;
}

async function wipe(): Promise<void> {
  await callBridge(bridge, "delete_actor", { actorLabel: ACTOR });
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
}

beforeAll(async () => {
  bridge = await getBridge();
  if (await pieRunning()) {
    await callBridge(bridge, "pie_control", { pieAction: "stop" });
    for (let i = 0; i < 20 && (await pieRunning()); i++) await sleep(500);
  }
  await wipe();
  await call("create_gameplay_tag", { tag: EVENT, comment: "Behavioural coverage StateTree event" });

  await call("create_state_tree", { name: "ST_BCovRuntime", packagePath: DIR, schema: SCHEMA });
  const root = ((await call("list_state_tree_states", { assetPath: ST })).states as Json[])[0];
  for (const name of ["Wait", "Evented", "Requested"]) {
    ids[name] = String((await call("add_state_tree_state", { assetPath: ST, stateId: root.id, name })).stateId);
    await call("add_state_tree_task", { assetPath: ST, stateId: ids[name], structType: DELAY, instanceProperties: { Duration: 600 } });
  }
  await call("add_state_tree_transition", {
    assetPath: ST, stateId: ids.Wait, trigger: "OnEvent", eventTag: EVENT, transitionType: "GotoState", targetStateId: ids.Evented,
  });
  const compiled = await call("compile_state_tree", { assetPath: ST });
  expect(compiled.compiled, JSON.stringify(compiled.errors)).toBe(true);

  await call("create_blueprint", { name: "BP_BCovRuntimeActor", packagePath: DIR, parentClass: "Actor" });
  const added = await call("add_state_tree_component", { blueprintPath: BP });
  await call("place_actor", { actorClass: BP, label: ACTOR, location: { x: 0, y: -12000, z: 100 } });
  await call("set_component_property", {
    actorLabel: ACTOR, componentName: String(added.component), propertyName: "StateTreeRef.StateTree", value: `${ST}.ST_BCovRuntime`,
  });

  await callBridge(bridge, "pie_control", { pieAction: "start" });
  for (let i = 0; i < 40 && !playing; i++) {
    playing = await pieRunning();
    if (!playing) {
      await answerPiePrompt();
      await sleep(1000);
    }
  }
}, 240_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "pie_control", { pieAction: "stop" });
    for (let i = 0; i < 20 && (await pieRunning()); i++) await sleep(500);
    await wipe();
    disconnectBridge();
  }
});

describe("StateTree runtime in PIE", () => {
  it("read_state_tree_runtime reports the running tree in its first child state", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    expect(await waitForActive("Wait")).toContain("Wait");
    const rt = await call("read_state_tree_runtime", { actorLabel: ACTOR, includeDebugStrings: true });
    expect(rt.running).toBe(true);
    expect(rt.runStatus).toBe("Running");
    expect(String(rt.stateTree)).toContain("ST_BCovRuntime");
    expect((rt.frames as Json[]).length).toBeGreaterThan(0);
    expect(typeof rt.debugInfo).toBe("string");
  }, 120_000);

  it("send_state_tree_event fires the OnEvent transition out of the waiting state", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    expect(await waitForActive("Wait")).toContain("Wait");
    const sent = await call("send_state_tree_event", { actorLabel: ACTOR, eventTag: EVENT, origin: "bcov" });
    expect(sent.eventTag).toBe(EVENT);
    expect((sent.pendingEvents as Json[]).some((e) => e.tag === EVENT && e.origin === "bcov")).toBe(true);
    const active = await waitForActive("Evented");
    expect(active).toContain("Evented");
    expect(active).not.toContain("Wait");
  }, 120_000);

  it("request_state_tree_transition moves the tree to the requested state", async (ctx) => {
    if (!playing) ctx.skip("PIE did not start");
    const requested = await call("request_state_tree_transition", { actorLabel: ACTOR, targetStateId: ids.Requested, priority: "High" });
    expect(String(requested.targetStateId).replace(/-/g, "").toLowerCase()).toBe(ids.Requested.replace(/-/g, "").toLowerCase());
    const active = await waitForActive("Requested");
    expect(active).toContain("Requested");
  }, 120_000);
});
