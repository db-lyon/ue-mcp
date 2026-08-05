// Regression: #806 - invoke_function with world=editor reported success while
// returning a zeroed parameter frame, because AActor::ProcessEvent refuses to
// run script in a world whose actors were never initialised for play unless the
// function is marked CallInEditor. The zeros read exactly like an answer from a
// class default object.
import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

const LABEL = "MCPTest_806_Cube";
const SPAWN = { x: 1800, y: -2000, z: 0 };

let bridge: EditorBridge;
beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "place_actor", {
    actorClass: "/Script/Engine.StaticMeshActor",
    label: LABEL,
    location: SPAWN,
  });
});
afterAll(async () => {
  await callBridge(bridge, "delete_actor", { actorLabel: LABEL }).catch(() => {});
  disconnectBridge();
});

/** Parse UE export text such as "(X=1800.000000,Y=-2000.000000,Z=0.000000)". */
function parseVector(text: string): { x: number; y: number; z: number } {
  const read = (axis: string) => {
    const m = new RegExp(`${axis}=(-?[0-9.eE+]+)`).exec(text);
    return m ? Number(m[1]) : Number.NaN;
  };
  return { x: read("X"), y: read("Y"), z: read("Z") };
}

describe("editor — invoke_function against the editor world (#806)", () => {
  it("reads the placed actor's own location, not a zeroed frame", async () => {
    const r = await callBridge(bridge, "invoke_function", {
      actorLabel: LABEL,
      functionName: "K2_GetActorLocation",
      world: "editor",
    });
    expect(r.ok, r.error).toBe(true);
    const result = r.result as Record<string, unknown>;
    expect(result.success).toBe(true);
    const returnValues = result.returnValues as Record<string, string>;
    const loc = parseVector(returnValues.ReturnValue ?? "");
    expect(loc.x).toBeCloseTo(SPAWN.x, 1);
    expect(loc.y).toBeCloseTo(SPAWN.y, 1);
    expect(loc.z).toBeCloseTo(SPAWN.z, 1);
  });

  it("names the instance that ran the call", async () => {
    const r = await callBridge(bridge, "invoke_function", {
      actorLabel: LABEL,
      functionName: "K2_GetActorLocation",
      world: "editor",
    });
    const result = r.result as Record<string, unknown>;
    expect(result.resolvedActorLabel).toBe(LABEL);
    expect(String(result.resolvedActorPath)).toContain(":PersistentLevel.");
    expect(result.world).toBe("editor");
  });

  it("mutates the placed actor, so a following read sees the new value", async () => {
    const moved = { x: 1900, y: -2100, z: 50 };
    const set = await callBridge(bridge, "invoke_function", {
      actorLabel: LABEL,
      functionName: "K2_SetActorLocation",
      world: "editor",
      args: { NewLocation: moved, bSweep: false, bTeleport: true },
    });
    expect(set.ok, set.error).toBe(true);
    expect((set.result as Record<string, unknown>).success).toBe(true);

    const details = await callBridge(bridge, "get_actor_details", { actorLabel: LABEL });
    expect(details.ok, details.error).toBe(true);
    const location = (details.result as Record<string, unknown>).location as Record<string, number>;
    expect(location.x).toBeCloseTo(moved.x, 1);
    expect(location.y).toBeCloseTo(moved.y, 1);
    expect(location.z).toBeCloseTo(moved.z, 1);
  });

  it("errors on an unknown label instead of answering from defaults", async () => {
    const r = await callBridge(bridge, "invoke_function", {
      actorLabel: "MCPTest_806_NoSuchActor",
      functionName: "K2_GetActorLocation",
      world: "editor",
    });
    const result = r.result as Record<string, unknown> | undefined;
    const failed = !r.ok || (result != null && result.success === false);
    expect(failed).toBeTruthy();
    const message = String(result?.error ?? r.error ?? "");
    expect(message).toContain("editor label");
    expect(message).toContain("object path");
  });
});
