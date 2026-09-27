/**
 * ProjectConfig: one cascade, one overlay rule, and one snapshot per state
 * of the files, shared by the settings block, the flow config and plugins.
 */
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { ProjectConfig } from "../../../src/config/project-config.js";
import { readUeMcpConfig } from "../../../src/config/project.js";
import { loadFlowConfig } from "../../../src/flow/loader.js";

let dir: string;
let clock = Date.now();

function write(name: string, text: string): void {
  const file = path.join(dir, name);
  fs.writeFileSync(file, text);
  const t = new Date(clock += 1000);
  fs.utimesSync(file, t, t);
}

beforeEach(() => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-project-config-"));
  vi.stubEnv("UE_MCP_GLOBAL_CONFIG", path.join(dir, "global.yml"));
  vi.stubEnv("UE_MCP_ENV", "");
  ProjectConfig.clearCache();
});

afterEach(() => {
  vi.unstubAllEnvs();
  ProjectConfig.clearCache();
  fs.rmSync(dir, { recursive: true, force: true });
});

describe("ProjectConfig", () => {
  it("merges settings, flows and plugins through the same overlay", () => {
    write("ue-mcp.yml", [
      "ue-mcp:",
      "  version: 1",
      "  env: ci",
      "  bridge: { port: 9100 }",
      "plugins:",
      "  - { name: base-plugin }",
      "flows:",
      "  base: { steps: { 1: { task: shell, options: { command: echo } } } }",
    ].join("\n"));
    write("ue-mcp.ci.yml", [
      "ue-mcp:",
      "  version: 1",
      "  bridge: { port: 9200 }",
      "plugins:",
      "  - { name: ci-plugin }",
      "flows:",
      "  ci_only: { steps: { 1: { task: shell, options: { command: echo } } } }",
    ].join("\n"));

    const config = ProjectConfig.for(dir);
    expect(config.overlay).toBe("ci");
    expect(config.lookup("bridge.port")).toBe(9200);
    expect(config.plugins.map((p) => p.name)).toEqual(["ci-plugin"]);
    expect(readUeMcpConfig(dir).bridge?.port).toBe(9200);
    const flows = loadFlowConfig([], dir).config.flows;
    expect(flows.base).toBeDefined();
    expect(flows.ci_only).toBeDefined();
  });

  it("lets UE_MCP_ENV choose the overlay over env:", () => {
    write("ue-mcp.yml", "ue-mcp:\n  env: ci\n");
    write("ue-mcp.ci.yml", "ue-mcp:\n  bridge: { port: 9200 }\n");
    write("ue-mcp.nightly.yml", "ue-mcp:\n  bridge: { port: 9300 }\n");
    vi.stubEnv("UE_MCP_ENV", "nightly");
    expect(ProjectConfig.for(dir).lookup("bridge.port")).toBe(9300);
  });

  it("keeps the local layer above the overlay", () => {
    write("ue-mcp.yml", "ue-mcp:\n  env: ci\n");
    write("ue-mcp.ci.yml", "ue-mcp:\n  bridge: { port: 9200 }\n");
    write("ue-mcp.local.yml", "ue-mcp:\n  bridge: { port: 9400 }\n");
    expect(ProjectConfig.for(dir).layers.map((l) => l.target)).toEqual(["global", "project", "env", "local"]);
    expect(ProjectConfig.for(dir).lookup("bridge.port")).toBe(9400);
  });

  it("answers the same snapshot until a layer changes", () => {
    write("ue-mcp.yml", "ue-mcp:\n  disable: [gas]\n");
    const first = ProjectConfig.for(dir);
    expect(ProjectConfig.for(dir)).toBe(first);
    write("ue-mcp.local.yml", "ue-mcp:\n  disable: [pcg]\n");
    const second = ProjectConfig.for(dir);
    expect(second).not.toBe(first);
    expect(second.lookup("disable")).toEqual(["pcg"]);
    expect(first.lookup("disable")).toEqual(["gas"]);
    expect(Object.isFrozen(second.plugins)).toBe(true);
  });

  it("reads a user-global plugin list like any other layer", () => {
    write("global.yml", "plugins:\n  - { name: everywhere }\n");
    write("ue-mcp.yml", "ue-mcp: {}\n");
    expect(ProjectConfig.for(dir).plugins.map((p) => p.name)).toEqual(["everywhere"]);
  });

  it("skips a layer that does not parse for settings, and refuses it for flows", () => {
    write("ue-mcp.yml", "ue-mcp:\n  disable: [gas]\n");
    write("ue-mcp.local.yml", "flows: [broken");
    expect(ProjectConfig.for(dir).lookup("disable")).toEqual(["gas"]);
    expect(() => loadFlowConfig([], dir)).toThrow();
  });
});
