// @live-editor headless
/**
 * blueprint(set_component_property) changes a component default the way the
 * Blueprint editor does: placed instances still on the old default take the
 * new one, instances that override it keep their value, and a level save
 * records neither as a stale override (#1604).
 * Refs #1604
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const HOME = "/Game/MCP_Home";
const DIR = "/Game/MCPTest/Fix1604";
const BP = `${DIR}/BP_Fix1604Box`;
const CLASS = `${BP}.BP_Fix1604Box_C`;
const FOLLOWER = "Fix1604_Follower";
const OVERRIDE = "Fix1604_Override";

type Json = Record<string, unknown>;
type Vec = [number, number, number];
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

/** A vector as {x,y,z}, {X,Y,Z} or "(X=..,Y=..,Z=..)", rounded. */
function vec(v: unknown): Vec {
  if (typeof v === "string") {
    const n = ["X", "Y", "Z"].map((k) => Number(new RegExp(`${k}=(-?[\\d.e+-]+)`).exec(v)?.[1]));
    return n.map((x) => Math.round(x * 1000) / 1000) as Vec;
  }
  const o = (v ?? {}) as Record<string, number>;
  return [o.x ?? o.X, o.y ?? o.Y, o.z ?? o.Z].map((x) => Math.round(Number(x) * 1000) / 1000) as Vec;
}

async function instanceScale(label: string): Promise<Vec> {
  const out = await call("get_component_details", {
    actorLabel: label, componentName: "Box", includeValues: true, propertyNames: ["RelativeScale3D"],
  });
  return vec(((out.component as Json).values as Json).RelativeScale3D);
}

async function templateScale(): Promise<Vec> {
  const out = await call("get_blueprint_component_property", { assetPath: BP, componentName: "Box", propertyName: "RelativeScale3D" });
  return vec(out.value);
}

const setDefault = (s: number) => call("set_blueprint_component_property", {
  assetPath: BP, componentName: "Box", propertyName: "RelativeScale3D", value: { x: s, y: s, z: s },
});

async function cleanup(): Promise<void> {
  await callBridge(bridge, "delete_actor", { actorLabel: FOLLOWER });
  await callBridge(bridge, "delete_actor", { actorLabel: OVERRIDE });
}

beforeAll(async () => {
  bridge = await getBridge();
  await call("demo_go_home");
  await cleanup();
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  await call("create_blueprint", { assetPath: BP, parentClass: "Actor" });
  await call("add_component", { assetPath: BP, componentClass: "BoxComponent", componentName: "Box" });
  await setDefault(2);
  await call("compile_blueprint", { assetPath: BP });
  await call("place_actor", { actorClass: CLASS, label: FOLLOWER, location: { x: 0, y: -17000, z: 200 } });
  await call("place_actor", { actorClass: CLASS, label: OVERRIDE, location: { x: 400, y: -17000, z: 200 } });
  await call("set_component_property", { actorLabel: OVERRIDE, componentName: "Box", propertyName: "RelativeScale3D", value: { x: 5, y: 5, z: 5 } });
}, 180_000);

afterAll(async () => {
  if (!bridge) return;
  try {
    await cleanup();
    await callBridge(bridge, "save_level", {});
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  } finally {
    disconnectBridge();
  }
}, 120_000);

describe("set_component_property propagates a new component default to placed instances (#1604)", () => {
  it("moves instances on the old default, keeps overrides, and a level save keeps both", async () => {
    expect(await instanceScale(FOLLOWER)).toEqual([2, 2, 2]);
    expect(await instanceScale(OVERRIDE)).toEqual([5, 5, 5]);

    const set = await setDefault(3);
    expect(set.updated).toBe(true);
    expect(set.instancesUpdated).toBe(1);
    expect(set.instancesOverridden).toBe(1);
    expect(await templateScale()).toEqual([3, 3, 3]);
    expect(await instanceScale(FOLLOWER)).toEqual([3, 3, 3]);
    expect(await instanceScale(OVERRIDE)).toEqual([5, 5, 5]);

    // A compile reinstances the actors; neither value may move.
    await call("compile_blueprint", { assetPath: BP });
    expect(await instanceScale(FOLLOWER)).toEqual([3, 3, 3]);
    expect(await instanceScale(OVERRIDE)).toEqual([5, 5, 5]);

    // Save, then reload the level from disk.
    await call("save_level", {});
    await call("load_level", { levelPath: HOME, discardUnsaved: true });
    expect(await instanceScale(FOLLOWER)).toEqual([3, 3, 3]);
    expect(await instanceScale(OVERRIDE)).toEqual([5, 5, 5]);

    // The saved follower holds no override: it follows the next default too.
    const again = await setDefault(4);
    expect(again.instancesUpdated).toBe(1);
    expect(await instanceScale(FOLLOWER)).toEqual([4, 4, 4]);
    expect(await instanceScale(OVERRIDE)).toEqual([5, 5, 5]);
  }, 180_000);
});
