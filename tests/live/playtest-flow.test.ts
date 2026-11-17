// @live-editor headless
/**
 * flow(run, playtest) against tests/ue_mcp, through the shipped server. The
 * model is ANTHROPIC_API_KEY when the shell has one; otherwise a local
 * stand-in for the Messages API, whose first draft names a value the pawn does
 * not have, so the editor's own pie(script_validate) has to refuse it and the
 * flow has to hand that refusal back before pie(script_create) saves the fix.
 */
import * as http from "node:http";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";
import { LiveServer, resultJson } from "./server.js";
import { closeLiveBridges, liveTarget } from "./harness.js";

type Json = Record<string, unknown>;
const DIR = "/Game/MCPTest/BCov/Pie/Playtest";
const NAME = "TS_LivePlaytest";
const REAL_KEY = process.env.ANTHROPIC_API_KEY?.trim() ?? "";

const BAD = { steps: [{ instruction: "Look around", expect: { value: "BaseEyeHieght", op: "gt", target: 0 } }] };
const GOOD = {
  steps: [
    { instruction: "Stand still", expect: { value: "BaseEyeHeight", op: "gt", target: 0, within_ms: 2000 } },
    { instruction: "The camera sits at eye height", judge: "manual" },
  ],
};

const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});

let bridge: EditorBridge;
let server: LiveServer;
let fake: http.Server | null = null;
const prompts: string[] = [];

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  const env: Record<string, string> = {};
  if (!REAL_KEY) {
    fake = http.createServer((req, res) => {
      const chunks: Buffer[] = [];
      req.on("data", (c) => chunks.push(c as Buffer));
      req.on("end", () => {
        const sent = JSON.parse(Buffer.concat(chunks).toString("utf8")) as { model: string; messages: Array<{ content: unknown }> };
        const prompt = sent.messages.map((m) => (typeof m.content === "string" ? m.content : JSON.stringify(m.content))).join("\n");
        prompts.push(prompt);
        const draft = prompt.includes("was refused") ? GOOD : BAD;
        res.writeHead(200, { "content-type": "application/json" });
        res.end(JSON.stringify({
          id: "msg_live_fake", type: "message", role: "assistant", model: sent.model,
          content: [{ type: "text", text: JSON.stringify(draft) }],
          stop_reason: "end_turn", stop_sequence: null, usage: { input_tokens: 1, output_tokens: 1 },
        }));
      });
    });
    await new Promise<void>((resolve) => fake!.listen(0, "127.0.0.1", () => resolve()));
    env.ANTHROPIC_API_KEY = "sk-ant-live-fake";
    env.ANTHROPIC_BASE_URL = `http://127.0.0.1:${(fake.address() as { port: number }).port}`;
  }
  const target = await liveTarget();
  server = await LiveServer.start({ projects: [target.uproject], env });
}, 240_000);

afterAll(async () => {
  await server?.close();
  if (fake) await new Promise<void>((r) => fake!.close(() => r()));
  if (bridge) await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  disconnectBridge();
  closeLiveBridges();
}, 60_000);

describe("the playtest flow on tests/ue_mcp", () => {
  it("drafts, has the editor check the draft, and saves a script that reads back", async () => {
    const result = await server.call("flow", {
      action: "run",
      flowName: "playtest",
      params: { target: "/Script/Engine.DefaultPawn", context: "the camera height of the default pawn", name: NAME, package_path: DIR },
      timeoutMs: 300_000,
    });
    const run = resultJson<{ success: boolean; steps: Json[] }>(result);
    expect(run.success, result.text).toBe(true);

    if (!REAL_KEY) {
      // The editor refused the first draft, and the second prompt carried its errors.
      expect(prompts.length).toBe(2);
      expect(prompts[0]).toContain("BaseEyeHeight");
      expect(prompts[0]).toContain("is_player_pawn");
      expect(prompts[1]).toContain("BaseEyeHieght");
      expect(prompts[1]).toContain("candidates");
    }

    const read = body(await callBridge(bridge, "pie_script_read", { path: `${DIR}/${NAME}` }));
    expect(read.success, JSON.stringify(read)).not.toBe(false);
    const steps = read.steps as Json[];
    expect(steps.length).toBeGreaterThan(0);
    expect(steps.some((s) => s.judge === "auto" && s.expect)).toBe(true);
    if (!REAL_KEY) expect((steps[0].expect as Json).value).toBe("BaseEyeHeight");

    // What was saved passes the editor's checks as it stands.
    const check = body(await callBridge(bridge, "pie_script_validate", { steps }));
    expect(check.valid, JSON.stringify(check.errors)).toBe(true);
  }, 360_000);

  it("fails before drafting when the target path resolves to nothing, and saves no script (#1610)", async () => {
    const missing = "/Game/NoSuchFolder/NoSuchAsset";
    const asked = prompts.length;
    const result = await server.call("flow", {
      action: "run",
      flowName: "playtest",
      params: { target: missing, name: "TS_LivePlaytestMissing", package_path: DIR },
      timeoutMs: 120_000,
    });
    expect(result.text).toContain(`playtest target ${missing} does not exist`);
    if (!REAL_KEY) expect(prompts.length).toBe(asked);
    const read = await callBridge(bridge, "pie_script_read", { path: `${DIR}/TS_LivePlaytestMissing` });
    expect(read.ok === false || body(read).success === false, JSON.stringify(read)).toBe(true);
  }, 180_000);
});
