/**
 * Per-path dispatch and leak assertions against a real editor (#817, 7.3).
 *
 * `editor` selects which editor a call runs in. It is a routing instruction
 * and the client has to consume it: a handler reads the fields it knows about
 * and ignores the rest, so a routing key forwarded into a bridge call succeeds,
 * returns the right answer, and is invisible to every assertion that can be
 * made on the response. Plan item 0.8 exists to give that assertion an oracle,
 * and this is the suite that can use it, because the oracle is the running
 * editor's own record of the parameter names each dispatch arrived with.
 *
 * Every path in the plan's 3.2 table is exercised here. The two that a client
 * drives end to end go through a real server over stdio; the rest are driven
 * in process against the same live bridge, which is where the engine-free tests
 * drives them too, but with a real editor answering and the echo reading back
 * what it received.
 *
 * The leak assertion is global rather than scoped to the methods this file
 * calls: any dispatch the editor recorded with a routing key on it is a leak,
 * whoever sent it.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import * as http from "node:http";
import { SessionRegistry, type EditorSession } from "../../src/session.js";
import { ALL_TOOLS } from "../../src/tools.js";
import { assetTool } from "../../src/tools/asset.js";
import { niagaraTool } from "../../src/tools/niagara.js";
import { buildMicroGateway } from "../../src/lean-context.js";
import { buildFlowRegistry } from "../../src/flow/registry.js";
import { loadFlowConfig } from "../../src/flow/loader.js";
import { createFlowTool } from "../../src/flow/flow-tool.js";
import { startFlowHttpServer } from "../../src/flow/http-server.js";
import { routeEditorCall } from "../../src/editor-gate.js";
import { cloneToolDef, injectEditorTarget, sessionContext, type ToolContext, type ToolDef } from "../../src/types.js";
import type { EditorBridge } from "../../src/bridge.js";
import { LiveServer } from "./server.js";
import {
  clearParamEcho,
  closeLiveBridges,
  liveBridge,
  liveTarget,
  makeTempProject,
  paramEchoUnavailable,
  readParamEcho,
  type ParamEchoEntry,
} from "./harness.js";

const target = await liveTarget();
const echoUnavailable = await paramEchoUnavailable();
if (echoUnavailable) {
  console.warn(`[live] skipping the leak assertions: ${echoUnavailable}`);
}

/** The name the server gives the live editor's session: its project name. */
const LIVE = "ue_mcp";
const TEMP = makeTempProject("LiveTierSecond");

const READ_DIRECTORY = "/Game";

let echo: EditorBridge;
let sessions: SessionRegistry;
let live: EditorSession;
let ctx: ToolContext;

/** Names of every dispatch the editor recorded, with the routing keys on it. */
function leaks(entries: ParamEchoEntry[]): string[] {
  return entries
    .filter((e) => e.paramNames.includes("editor") || e.paramNames.includes("toEditor"))
    .map((e) => `${e.method}(${e.paramNames.join(", ")})`);
}

function methodsIn(entries: ParamEchoEntry[]): string[] {
  return entries.map((e) => e.method);
}

beforeAll(async () => {
  echo = await liveBridge();
  sessions = new SessionRegistry();
  live = sessions.register({ projectPath: target.uproject });
  sessions.register({ projectPath: TEMP.uproject });
  await live.bridge.connect(5000);
  ctx = {
    bridge: live.guarded,
    project: live.project,
    session: live,
    sessions,
  };
}, 120_000);

afterAll(async () => {
  live?.bridge.disconnect();
  closeLiveBridges();
  TEMP.cleanup();
});

/** A copy of a category tool with the routing parameter injected, as advertised. */
function targetable(tool: ToolDef): ToolDef {
  const copy = cloneToolDef(tool);
  injectEditorTarget(copy, sessions.list().map((s) => s.name));
  return copy;
}

/** One dispatch through the routing layer, into the routed session's context. */
async function dispatch(tool: ToolDef, params: Record<string, unknown>): Promise<unknown> {
  const routed = routeEditorCall(tool, params, sessions);
  return tool.handler(sessionContext(ctx, routed.session), routed.params);
}

describe.skipIf(echoUnavailable !== null)("the client consumes the routing key, on every path", () => {
  it("MCP category tools: the editor receives the call and no routing key", async () => {
    const server = await LiveServer.start({ projects: [target.uproject, TEMP.uproject] });
    try {
      await clearParamEcho(echo);
      const result = await server.call("asset", {
        action: "list_textures",
        directory: READ_DIRECTORY,
        recursive: false,
        editor: LIVE,
      });
      expect(result.isError).toBe(false);

      const entries = await readParamEcho(echo);
      // The call has to have arrived, or "no leak" is trivially true.
      expect(methodsIn(entries)).toContain("list_textures");
      expect(leaks(entries)).toEqual([]);
    } finally {
      await server.close();
    }
  }, 240_000);

  it("flows: neither the run's params nor a step's own options carry it through", async () => {
    // The fixture flow in tests/ue_mcp/ue-mcp.yml writes `editor` into every
    // step's options by hand, which is the case the per-action bridge task,
    // the handler-backed task and the generic class_path task each have to
    // strip for themselves. `params` adds the same key from the call side,
    // where it is forwarded verbatim into every step.
    const server = await LiveServer.start({ projects: [target.uproject, TEMP.uproject] });
    try {
      await clearParamEcho(echo);
      const result = await server.call("flow", {
        action: "run",
        flowName: "live_tier_leak_probe",
        editor: LIVE,
        params: { editor: LIVE },
      });
      expect(result.isError).toBe(false);

      const entries = await readParamEcho(echo);
      expect(methodsIn(entries)).toContain("list_textures");
      expect(leaks(entries)).toEqual([]);
    } finally {
      await server.close();
    }
  }, 240_000);

  it("the micro gateway: a target nested inside args does not travel", async () => {
    const gateway = targetable(buildMicroGateway(ALL_TOOLS));
    await clearParamEcho(echo);
    await dispatch(gateway, {
      action: "call",
      category: "asset",
      method: "list_textures",
      args: { directory: READ_DIRECTORY, recursive: false, editor: LIVE },
    });

    const entries = await readParamEcho(echo);
    expect(methodsIn(entries)).toContain("list_textures");
    expect(leaks(entries)).toEqual([]);
  }, 120_000);

  it("niagara(batch), which bypasses the task registry entirely", async () => {
    const tool = targetable(niagaraTool);
    await clearParamEcho(echo);
    // The op is expected to fail in the editor: the system path does not
    // exist. What matters is that the call reached the bridge and what it
    // carried when it did.
    await dispatch(tool, {
      action: "batch",
      editor: LIVE,
      ops: [{ action: "list_system_parameters", params: { systemPath: "/Game/LiveTierNoSuchSystem" } }],
    });

    const entries = await readParamEcho(echo);
    expect(methodsIn(entries)).toContain("list_niagara_system_parameters");
    expect(leaks(entries)).toEqual([]);
  }, 120_000);

  it("the category tool's own handler, which is not the MCP path but is kept correct", async () => {
    const tool = targetable(assetTool);
    await clearParamEcho(echo);
    await tool.handler(ctx, {
      action: "list_textures",
      directory: READ_DIRECTORY,
      recursive: false,
      editor: LIVE,
    });

    const entries = await readParamEcho(echo);
    expect(methodsIn(entries)).toContain("list_textures");
    expect(leaks(entries)).toEqual([]);
  }, 120_000);

  it("the HTTP flow surface resolves the target instead of forwarding it", async () => {
    const load = loadFlowConfig(ALL_TOOLS, live.project.projectDir ?? undefined);
    const registry = buildFlowRegistry(ALL_TOOLS);
    const flowTool = createFlowTool(registry, () => load.config);
    const started = startFlowHttpServer(flowTool, ctx, { port: 0, token: "live-tests-token" });
    await new Promise<void>((resolve) => started.server.once("listening", () => resolve()));
    const address = started.server.address();
    const port = typeof address === "object" && address !== null ? address.port : 0;

    try {
      await clearParamEcho(echo);
      const response = await postJson(port, "/flows/live_tier_leak_probe/run", started.token, {
        editor: LIVE,
        params: { editor: LIVE },
      });
      expect(response.status).toBe(200);

      const entries = await readParamEcho(echo);
      expect(methodsIn(entries)).toContain("list_textures");
      expect(leaks(entries)).toEqual([]);
    } finally {
      await new Promise<void>((resolve) => started.server.close(() => resolve()));
    }
  }, 180_000);
});

/** Minimal loopback POST, so the HTTP surface is exercised over a real socket. */
function postJson(
  port: number,
  path: string,
  token: string,
  body: unknown,
): Promise<{ status: number; body: string }> {
  const payload = JSON.stringify(body);
  return new Promise((resolve, reject) => {
    const req = http.request(
      {
        host: "127.0.0.1",
        port,
        path,
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          "Content-Length": Buffer.byteLength(payload),
          Authorization: `Bearer ${token}`,
        },
      },
      (res) => {
        const chunks: Buffer[] = [];
        res.on("data", (c: Buffer) => chunks.push(c));
        res.on("end", () =>
          resolve({ status: res.statusCode ?? 0, body: Buffer.concat(chunks).toString("utf-8") }),
        );
      },
    );
    req.on("error", reject);
    req.end(payload);
  });
}
