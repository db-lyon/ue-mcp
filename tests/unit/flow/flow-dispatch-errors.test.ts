import { describe, it, expect } from "vitest";
import { dispatchFlowCall, errorResult, type DispatchDeps } from "../../../src/dispatch/server-dispatch.js";
import { SessionRegistry } from "../../../src/sessions/session.js";
import { GuardRegistry } from "../../../src/flow/guard.js";
import { McpError, ErrorCode } from "../../../src/core/errors.js";
import type { ToolDef } from "../../../src/core/types.js";

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

describe("flow calls carry the request's extra", () => {
  it("hand every step the request's progress reporter, elicitation and client", async () => {
    const { deps, sessions } = depsWithOneSession();
    const elicit = Object.assign(async () => ({ action: "accept" as const }), {
      clientAdvertisesElicitation: () => true,
    });
    const withClient = {
      ...deps,
      elicit: () => elicit,
      client: () => ({ name: "test-client", version: "1" }),
    } as unknown as DispatchDeps;
    const sent: Array<{ method: string; params: Record<string, unknown> }> = [];
    let seen: Record<string, unknown> | undefined;
    const flowTool: ToolDef = {
      name: "flow",
      description: "",
      schema: {},
      actions: {},
      handler: async (ctx) => {
        seen = { elicit: ctx.elicit, client: ctx.client };
        ctx.onProgress?.({ progress: 1, total: 2, message: "step 1" });
        return { ok: true };
      },
    };
    const result = await dispatchFlowCall(
      withClient,
      flowTool,
      { bridge: sessions.active.guarded, project: sessions.active.project },
      { action: "run", flowName: "x" },
      {
        _meta: { progressToken: "tok" },
        sendNotification: (async (n: { method: string; params: Record<string, unknown> }) => {
          sent.push(n);
        }) as never,
      },
    );
    expect(result.isError).toBeUndefined();
    expect(seen).toEqual({ elicit, client: { name: "test-client", version: "1" } });
    await new Promise((r) => setTimeout(r, 0));
    expect(sent).toEqual([{
      method: "notifications/progress",
      params: { progressToken: "tok", progress: 1, total: 2, message: "step 1" },
    }]);
  });
});
