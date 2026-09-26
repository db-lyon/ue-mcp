import { describe, expect, it, vi } from "vitest";
import { z } from "zod";
import { blueprintTool } from "../../src/tools/blueprint.js";
import type { ToolContext } from "../../src/core/types.js";

// refresh_node and disconnect_pins (#1132): the C++ handlers declare assetPath,
// graphName, nodeId (alias nodeName), pinName, linkedNodeId, linkedPinName and
// breakOrphanedPins in their specs, so the server forwards the bag as sent and
// the registry resolves the aliases (#1057).
const BP = "/Game/Blueprints/BP_Actor";
const NODE = "0123456789ABCDEF0123456789ABCDEF";
const OTHER = "FEDCBA9876543210FEDCBA9876543210";

async function route(input: Record<string, unknown>) {
  const call = vi.fn().mockResolvedValue({ success: true });
  const ctx = { bridge: { call } } as unknown as ToolContext;
  await blueprintTool.handler(ctx, z.object(blueprintTool.schema).parse(input));
  return call.mock.calls[0] as [string, Record<string, unknown>];
}

describe("blueprint.refresh_node", () => {
  it("routes the node address and breakOrphanedPins", async () => {
    const [method, params] = await route({ action: "refresh_node", assetPath: BP, graphName: "EventGraph", nodeId: NODE, breakOrphanedPins: true });
    expect(method).toBe("refresh_node");
    expect(params).toMatchObject({ assetPath: BP, graphName: "EventGraph", nodeId: NODE, breakOrphanedPins: true });
  });

  it("rejects a non-boolean breakOrphanedPins at the schema", () => {
    expect(z.object(blueprintTool.schema).safeParse({ action: "refresh_node", assetPath: BP, nodeId: NODE, breakOrphanedPins: "yes" }).success).toBe(false);
  });
});

describe("blueprint.disconnect_pins", () => {
  it("routes one targeted link", async () => {
    const [method, params] = await route({ action: "disconnect_pins", assetPath: BP, nodeId: NODE, pinName: "ReturnValue", linkedNodeId: OTHER, linkedPinName: "A" });
    expect(method).toBe("disconnect_pins");
    expect(params).toMatchObject({ assetPath: BP, nodeId: NODE, pinName: "ReturnValue", linkedNodeId: OTHER, linkedPinName: "A" });
  });

  it("routes a whole-pin break by node title", async () => {
    const [, params] = await route({ action: "disconnect_pins", assetPath: BP, nodeName: "Switch on E_Team", pinName: "Red" });
    expect(params).toMatchObject({ assetPath: BP, nodeName: "Switch on E_Team", pinName: "Red" });
    expect(params.linkedNodeId).toBeUndefined();
  });
});
