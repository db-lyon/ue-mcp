import { describe, expect, it } from "vitest";
import { parseParams } from "../../src/surface/action-schema.js";
import { pcgTool } from "../../src/tools/pcg.js";
import { handlerSpecs } from "../../src/tools/specs/pcg.generated.js";

describe("pcg unwrap_instance_nodes surface (#1087)", () => {
  it("routes to the bridge handler and documents its params", () => {
    const spec = pcgTool.actions.unwrap_instance_nodes;
    expect(spec.kind).toBe("bridge");
    expect(spec.kind === "bridge" && spec.bridge).toBe("unwrap_pcg_instance_nodes");
    expect(spec.effect).toBe("mutate");
    const params = parseParams(spec.description ?? "").params.map((p) => p.name);
    expect(params).toEqual(expect.arrayContaining(["assetPath", "nodeName"]));
  });

  it("takes the graph and an optional node, as its C++ spec declares (#1057)", () => {
    // Spec'd: the bag is forwarded as sent, and a key the handler never reads
    // comes back as paramsNotRead rather than being dropped here.
    expect(pcgTool.actions.unwrap_instance_nodes.mapParams).toBeUndefined();
    const params = handlerSpecs.unwrap_pcg_instance_nodes.params;
    expect(params.map((p) => [p.name, p.required])).toEqual([["assetPath", true], ["nodeName", false]]);
  });
});
