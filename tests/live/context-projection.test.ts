// @live-editor headless
/**
 * A projected call reaches the editor (api.context.call). The runtime runs
 * micro, the projection is lean or micro scoped to `level`, and the call goes
 * through the in-process dispatch against the projection's own registry: a
 * lean category call and a micro gateway call both land on the editor and
 * answer, and a category outside the scope is refused without a call.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createRuntime, type Runtime } from "../../src/server/runtime.js";
import { ContextProjector } from "../../src/server/context-projection.js";
import { closeLiveBridges, liveTarget } from "./harness.js";

type Json = Record<string, any>;
const ENV = ["UE_MCP_PORT", "UE_MCP_HOST", "UE_MCP_CONTEXT_STRATEGY", "UE_MCP_DISABLE_UPDATE_CHECK"] as const;
const saved: Partial<Record<(typeof ENV)[number], string | undefined>> = {};

let rt: Runtime;
let projector: ContextProjector;

const text = (r: { content: Array<{ text?: string }> }): string => r.content.map((c) => c.text ?? "").join("\n");

beforeAll(async () => {
  const target = await liveTarget();
  for (const key of ENV) saved[key] = process.env[key];
  process.env.UE_MCP_PORT = String(target.port);
  process.env.UE_MCP_HOST = target.host;
  process.env.UE_MCP_CONTEXT_STRATEGY = "micro";
  process.env.UE_MCP_DISABLE_UPDATE_CHECK = "1";
  rt = await createRuntime([target.uproject]);
  await rt.connectBridges();
  projector = new ContextProjector(rt);
}, 120_000);

afterAll(() => {
  for (const s of rt?.sessions.list() ?? []) s.bridge.disconnect();
  for (const key of ENV) {
    if (saved[key] === undefined) delete process.env[key];
    else process.env[key] = saved[key];
  }
  closeLiveBridges();
});

describe("a projected call against the editor", () => {
  it("runs a lean category call the micro process does not advertise", async () => {
    const r = await projector.call({ strategy: "lean", categories: ["level"] }, "level", { action: "get_outliner", args: { limit: 5 } });
    expect(r.isError, text(r)).toBeFalsy();
    const body = JSON.parse(r.content[0].text ?? "{}") as Json;
    expect(Array.isArray(body.actors)).toBe(true);
  });

  it("runs a micro gateway call scoped to the category", async () => {
    const r = await projector.call({ strategy: "micro", categories: ["level"] }, "tools", {
      action: "call", category: "level", method: "get_outliner", args: { limit: 5 },
    });
    expect(r.isError, text(r)).toBeFalsy();
  });

  it("refuses a category outside the scope without calling the editor", async () => {
    const r = await projector.call({ strategy: "full", categories: ["level"] }, "asset", { action: "list", args: { path: "/Game" } });
    expect(r.isError).toBe(true);
    expect(text(r)).toMatch(/'asset' is outside this context's categories/);
  });
});
