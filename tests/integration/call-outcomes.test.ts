/**
 * What a client is answered on every way a call can end, through the real
 * server over stdio: a result, a refusal from each gate in order, a failure
 * from the editor, and the same calls through the lean catalog, the micro
 * gateway, a flow and a second editor.
 *
 * The answers are recorded in tests/golden/call-outcomes.json (sandbox paths,
 * ports and process ids rewritten), so a change to the call pipeline that
 * moves a single byte of a response shows up as a diff. Re-record with
 * `npx vitest run tests/integration/call-outcomes.test.ts -u` and read the
 * diff before committing it.
 */
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { FakeBridge } from "../fake-bridge.js";
import { McpServer, runCli, Sandbox, stopSandboxDaemons, waitFor, type CallResult } from "./harness.js";
import { call, startDaemonCli } from "./daemon-fakes.js";

const CONFIG = `ue-mcp:
  version: 1
  disable:
    - foliage
flows:
  outline:
    description: Read the outliner, then fail in the editor.
    steps:
      "1":
        task: level.get_outliner
      "2":
        task: asset.delete
        options:
          assetPath: /Game/Refused
`;

const MODAL = { title: "Save Content", message: "Save changes to 1 package?", buttons: ["Save", "Don't Save"] };

let sb: Sandbox;
let game: string;
let other: string;
let gameBridge: FakeBridge;
let otherBridge: FakeBridge;
let dialogUp = false;

function handlers(): Record<string, (p: Record<string, unknown>) => unknown> {
  return {
    list_dialogs: () => ({ success: true, dialogs: dialogUp ? [MODAL] : [] }),
    get_world_outliner: () => ({ success: true, actors: [{ label: "Floor", class: "StaticMeshActor" }], count: 1 }),
    place_actor: (p) => ({ success: true, actorLabel: String(p.label ?? "Cube"), actorPath: "/Game/Map.Map:PersistentLevel.Cube" }),
    delete_asset: () => ({ success: false, error: "The editor refused to delete /Game/Refused." }),
    get_selected_actors: () => ({ success: true, media: [{ type: "image", mimeType: "image/png", data: "iVBORw0KGgo=", width: 1, height: 1 }] }),
    get_engine_state: () => ({ success: true, state: "idle" }),
  };
}

/** Sandbox paths, ports and pids differ per run; the shape of the answer does not. */
function normalise(text: string): string {
  const root = sb.root;
  const variants = [root, root.replace(/\\/g, "/"), JSON.stringify(root).slice(1, -1), JSON.stringify(root.replace(/\\/g, "/")).slice(1, -1)];
  let out = text;
  for (const v of variants.sort((a, b) => b.length - a.length)) out = out.split(v).join("<SANDBOX>");
  for (const port of [gameBridge.port, otherBridge.port]) out = out.split(String(port)).join("<PORT>");
  // A path recorded on Windows reads the same as one recorded on the CI runners.
  out = out.replace(/<SANDBOX>((?:\\\\|\\|\/)[\w.-]+)+/g, (m) => m.replace(/\\\\|\\/g, "/"));
  out = out.replace(/"pid":\s*\d+/g, '"pid": "<PID>"');
  out = out.replace(/\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d+)?Z/g, "<TIME>");
  out = out.replace(/"(duration|durationMs|elapsedMs|ms|startedAt|endedAt|runId)":\s*("[^"]*"|\d+)/g, '"$1": "<VAR>"');
  out = out.replace(/\b\d+ms\b/g, "<N>ms");
  out = out.replace(/[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}/g, "<UUID>");
  return out;
}

function record(r: CallResult): { isError: boolean; blocks: string[]; media: number } {
  return { isError: r.isError, blocks: r.blocks.map(normalise), media: r.media.length };
}

const outcomes: Record<string, unknown> = {};

beforeAll(async () => {
  sb = new Sandbox();
  game = sb.makeProject("Game", { config: CONFIG, content: true });
  other = sb.makeProject("Other", { content: true });
  gameBridge = await FakeBridge.start({ projectDir: path.dirname(game), handlers: handlers(), answerAnything: false });
  otherBridge = await FakeBridge.start({ projectDir: path.dirname(other), handlers: handlers(), answerAnything: false });
  // A person starts playtests in Game.
  const set = await runCli(sb, ["playtest", "mode", "manual", "--editor", game]);
  expect(set.code, set.stderr).toBe(0);
}, 120_000);

afterAll(async () => {
  await Promise.all([gameBridge?.stop(), otherBridge?.stop()]);
  sb.cleanup();
});

/** One call per way a category call can end, aimed at one strategy's tools. */
type Caller = (category: string, action: string, args?: Record<string, unknown>) => Promise<CallResult>;

async function categoryMatrix(call: Caller): Promise<Record<string, unknown>> {
  const out: Record<string, unknown> = {};
  out.read = record(await call("level", "get_outliner"));
  out.mutate = record(await call("level", "place_actor", { actorClass: "StaticMeshActor", label: "Cube" }));
  out.undeclaredInTask = record(await call("level", "place_actor", { actorClass: "StaticMeshActor", actorLabel: "Cube" }));
  out.media = record(await call("level", "get_selected"));
  out.taskFailed = record(await call("asset", "delete", { assetPath: "/Game/Refused" }));
  out.bridgeError = record(await call("level", "get_current"));
  out.handler = record(await call("project", "describe_action", { name: "level.get_selected" }));
  out.unknownAction = record(await call("level", "get_outliners"));
  out.invalidParams = record(await call("level", "get_outliner", { limit: "many" }));
  out.undeclaredKey = record(await call("level", "get_outliner", { notAParam: 1 }));
  out.disabled = record(await call("foliage", "list_types"));
  out.playtest = record(await call("pie", "script_run", { script: "Smoke" }));
  out.staleEditor = record(await call("level", "get_outliner", { editor: "Nowhere" }));
  dialogUp = true;
  try {
    out.dialogRead = record(await call("level", "get_outliner"));
    out.dialogMutate = record(await call("level", "place_actor", { actorClass: "StaticMeshActor" }));
    out.dialogList = record(await call("editor", "list_dialogs"));
  } finally {
    dialogUp = false;
  }
  return out;
}

async function flowMatrix(server: McpServer): Promise<Record<string, unknown>> {
  const out: Record<string, unknown> = {};
  out.plan = record(await server.callFlow({ action: "plan", flowName: "outline" }));
  out.planUnknown = record(await server.callFlow({ action: "plan", flowName: "nope" }));
  out.run = record(await server.callFlow({ action: "run", flowName: "outline" }));
  dialogUp = true;
  try {
    out.runUnderDialog = record(await server.callFlow({ action: "run", flowName: "outline" }));
  } finally {
    dialogUp = false;
  }
  return out;
}

async function withServer<T>(opts: Parameters<typeof McpServer.start>[1], body: (s: McpServer) => Promise<T>): Promise<T> {
  const server = await McpServer.start(sb, { port: null, ...opts, env: { UE_MCP_DIALOG_MODE: "defer", ...opts.env } });
  try {
    return await body(server);
  } finally {
    await server.close();
  }
}

describe("call outcomes", () => {
  it("answers each way a call ends under full", async () => {
    outcomes.full = await withServer({ projects: [game], strategy: "full" }, async (s) => ({
      category: await categoryMatrix((c, a, args = {}) => s.call(c, { action: a, args })),
      flat: record(await s.call("level", { action: "get_outliner", limit: 5 })),
      flow: await flowMatrix(s),
    }));
  }, 180_000);

  it("answers each way a call ends under lean, with its discovery", async () => {
    outcomes.lean = await withServer({ projects: [game], strategy: "lean" }, async (s) => ({
      category: await categoryMatrix((c, a, args = {}) => s.call(c, { action: a, args })),
      describe: record(await s.call("level", { action: "describe" })),
      catalogUnknown: record(await s.call("catalog", { action: "describe", category: "nope" })),
      catalogEmpty: record(await s.call("catalog", { action: "search" })),
    }));
  }, 180_000);

  it("answers each way a call ends through the micro gateway", async () => {
    outcomes.micro = await withServer({ projects: [game], strategy: "micro" }, async (s) => {
      const gw = (args: Record<string, unknown>) => s.call("tools", args);
      return {
        category: await categoryMatrix((c, a, args = {}) => gw({ action: "call", category: c, method: a, args })),
        unknownCategory: record(await gw({ action: "call", category: "nope", method: "x", args: {} })),
        unknownMethod: record(await gw({ action: "call", category: "level", method: "nope", args: {} })),
        nestedAction: record(await gw({ action: "call", category: "level", method: "get_outliner", args: { action: "other" } })),
        routingBeside: record(await gw({ action: "call", category: "level", method: "get_outliner", args: { select: "count" }, select: "actors" })),
        describeUnknown: record(await gw({ action: "describe", category: "nope" })),
        searchEmpty: record(await gw({ action: "search" })),
        flow: await flowMatrix(s),
      };
    });
  }, 180_000);

  it("answers routing, attribution and the untargeted refusal beyond one editor", async () => {
    outcomes.twoEditors = await withServer({ projects: [game, other], strategy: "full" }, async (s) => {
      const call: Caller = (c, a, args = {}) => s.call(c, { action: a, args });
      return {
        untargetedRead: record(await call("level", "get_outliner")),
        untargetedMutate: record(await call("level", "place_actor", { actorClass: "StaticMeshActor" })),
        targetedMutate: record(await s.call("level", { action: "place_actor", args: { actorClass: "StaticMeshActor" }, editor: "Other" })),
        staleEditor: record(await s.call("level", { action: "get_outliner", editor: "Nowhere" })),
        flowUntargeted: record(await s.call("flow", { action: "run", flowName: "outline" })),
        flowList: record(await s.call("flow", { action: "list" })),
      };
    });
  }, 180_000);

  it("answers the daemon's tools, leases and flow routes", async () => {
    const d = await startDaemonCli(sb, game, { UE_MCP_PORT: undefined, UE_MCP_DIALOG_MODE: "defer" });
    try {
      const http = async (method: string, route: string, body?: unknown) => {
        const r = await call(d, method, route, body);
        return { status: r.status, text: normalise(r.text) };
      };
      const out: Record<string, unknown> = {};
      out.tools = await http("GET", "/v1/tools");
      out.categoryRefused = await http("POST", "/v1/tools/categories", { category: "project", enabled: false });
      out.categoryUnknown = await http("POST", "/v1/tools/categories", { category: "nope", enabled: false });
      out.categoryInvalid = await http("POST", "/v1/tools/categories", { category: "gas", enabled: "yes" });
      out.leases = await http("GET", "/v1/leases");
      out.flows = await http("GET", "/v1/flows");
      out.plan = await http("GET", "/v1/flows/outline/plan");
      out.run = await http("POST", "/v1/flows/outline/run", {});
      out.runStaleEditor = await http("POST", "/v1/flows/outline/run", { editor: "Nowhere" });
      dialogUp = true;
      try {
        await waitFor(async () => (await call(d, "GET", "/v1/flows")).status === 409, 20_000, "the daemon to see the dialog");
        out.runUnderDialog = await http("POST", "/v1/flows/outline/run", {});
      } finally {
        dialogUp = false;
      }
      outcomes.daemon = out;
    } finally {
      await stopSandboxDaemons(sb);
    }
  }, 180_000);

  it("matches the recording", async () => {
    await expect(JSON.stringify(outcomes, null, 2) + "\n").toMatchFileSnapshot("../golden/call-outcomes.json");
  });
});
