/**
 * The first-party anthropic extension (extensions/anthropic): what it registers
 * from the daemon's environment, the Messages API request it builds from
 * flowkit's neutral one, and the release assets it ships as.
 */
import { afterEach, describe, expect, it } from "vitest";
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { activate, anthropicProvider, DEFAULT_MODEL, toMessages } from "../../extensions/anthropic/index.js";
import { packageExtensions } from "../../scripts/extension-assets.mjs";
import { installExtension } from "../../src/daemon/extension-install.js";

type Sent = { url: string; init: { headers: Record<string, string>; body: string } };

function fakeFetch(reply: { status?: number; body: unknown }, sent: Sent[]): typeof fetch {
  return (async (url: string, init: Sent["init"]) => {
    sent.push({ url, init });
    const text = typeof reply.body === "string" ? reply.body : JSON.stringify(reply.body);
    return { ok: (reply.status ?? 200) < 400, status: reply.status ?? 200, text: async () => text };
  }) as unknown as typeof fetch;
}

function fakeApi(): { api: Record<string, unknown>; registered: unknown[]; warnings: string[] } {
  const registered: unknown[] = [];
  const warnings: string[] = [];
  const api = {
    models: { register: (p: unknown) => { registered.push(p); return () => registered.splice(registered.indexOf(p), 1); } },
    log: { info: () => {}, warn: (m: string) => warnings.push(m) },
  };
  return { api, registered, warnings };
}

describe("the anthropic extension", () => {
  it("registers a provider only when ANTHROPIC_API_KEY is set, and says why when it is not", async () => {
    const none = fakeApi();
    expect(await activate(none.api, {})).toBeUndefined();
    expect(none.registered).toHaveLength(0);
    expect(none.warnings[0]).toMatch(/ANTHROPIC_API_KEY is not set/);

    const keyed = fakeApi();
    const handle = await activate(keyed.api, { ANTHROPIC_API_KEY: "sk-ant-x" });
    expect(keyed.registered).toHaveLength(1);
    handle!.dispose();
    expect(keyed.registered).toHaveLength(0);

    const old = fakeApi();
    delete old.api.models;
    await activate(old.api, { ANTHROPIC_API_KEY: "sk-ant-x" });
    expect(old.warnings[0]).toMatch(/models\.register/);
  });

  it("turns flowkit's messages into a Messages API body", () => {
    expect(toMessages({
      system: "be brief",
      messages: [
        { role: "system", content: "and kind" },
        { role: "user", content: "hi" },
        { role: "assistant", content: "calling", toolCalls: [{ id: "t1", name: "look", arguments: { at: 1 } }] },
        { role: "tool", toolCallId: "t1", content: "seen" },
      ],
    })).toEqual({
      system: "be brief\n\nand kind",
      messages: [
        { role: "user", content: "hi" },
        { role: "assistant", content: [{ type: "text", text: "calling" }, { type: "tool_use", id: "t1", name: "look", input: { at: 1 } }] },
        { role: "user", content: [{ type: "tool_result", tool_use_id: "t1", content: "seen" }] },
      ],
    });
    expect(toMessages({ prompt: "one shot" })).toEqual({ messages: [{ role: "user", content: "one shot" }] });
  });

  it("posts to the endpoint with the key and maps the reply back", async () => {
    const sent: Sent[] = [];
    const provider = anthropicProvider({
      apiKey: "sk-ant-x",
      baseUrl: "http://127.0.0.1:1/",
      fetchImpl: fakeFetch({ body: {
        model: "m-served", stop_reason: "tool_use", usage: { input_tokens: 3, output_tokens: 4 },
        content: [{ type: "text", text: "ok" }, { type: "tool_use", id: "t", name: "go", input: { a: 1 } }],
      } }, sent),
    });
    const res = await provider.complete({ prompt: "hi", stop: ["END"], tools: [{ name: "go", description: "d", parameters: { type: "object" } }] });
    expect(sent[0].url).toBe("http://127.0.0.1:1/v1/messages");
    expect(sent[0].init.headers["x-api-key"]).toBe("sk-ant-x");
    const body = JSON.parse(sent[0].init.body);
    expect(body).toMatchObject({ model: DEFAULT_MODEL, max_tokens: 16000, stop_sequences: ["END"], tools: [{ name: "go", description: "d", input_schema: { type: "object" } }] });
    expect(res).toEqual({ text: "ok", toolCalls: [{ id: "t", name: "go", arguments: { a: 1 } }], finishReason: "tool_use", usage: { inputTokens: 3, outputTokens: 4 }, model: "m-served" });
  });

  it("names the HTTP status and the API's message when a call fails", async () => {
    const provider = anthropicProvider({ apiKey: "k", fetchImpl: fakeFetch({ status: 401, body: { error: { message: "invalid x-api-key" } } }, []) });
    await expect(provider.complete({ prompt: "hi" })).rejects.toThrow("The Anthropic Messages API answered HTTP 401: invalid x-api-key");
  });
});

describe("the extension release assets", () => {
  let out: string | undefined;
  afterEach(() => {
    if (out) fs.rmSync(out, { recursive: true, force: true });
  });

  it("package a zip and manifest per extension that `extension install` accepts, at the stamped version", async () => {
    out = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-ext-assets-"));
    const files = packageExtensions("9.9.9", path.join(out, "assets"));
    expect(files.map((f) => path.basename(f))).toEqual(["ue-mcp-anthropic-9.9.9.zip", "ue-mcp-anthropic-9.9.9-manifest.json"]);
    const manifest = JSON.parse(fs.readFileSync(files[1], "utf8"));
    expect(manifest).toEqual({ url: "ue-mcp-anthropic-9.9.9.zip", sha256: crypto.createHash("sha256").update(fs.readFileSync(files[0])).digest("hex"), version: "9.9.9" });

    const root = path.join(out, "extensions");
    const installed = await installExtension(files[0], { sha256: manifest.sha256, root });
    expect(installed).toMatchObject({ name: "anthropic", version: "9.9.9" });
    expect(fs.existsSync(path.join(root, "anthropic@9.9.9", "index.js"))).toBe(true);
  });
});
