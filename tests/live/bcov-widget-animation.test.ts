// @live-editor headless
/**
 * UMG animation authoring read back through get_widget_animation and
 * read_widget_animations: create and delete, property tracks and keys, event
 * keys, and the Finished event node in the event graph.
 * Refs #1223
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/Widget";
const WBP = `${DIR}/WBP_BCov_Anim`;
const ANIM = "BCovFade";

type Json = Record<string, unknown>;
type Key = { time: number; value: number };
type Track = { propertyName?: string; sections?: Array<{ channels?: Array<{ keys?: Key[] }> }> };
type Binding = { widgetName: string; tracks: Track[] };
type EventTrack = { trackName: string; events: Array<{ time: number; functionName: string; bound: boolean }> };
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function animation(): Promise<Json> {
  return call("get_widget_animation", { assetPath: WBP, animationName: ANIM });
}

function opacityTrack(anim: Json): Track | undefined {
  const binding = (anim.bindings as Binding[]).find((b) => b.widgetName === "Txt");
  return binding?.tracks.find((t) => t.propertyName === "RenderOpacity");
}

function opacityKeys(anim: Json): Key[] {
  return (opacityTrack(anim)?.sections ?? []).flatMap((s) => (s.channels ?? []).flatMap((c) => c.keys ?? []));
}

function events(anim: Json): EventTrack["events"] {
  return (anim.eventTracks as EventTrack[]).flatMap((t) => t.events);
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_asset", { assetPath: WBP, force: true });
  await call("create_widget_blueprint", { assetPath: WBP });
  await call("add_widget", { assetPath: WBP, widgetClass: "CanvasPanel", widgetName: "Root" });
  await call("add_widget", { assetPath: WBP, widgetClass: "TextBlock", widgetName: "Txt", parentWidgetName: "Root" });
}, 180_000);

afterAll(async () => {
  if (bridge) {
    await callBridge(bridge, "delete_asset", { assetPath: WBP, force: true });
    disconnectBridge();
  }
});

describe("widget animation authoring", () => {
  it("create_widget_animation adds an animation get_widget_animation and read_widget_animations read", async () => {
    const created = await call("create_widget_animation", { assetPath: WBP, animationName: ANIM, durationSeconds: 2 });
    expect(created.animationName).toBe(ANIM);

    const anim = await animation();
    expect(anim.animationName).toBe(ANIM);
    expect(Number(anim.endTime) - Number(anim.startTime)).toBeCloseTo(2, 3);

    const all = await call("read_widget_animations", { assetPath: WBP });
    const row = (all.animations as Array<{ name: string; duration?: number }>).find((a) => a.name === ANIM);
    expect(row?.duration).toBeCloseTo(2, 3);
  }, 120_000);

  it("add and remove a property track and its keys, read back by get_widget_animation", async () => {
    const target = { assetPath: WBP, animationName: ANIM, widgetName: "Txt", propertyName: "RenderOpacity" };
    await call("add_widget_animation_track", target);
    expect(opacityTrack(await animation())).toBeDefined();

    const keyed = await call("add_widget_animation_key", { ...target, time: 0.5, value: 0.3 });
    expect(keyed.keyCount).toBe(1);
    const keys = opacityKeys(await animation());
    expect(keys).toHaveLength(1);
    expect(keys[0].time).toBeCloseTo(0.5, 3);
    expect(keys[0].value).toBeCloseTo(0.3, 3);

    await call("remove_widget_animation_key", { ...target, time: 0.5 });
    expect(opacityKeys(await animation())).toHaveLength(0);

    await call("remove_widget_animation_track", target);
    expect(opacityTrack(await animation())).toBeUndefined();
  }, 120_000);

  it("add_widget_animation_event_key and remove_widget_animation_event_key edit the event track", async () => {
    const added = await call("add_widget_animation_event_key", {
      assetPath: WBP,
      animationName: ANIM,
      functionName: "RemoveFromParent",
      time: 1,
    });
    expect(added.functionName).toBe("RemoveFromParent");
    const hit = events(await animation()).find((e) => Math.abs(e.time - 1) < 1e-3);
    expect(hit?.functionName).toBe("RemoveFromParent");
    expect(hit?.bound).toBe(true);

    const removed = await call("remove_widget_animation_event_key", { assetPath: WBP, animationName: ANIM, time: 1 });
    expect(removed.alreadyRemoved).toBe(false);
    expect(removed.removedFunctionName).toBe("RemoveFromParent");
    expect(events(await animation()).some((e) => Math.abs(e.time - 1) < 1e-3)).toBe(false);
  }, 120_000);

  it("bind_widget_animation_event adds one Finished node and unbind_widget_animation_event removes it", async () => {
    const bound = await call("bind_widget_animation_event", { assetPath: WBP, animationName: ANIM, event: "Finished" });
    const nodeName = String(bound.nodeName);
    expect(nodeName.length).toBeGreaterThan(0);
    const again = await call("bind_widget_animation_event", { assetPath: WBP, animationName: ANIM, event: "Finished" });
    expect(again.nodeName).toBe(nodeName);

    const unbound = await call("unbind_widget_animation_event", { assetPath: WBP, animationName: ANIM, event: "Finished" });
    expect(unbound.alreadyRemoved).toBe(false);
    expect(unbound.removedNodeName).toBe(nodeName);
    const second = await call("unbind_widget_animation_event", { assetPath: WBP, animationName: ANIM, event: "Finished" });
    expect(second.alreadyRemoved).toBe(true);
  }, 120_000);

  it("delete_widget_animation removes the animation read_widget_animations listed", async () => {
    const deleted = await call("delete_widget_animation", { assetPath: WBP, animationName: ANIM });
    expect(deleted.alreadyDeleted).toBe(false);
    const all = await call("read_widget_animations", { assetPath: WBP });
    expect((all.animations as Array<{ name: string }>).some((a) => a.name === ANIM)).toBe(false);
    expect(succeeded(await callBridge(bridge, "get_widget_animation", { assetPath: WBP, animationName: ANIM }))).toBe(false);
  }, 120_000);
});
