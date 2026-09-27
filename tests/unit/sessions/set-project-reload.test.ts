/**
 * Defect 1: project(set_project) moved path resolution and the socket, but the
 * session kept the previous project's flow config, task overrides, plugins and
 * guards. The switch rebuilds the session's load from the new project.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { ProjectFixture } from "../../helpers/project-fixture.js";

vi.mock("../../../src/editor/deployer.js", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../../../src/editor/deployer.js")>();
  return {
    ...actual,
    attach: vi.fn(() => ({ action: "skipped" })),
    attachSummary: vi.fn(() => "stubbed"),
    deploy: vi.fn(() => ({ action: "skipped" })),
    deploySummary: vi.fn(() => "stubbed"),
  };
});

const { SessionRegistry } = await import("../../../src/sessions/session.js");
const { SessionLoads } = await import("../../../src/sessions/session-load.js");
const { projectTool } = await import("../../../src/tools/project.js");

let fixture: ProjectFixture;

function projectWith(name: string, yml: string): string {
  const uproject = fixture.makeProject(name, { content: true });
  fs.writeFileSync(path.join(path.dirname(uproject), "ue-mcp.yml"), yml, "utf-8");
  return uproject;
}

const ALPHA = [
  "tasks:",
  "  alpha_task:",
  "    class_path: shell",
  "    options: { command: echo alpha }",
  "flows:",
  "  alpha_flow:",
  "    description: alpha only",
  "    steps:",
  "      \"1\": { task: alpha_task }",
  "guards:",
  "  alpha_guard:",
  "    before: { class_path: shell, options: { command: echo guard } }",
  "",
].join("\n");

const BETA = [
  "flows:",
  "  beta_flow:",
  "    description: beta only",
  "    steps:",
  "      \"1\": { task: shell, options: { command: echo beta } }",
  "",
].join("\n");

beforeEach(() => {
  fixture = new ProjectFixture("ue-mcp-set-project-reload-");
  delete process.env.UE_MCP_PORT;
  process.env.UE_MCP_GLOBAL_CONFIG = path.join(fixture.root, "global.yml");
});

afterEach(() => {
  fixture.cleanup();
  delete process.env.UE_MCP_PORT;
  delete process.env.UE_MCP_GLOBAL_CONFIG;
});

describe("project(set_project)", () => {
  it("rebuilds the session's flows, tasks and guards from the new project", async () => {
    const registry = new SessionRegistry();
    const session = registry.register({ projectPath: projectWith("Alpha", ALPHA) });
    const loads = new SessionLoads(registry, session, "full", "0.0.0");
    await loads.buildSurface(session);
    await loads.finishStartup();
    registry.reloadSession = async (s) => { await loads.rebuild(s); };

    expect(loads.getFlows(session).map((f) => f.name)).toContain("alpha_flow");
    expect(session.guards.names()).toContain("alpha_guard");

    const ctx = { ...loads.contextFor(session) };
    session.bridge.connect = async () => {};
    await projectTool.handler(ctx as never, { action: "set_project", projectPath: projectWith("Beta", BETA) });

    const flows = loads.getFlows(session).map((f) => f.name);
    expect(flows).toContain("beta_flow");
    expect(flows).not.toContain("alpha_flow");
    expect(Object.keys(loads.loadFor(ctx as never).flowConfig.get().tasks)).not.toContain("alpha_task");
    expect(session.guards.names()).not.toContain("alpha_guard");
    expect(loads.surfaces).toHaveLength(1);
  });
});
