import { describe, it, expect } from "vitest";
import { dispatchFlowCall, errorResult, type DispatchDeps } from "../../src/server/server-dispatch.js";
import { SessionRegistry } from "../../src/sessions/session.js";
import { GuardRegistry } from "../../src/flow/guard.js";
import { McpError, ErrorCode } from "../../src/core/errors.js";
import type { ToolDef } from "../../src/surface/tool-types.js";
import { blockText } from "../helpers/content-text.js";

/** Deps for one project-less session whose flow tool throws, with a dialog guard that allows everything. */
function depsWithFlowThrowing(error: unknown): DispatchDeps {
  const sessions = new SessionRegistry(new GuardRegistry());
  sessions.register({});
  const tool: ToolDef = {
    name: "flow",
    description: "",
    schema: {},
    actions: {},
    handler: async () => {
      throw error;
    },
  };
  return {
    origin: "mcp",
    sessions,
    loads: { dispatchUnion: { tools: [] } },
    lockingCfg: { enabled: false, ttlSeconds: 0 },
    dialogGuardFor: () => ({ check: async () => ({ allow: true }) }),
    elicit: () => undefined,
    client: () => undefined,
    flow: { tool, ctx: { bridge: sessions.active.guarded, project: sessions.active.project } },
  } as unknown as DispatchDeps;
}

describe("flow tool errors", () => {
  it("carry the same Error [CODE] prefix as a category tool's", async () => {
    const deps = depsWithFlowThrowing(new McpError(ErrorCode.NOT_FOUND, "no such flow"));
    const result = await dispatchFlowCall(deps, { action: "run", flow: "missing" });
    expect(result.isError).toBe(true);
    expect(result.content.map((b) => blockText(b))).toContain("Error [NOT_FOUND]: no such flow");
  });

  it("name an error that carries no code UNKNOWN, as a category tool does", async () => {
    const result = await dispatchFlowCall(depsWithFlowThrowing(new Error("boom")), { action: "run" });
    expect(result.content.map((b) => blockText(b))).toEqual(
      errorResult("UNKNOWN", "boom").content.map((b) => blockText(b)),
    );
  });
});
