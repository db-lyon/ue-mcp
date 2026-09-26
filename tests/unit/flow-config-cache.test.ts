/**
 * The flow config is reread only when one of its layer files changes, so a
 * live call does not pay for a YAML parse and a schema pass every time.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { FlowConfigCache } from "../../src/flow/config-cache.js";
import type { ToolDef } from "../../src/core/types.js";

const tools: ToolDef[] = [];
let dir: string;
let savedGlobal: string | undefined;
let savedEnv: string | undefined;

function flowYaml(name: string, pad = ""): string {
  return `flows:\n  ${name}:\n    steps:\n      1: { task: shell, options: { command: "echo ${pad}" } }\n`;
}

/** Write a file and move its mtime, so the stamp differs even within one clock tick. */
function write(file: string, text: string, bump: number): void {
  fs.writeFileSync(file, text);
  const t = new Date(Date.now() + bump * 1000);
  fs.utimesSync(file, t, t);
}

beforeEach(() => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-config-cache-"));
  savedGlobal = process.env.UE_MCP_GLOBAL_CONFIG;
  savedEnv = process.env.UE_MCP_ENV;
  process.env.UE_MCP_GLOBAL_CONFIG = path.join(dir, "global.yml");
  delete process.env.UE_MCP_ENV;
});

afterEach(() => {
  fs.rmSync(dir, { recursive: true, force: true });
  if (savedGlobal === undefined) delete process.env.UE_MCP_GLOBAL_CONFIG;
  else process.env.UE_MCP_GLOBAL_CONFIG = savedGlobal;
  if (savedEnv === undefined) delete process.env.UE_MCP_ENV;
  else process.env.UE_MCP_ENV = savedEnv;
});

describe("FlowConfigCache", () => {
  it("returns the same config while no layer changes", () => {
    write(path.join(dir, "ue-mcp.yml"), flowYaml("first"), 1);
    const cache = new FlowConfigCache(tools, dir);
    const a = cache.get();
    expect(cache.get()).toBe(a);
    expect(a.flows.first).toBeDefined();
  });

  it("rereads when the project file changes", () => {
    const file = path.join(dir, "ue-mcp.yml");
    write(file, flowYaml("first"), 1);
    const cache = new FlowConfigCache(tools, dir);
    const a = cache.get();
    write(file, flowYaml("second", "longer"), 2);
    const b = cache.get();
    expect(b).not.toBe(a);
    expect(b.flows.second).toBeDefined();
    expect(b.flows.first).toBeUndefined();
  });

  it("rereads when a local layer appears and when it goes away", () => {
    write(path.join(dir, "ue-mcp.yml"), flowYaml("first"), 1);
    const cache = new FlowConfigCache(tools, dir);
    expect(cache.get().flows.local_only).toBeUndefined();
    const local = path.join(dir, "ue-mcp.local.yml");
    write(local, flowYaml("local_only"), 2);
    expect(cache.get().flows.local_only).toBeDefined();
    fs.rmSync(local);
    expect(cache.get().flows.local_only).toBeUndefined();
  });

  it("rereads when the env overlay is selected", () => {
    write(path.join(dir, "ue-mcp.yml"), flowYaml("first"), 1);
    write(path.join(dir, "ue-mcp.ci.yml"), flowYaml("ci_only"), 1);
    const cache = new FlowConfigCache(tools, dir);
    expect(cache.get().flows.ci_only).toBeUndefined();
    process.env.UE_MCP_ENV = "ci";
    expect(cache.get().flows.ci_only).toBeDefined();
  });

  it("rereads when the user-global layer changes", () => {
    const cache = new FlowConfigCache(tools, dir);
    expect(cache.get().flows.global_only).toBeUndefined();
    write(process.env.UE_MCP_GLOBAL_CONFIG!, flowYaml("global_only"), 1);
    expect(cache.get().flows.global_only).toBeDefined();
  });

  it("caches nothing for a file that does not parse, and recovers once it does", () => {
    const file = path.join(dir, "ue-mcp.yml");
    write(file, "flows: [unclosed", 1);
    const cache = new FlowConfigCache(tools, dir);
    expect(() => cache.get()).toThrow();
    write(file, flowYaml("fixed"), 2);
    expect(cache.get().flows.fixed).toBeDefined();
  });
});
