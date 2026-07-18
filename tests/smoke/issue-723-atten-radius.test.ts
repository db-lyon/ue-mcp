// Regression: #723 — spawn_light accepted attenuationRadius but never applied it,
// so spawned lights always came out with the class-default radius.
import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
beforeAll(async () => { bridge = await getBridge(); });
afterAll(async () => {
  await callBridge(bridge, "delete_actor", { actorLabel: "MCPTest_AttenLight" }).catch(() => {});
  disconnectBridge();
});

describe("level — spawn_light attenuationRadius (#723)", () => {
  it("applies attenuationRadius to the spawned point-light component", async () => {
    const r = await callBridge(bridge, "spawn_light", {
      lightType: "point",
      location: { x: 400, y: 0, z: 500 },
      intensity: 5000,
      attenuationRadius: 2500,
      label: "MCPTest_AttenLight",
    });
    expect(r.ok, r.error).toBe(true);
    const verify = await callBridge(bridge, "execute_python", {
      code: [
        "import unreal",
        "eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)",
        "a = [x for x in eas.get_all_level_actors() if x.get_actor_label() == 'MCPTest_AttenLight'][0]",
        "c = a.get_component_by_class(unreal.PointLightComponent)",
        "print('ATTEN:' + str(round(c.attenuation_radius)))",
      ].join("\n"),
    });
    expect(verify.ok, verify.error).toBe(true);
    expect(JSON.stringify(verify.result)).toContain("ATTEN:2500");
  });
});
