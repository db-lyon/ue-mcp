import { describe, expect, it } from "vitest";
import { parseParamsClause } from "../../src/action-schema.js";
import { pcgTool } from "../../src/tools/pcg.js";

describe("pcg unwrap_instance_nodes surface (#1087)", () => {
  it("routes to the bridge handler and documents its params", () => {
    const spec = pcgTool.actions.unwrap_instance_nodes;
    expect(spec.kind).toBe("bridge");
    expect(spec.kind === "bridge" && spec.bridge).toBe("unwrap_pcg_instance_nodes");
    expect(spec.effect).toBe("mutate");
    const params = parseParamsClause(spec.description ?? "").map((p) => p.name);
    expect(params).toEqual(expect.arrayContaining(["assetPath", "nodeName"]));
  });

  it("forwards only the graph and optional node", () => {
    expect(pcgTool.actions.unwrap_instance_nodes.mapParams?.({
      assetPath: "/Game/PCG/PCG_Rocks",
      nodeName: "SurfaceSampler_0",
      settings: { ignored: true },
    })).toEqual({ assetPath: "/Game/PCG/PCG_Rocks", nodeName: "SurfaceSampler_0" });
  });
});
