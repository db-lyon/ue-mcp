/**
 * An argument the action does not take is refused, not dropped (#1336).
 *
 * A typo used to read as a successful no-op: `offset` on the cursor-paged
 * get_connections was dropped and page 0 came back again. Every action whose
 * keys are knowable now refuses an undeclared one, naming what it takes.
 */
import { describe, expect, it, vi } from "vitest";
import { blueprintTool } from "../../src/tools/blueprint.js";
import { projectTool } from "../../src/tools/project.js";
import { actionAccepts } from "../../src/surface/action-schema.js";
import { categoryTool } from "../../src/surface/category-tool.js";
import { flatValidationMessage } from "../../src/surface/context/call-envelope.js";
import { buildMicroGateway } from "../../src/surface/context/micro-context.js";
import type { ActionSpec, ToolContext } from "../../src/surface/tool-types.js";

const BP = "/Game/BP_Test";

function ctxWith(call = vi.fn().mockResolvedValue({ success: true })): ToolContext {
  return { bridge: { call, isConnected: true }, project: {} } as unknown as ToolContext;
}

describe("a spec'd core action", () => {
  it("refuses offset on get_connections, naming cursor among what it takes", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    await expect(blueprintTool.handler(ctxWith(call), { action: "get_connections", assetPath: BP, offset: 160, limit: 20 }))
      .rejects.toThrow(/get_connections does not take offset; it takes .*cursor/);
    expect(call).not.toHaveBeenCalled();
  });

  it("is refused the same way through the micro gateway", async () => {
    const gateway = buildMicroGateway([blueprintTool]);
    await expect(gateway.handler(ctxWith(), {
      action: "call", category: "blueprint", method: "get_connections", args: { assetPath: BP, offset: 160 },
    })).rejects.toThrow(/does not take offset/);
  });

  it("still accepts every declared name and alias", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    await blueprintTool.handler(ctxWith(call), { action: "get_connections", assetPath: BP, cursor: "c", limit: 20 });
    expect(call).toHaveBeenCalledTimes(1);
  });

  it("refuses a key outside the whole category shape before validation would strip it", () => {
    const { message } = flatValidationMessage(blueprintTool, { action: "get_connections", assetPath: BP, notAThing: 1 });
    expect(message).toMatch(/get_connections does not take notAThing/);
  });
});

describe("an in-process handler", () => {
  it("refuses a key its Params clause and its reads do not name", async () => {
    await expect(projectTool.handler(ctxWith(), { action: "search_tools", query: "save asset", limitt: 3 }))
      .rejects.toThrow(/search_tools does not take limitt; it takes query, limit/);
  });

  it("is not checked when its bag escapes to code a scan cannot follow", () => {
    const spread: ActionSpec = { kind: "handler", effect: "read", description: "Params: a", handler: async (_ctx, p) => ({ ...p }) };
    const passed: ActionSpec = { kind: "handler", effect: "read", description: "Params: a", handler: async (_ctx, p) => JSON.stringify(p) };
    const reads: ActionSpec = { kind: "handler", effect: "read", description: "Params: a", handler: async (_ctx, p) => p.a ?? p["b"] };
    expect(actionAccepts(spread)).toBeUndefined();
    expect(actionAccepts(passed)).toBeUndefined();
    expect([...actionAccepts(reads)!.keys].sort()).toEqual(["a", "b"]);
  });

  it("lets routing names through", async () => {
    const tool = categoryTool("demo", "Demo", {
      go: { kind: "handler", effect: "read", description: "Params: a", handler: async (_ctx, p) => ({ a: p.a }) },
    });
    await expect(tool.handler(ctxWith(), { action: "go", a: 1, timeoutMs: 5_000, select: "a" })).resolves.toBeDefined();
  });
});
