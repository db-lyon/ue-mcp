import { describe, expect, it, vi } from "vitest";
import { z } from "zod";
import { blueprintTool } from "../../src/tools/blueprint.js";
import type { ToolContext } from "../../src/types.js";

// #1100: graphSelector reaches every single-graph wiring write, and
// get_connections takes a node filter. The bridge reads graphSelector ahead of
// graphName, so the server must forward it rather than drop it.
const BP = "/Game/Anim/ABP_Character";
const NODE = "0123456789ABCDEF0123456789ABCDEF";

async function route(input: Record<string, unknown>) {
  const call = vi.fn().mockResolvedValue({ success: true });
  const ctx = { bridge: { call } } as unknown as ToolContext;
  await blueprintTool.handler(ctx, z.object(blueprintTool.schema).parse(input));
  return call.mock.calls[0] as [string, Record<string, unknown>];
}

describe("graphSelector on graph writes", () => {
  it("connect_pins forwards graphSelector", async () => {
    const [method, params] = await route({
      action: "connect_pins", assetPath: BP, graphSelector: "Aim[1]",
      sourceNodeId: NODE, sourcePin: "Pose", targetNodeId: NODE, targetPin: "Result",
    });
    expect(method).toBe("connect_pins");
    expect(params).toMatchObject({ graphSelector: "Aim[1]" });
  });

  for (const action of ["delete_node", "refresh_node", "disconnect_pins"]) {
    it(`${action} forwards graphSelector`, async () => {
      const [method, params] = await route({ action, assetPath: BP, graphSelector: "Aim[1]", nodeId: NODE, pinName: "Pose" });
      expect(method).toBe(action);
      expect(params).toMatchObject({ path: BP, graphSelector: "Aim[1]", nodeId: NODE });
    });
  }
});

describe("get_connections node filter", () => {
  it("forwards nodeId alongside the graph address", async () => {
    const [method, params] = await route({ action: "get_connections", assetPath: BP, graphSelector: "Aim[1]", nodeId: NODE });
    expect(method).toBe("get_blueprint_connections");
    expect(params).toMatchObject({ assetPath: BP, graphSelector: "Aim[1]", nodeId: NODE });
  });

  it("leaves nodeId out when no filter is asked for", async () => {
    const [, params] = await route({ action: "get_connections", assetPath: BP });
    expect(params.nodeId).toBeUndefined();
  });
});
