// Regression: #717 — no way to query/set per-actor editor visibility, so both
// the query (find editor-hidden actors) and the bulk unhide needed Python.
import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge, resultArray } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
beforeAll(async () => { bridge = await getBridge(); });
afterAll(async () => {
  await callBridge(bridge, "delete_actor", { actorLabel: "MCPTest_HideCube" }).catch(() => {});
  disconnectBridge();
});

describe("level — editor visibility (#717)", () => {
  beforeAll(async () => {
    await callBridge(bridge, "place_actor", {
      actorClass: "/Script/Engine.StaticMeshActor",
      label: "MCPTest_HideCube",
      location: { x: 0, y: 0, z: 700 },
    });
  });

  it("get_outliner reports the editorHidden flag per actor", async () => {
    const r = await callBridge(bridge, "get_world_outliner", { nameFilter: "MCPTest_HideCube", limit: 10 });
    expect(r.ok, r.error).toBe(true);
    const actors = (resultArray(r.result, "actors") ?? []) as Array<Record<string, unknown>>;
    const cube = actors.find((a) => a.label === "MCPTest_HideCube");
    expect(cube).toBeDefined();
    expect(typeof cube!.editorHidden).toBe("boolean");
  });

  it("set_editor_visibility hides then shows the actor, and the filter tracks it", async () => {
    const hide = await callBridge(bridge, "set_editor_visibility", {
      actorLabels: ["MCPTest_HideCube"], hidden: true,
    });
    expect(hide.ok, hide.error).toBe(true);
    expect(Number((hide.result as Record<string, unknown>).changed)).toBeGreaterThanOrEqual(1);

    const hiddenList = await callBridge(bridge, "get_world_outliner", { editorHidden: true, limit: 500 });
    const labels = ((resultArray(hiddenList.result, "actors") ?? []) as Array<Record<string, unknown>>).map((a) => a.label);
    expect(labels).toContain("MCPTest_HideCube");

    const show = await callBridge(bridge, "set_editor_visibility", {
      actorLabels: ["MCPTest_HideCube"], hidden: false,
    });
    expect(show.ok, show.error).toBe(true);
  });

  it("requires a target", async () => {
    const r = await callBridge(bridge, "set_editor_visibility", { hidden: true });
    const result = r.result as Record<string, unknown> | undefined;
    const failed = !r.ok || (result != null && result.success === false);
    expect(failed).toBeTruthy();
  });
});
