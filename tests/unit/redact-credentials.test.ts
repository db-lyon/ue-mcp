import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import * as fs from "node:fs";
import * as path from "node:path";
import { redactCredentials } from "../../src/core/redact-credentials.js";

// #1866: the bridge handshake wrote "Authorization: Bearer <token>" into the
// editor log. Every server log line now goes through redactCredentials in
// log.ts, and every plugin handshake line through UE_MCP_LOG_WIRE, which calls
// the C++ twin (covered by UE.MCP.Bridge.SessionToken.Redaction).

const TOKEN = "0123456789abcdef".repeat(4);

const UPGRADE_REQUEST = [
  "GET / HTTP/1.1",
  "Sec-WebSocket-Version: 13",
  "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==",
  "Connection: Upgrade",
  "Upgrade: websocket",
  `Authorization: Bearer ${TOKEN}`,
  "Host: 127.0.0.1:9877",
  "",
  "",
].join("\r\n");

describe("redactCredentials", () => {
  it("redacts the bearer token in an upgrade request and keeps the rest", () => {
    const out = redactCredentials(UPGRADE_REQUEST);
    expect(out).not.toContain(TOKEN);
    expect(out).toContain("Authorization: Bearer [REDACTED]\r\n");
    expect(out).toContain("Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n");
    expect(out.endsWith("\r\n\r\n")).toBe(true);
  });

  it.each([
    ["a lower-case header", `authorization: bearer ${TOKEN}`],
    ["a header map as JSON", `{"Authorization":"Bearer ${TOKEN}","Host":"x"}`],
    ["a header map as a JS object", `{ Authorization: 'Bearer ${TOKEN}' }`],
    ["a basic credential", `Authorization: Basic ${Buffer.from(`user:${TOKEN}`).toString("base64")}`],
    ["a bare scheme with no header name", `retrying with Bearer ${TOKEN}`],
    ["the flow token header", `X-UE-MCP-Token: ${TOKEN}`],
    ["a proxy credential", `Proxy-Authorization: Bearer ${TOKEN}`],
    ["a cookie", `Cookie: session=${TOKEN}; theme=dark`],
    ["a query string", `GET /flows?token=${TOKEN}&x=1`],
    ["a JSON access_token", `{"access_token": "${TOKEN}"}`],
    ["a token label", `flow HTTP token: ${TOKEN}`],
    ["an api key assignment", `api_key=${TOKEN}`],
    ["a header cut off part way through the value", `Authorization: Bearer ${TOKEN.slice(0, 20)}`],
  ])("redacts %s", (_label, input) => {
    const out = redactCredentials(input);
    expect(out).toContain("[REDACTED]");
    expect(out).not.toContain(TOKEN.slice(0, 20));
  });

  it("keeps the scheme word so a reader can see a token was sent", () => {
    expect(redactCredentials(`Authorization: Bearer ${TOKEN}`)).toBe("Authorization: Bearer [REDACTED]");
    expect(redactCredentials(`{"authorization": "Bearer ${TOKEN}"}`)).toBe(`{"authorization": "Bearer [REDACTED]"}`);
  });

  it.each([
    "send it as 'Authorization: Bearer <token>' or 'X-UE-MCP-Token: <token>'",
    "The Authorization header must be 'Bearer <session token>'.",
    "malformed Authorization header",
    "Session token published to C:/Users/x/.ue-mcp/bridge/9877.token",
    "bearer tokens are required on every upgrade",
    "Connection to editor bridge timed out (ws://127.0.0.1:9877)",
  ])("leaves text with no credential alone: %s", (input) => {
    expect(redactCredentials(input)).toBe(input);
  });

  it("is idempotent", () => {
    const once = redactCredentials(UPGRADE_REQUEST);
    expect(redactCredentials(once)).toBe(once);
  });
});

describe("log output", () => {
  const originalLevel = process.env.UE_MCP_LOG_LEVEL;
  let stderr: ReturnType<typeof vi.spyOn>;

  beforeEach(() => {
    process.env.UE_MCP_LOG_LEVEL = "debug";
    stderr = vi.spyOn(console, "error").mockImplementation(() => {});
  });

  afterEach(() => {
    stderr.mockRestore();
    if (originalLevel === undefined) delete process.env.UE_MCP_LOG_LEVEL;
    else process.env.UE_MCP_LOG_LEVEL = originalLevel;
    vi.resetModules();
  });

  it("never carries a token, through any level, message or error", async () => {
    vi.resetModules();
    const log = await import("../../src/core/log.js");
    log.debug("bridge", `upgrade request:\n${UPGRADE_REQUEST}`);
    log.info("http", `headers ${JSON.stringify({ authorization: `Bearer ${TOKEN}` })}`);
    log.warn("bridge", "upgrade refused", new Error(`401 for Authorization: Bearer ${TOKEN}`));
    log.error("http", "request failed", `token=${TOKEN}`);
    expect(stderr).toHaveBeenCalledTimes(4);
    for (const [line] of stderr.mock.calls) {
      expect(line).not.toContain(TOKEN);
      expect(line).toContain("[REDACTED]");
    }
  });
});

describe("plugin handshake logging", () => {
  const source = path.resolve(
    __dirname,
    "../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/BridgeWebSocket.cpp",
  );

  it("sends every handshake log line through the redacting macro", () => {
    const text = fs.readFileSync(source, "utf8");
    expect(text).toContain("UE_MCP_LOG_WIRE(");
    // A plain UE_LOG here could quote the upgrade request, Authorization header included.
    expect(text).not.toMatch(/\bUE_LOG\s*\(/);
  });
});
