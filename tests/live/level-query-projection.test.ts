// @live-editor headless
/**
 * level(query_components) as a per-actor audit: one row per placed actor with
 * its attach parent, component properties read through dotted struct paths,
 * names that resolve to nothing reported rather than dropped, and a predicate
 * on props.X that projects X by itself.
 * Refs #1530
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const PREFIX = "BCov_QProj_";
const PARENT = `${PREFIX}Parent`;
const CHILD = `${PREFIX}Child`;
const PROFILE = "BodyInstance.CollisionProfileName";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const rows = (v: unknown): Json[] => (Array.isArray(v) ? (v as Json[]) : []);
const obj = (v: unknown): Json => (v && typeof v === "object" ? (v as Json) : {});

let bridge: EditorBridge;

async function call(method: string, params: Json = {}): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function wipe(): Promise<void> {
  await callBridge(bridge, "delete_actor", { actorLabel: CHILD });
  await callBridge(bridge, "delete_actor", { actorLabel: PARENT });
}

beforeAll(async () => {
  bridge = await getBridge();
  await wipe();
  for (const [label, y] of [[PARENT, -14000], [CHILD, -14200]] as const) {
    await call("place_actor", {
      actorClass: "StaticMeshActor", staticMesh: "/Engine/BasicShapes/Cube", label,
      location: { x: 0, y, z: 300 },
    });
  }
  await call("attach_actor", { childLabel: CHILD, parentLabel: PARENT });
  await call("set_collision_profile", { actorLabel: CHILD, profileName: "OverlapAll" });
}, 120_000);

afterAll(async () => {
  if (bridge) {
    await wipe();
    disconnectBridge();
  }
});

describe("query_components per-actor audit", () => {
  it("returns one row per actor with its attach parent, dotted component paths and missing names", async () => {
    const found = await call("query_components", {
      actorLabelPrefix: PREFIX,
      fields: ["attachment"],
      where: [{ field: "attachment.isRoot", op: "isTrue" }],
      propertyNames: [PROFILE, "NoSuchComponentProp"],
      actorPropertyNames: ["Tags", "NoSuchActorProp"],
    });
    const byLabel = new Map(rows(found.rows).map((r) => [String(r.actorLabel), r]));
    expect([...byLabel.keys()].sort()).toEqual([CHILD, PARENT].sort());

    const parent = byLabel.get(PARENT)!;
    const child = byLabel.get(CHILD)!;
    expect(obj(child.attachment).parentActor).toBe(parent.actorPath);
    expect(obj(parent.attachment).parentActor).toBe("");

    expect(obj(child.props)[PROFILE]).toBe("OverlapAll");
    expect(typeof obj(parent.props)[PROFILE]).toBe("string");
    expect(obj(parent.props)[PROFILE]).not.toBe("OverlapAll");
    expect(Object.keys(obj(child.missingProps))).toEqual(["NoSuchComponentProp"]);
    expect("Tags" in obj(child.actorProps)).toBe(true);
    expect(Object.keys(obj(child.missingActorProps))).toEqual(["NoSuchActorProp"]);
  }, 60_000);

  it("filters on a dotted props path without a separate propertyNames entry", async () => {
    const found = await call("query_components", {
      actorLabelPrefix: PREFIX,
      where: [
        { field: "attachment.isRoot", op: "isTrue" },
        { field: `props.${PROFILE}`, op: "eq", value: "OverlapAll" },
      ],
    });
    expect(rows(found.rows).map((r) => r.actorLabel)).toEqual([CHILD]);
  }, 60_000);
});
