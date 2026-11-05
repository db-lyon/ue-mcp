// @live-editor headless
/**
 * Live skeletal actor edits on a placed two-component actor, each read back:
 * editor animation preview, leader pose rebinding and the transient
 * post-process AnimBP override. Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, TEST_PREFIX, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const ENGINE = "/Engine/Tutorial/SubEditors/TutorialAssets/Character";
const ENGINE_SKELETON = `${ENGINE}/TutorialTPP_Skeleton`;
const DIR = `${TEST_PREFIX}/BCov/AnimB/Live`;
const MESH = `${DIR}/SKM_BCovTPP`;
const BP = `${DIR}/BP_BCovLive`;
const POST_ABP = `${DIR}/ABP_BCovPost`;
const POST_CLASS = `${POST_ABP}.ABP_BCovPost_C`;
const LABEL = "BCov_AnimB_Live";
const ASSETS = [BP, POST_ABP, MESH];

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

/** A property on one of the actor's components, as export text. */
async function componentProperty(actorPath: string, component: string, property: string): Promise<string> {
  const viaActor = await callBridge(bridge, "get_property", { objectPath: actorPath, propertyName: `${component}.${property}` });
  if (succeeded(viaActor)) return String(body(viaActor).valueText ?? JSON.stringify(body(viaActor).value));
  const read = await call("get_property", { objectPath: `${actorPath}.${component}`, propertyName: property });
  return String(read.valueText ?? JSON.stringify(read.value));
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_actor", { actorLabel: LABEL });
  for (const path of ASSETS) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
  await call("duplicate_asset", { sourcePath: `${ENGINE}/TutorialTPP`, destinationPath: MESH });
  await call("create_anim_blueprint", { name: "ABP_BCovPost", skeletonPath: ENGINE_SKELETON, packagePath: DIR });
  await call("compile_blueprint", { assetPath: POST_ABP });
  await call("create_blueprint", { assetPath: BP, parentClass: "Actor" });
  await call("add_component", { assetPath: BP, componentClass: "SkeletalMeshComponent", componentName: "Body" });
  await call("add_component", { assetPath: BP, componentClass: "SkeletalMeshComponent", componentName: "Hat" });
  await call("compile_blueprint", { assetPath: BP });
  await call("place_actor", { actorClass: `${BP}.BP_BCovLive_C`, label: LABEL, location: { x: 0, y: -4000, z: 100 } });
  await call("set_component_property", { actorLabel: LABEL, componentName: "Body", propertyName: "SkeletalMeshAsset", value: MESH });
}, 300_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_actor", { actorLabel: LABEL });
    for (const path of ASSETS) await callBridge(bridge, "delete_asset", { assetPath: path, force: true });
    disconnectBridge();
  }
});

describe("live skeletal actor", () => {
  it("preview_animation switches the components' editor tick on and off", async () => {
    const on = await call("preview_animation", { actorLabel: LABEL, enabled: true });
    expect(on.componentsUpdated).toBe(2);
    const actorPath = String(on.actorPath);
    expect(await componentProperty(actorPath, "Body", "VisibilityBasedAnimTickOption")).toContain("AlwaysTickPoseAndRefreshBones");

    const off = await call("preview_animation", { actorLabel: LABEL, enabled: false });
    expect(off.enabled).toBe(false);
    expect(await componentProperty(actorPath, "Hat", "VisibilityBasedAnimTickOption")).toContain("OnlyTickMontagesAndRefreshBonesWhenPlayingMontages");
  }, 120_000);

  it("rebind_leader_pose makes the other component follow the body, and a replay finds it bound", async () => {
    const first = await call("rebind_leader_pose", { actorLabel: LABEL, bodyComponent: "Body" });
    expect(first.body).toBe("Body");
    expect(first.rebound).toBe(1);
    expect(first.components).toEqual(["Hat"]);

    const replay = await call("rebind_leader_pose", { actorLabel: LABEL, bodyComponent: "Body" });
    expect(replay.alreadyBoundToBody).toBe(1);
    expect(replay.unchanged).toBe(true);
    const previous = replay.previousLeaders as Array<{ component: string; previousLeader: string }>;
    expect(previous[0].previousLeader).toBe("Body");
  }, 120_000);

  it("set_live_post_process_anim_blueprint sets and clears the override", async () => {
    const set = await call("set_live_post_process_anim_blueprint", {
      actorLabel: LABEL, componentName: "Body", animBlueprintClassPath: POST_CLASS,
    });
    expect(set.alreadySet).toBe(false);
    expect(String(set.overrideClass)).toContain("ABP_BCovPost_C");

    const replay = await call("set_live_post_process_anim_blueprint", {
      actorLabel: LABEL, componentName: "Body", animBlueprintClassPath: POST_CLASS,
    });
    expect(replay.alreadySet).toBe(true);
    expect(String(replay.previousOverrideClass)).toContain("ABP_BCovPost_C");

    const cleared = await call("set_live_post_process_anim_blueprint", { actorLabel: LABEL, componentName: "Body", clear: true });
    expect(cleared.overrideClass).toBe("");
    const again = await call("set_live_post_process_anim_blueprint", {
      actorLabel: LABEL, componentName: "Body", animBlueprintClassPath: POST_CLASS,
    });
    expect(again.previousOverrideClass).toBe("");
    await call("set_live_post_process_anim_blueprint", { actorLabel: LABEL, componentName: "Body", clear: true });
  }, 120_000);
});
