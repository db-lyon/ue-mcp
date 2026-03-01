import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;

beforeAll(async () => { bridge = await getBridge(); });
afterAll(() => disconnectBridge());

describe("reflection — read", () => {
  it("reflect_class (Actor)", async () => {
    const r = await callBridge(bridge, "reflect_class", { className: "Actor" });
    expect(r.ok, r.error).toBe(true);
  });

  it("reflect_class (Character)", async () => {
    const r = await callBridge(bridge, "reflect_class", { className: "Character", includeInherited: true });
    expect(r.ok, r.error).toBe(true);
  });

  it("reflect_struct (FVector)", async () => {
    const r = await callBridge(bridge, "reflect_struct", { structName: "/Script/CoreUObject.Vector" });
    expect(r.ok, r.error).toBe(true);
  });

  it("reflect_enum (ECollisionChannel)", async () => {
    const r = await callBridge(bridge, "reflect_enum", { enumName: "/Script/Engine.ECollisionChannel" });
    expect(r.ok, r.error).toBe(true);
  });

  it("list_classes (Actor children, limit 10)", async () => {
    const r = await callBridge(bridge, "list_classes", { parentFilter: "Actor", limit: 10 });
    expect(r.ok, r.error).toBe(true);
  });

  it("list_gameplay_tags", async () => {
    const r = await callBridge(bridge, "list_gameplay_tags");
    expect(r.ok, r.error).toBe(true);
  });
});

describe("reflection — write (safe)", () => {
  it("create_gameplay_tag", async () => {
    const r = await callBridge(bridge, "create_gameplay_tag", {
      tag: "MCPTest.SmokeTest", comment: "Created by smoke test",
    });
    expect(r.ok, r.error).toBe(true);
  });
});
