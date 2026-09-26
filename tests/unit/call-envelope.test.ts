/**
 * The `action` + `args` call shape (#1172).
 *
 * Category tools stopped advertising their flat parameter schema, but the
 * contract did not move: every call is still validated against that schema,
 * and must be refused with the very text the MCP layer used to produce. The
 * strongest check of that is to register both shapes on a real McpServer and
 * compare what a client is handed for the same bad call.
 */
import { describe, expect, it } from "vitest";
import { z } from "zod";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { InMemoryTransport } from "@modelcontextprotocol/sdk/inMemory.js";
import { McpError as SdkMcpError } from "@modelcontextprotocol/sdk/types.js";
import {
  envelopeInputSchema,
  unwrapArgsEnvelope,
  usesArgsEnvelope,
  validateCategoryParams,
} from "../../src/call-envelope.js";
import { buildCatalogTool, buildMicroGateway, describeCategory, DESCRIBE_PAGE_CHARS, resolveMicroCall } from "../../src/lean-context.js";
import { bp, categoryTool, injectEditorTarget, type ToolDef } from "../../src/types.js";

function levelTool(): ToolDef {
  return categoryTool("level", "Level.", {
    place_actor: bp("mutate", "Place. Params: actorClass, location?", "place_actor"),
  }, {
    actorClass: z.string().optional(),
    location: z.object({ x: z.number(), y: z.number(), z: z.number() }).optional(),
  });
}

function editorTool(): ToolDef {
  return categoryTool("editor", "Editor.", {
    invoke_function: bp("unknown", "Call. Params: functionName, args?", "invoke_function"),
  }, {
    functionName: z.string().optional(),
    args: z.record(z.unknown()).optional(),
  });
}

describe("unwrapArgsEnvelope", () => {
  it("lifts args into the flat bag and keeps routing keys beside it", () => {
    expect(unwrapArgsEnvelope(levelTool(), { action: "place_actor", args: { actorClass: "A" }, timeoutMs: 5 }))
      .toEqual({ actorClass: "A", action: "place_actor", timeoutMs: 5 });
  });

  it("serves a flat call unchanged, which is the backward-compatible route", () => {
    const flat = { action: "place_actor", actorClass: "A" };
    expect(unwrapArgsEnvelope(levelTool(), flat)).toBe(flat);
  });

  it("merges parameters split between args and the top level", () => {
    expect(unwrapArgsEnvelope(levelTool(), { action: "place_actor", args: { actorClass: "A" }, location: { x: 0, y: 0, z: 0 } }))
      .toEqual({ action: "place_actor", actorClass: "A", location: { x: 0, y: 0, z: 0 } });
  });

  it("refuses one parameter given twice with two values", () => {
    expect(() => unwrapArgsEnvelope(levelTool(), { action: "place_actor", args: { actorClass: "A" }, actorClass: "B" }))
      .toThrow(/actorClass' is given both inside args and beside it/);
  });

  it("keeps a category's OWN args parameter meaning what it always did on a flat call", () => {
    const flat = { action: "invoke_function", functionName: "F", args: { bEnabled: true } };
    expect(unwrapArgsEnvelope(editorTool(), flat)).toBe(flat);
  });

  it("reads args as the envelope when nothing but routing keys sit beside it", () => {
    expect(unwrapArgsEnvelope(editorTool(), { action: "invoke_function", args: { functionName: "F", args: { a: 1 } } }))
      .toEqual({ action: "invoke_function", functionName: "F", args: { a: 1 } });
  });
});

describe("which tools take the envelope", () => {
  it("covers a category, never the micro gateway or the lean catalog", () => {
    expect(usesArgsEnvelope(levelTool())).toBe(true);
    expect(usesArgsEnvelope(buildMicroGateway([levelTool()]))).toBe(false);
    expect(usesArgsEnvelope(buildCatalogTool([levelTool()]))).toBe(false);
  });

  it("advertises action and args, plus the editor target only while it is injected", () => {
    const tool = levelTool();
    const keys = (t: ToolDef) => Object.keys((envelopeInputSchema(t) as z.AnyZodObject).shape);
    expect(keys(tool)).toEqual(["action", "args"]);
    injectEditorTarget(tool, ["A", "B"]);
    expect(keys(tool)).toEqual(["action", "args", "editor"]);
  });
});

/** Register one tool the old way (flat shape) and one the new way, and call both. */
async function refusalTexts(bad: Record<string, unknown>): Promise<{ before: string; after: string }> {
  const tool = levelTool();
  const server = new McpServer({ name: "t", version: "0" });
  server.tool("before", "flat", tool.schema, async () => ({ content: [{ type: "text", text: "ran" }] }));
  server.registerTool("after", { description: "envelope", inputSchema: envelopeInputSchema(tool) }, (async (raw: Record<string, unknown>) => {
    validateCategoryParams(tool, unwrapArgsEnvelope(tool, raw));
    return { content: [{ type: "text", text: "ran" }] };
  }) as never);
  const [a, b] = InMemoryTransport.createLinkedPair();
  const client = new Client({ name: "c", version: "0" });
  await Promise.all([server.connect(a), client.connect(b)]);
  const text = async (name: string, args: Record<string, unknown>) => {
    const res = await client.callTool({ name, arguments: args });
    return (res.content as Array<{ text: string }>)[0].text;
  };
  const before = await text("before", bad);
  const after = await text("after", { action: bad.action, args: Object.fromEntries(Object.entries(bad).filter(([k]) => k !== "action")) });
  await client.close();
  return { before, after: after.replace("tool level", "tool before") };
}

describe("validation stays server-side, with the same refusal", () => {
  it("refuses a wrong type with the text the MCP layer used to produce", async () => {
    const { before, after } = await refusalTexts({ action: "place_actor", actorClass: "A", location: "here" });
    expect(before).toMatch(/^MCP error -32602: Input validation error: Invalid arguments for tool before:/);
    expect(after).toBe(before);
  });

  it("still accepts a flat call through the advertised schema", async () => {
    const tool = levelTool();
    const parsed = (envelopeInputSchema(tool) as z.AnyZodObject).safeParse({ action: "place_actor", actorClass: "A" });
    expect(parsed.success && parsed.data.actorClass).toBe("A");
  });

  it("throws the SDK's own error type, so the SDK words it", () => {
    expect(() => validateCategoryParams(levelTool(), { action: "place_actor", location: 5 })).toThrow(SdkMcpError);
  });

  it("holds a micro gateway call to the same shape, and forwards what the caller sent", () => {
    const tools = [levelTool()];
    expect(() => resolveMicroCall(tools, { category: "level", method: "place_actor", args: { location: "here" } }))
      .toThrow(/Input validation error: Invalid arguments for tool level:/);
    const ok = resolveMicroCall(tools, { category: "level", method: "place_actor", args: { actorClass: "A", extra: 1 } });
    expect(ok.params).toEqual({ actorClass: "A", extra: 1 });
  });
});

describe("describe pages a large category", () => {
  it("bounds each page and walks every action exactly once", () => {
    const actions: Record<string, ReturnType<typeof bp>> = {};
    for (let i = 0; i < 400; i++) actions[`action_number_${i}`] = bp("read", `Do ${i}. Params: someParameter, anotherOne?`, `m${i}`);
    const tool = categoryTool("big", "Big.", actions, {
      someParameter: z.string().optional(),
      anotherOne: z.number().optional(),
    });
    const seen: string[] = [];
    let offset = 0;
    for (;;) {
      const page = describeCategory(tool, offset) as { signatures: string[]; nextOffset?: number; count: number };
      expect(page.count).toBe(400);
      expect(page.signatures.join("").length).toBeLessThanOrEqual(DESCRIBE_PAGE_CHARS);
      seen.push(...page.signatures);
      if (page.nextOffset === undefined) break;
      offset = page.nextOffset;
    }
    expect(seen).toHaveLength(400);
    expect(new Set(seen).size).toBe(400);
  });
});
