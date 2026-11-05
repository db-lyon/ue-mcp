/**
 * The config cascade as a client sees it: global < ue-mcp.yml < the env
 * overlay < ue-mcp.local.yml. Each layer contributes a flow and overrides a
 * shared one, so flow(list) shows which files were merged and in what order,
 * and the `ue-mcp:` block's context strategy shows through tools/list.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { json, McpServer, Sandbox } from "./harness.js";

/** A config file: an optional `ue-mcp:` block, a flow of its own, and its word on `layered`. */
function layer(name: string, block: string[] = []): string {
  const step = ['    steps:', '      "1":', "        task: project.get_info"];
  return [
    ...(block.length ? ["ue-mcp:", ...block.map((l) => `  ${l}`)] : []),
    "flows:",
    `  ${name}_only:`,
    `    description: from ${name}`,
    ...step,
    "  layered:",
    `    description: won by ${name}`,
    ...step,
    "",
  ].join("\n");
}

function write(dir: string, file: string, text: string): void {
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(path.join(dir, file), text);
}

interface Seen {
  flows: Record<string, string>;
  tools: string[];
}

async function observe(sb: Sandbox, uproject: string, env: Record<string, string | undefined> = {}): Promise<Seen> {
  const server = await McpServer.start(sb, { projects: [uproject], env });
  try {
    const listed = json<{ flows: Array<{ name: string; description: string }> }>(await server.call("flow", { action: "list" }));
    return {
      flows: Object.fromEntries(listed.flows.map((f) => [f.name, f.description])),
      tools: await server.toolNames(),
    };
  } finally {
    await server.close();
  }
}

const MICRO = ["flow", "tools"];

let sb: Sandbox;
const seen: Record<string, Seen> = {};

beforeAll(async () => {
  sb = new Sandbox();
  write(path.join(sb.home, ".ue-mcp"), "config.yml", layer("global", ["context:", "  strategy: full"]));

  // Every layer present, the overlay picked by UE_MCP_ENV.
  const all = sb.makeProject("AllLayers", { config: layer("project", ["version: 1", "context:", "  strategy: lean"]) });
  write(path.dirname(all), "ue-mcp.staging.yml", layer("env", ["context:", "  strategy: full"]));
  write(path.dirname(all), "ue-mcp.local.yml", layer("local", ["context:", "  strategy: micro"]));

  // No local layer: the overlay's strategy stands.
  const noLocal = sb.makeProject("NoLocal", { config: layer("project", ["version: 1", "context:", "  strategy: micro"]) });
  write(path.dirname(noLocal), "ue-mcp.staging.yml", layer("env", ["context:", "  strategy: lean"]));

  // The overlay named by the project's own `env:` key.
  const byKey = sb.makeProject("ByKey", { config: layer("project", ["version: 1", "env: staging"]) });
  write(path.dirname(byKey), "ue-mcp.staging.yml", layer("env", ["context:", "  strategy: micro"]));
  write(path.dirname(byKey), "ue-mcp.qa.yml", layer("qa"));

  // Nothing beside the descriptor: only the global layer applies.
  const bare = sb.makeProject("Bare");

  // No ue-mcp.yml at all, only the per-machine layer.
  const localOnly = sb.makeProject("LocalOnly");
  write(path.dirname(localOnly), "ue-mcp.local.yml", layer("local"));

  const staging = { UE_MCP_ENV: "staging" };
  const runs: Array<[string, Promise<Seen>]> = [
    ["all", observe(sb, all, staging)],
    ["allNoEnv", observe(sb, all)],
    ["noLocal", observe(sb, noLocal, staging)],
    ["byKey", observe(sb, byKey)],
    ["byKeyVariableWins", observe(sb, byKey, { UE_MCP_ENV: "qa" })],
    ["localOnly", observe(sb, localOnly)],
    ["bare", observe(sb, bare)],
  ];
  const results = await Promise.all(runs.map(([, run]) => run));
  runs.forEach(([name], i) => (seen[name] = results[i]));
}, 120_000);

afterAll(() => sb.cleanup());

describe("the cascade", () => {
  it("merges global, ue-mcp.yml, the overlay and ue-mcp.local.yml, each over the last", () => {
    covers("config.cascade");
    const { flows, tools } = seen.all;
    for (const name of ["global_only", "project_only", "env_only", "local_only"]) expect(flows).toHaveProperty(name);
    expect(flows.layered).toBe("won by local");
    expect(flows.niagara_fire).toBeDefined();
    expect(tools).toEqual(MICRO);
  });

  it("lets the overlay win over ue-mcp.yml and global when there is no local layer", () => {
    covers("config.cascade");
    const { flows, tools } = seen.noLocal;
    expect(flows.layered).toBe("won by env");
    expect(flows).toHaveProperty("global_only");
    expect(flows).not.toHaveProperty("local_only");
    expect(tools).not.toEqual(MICRO);
    expect(tools).toContain("flow");
  });

  it("reads ue-mcp.local.yml when the project has no ue-mcp.yml", () => {
    covers("config.cascade");
    const { flows } = seen.localOnly;
    expect(flows).toHaveProperty("local_only");
    expect(flows.layered).toBe("won by local");
    expect(flows).toHaveProperty("global_only");
  });

  it("applies a global layer that only sets preferences to a project with no ue-mcp.yml", () => {
    covers("config.cascade");
    const { flows, tools } = seen.bare;
    expect(flows).toHaveProperty("global_only");
    expect(flows.layered).toBe("won by global");
    expect(tools).toContain("blueprint");
  });
});

describe("the env overlay", () => {
  it("is picked by UE_MCP_ENV, and skipped without it", () => {
    covers("config.env-overlay");
    expect(seen.all.flows).toHaveProperty("env_only");
    expect(seen.allNoEnv.flows).not.toHaveProperty("env_only");
    expect(seen.allNoEnv.flows.layered).toBe("won by local");
  });

  it("is picked by the project's env: key, for flows as well as settings", () => {
    covers("config.env-overlay");
    const { flows, tools } = seen.byKey;
    expect(flows).toHaveProperty("env_only");
    expect(flows.layered).toBe("won by env");
    expect(flows).not.toHaveProperty("qa_only");
    expect(tools).toEqual(MICRO);
  });

  it("follows UE_MCP_ENV over the env: key", () => {
    covers("config.env-overlay");
    const { flows, tools } = seen.byKeyVariableWins;
    expect(flows).toHaveProperty("qa_only");
    expect(flows).not.toHaveProperty("env_only");
    expect(flows.layered).toBe("won by qa");
    // The qa overlay sets no strategy, so the global layer's full stands.
    expect(tools).toContain("blueprint");
  });
});
