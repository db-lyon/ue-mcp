/**
 * The server a client starts, per context strategy and per server mode: what
 * `initialize` and `tools/list` hand over, and that the daemon path hands over
 * exactly what the in-process server does.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { McpServer, Sandbox } from "./harness.js";

type Strategy = "micro" | "lean" | "full";

let sb: Sandbox;
let project: string;
beforeAll(() => {
  sb = new Sandbox();
  project = sb.makeProject("Game");
});
afterAll(() => sb.cleanup());

interface Surface {
  instructions: string;
  tools: Array<{ name: string; description: string; inputSchema: unknown }>;
}

async function surface(strategy: Strategy | undefined, mode: "daemon" | "in-process", env: Record<string, string> = {}, uproject = project): Promise<Surface> {
  const server = await McpServer.start(sb, { projects: [uproject], strategy, mode, env });
  try {
    return { instructions: server.instructions, tools: (await server.listTools()).sort((a, b) => a.name.localeCompare(b.name)) };
  } finally {
    await server.close();
  }
}

const inProcess = new Map<Strategy, Surface>();
async function inProcessSurface(strategy: Strategy): Promise<Surface> {
  if (!inProcess.has(strategy)) inProcess.set(strategy, await surface(strategy, "in-process"));
  return inProcess.get(strategy)!;
}

describe("in process", () => {
  it("micro advertises the gateway and flow, and says how to use them", async () => {
    covers("server.startup.micro.in-process");
    const s = await inProcessSurface("micro");
    expect(s.tools.map((t) => t.name)).toEqual(["flow", "tools"]);
    expect(s.instructions).toContain("micro mode");
    expect(s.instructions).toContain('tools(action="search"');
    const gateway = s.tools.find((t) => t.name === "tools")!;
    expect(JSON.stringify(gateway.inputSchema)).toMatch(/list_categories/);
  });

  it("lean advertises a catalog tool beside trimmed category tools", async () => {
    covers("server.startup.lean.in-process");
    const lean = await inProcessSurface("lean");
    const full = await inProcessSurface("full");
    const names = lean.tools.map((t) => t.name);
    expect(names).toContain("flow");
    expect(names.length).toBeGreaterThan(2);
    const leanSize = JSON.stringify(lean.tools).length;
    const fullSize = JSON.stringify(full.tools).length;
    expect(leanSize).toBeLessThan(fullSize);
    expect(lean.instructions).not.toBe(full.instructions);
  });

  it("full advertises every category tool with its action enum", async () => {
    covers("server.startup.full.in-process");
    const full = await inProcessSurface("full");
    const names = full.tools.map((t) => t.name);
    for (const category of ["project", "asset", "blueprint", "level", "material", "editor", "feedback", "flow"]) {
      expect(names).toContain(category);
    }
    const project = full.tools.find((t) => t.name === "project")!;
    expect(JSON.stringify(project.inputSchema)).toContain("get_status");
  });

  it("takes the strategy from ue-mcp.yml when no variable names one", async () => {
    covers("server.strategy.config");
    const configured = sb.makeProject("FullByConfig", { config: "ue-mcp:\n  version: 1\n  context:\n    strategy: full\n" });
    const s = await surface(undefined, "in-process", {}, configured);
    expect(s.tools.map((t) => t.name)).toEqual((await inProcessSurface("full")).tools.map((t) => t.name));
    const env = await surface("micro", "in-process", {}, configured);
    expect(env.tools.map((t) => t.name)).toEqual(["flow", "tools"]);
  });
});

describe("through the daemon", () => {
  async function sameAsInProcess(strategy: Strategy): Promise<void> {
    const viaDaemon = await surface(strategy, "daemon");
    const direct = await inProcessSurface(strategy);
    expect(viaDaemon.instructions).toBe(direct.instructions);
    expect(viaDaemon.tools).toEqual(direct.tools);
  }

  it("micro advertises exactly what the in-process server does", async () => {
    covers("server.startup.micro.daemon");
    await sameAsInProcess("micro");
  });

  it("lean advertises exactly what the in-process server does", async () => {
    covers("server.startup.lean.daemon");
    await sameAsInProcess("lean");
  });

  it("full advertises exactly what the in-process server does", async () => {
    covers("server.startup.full.daemon");
    await sameAsInProcess("full");
  });
});
