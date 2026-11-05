// @live-editor headless
/**
 * The engine toolset registry readers: status, the toolset listing and one
 * toolset's description, cross-checked against each other. The test project
 * targets 5.8 with ToolsetRegistry enabled. Refs #1223.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

interface Toolset { name: string; toolCount: number; tools: unknown[] }

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function listToolsets(params: Json = {}): Promise<{ count: number; toolsets: Toolset[] }> {
  const listed = await call("epic_list_toolsets", params);
  return { count: Number(listed.toolsetCount), toolsets: (listed.toolsets as Toolset[]) ?? [] };
}

beforeAll(async () => {
  bridge = await getBridge();
}, 60_000);

afterAll(() => {
  if (bridge) disconnectBridge();
});

describe("epic toolset registry", () => {
  it("epic_status reports the registry available with as many toolsets as the listing", async () => {
    const status = await call("epic_status", {});
    expect(status.available, JSON.stringify(status)).toBe(true);
    expect(Number(status.toolsetCount)).toBeGreaterThan(0);
    const listed = await call("epic_list_toolsets", {});
    expect(listed.toolsetCount).toBe(status.toolsetCount);
    expect((listed.toolsets as unknown[]).length).toBe(Number(status.toolsetCount));
  }, 60_000);

  it("epic_list_toolsets names each toolset with its tools, and nameFilter narrows the list", async () => {
    const all = await listToolsets();
    expect(all.toolsets).toHaveLength(all.count);
    for (const t of all.toolsets) {
      expect(t.name).toBeTruthy();
      expect(t.tools).toHaveLength(t.toolCount);
      for (const tool of t.tools) expect(typeof tool).toBe("string");
    }

    const first = all.toolsets[0].name;
    const filtered = await listToolsets({ nameFilter: first });
    expect(filtered.count).toBeGreaterThanOrEqual(1);
    expect(filtered.count).toBeLessThanOrEqual(all.count);
    for (const t of filtered.toolsets) expect(t.name).toContain(first);

    const withSchemas = await listToolsets({ nameFilter: first, includeSchemas: true });
    const schemaTools = withSchemas.toolsets.find((t) => t.name === first)?.tools ?? [];
    expect(schemaTools.length).toBe(all.toolsets[0].toolCount);
    for (const tool of schemaTools) expect(typeof tool).toBe("object");
  }, 60_000);

  it("epic_describe_toolset returns the toolset the listing named, with the same tools", async () => {
    const all = await listToolsets();
    const target = all.toolsets.find((t) => t.toolCount > 0) ?? all.toolsets[0];
    const described = await call("epic_describe_toolset", { toolset: target.name });
    const toolset = described.toolset as { name: string; tools?: Array<{ name: string }> };
    expect(toolset.name).toBe(target.name);
    expect((toolset.tools ?? []).map((t) => t.name)).toEqual(target.tools);
  }, 60_000);
});
