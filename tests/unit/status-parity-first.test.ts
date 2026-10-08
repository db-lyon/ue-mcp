/**
 * get_status reports parity first (spec 6.4): pluginStale comes from the
 * running plugin's handler list, ahead of the build-timestamp verdict.
 */
import { describe, it, expect } from "vitest";
import { sessionActions } from "../../src/tools/project/sessions.js";
import { ProjectContext } from "../../src/config/project.js";
import { bp, categoryTool } from "../../src/surface/category-tool.js";
import type { ToolContext, ToolDef } from "../../src/core/types.js";

const graph: ToolDef[] = [
  categoryTool("alpha", "Alpha", {
    list: bp("read", "List things. Params: none", "alpha_list"),
    save: bp("mutate", "Save a thing. Params: none", "alpha_save"),
  }),
];

function ctxWith(actions: string[] | undefined): ToolContext {
  return {
    bridge: {
      isConnected: true,
      capabilities: { protocolVersion: 2, legacy: false, actions, actionCount: actions?.length },
      registeredActions: actions ?? null,
      call: async () => ({}),
      connect: async () => {},
      retargetProject: () => { throw new Error("unused"); },
      getTarget: () => ({ projectPath: null, port: 1, portSource: "default" }),
    },
    project: new ProjectContext(),
    getToolGraph: () => graph,
  } as unknown as ToolContext;
}

async function status(actions: string[] | undefined): Promise<Record<string, unknown>> {
  const spec = sessionActions.get_status;
  if (spec.kind !== "handler") throw new Error("get_status is a handler");
  return (await spec.handler(ctxWith(actions), {})) as Record<string, unknown>;
}

describe("get_status staleness", () => {
  it("leads with the parity verdict when the plugin published a list", async () => {
    const missing = await status(["alpha_list"]);
    expect(missing.pluginStale).toBe(true);
    expect(missing.pluginStaleSource).toBe("parity");
    const keys = Object.keys(missing);
    expect(keys.indexOf("pluginStale")).toBeLessThan(keys.indexOf("deployedPlugin"));
    expect(keys.indexOf("deployedPlugin")).toBeLessThan(keys.indexOf("pluginBuildStale"));

    const clean = await status(["alpha_list", "alpha_save"]);
    expect(clean.pluginStale).toBe(false);
    expect(clean.pluginStaleSource).toBe("parity");
  });

  it("has no verdict without a list or a project to time-stamp", async () => {
    const out = await status(undefined);
    expect(out.pluginStale).toBeUndefined();
    expect(out.pluginStaleSource).toBeUndefined();
  });
});
