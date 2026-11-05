/**
 * The flow tool a client sees: list, plan and run, for flows from
 * ue-mcp.yml, a plugin and the built-ins, against a bridge that answers
 * like an editor. Then the opt-in flow HTTP server, which serves the same
 * three routes behind a token and a Host allowlist.
 */
import * as fs from "node:fs";
import * as http from "node:http";
import * as net from "node:net";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeBridge } from "../fake-bridge.js";
import { json, McpServer, Sandbox } from "./harness.js";

const BUILT_INS = ["niagara_fire", "beacon", "texture_bomb", "neon_shrine", "neon_shrine_cleanup"];

const CONFIG = (httpPort?: number) => `ue-mcp:
  version: 1
${httpPort === undefined ? "" : `  http:\n    enabled: true\n    port: ${httpPort}\n    host: 127.0.0.1\n`}plugins:
  - name: ue-mcp-flowpack
flows:
  inspect:
    description: Read the project, then list its textures in the editor.
    steps:
      "1":
        task: project.get_info
      "2":
        task: asset.list_textures
        options:
          directory: /Game/\${steps.1.projectName}
  stops_on_failure:
    description: A step the editor fails, and one that must not run after it.
    steps:
      "1":
        task: asset.list_textures
        options:
          directory: /Game
      "2":
        task: asset.delete
        options:
          assetPath: /Game/Refused
      "3":
        task: asset.list_textures
        options:
          directory: /Game/After
`;

/** A plugin package that contributes one flow, installed where a project's npm puts it. */
function installFlowPlugin(projectDir: string): void {
  const dir = path.join(projectDir, "node_modules", "ue-mcp-flowpack");
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(
    path.join(dir, "package.json"),
    JSON.stringify({ name: "ue-mcp-flowpack", version: "1.0.0", type: "module", keywords: ["ue-mcp-plugin"] }),
  );
  fs.writeFileSync(
    path.join(dir, "ue-mcp.plugin.yml"),
    [
      "actionPrefix: flowpack",
      "flows:",
      "  pack_info:",
      "    description: Plugin flow that reads the project.",
      "    steps:",
      '      "1":',
      "        task: project.get_info",
      "",
    ].join("\n"),
  );
}

/** Answers that let a flow thread one step's output into the next. */
const EDITOR_LIKE = {
  create_niagara_emitter: (p: Record<string, unknown>) => ({ success: true, path: `${p.packagePath}/${p.name}` }),
  create_niagara_system: (p: Record<string, unknown>) => ({ success: true, path: `${p.packagePath}/${p.name}` }),
  add_emitter_to_system: () => ({ success: true, emitterIndex: 0 }),
  set_niagara_module_input: (p: Record<string, unknown>) => ({ success: true, writePath: "overrideMap", inputName: p.inputName }),
  validate_niagara_system: () => ({ success: true, valid: true, emitters: [{ name: "E_Flame", enabled: true, hasSpawn: true, hasRenderer: true }] }),
  list_textures: (p: Record<string, unknown>) => ({ success: true, directory: p.directory, textures: [] }),
};

interface StepResult {
  stepNumber: number;
  name: string;
  success: boolean;
  skipped: boolean;
  data?: Record<string, unknown>;
  error?: string;
}

interface RunResult {
  success: boolean;
  summary: string;
  stepCount: number;
  steps: StepResult[];
  runId: string;
}

async function freePort(): Promise<number> {
  return new Promise((resolve) => {
    const s = net.createServer().listen(0, "127.0.0.1", () => {
      const port = (s.address() as net.AddressInfo).port;
      s.close(() => resolve(port));
    });
  });
}

let sb: Sandbox;
let project: string;
let bridge: FakeBridge;
let server: McpServer;
let httpPort: number;
const TOKEN = "integration-flow-token";

const made = () => bridge.calls.filter((c) => c.method !== "list_dialogs");

beforeAll(async () => {
  sb = new Sandbox();
  httpPort = await freePort();
  project = sb.makeProject("Flows", { config: CONFIG(httpPort) });
  installFlowPlugin(path.dirname(project));
  bridge = await FakeBridge.start({ projectDir: path.dirname(project), handlers: EDITOR_LIKE, answerAnything: false });
  server = await McpServer.start(sb, { projects: [project], port: null, env: { UE_MCP_HTTP_TOKEN: TOKEN } });
}, 60_000);

afterAll(async () => {
  await server?.close();
  await bridge?.stop();
  sb.cleanup();
});

const flow = (args: Record<string, unknown>) => server.call("flow", args);

describe("the flow tool", () => {
  it("lists built-in, plugin and ue-mcp.yml flows", async () => {
    covers("flow.list");
    const listed = json<{ flowCount: number; flows: Array<{ name: string; description: string; stepCount: number }> }>(
      await flow({ action: "list" }),
    );
    const names = listed.flows.map((f) => f.name);
    expect(listed.flowCount).toBe(names.length);
    for (const b of BUILT_INS) expect(names).toContain(b);
    expect(listed.flows.find((f) => f.name === "pack_info")).toMatchObject({ description: "Plugin flow that reads the project.", stepCount: 1 });
    expect(listed.flows.find((f) => f.name === "inspect")).toMatchObject({ stepCount: 2 });
    expect(listed.flows.find((f) => f.name === "niagara_fire")!.stepCount).toBe(7);
  });

  it("plans a flow's steps without running any of them", async () => {
    covers("flow.plan");
    bridge.calls.length = 0;
    const plan = json<{ success: boolean; steps: Array<{ stepNumber: number; name: string; skipped: boolean }>; definition: { steps: Record<string, { options?: Record<string, unknown> }> } }>(
      await flow({ action: "plan", flowName: "inspect" }),
    );
    expect(plan.steps.map((s) => [s.stepNumber, s.name])).toEqual([[1, "project.get_info"], [2, "asset.list_textures"]]);
    expect(plan.definition.steps["2"].options).toEqual({ directory: "/Game/${steps.1.projectName}" });
    expect(made()).toEqual([]);

    const missing = await flow({ action: "plan", flowName: "no_such_flow" });
    expect(missing.isError).toBe(true);
    expect(missing.text).toContain("no_such_flow");
  });

  it("runs a ue-mcp.yml flow, threading one step's output into the next", async () => {
    covers("flow.run.yaml");
    bridge.calls.length = 0;
    const run = json<RunResult>(await flow({ action: "run", flowName: "inspect" }));
    expect(run.success).toBe(true);
    expect(run.stepCount).toBe(2);
    expect(run.steps.map((s) => [s.name, s.success])).toEqual([["project.get_info", true], ["asset.list_textures", true]]);
    expect(run.steps[0].data?.projectName).toBe("Flows");
    expect(run.summary).toContain("asset.list_textures");
    expect(made()).toEqual([{ method: "list_textures", params: { directory: "/Game/Flows" } }]);
  });

  it("reports the failing step and runs nothing after it", async () => {
    covers("flow.run.yaml");
    bridge.calls.length = 0;
    const r = await flow({ action: "run", flowName: "stops_on_failure" });
    const run = json<RunResult>(r);
    expect(run.success).toBe(false);
    const byNumber = Object.fromEntries(run.steps.map((s) => [s.stepNumber, s]));
    expect(byNumber[1].success).toBe(true);
    expect(byNumber[2].success).toBe(false);
    expect(JSON.stringify(byNumber[2])).toContain("Unknown method: delete_asset");
    expect(byNumber[3]?.success).not.toBe(true);
    expect(made().map((c) => c.method)).toEqual(["list_textures", "delete_asset"]);
  });

  it("runs a plugin's flow", async () => {
    covers("flow.run.yaml");
    const run = json<RunResult>(await flow({ action: "run", flowName: "pack_info" }));
    expect(run.success).toBe(true);
    expect(run.steps[0].data?.projectName).toBe("Flows");
  });

  it("runs the built-in niagara_fire against the editor, each step on the asset the last one made", async () => {
    covers("flow.run.builtin");
    bridge.calls.length = 0;
    const run = json<RunResult>(await flow({ action: "run", flowName: "niagara_fire" }));
    expect(run.success).toBe(true);
    expect(run.stepCount).toBe(7);
    expect(run.steps.every((s) => s.success)).toBe(true);
    const sent = made();
    expect(sent.map((c) => c.method)).toEqual([
      "create_niagara_emitter",
      "create_niagara_system",
      "add_emitter_to_system",
      "set_niagara_module_input",
      "set_niagara_module_input",
      "set_niagara_module_input",
      "validate_niagara_system",
    ]);
    expect(sent[2].params).toEqual({ systemPath: "/Game/VFX/Fire/NS_Fire", emitterPath: "/Game/VFX/Fire/E_Flame" });
    expect(sent.slice(3, 6).map((c) => c.params.inputName)).toEqual(["SpawnRate", "Velocity", "Color"]);
    expect(sent[6].params).toEqual({ systemPath: "/Game/VFX/Fire/NS_Fire" });
  });
});

describe("the flow HTTP server", () => {
  function request(
    method: string,
    route: string,
    headers: Record<string, string> = {},
    body?: string,
  ): Promise<{ status: number; body: string }> {
    return new Promise((resolve, reject) => {
      const req = http.request({ host: "127.0.0.1", port: httpPort, path: route, method, headers }, (res) => {
        let text = "";
        res.on("data", (d) => (text += d));
        res.on("end", () => resolve({ status: res.statusCode ?? 0, body: text }));
      });
      req.on("error", reject);
      if (body !== undefined) req.write(body);
      req.end();
    });
  }
  const bearer = { authorization: `Bearer ${TOKEN}` };

  it("lists, plans and runs flows behind its token and Host allowlist", async () => {
    covers("flow.http");
    const health = await request("GET", "/health", bearer);
    expect(health.status).toBe(200);
    expect(JSON.parse(health.body)).toMatchObject({ ok: true, port: httpPort });

    const listed = await request("GET", "/flows", { "x-ue-mcp-token": TOKEN });
    expect(listed.status).toBe(200);
    expect((JSON.parse(listed.body) as { flows: Array<{ name: string }> }).flows.map((f) => f.name)).toContain("inspect");

    const planned = await request("GET", "/flows/inspect/plan", bearer);
    expect(planned.status).toBe(200);
    expect((JSON.parse(planned.body) as { steps: unknown[] }).steps).toHaveLength(2);

    bridge.calls.length = 0;
    const ran = await request("POST", "/flows/inspect/run", { ...bearer, "content-type": "application/json" }, "{}");
    expect(ran.status).toBe(200);
    const run = JSON.parse(ran.body) as RunResult;
    expect(run.success).toBe(true);
    expect(made()).toEqual([{ method: "list_textures", params: { directory: "/Game/Flows" } }]);

    // Refusals: no token, a wrong one, and a Host a rebinding page would send.
    bridge.calls.length = 0;
    const anonymous = await request("POST", "/flows/inspect/run", {}, "{}");
    expect(anonymous.status).toBe(401);
    expect((await request("GET", "/flows", { authorization: "Bearer wrong" })).status).toBe(401);
    const rebound = await request("POST", "/flows/inspect/run", { ...bearer, host: "attacker.example" }, "{}");
    expect(rebound.status).toBe(403);
    expect(JSON.parse(rebound.body).error).toContain("Host");
    expect(made()).toEqual([]);

    expect((await request("GET", "/nowhere", bearer)).status).toBe(404);
  });
});
