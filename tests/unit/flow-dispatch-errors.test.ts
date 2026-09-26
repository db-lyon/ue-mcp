import { describe, it, expect } from "vitest";
import { dispatchFlowCall, errorResult, type DispatchDeps } from "../../src/server-dispatch.js";
import { SessionRegistry } from "../../src/sessions/session.js";
import { GuardRegistry } from "../../src/flow/guard.js";
import { McpError, ErrorCode } from "../../src/core/errors.js";
import type { ToolDef } from "../../src/core/types.js";

/** Deps for one project-less session, with a dialog guard that allows everything. */
function depsWithOneSession(): { deps: DispatchDeps; sessions: SessionRegistry } {
  const sessions = new SessionRegistry(new GuardRegistry());
  sessions.register({});
  const deps = {
    sessions,
    loads: { dispatchUnion: { tools: [] } },
    lockingCfg: { enabled: false, ttlSeconds: 0 },
    dialogGuardFor: () => ({ check: async () => ({ allow: true }) }),
    elicit: () => undefined,
    client: () => undefined,
  } as unknown as DispatchDeps;
  return { deps, sessions };
}

function flowToolThrowing(error: unknown): ToolDef {
  return {
    name: "flow",
    description: "",
    schema: {},
    actions: {},
    handler: async () => {
      throw error;
    },
  };
}

describe("flow tool errors", () => {
  it("carry the same Error [CODE] prefix as a category tool's", async () => {
    const { deps, sessions } = depsWithOneSession();
    const result = await dispatchFlowCall(
      deps,
      flowToolThrowing(new McpError(ErrorCode.NOT_FOUND, "no such flow")),
      { bridge: sessions.active.guarded, project: sessions.active.project },
      { action: "run", flow: "missing" },
    );
    expect(result.isError).toBe(true);
    expect(result.content.map((b) => b.text)).toContain("Error [NOT_FOUND]: no such flow");
  });

  it("name an error that carries no code UNKNOWN, as a category tool does", async () => {
    const { deps, sessions } = depsWithOneSession();
    const result = await dispatchFlowCall(
      deps,
      flowToolThrowing(new Error("boom")),
      { bridge: sessions.active.guarded, project: sessions.active.project },
      { action: "run" },
    );
    expect(result.content.map((b) => b.text)).toContain("Error [UNKNOWN]: boom");
    expect(result.content.map((b) => b.text)).toEqual(
      errorResult("UNKNOWN", "boom").content.map((b) => b.text),
    );
  });
});
