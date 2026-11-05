/**
 * The context projection is what the server advertises. For every strategy,
 * the projection over all categories, made from a runtime started under any
 * strategy, equals the real MCP server's initialize instructions and tools/list
 * for that strategy. A scoped projection is smaller and holds only its
 * categories, and a projected call runs as the server would run it.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { InMemoryTransport } from "@modelcontextprotocol/sdk/inMemory.js";
import { createRuntime, type Runtime } from "../../src/server/runtime.js";
import { createMcpServer } from "../../src/server/mcp-server.js";
import { ContextProjector, ContextProjectionError, CONTEXT_STRATEGIES } from "../../src/server/context-projection.js";
import type { ContextStrategy } from "../../src/surface/context/lean-context.js";

let tmp: string;
let uproject: string;
const saved: Record<string, string | undefined> = {};
const runtimes = {} as Record<ContextStrategy, Runtime>;
const served = {} as Record<ContextStrategy, { instructions: string; tools: Array<Record<string, unknown>> }>;

function setEnv(key: string, value: string | undefined): void {
  if (!(key in saved)) saved[key] = process.env[key];
  if (value === undefined) delete process.env[key];
  else process.env[key] = value;
}

async function serve(rt: Runtime): Promise<{ instructions: string; tools: Array<Record<string, unknown>> }> {
  const { server, dispose } = createMcpServer(rt);
  const client = new Client({ name: "context-projection-test", version: "0" });
  const [a, b] = InMemoryTransport.createLinkedPair();
  await Promise.all([server.connect(b), client.connect(a)]);
  try {
    return { instructions: client.getInstructions() ?? "", tools: (await client.listTools()).tools as Array<Record<string, unknown>> };
  } finally {
    dispose();
    await client.close();
    await server.close();
  }
}

beforeAll(async () => {
  tmp = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-context-projection-"));
  const dir = path.join(tmp, "Projected");
  fs.mkdirSync(path.join(dir, "Content"), { recursive: true });
  uproject = path.join(dir, "Projected.uproject");
  fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, EngineAssociation: "5.8", Modules: [] }));
  setEnv("UE_MCP_PORT", "1");
  setEnv("UE_MCP_HOST", "127.0.0.1");
  setEnv("UE_MCP_DISABLE_UPDATE_CHECK", "1");
  for (const strategy of CONTEXT_STRATEGIES) {
    setEnv("UE_MCP_CONTEXT_STRATEGY", strategy);
    runtimes[strategy] = await createRuntime([uproject]);
    served[strategy] = await serve(runtimes[strategy]);
  }
  setEnv("UE_MCP_CONTEXT_STRATEGY", undefined);
}, 180_000);

afterAll(() => {
  for (const [key, value] of Object.entries(saved)) {
    if (value === undefined) delete process.env[key];
    else process.env[key] = value;
  }
  fs.rmSync(tmp, { recursive: true, force: true });
});

describe("the projection of every category", () => {
  for (const from of CONTEXT_STRATEGIES) {
    for (const strategy of CONTEXT_STRATEGIES) {
      it(`made under ${from} is what the ${strategy} server sends`, () => {
        const p = new ContextProjector(runtimes[from]).project({ strategy });
        const real = served[strategy];
        expect(p.strategy).toBe(strategy);
        expect(p.instructions).toBe(real.instructions);
        expect(p.tools.map(({ category: _c, ...wire }) => wire)).toEqual(
          real.tools.map(({ execution: _e, ...wire }) => wire),
        );
        // The estimate context-tax makes, over exactly what the client received.
        expect(p.seedTokens).toBe(Math.round(JSON.stringify({ instructions: real.instructions, tools: real.tools }).length / 4));
        expect(p.estimator).toBe("chars/4");
      });
    }
  }

  it("names every registered category and marks which tools are categories", () => {
    const rt = runtimes.full;
    const all = rt.primaryLoad.surface.tools.map((t) => t.name);
    const projector = new ContextProjector(rt);
    expect(projector.project({ strategy: "full" }).categories).toEqual(all);
    const lean = projector.project({ strategy: "lean" });
    expect(lean.tools.find((t) => t.name === "catalog")?.category).toBeNull();
    expect(lean.tools.find((t) => t.name === "flow")?.category).toBeNull();
    expect(lean.tools.find((t) => t.name === "level")?.category).toBe("level");
    expect(projector.project({ strategy: "micro" }).tools.map((t) => [t.name, t.category])).toEqual([["tools", null], ["flow", null]]);
  });
});

describe("a scoped projection", () => {
  const scope = ["level", "blueprint"];

  for (const strategy of CONTEXT_STRATEGIES) {
    it(`is strictly smaller and holds only its categories (${strategy})`, () => {
      const projector = new ContextProjector(runtimes.micro);
      const all = projector.project({ strategy });
      const some = projector.project({ strategy, categories: scope });
      expect(some.categories).toEqual(["level", "blueprint"].filter((c) => all.categories.includes(c)).sort((a, b) => all.categories.indexOf(a) - all.categories.indexOf(b)));
      expect(some.seedTokens).toBeLessThan(all.seedTokens);
      expect(some.instructions.length).toBeLessThan(all.instructions.length);
      const categoryTools = some.tools.filter((t) => t.category !== null).map((t) => t.name);
      expect(categoryTools.every((n) => scope.includes(n))).toBe(true);
      expect(some.instructions).toContain("2 categor");
      if (strategy !== "micro") expect(categoryTools.sort()).toEqual([...scope].sort());
    });
  }

  it("refuses an unknown strategy or category", () => {
    const projector = new ContextProjector(runtimes.full);
    expect(() => projector.project({ strategy: "tiny" as ContextStrategy })).toThrow(ContextProjectionError);
    expect(() => projector.project({ strategy: "lean", categories: ["level", "nope"] })).toThrow(/no such category: nope/);
  });
});

describe("a projected call", () => {
  const text = (r: { content: Array<{ text?: string }> }) => r.content.map((c) => c.text ?? "").join("\n");

  it("runs micro's gateway discovery over only the projected categories", async () => {
    const projector = new ContextProjector(runtimes.full);
    const opts = { strategy: "micro" as const, categories: ["level", "blueprint"] };
    const listed = await projector.call(opts, "tools", { action: "list_categories" });
    expect(listed.isError, text(listed)).toBeFalsy();
    expect(JSON.parse(text(listed)).categories.map((c: { category: string }) => c.category).sort()).toEqual(["blueprint", "level"]);

    const refused = await projector.call(opts, "tools", { action: "call", category: "asset", method: "list", args: {} });
    expect(refused.isError).toBe(true);
    expect(text(refused)).toMatch(/'asset' is outside this context's categories/);
  });

  it("runs lean's per-category describe and catalog search, which the process strategy does not have", async () => {
    const projector = new ContextProjector(runtimes.micro);
    const opts = { strategy: "lean" as const, categories: ["level"] };
    const described = await projector.call(opts, "level", { action: "describe" });
    expect(described.isError, text(described)).toBeFalsy();
    expect(JSON.parse(text(described))).toMatchObject({ category: "level" });

    const searched = await projector.call(opts, "catalog", { action: "search", query: "place actor" });
    expect(searched.isError, text(searched)).toBeFalsy();
    const results = JSON.parse(text(searched)).results as string[];
    expect(results.length).toBeGreaterThan(0);
    expect(results.every((r) => r.startsWith("level."))).toBe(true);

    const outside = await projector.call(opts, "asset", { action: "list" });
    expect(outside.isError).toBe(true);
    expect(text(outside)).toMatch(/'asset' is outside this context's categories/);
    const unknown = await projector.call(opts, "nothing", { action: "list" });
    expect(text(unknown)).toMatch(/No tool 'nothing'/);
  });

  it("validates a full call the way the server does", async () => {
    const projector = new ContextProjector(runtimes.lean);
    const bad = await projector.call({ strategy: "full" }, "level", { action: "no_such_action" });
    expect(bad.isError).toBe(true);
  });
});
