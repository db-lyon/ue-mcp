/**
 * Every advertised array parameter must carry a concrete `items` (#936).
 *
 * VS Code validates each tool schema before it will load the tool, and an
 * array with no `items` is rejected outright:
 *
 *   Failed to validate tool mcp_ue-mcp_audio:
 *   Error: tool parameters array type must have items.
 *
 * One such field takes the whole category out of service for that client, and
 * the only workaround a user has is disabling the category. `z.array(z.any())`
 * is what produces it, so this test is deliberately generic: it walks the
 * serialized `tools/list` output rather than naming the nine fields that were
 * wrong, and any future untyped array anywhere in any tool fails it.
 *
 * Two surfaces are checked, because they fail at different times:
 *
 *  - The live surface, built here from ALL_TOOLS through a real McpServer, so
 *    the schema a client would be handed today is the thing under test.
 *  - The committed editor-down golden baseline, which additionally covers the
 *    tools that are not in ALL_TOOLS (the flow tool) and which is the exact
 *    JSON a client receives at startup.
 */
import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { z } from "zod";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { InMemoryTransport } from "@modelcontextprotocol/sdk/inMemory.js";
import { ALL_TOOLS } from "../../src/tools.js";
import { repoPath } from "../helpers/repo-root.js";

const GOLDEN = repoPath("tests", "golden", "editor-down.json");

/** A JSON Schema node, walked structurally rather than by any known shape. */
type SchemaNode = Record<string, unknown>;

/**
 * Report every array-typed node that advertises no element schema, by path.
 * `prefixItems` counts: a tuple schema states its element types too.
 */
function arraysWithoutItems(node: unknown, at: string, out: string[] = []): string[] {
  if (node === null || typeof node !== "object") return out;
  if (Array.isArray(node)) {
    node.forEach((child, i) => arraysWithoutItems(child, `${at}[${i}]`, out));
    return out;
  }
  const obj = node as SchemaNode;
  const type = obj.type;
  const isArray = type === "array" || (Array.isArray(type) && type.includes("array"));
  if (isArray && obj.items === undefined && obj.prefixItems === undefined) out.push(at);
  for (const [key, value] of Object.entries(obj)) {
    arraysWithoutItems(value, `${at}.${key}`, out);
  }
  return out;
}

/** The tools/list payload the server would answer with, built in process. */
async function listToolSchemas(): Promise<Array<{ name: string; inputSchema: unknown }>> {
  const server = new McpServer(
    { name: "ue-mcp-array-items-test", version: "0.0.0" },
    { capabilities: { tools: {} } },
  );
  for (const tool of ALL_TOOLS) {
    const shape: Record<string, z.ZodType> = {};
    for (const [key, schema] of Object.entries(tool.schema)) shape[key] = schema;
    server.tool(tool.name, tool.description, shape, async () => ({ content: [] }));
  }

  const client = new Client({ name: "array-items-test", version: "0.0.0" });
  const [clientTransport, serverTransport] = InMemoryTransport.createLinkedPair();
  await Promise.all([server.connect(serverTransport), client.connect(clientTransport)]);
  try {
    const listed = await client.listTools();
    return listed.tools.map((t) => ({ name: t.name, inputSchema: t.inputSchema as unknown }));
  } finally {
    await client.close();
    await server.close();
  }
}

describe("advertised array parameters (#936)", () => {
  it("is checking a surface worth checking", async () => {
    const tools = await listToolSchemas();
    expect(tools.length).toBe(ALL_TOOLS.length);
    expect(tools.length).toBeGreaterThan(15);
  });

  it("gives every array in the live tools/list surface a concrete items schema", async () => {
    const tools = await listToolSchemas();
    const offenders: string[] = [];
    for (const tool of tools) {
      offenders.push(...arraysWithoutItems(tool.inputSchema, tool.name));
    }
    expect(
      offenders,
      "VS Code refuses to load a tool whose schema has an array with no 'items'. "
        + "Replace z.array(z.any()) with a concrete element schema (a z.union([...]) "
        + "when the field genuinely accepts more than one shape).",
    ).toEqual([]);
  });

  it("gives every array in the committed editor-down baseline a concrete items schema", () => {
    const baseline = JSON.parse(readFileSync(GOLDEN, "utf8")) as {
      tools: Array<{ name: string; inputSchema: unknown }>;
    };
    expect(baseline.tools.length).toBeGreaterThan(15);
    const offenders: string[] = [];
    for (const tool of baseline.tools) {
      offenders.push(...arraysWithoutItems(tool.inputSchema, tool.name));
    }
    expect(
      offenders,
      "The recorded startup surface still advertises an array with no 'items'. "
        + "Fix the schema, then re-record with `npm run golden:record`.",
    ).toEqual([]);
  });

  it("detects a missing items schema when there is one", () => {
    // What z.array(z.any()) serialises to, and what VS Code rejects. Guards the
    // walker itself, so a green suite above cannot mean a blind detector.
    const untyped = arraysWithoutItems(
      { type: "object", properties: { paths: { type: "array" } } },
      "fixture",
    );
    expect(untyped).toEqual(["fixture.properties.paths"]);

    const typed = arraysWithoutItems(
      { type: "object", properties: { paths: { type: "array", items: { type: "string" } } } },
      "fixture",
    );
    expect(typed).toEqual([]);

    // Nested inside a union branch, which is where a fixed schema puts them.
    const nested = arraysWithoutItems(
      { anyOf: [{ type: "object", properties: { flags: { type: "array" } } }] },
      "fixture",
    );
    expect(nested).toEqual(["fixture.anyOf[0].properties.flags"]);
  });
});
