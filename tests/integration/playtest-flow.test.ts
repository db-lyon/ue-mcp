/**
 * The built-in playtest flow and the model behind flow agent steps: a daemon
 * extension's provider through /v1/flows, the first-party anthropic extension
 * against a local stand-in for the Messages API, and the refusal when no
 * model is configured. The editor is a fake bridge whose pie_script_validate
 * refuses the first draft, so the flow has to hand the errors back.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeBridge } from "../fake-bridge.js";
import { FakeHttp, McpServer, REPO_ROOT, Sandbox, stopSandboxDaemons, waitFor } from "./harness.js";
import { call, startDaemonCli, writeExtension, type Discovery } from "./daemon-fakes.js";

type Json = Record<string, unknown>;

const BP = "/Game/Hero/BP_Hero";
const MISSING = "/Game/NoSuchFolder/NoSuchAsset";
const BAD = { steps: [{ instruction: "Lose health", expect: { value: "Helth", op: "decreasing" } }] };
const GOOD = { steps: [{ instruction: "Lose health", expect: { value: "Health", op: "decreasing", within_ms: 3000 } }, { instruction: "It feels fair", judge: "manual" }] };
const NO_MODEL = "No model is configured for flow agent steps.";

/** An editor that knows the hero, and refuses a script naming a value it does not have. */
function editorLike(created: Json[]): Record<string, (p: Json) => unknown> {
  return {
    search_assets: (p) => ({ success: true, results: String(p.query).toLowerCase().includes("hero") ? [{ path: BP, name: "BP_Hero", className: "Blueprint" }] : [] }),
    read_blueprint: (p) => ({ success: true, assetPath: p.assetPath, variables: [{ name: "Health", type: "float" }] }),
    list_input_assets: () => ({ success: true, inputMappingContexts: [{ path: "/Game/Input/IMC_Default" }], inputActions: [] }),
    read_imc: (p) => ({ success: true, imcPath: p.imcPath, mappings: [{ key: "SpaceBar", inputAction: "/Game/Input/IA_Jump" }] }),
    pie_script_values: (p) => ({
      success: true, pawn: { class: "/Game/Hero/BP_Hero.BP_Hero_C" }, target: { requested: p.target, found: p.target !== MISSING, ...(p.target !== MISSING ? { is_player_pawn: true } : {}) },
      values: [{ path: "Health", kind: "number", type: "float" }], actors: [], widgets: [], profile_values: [], spawned_classes: [],
    }),
    pie_script_validate: (p) => {
      const bad = (p.steps as Json[]).some((s) => (s.expect as Json | undefined)?.value === "Helth");
      return bad
        ? { success: true, valid: false, summary: "steps[0].expect.value: 'Helth' cannot be read",
            errors: [{ step: 0, field: "expect.value", message: "steps[0].expect.value: 'Helth' cannot be read", candidates: ["Health"] }] }
        : { success: true, valid: true, errors: [] };
    },
    pie_script_create: (p) => {
      created.push(p);
      return { success: true, created: true, path: `/Game/PIEStudio/Scripts/${p.name}`, step_count: (p.steps as unknown[]).length };
    },
  };
}

/** Which drafts the model produced, judged by what each prompt carried. */
function draftFor(prompt: string): Json {
  return prompt.includes("was refused") ? GOOD : BAD;
}

/** extensions/anthropic installed alone into a fresh extensions directory, as a release zip would land; returns that directory. */
function installAnthropic(label: string): string {
  const root = sb.path(`extensions-${label}`);
  const dir = path.join(root, "anthropic");
  fs.cpSync(path.join(REPO_ROOT, "extensions", "anthropic"), dir, { recursive: true });
  const pkg = JSON.parse(fs.readFileSync(path.join(dir, "package.json"), "utf8")) as Json;
  fs.writeFileSync(path.join(dir, "package.json"), JSON.stringify({ ...pkg, version: "1.0.0" }));
  return root;
}

/** A local stand-in for the Messages API that answers with `draft(prompt)`. */
function messagesApi(draft: (prompt: string) => Json): Promise<FakeHttp> {
  return FakeHttp.start((req) => {
    const body = JSON.parse(req.body) as { model: string; messages: Array<{ content: unknown }> };
    const prompt = body.messages.map((m) => (typeof m.content === "string" ? m.content : JSON.stringify(m.content))).join("\n");
    return {
      json: {
        id: "msg_fake", type: "message", role: "assistant", model: body.model,
        content: [{ type: "text", text: JSON.stringify(draft(prompt)) }],
        stop_reason: "end_turn", stop_sequence: null, usage: { input_tokens: 12, output_tokens: 34 },
      },
    };
  });
}

let sb: Sandbox;

beforeAll(() => {
  sb = new Sandbox("ue-mcp-playtest-");
});
afterAll(async () => {
  await stopSandboxDaemons(sb);
  sb.cleanup();
});

describe("the playtest flow", () => {
  it("runs on a daemon extension's provider through /v1/flows, handing the refusal back once", async () => {
    covers("daemon.model-provider", "flow.playtest", "daemon.flows");
    const project = sb.makeProject("PlaytestDaemon");
    const created: Json[] = [];
    const bridge = await FakeBridge.start({ projectDir: path.dirname(project), handlers: editorLike(created), answerAnything: false });
    const prompts = sb.path("provider-prompts.jsonl");
    writeExtension(path.join(sb.home, ".ue-mcp", "extensions"), {
      name: "model",
      version: "1.0.0",
      pkg: { ueMcpExtension: { name: "model", api: { min: 2, max: 2 } } },
      activate: `
        api.models.register({
          async complete(req) {
            const prompt = (req.messages ?? []).map((m) => m.content).join("\\n") || req.prompt || "";
            (await import("node:fs")).appendFileSync(${JSON.stringify(prompts)}, JSON.stringify({ prompt, hasSchema: !!req.schema, tools: (req.tools ?? []).length, features: api.features, apiVersion: api.apiVersion }) + "\\n");
            const draft = prompt.includes("was refused") ? ${JSON.stringify(GOOD)} : ${JSON.stringify(BAD)};
            return { text: JSON.stringify(draft), parsed: draft, finishReason: "stop", usage: { inputTokens: 10, outputTokens: 10 } };
          },
        });
      `,
    });
    let d: Discovery;
    try {
      d = await startDaemonCli(sb, project, { UE_MCP_PORT: undefined });
      await waitFor(async () => (await call(d, "GET", "/v1/health")).body.editors[0]?.connected === true, 15_000, "the daemon to reach the fake editor");
      expect((await call(d, "GET", "/v1/health")).body.extensions).toEqual([{ name: "model", version: "1.0.0" }]);

      const run = await call(d, "POST", "/v1/flows/playtest/run", { params: { target: "the hero", context: "taking damage" } });
      expect(run.status, run.text).toBe(200);
      expect(run.text).toMatch(/"success":\s*true/);

      const asked = fs.readFileSync(prompts, "utf8").trim().split("\n").map((l) => JSON.parse(l) as Json);
      expect(asked.length).toBe(2);
      expect(asked.every((a) => a.tools === 0)).toBe(true);
      expect(asked[0].apiVersion).toBe(3);
      expect(asked[0].features).toContain("models.provider");
      expect(String(asked[0].prompt)).toContain(BP);
      expect(String(asked[0].prompt)).toContain("taking damage");
      expect(String(asked[0].prompt)).toContain("IMC_Default");
      expect(String(asked[1].prompt)).toContain("candidates");
      expect(String(asked[1].prompt)).toContain("Helth");

      const validated = bridge.calls.filter((c) => c.method === "pie_script_validate");
      expect(validated.length).toBe(2);
      expect(created).toHaveLength(1);
      expect(created[0]).toMatchObject({ name: "TS_Playtest_BP_Hero", onConflict: "replace", steps: GOOD.steps });
      expect(bridge.calls.find((c) => c.method === "pie_script_values")!.params).toMatchObject({ target: BP });
    } finally {
      await stopSandboxDaemons(sb);
      await bridge.stop();
    }
  }, 120_000);

  it("runs on the anthropic extension with ANTHROPIC_API_KEY, and fails with the errors once every draft is refused", async () => {
    covers("flow.agent.anthropic-extension", "flow.playtest");
    const project = sb.makeProject("PlaytestEnv");
    const created: Json[] = [];
    const bridge = await FakeBridge.start({ projectDir: path.dirname(project), handlers: editorLike(created), answerAnything: false });
    let alwaysBad = false;
    const api = await messagesApi((prompt) => (alwaysBad ? BAD : draftFor(prompt)));
    try {
      const d = await startDaemonCli(sb, project, {
        UE_MCP_PORT: undefined,
        UE_MCP_EXTENSIONS_DIR: installAnthropic("env"),
        ANTHROPIC_API_KEY: "sk-ant-fake-integration",
        ANTHROPIC_BASE_URL: api.origin,
      });
      await waitFor(async () => (await call(d, "GET", "/v1/health")).body.editors[0]?.connected === true, 15_000, "the daemon to reach the fake editor");
      expect((await call(d, "GET", "/v1/health")).body.extensions).toEqual([{ name: "anthropic", version: "1.0.0" }]);

      const ok = await call(d, "POST", "/v1/flows/playtest/run", { params: { target: BP } });
      expect(ok.status, ok.text).toBe(200);
      expect(ok.text).toMatch(/"success":\s*true/);
      expect(created).toHaveLength(1);
      const sent = api.requests.filter((r) => r.url.endsWith("/v1/messages"));
      expect(sent.length).toBe(2);
      expect(sent[0].headers["x-api-key"]).toBe("sk-ant-fake-integration");
      expect((JSON.parse(sent[0].body) as { model: string }).model).toBe("claude-opus-5-5");

      alwaysBad = true;
      created.length = 0;
      const failed = await call(d, "POST", "/v1/flows/playtest/run", { params: { target: BP } });
      expect(failed.text).toMatch(/still refused after 3 attempts/);
      expect(failed.text).toContain("Helth");
      expect(created).toHaveLength(0);
      expect(bridge.calls.filter((c) => c.method === "pie_script_validate").length).toBe(2 + 3);
    } finally {
      await stopSandboxDaemons(sb);
      await bridge.stop();
      await api.stop();
    }
  }, 120_000);

  it("fails before drafting when the target path resolves to nothing, and saves no script", async () => {
    covers("flow.playtest");
    const project = sb.makeProject("PlaytestMissing");
    const created: Json[] = [];
    const bridge = await FakeBridge.start({ projectDir: path.dirname(project), handlers: editorLike(created), answerAnything: false });
    const api = await messagesApi(() => GOOD);
    try {
      const d = await startDaemonCli(sb, project, {
        UE_MCP_PORT: undefined,
        UE_MCP_EXTENSIONS_DIR: installAnthropic("missing"),
        ANTHROPIC_API_KEY: "sk-ant-fake-integration",
        ANTHROPIC_BASE_URL: api.origin,
      });
      await waitFor(async () => (await call(d, "GET", "/v1/health")).body.editors[0]?.connected === true, 15_000, "the daemon to reach the fake editor");
      const r = await call(d, "POST", "/v1/flows/playtest/run", { params: { target: MISSING } });
      expect(r.text).toContain(`playtest target ${MISSING} does not exist`);
      expect(r.text).not.toMatch(/"success":\s*true/);
      expect(api.requests.filter((q) => q.url.endsWith("/v1/messages"))).toHaveLength(0);
      expect(bridge.calls.some((c) => c.method === "pie_script_validate")).toBe(false);
      expect(created).toHaveLength(0);

      const described = await call(d, "POST", "/v1/flows/playtest/run", { params: { target: "the hero" } });
      expect(described.text).toMatch(/"success":\s*true/);
      expect(created).toHaveLength(1);
    } finally {
      await stopSandboxDaemons(sb);
      await bridge.stop();
      await api.stop();
    }
  }, 120_000);

  it("fails an agent step with no model configured, before asking the editor to save anything", async () => {
    covers("flow.agent.no-model");
    const project = sb.makeProject("PlaytestNoModel");
    const created: Json[] = [];
    const bridge = await FakeBridge.start({ projectDir: path.dirname(project), handlers: editorLike(created), answerAnything: false });
    // A key alone is not a model any more: the provider that reads it is an extension.
    const server = await McpServer.start(sb, { projects: [project], port: null, env: { ANTHROPIC_API_KEY: "sk-ant-fake-integration" } });
    try {
      const r = await server.callFlow({ action: "run", flowName: "playtest", params: { target: BP } });
      expect(r.text).toContain(NO_MODEL);
      expect(r.text).toContain("ue-mcp install anthropic");
      expect(created).toHaveLength(0);
      expect(bridge.calls.some((c) => c.method === "pie_script_validate")).toBe(false);
    } finally {
      await server.close();
      await bridge.stop();
    }
  }, 60_000);
});
