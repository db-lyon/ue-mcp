/**
 * set_project, add_editor, use_editor and drop_editor create, choose or move
 * the editor a call targets. The runtime dispatches them outside the task
 * registry, so a direct call and a gateway call still reach them and a flow
 * step cannot.
 */
import * as path from "node:path";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import type { FlowConfig } from "../../../src/flow/schema.js";
import { ProjectFixture } from "../../helpers/project-fixture.js";
import { UeMcpRuntime } from "../../../src/runtime/runtime.js";
import { isContextCommand } from "../../../src/runtime/context-commands.js";
import { buildFlowRegistry } from "../../../src/flow/registry.js";
import { buildDefaults } from "../../../src/flow/loader.js";
import { FlowConfigSchema } from "../../../src/flow/schema.js";
import { createFlowTool } from "../../../src/flow/flow-tool.js";
import { dispatchCategoryCall, type DispatchDeps } from "../../../src/dispatch/server-dispatch.js";
import { ALL_TOOLS } from "../../../src/tools.js";
import type { ToolContext } from "../../../src/core/types.js";

const COMMANDS = ["project.set_project", "project.add_editor", "project.use_editor", "project.drop_editor"];

let fixture: ProjectFixture;

beforeEach(() => {
  fixture = new ProjectFixture("ue-mcp-context-commands-");
  vi.stubEnv("UE_MCP_GLOBAL_CONFIG", path.join(fixture.root, "global.yml"));
  vi.stubEnv("UE_MCP_PORT", undefined);
  vi.stubEnv("UE_MCP_CONTEXT_STRATEGY", undefined);
});

afterEach(() => {
  vi.unstubAllEnvs();
  fixture.cleanup();
});

async function server(strategy: "full" | "micro") {
  const runtime = await UeMcpRuntime.start({
    projectArgs: [fixture.makeProject("Alpha")],
    packageVersion: "0.0.0",
    settings: { context: { strategy } },
  });
  const deps: DispatchDeps = {
    sessions: runtime.sessions,
    loads: runtime.loads,
    lockingCfg: runtime.settings.locking,
    dialogGuardFor: (s, canElicit) => runtime.dialogGuardFor(s, canElicit),
    elicit: () => undefined,
    client: () => undefined,
  };
  return { runtime, deps, tools: await runtime.advertisedTools() };
}

function stop(runtime: UeMcpRuntime): void {
  for (const s of runtime.sessions.list()) s.dialogGuard?.stopWatching();
}

describe("context commands", () => {
  it("are not tasks, so no registry holds them", () => {
    const registered = buildFlowRegistry(ALL_TOOLS).listRegistered();
    for (const name of COMMANDS) {
      expect(isContextCommand(name)).toBe(true);
      expect(registered).not.toContain(name);
    }
    expect(isContextCommand("project.list_editors")).toBe(false);
    expect(registered).toContain("project.list_editors");
  });

  it("cannot be called by a flow", async () => {
    const registry = buildFlowRegistry(ALL_TOOLS);
    const config = FlowConfigSchema.parse({
      tasks: buildDefaults(ALL_TOOLS).tasks,
      flows: { hop: { steps: { 1: { task: "project.use_editor", options: { editorTarget: "Beta" } } } } },
    }) as FlowConfig;
    const flow = createFlowTool(registry, () => config);
    const ctx = { bridge: { isConnected: false }, project: {} } as unknown as ToolContext;
    const result = await flow.handler(ctx, { action: "run", flowName: "hop" }).catch((e: unknown) => ({ success: false, thrown: e }));
    expect((result as { success?: boolean }).success).toBe(false);
    expect(JSON.stringify(result)).toContain("a flow cannot call it");
  });

  it("run from a direct category call", async () => {
    const { runtime, deps, tools } = await server("full");
    try {
      const project = tools.find((t) => t.name === "project")!;
      const out = await dispatchCategoryCall(deps, project, false, { action: "use_editor", editorTarget: "Alpha" }, {});
      expect(out.isError).toBeUndefined();
      expect(JSON.parse(out.content[0].text)).toMatchObject({ success: true, activeEditor: "Alpha" });
    } finally {
      stop(runtime);
    }
  });

  it("run through the micro gateway", async () => {
    const { runtime, deps, tools } = await server("micro");
    try {
      const gateway = tools[0];
      const out = await dispatchCategoryCall(deps, gateway, false, {
        action: "call", category: "project", method: "use_editor", args: { editorTarget: "Alpha" },
      }, {});
      expect(out.isError).toBeUndefined();
      expect(JSON.parse(out.content[0].text)).toMatchObject({ success: true, activeEditor: "Alpha" });
      const refused = await dispatchCategoryCall(deps, gateway, false, {
        action: "call", category: "project", method: "use_editor", args: {},
      }, {});
      expect(refused.isError).toBe(true);
      expect(refused.content[0].text).toContain("Missing 'editorTarget'");
    } finally {
      stop(runtime);
    }
  });
});
