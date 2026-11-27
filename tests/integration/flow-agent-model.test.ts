/**
 * The model a flow's agent step runs on, spawned: a daemon extension's
 * provider through /v1/flows, the ANTHROPIC_API_KEY fallback against a local
 * fake of the Messages API, and the refusal when neither is there.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeHttp, json, McpServer, Sandbox, stopSandboxDaemons } from "./harness.js";
import { call, startDaemonCli, writeExtension, type Discovery } from "./daemon-fakes.js";

type Json = Record<string, unknown>;

const NO_MODEL = "No model is configured for flow agent steps.";
const CONFIG = [
  "ue-mcp:",
  "  version: 1",
  "tasks:",
  "  summarize:",
  "    class_path: agent_prompt",
  "    options:",
  "      system: You describe Unreal levels in one sentence.",
  "      maxTokens: 200",
  "flows:",
  "  describe:",
  "    description: Describe the arena.",
  "    steps:",
  "      1: { task: summarize, options: { prompt: \"Describe the arena level\" } }",
  "",
].join("\n");

let sb: Sandbox;

beforeAll(() => {
  sb = new Sandbox("ue-mcp-agent-model-");
});
afterAll(async () => {
  await stopSandboxDaemons(sb);
  sb.cleanup();
});

describe("a flow's agent step", () => {
  it("runs on a daemon extension's provider through /v1/flows", async () => {
    covers("daemon.model-provider", "daemon.flows");
    const project = sb.makeProject("AgentDaemon", { config: CONFIG });
    const prompts = sb.path("provider-prompts.jsonl");
    writeExtension(path.join(sb.home, ".ue-mcp", "extensions"), {
      name: "model",
      version: "1.0.0",
      pkg: { ueMcpExtension: { name: "model", api: { min: 2, max: 2 } } },
      activate: `
        api.models.register({
          async complete(req) {
            const prompt = (req.messages ?? []).map((m) => m.content).join("\\n") || req.prompt || "";
            (await import("node:fs")).appendFileSync(${JSON.stringify(prompts)}, JSON.stringify({ prompt, system: req.system ?? "", features: api.features }) + "\\n");
            return { text: "A square arena.", finishReason: "stop", usage: { inputTokens: 10, outputTokens: 4 } };
          },
        });
      `,
    });
    let d: Discovery | undefined;
    try {
      d = await startDaemonCli(sb, project, { UE_MCP_PORT: undefined });
      expect((await call(d, "GET", "/v1/health")).body.extensions).toEqual([{ name: "model", version: "1.0.0" }]);
      const run = await call(d, "POST", "/v1/flows/describe/run", { params: {} });
      expect(run.status, run.text).toBe(200);
      expect(run.text).toMatch(/"success":\s*true/);
      expect(run.text).toContain("A square arena.");
      const asked = fs.readFileSync(prompts, "utf8").trim().split("\n").map((l) => JSON.parse(l) as Json);
      expect(asked).toHaveLength(1);
      expect(String(asked[0].prompt)).toContain("Describe the arena level");
      expect(String(asked[0].system)).toContain("one sentence");
      expect(asked[0].features).toContain("models.provider");
    } finally {
      await stopSandboxDaemons(sb);
    }
  }, 120_000);

  it("falls back to ANTHROPIC_API_KEY", async () => {
    covers("flow.agent.env-model");
    const project = sb.makeProject("AgentEnv", { config: CONFIG });
    const seen: string[] = [];
    const api = await FakeHttp.start((req) => {
      const body = JSON.parse(req.body) as { model: string; messages: Array<{ content: unknown }> };
      seen.push(body.messages.map((m) => (typeof m.content === "string" ? m.content : JSON.stringify(m.content))).join("\n"));
      return {
        json: {
          id: "msg_fake", type: "message", role: "assistant", model: body.model,
          content: [{ type: "text", text: "A round arena." }],
          stop_reason: "end_turn", stop_sequence: null, usage: { input_tokens: 12, output_tokens: 4 },
        },
      };
    });
    const server = await McpServer.start(sb, { projects: [project], port: null, env: { ANTHROPIC_API_KEY: "sk-ant-fake-integration", ANTHROPIC_BASE_URL: api.origin } });
    try {
      const r = json<{ success: boolean }>(await server.callFlow({ action: "run", flowName: "describe", params: {} }));
      expect(r.success, JSON.stringify(r)).toBe(true);
      expect(JSON.stringify(r)).toContain("A round arena.");
      expect(seen).toHaveLength(1);
      expect(seen[0]).toContain("Describe the arena level");
    } finally {
      await server.close();
      await api.stop();
    }
  }, 120_000);

  it("fails with no model configured", async () => {
    covers("flow.agent.no-model");
    const project = sb.makeProject("AgentNone", { config: CONFIG });
    const server = await McpServer.start(sb, { projects: [project], port: null, env: { ANTHROPIC_API_KEY: undefined } });
    try {
      const r = await server.callFlow({ action: "run", flowName: "describe", params: {} });
      expect(r.text).toContain(NO_MODEL);
      expect(json<{ success: boolean }>(r).success).toBe(false);
    } finally {
      await server.close();
    }
  }, 120_000);
});
