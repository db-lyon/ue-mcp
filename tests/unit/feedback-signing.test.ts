import { describe, it, expect, beforeEach, afterEach, vi } from "vitest";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";

/**
 * The anonymous feedback path posts to a hosted signing service that holds the
 * GitHub App key. Two things are under test: that the client speaks that
 * protocol correctly, and that it carries no credential of its own.
 */

const { submitFeedback } = await import("../../src/feedback/github-app.js");

const REPO = { owner: "db-lyon", repo: "ue-mcp" };
const ENDPOINT = "https://signing.example.test/api/feedback";

/** The two origins tried by default, in order. */
const FEEDBACK_HOST = "https://feedback.ue-mcp.com/";
const REGISTRY_PATH = "https://plugins.ue-mcp.com/api/feedback";

const originalFetch = globalThis.fetch;
const originalEndpointEnv = process.env.UE_MCP_FEEDBACK_ENDPOINT;
const originalFeedbackEnv = process.env.UE_MCP_FEEDBACK;
const originalRegistryEnv = process.env.UE_MCP_REGISTRY;

function jsonResponse(status: number, body: unknown, headers: Record<string, string> = {}): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "content-type": "application/json", ...headers },
  });
}

describe("anonymous feedback goes through the hosted signing service", () => {
  const fetchMock = vi.fn();

  beforeEach(() => {
    process.env.UE_MCP_FEEDBACK_ENDPOINT = ENDPOINT;
    delete process.env.UE_MCP_FEEDBACK;
    delete process.env.UE_MCP_REGISTRY;
    fetchMock.mockReset();
    globalThis.fetch = fetchMock as unknown as typeof fetch;
  });

  afterEach(() => {
    globalThis.fetch = originalFetch;
    if (originalEndpointEnv === undefined) delete process.env.UE_MCP_FEEDBACK_ENDPOINT;
    else process.env.UE_MCP_FEEDBACK_ENDPOINT = originalEndpointEnv;
    if (originalFeedbackEnv === undefined) delete process.env.UE_MCP_FEEDBACK;
    else process.env.UE_MCP_FEEDBACK = originalFeedbackEnv;
    if (originalRegistryEnv === undefined) delete process.env.UE_MCP_REGISTRY;
    else process.env.UE_MCP_REGISTRY = originalRegistryEnv;
  });

  it("posts the report to the endpoint and sends no credential", async () => {
    fetchMock.mockResolvedValue(
      jsonResponse(201, {
        ok: true,
        url: "https://github.com/db-lyon/ue-mcp/issues/900",
        number: 900,
        repo: "db-lyon/ue-mcp",
        authoredBy: "ue-mcp-feedback[bot]",
      }),
    );

    const result = await submitFeedback("A title", "A body", ["agent-feedback"], {
      useBot: true,
      repo: REPO,
    });

    expect(fetchMock).toHaveBeenCalledTimes(1);
    const [url, init] = fetchMock.mock.calls[0] as [string, RequestInit];
    expect(url).toBe(ENDPOINT);
    expect(init.method).toBe("POST");

    const headers = init.headers as Record<string, string>;
    expect(Object.keys(headers).map((k) => k.toLowerCase())).not.toContain("authorization");

    expect(JSON.parse(init.body as string)).toEqual({
      title: "A title",
      body: "A body",
      labels: ["agent-feedback"],
      repo: "db-lyon/ue-mcp",
    });

    expect(result).toEqual({
      kind: "submitted",
      url: "https://github.com/db-lyon/ue-mcp/issues/900",
      number: 900,
      authoredBy: "ue-mcp-feedback[bot]",
      authoredAs: "bot",
      repo: "db-lyon/ue-mcp",
    });
  });

  it("reports an unconfigured deployment instead of throwing", async () => {
    fetchMock.mockResolvedValue(
      jsonResponse(503, { error: "not configured here", code: "signing_not_configured" }),
    );

    const result = await submitFeedback("A title", "A body", [], { useBot: true, repo: REPO });

    expect(result.kind).toBe("bot_unavailable");
    if (result.kind !== "bot_unavailable") throw new Error("unreachable");
    expect(result.code).toBe("not_configured");
  });

  it("treats a missing endpoint as the anonymous path being off, not a tracker refusal", async () => {
    fetchMock.mockResolvedValue(new Response("<html>not found</html>", { status: 404 }));

    const result = await submitFeedback("A title", "A body", [], { useBot: true, repo: REPO });

    expect(result.kind).toBe("bot_unavailable");
    if (result.kind !== "bot_unavailable") throw new Error("unreachable");
    expect(result.code).toBe("not_configured");
    expect(result.message).toContain(ENDPOINT);
  });

  it("surfaces a rate limit with its retry hint", async () => {
    fetchMock.mockResolvedValue(
      jsonResponse(429, { error: "slow down", code: "rate_limited", retry_after: 900 }),
    );

    const result = await submitFeedback("A title", "A body", [], { useBot: true, repo: REPO });

    expect(result.kind).toBe("bot_unavailable");
    if (result.kind !== "bot_unavailable") throw new Error("unreachable");
    expect(result.code).toBe("rate_limited");
    expect(result.retryAfter).toBe(900);
  });

  it("reports an unreachable service rather than failing the call", async () => {
    fetchMock.mockRejectedValue(new Error("getaddrinfo ENOTFOUND"));

    const result = await submitFeedback("A title", "A body", [], { useBot: true, repo: REPO });

    expect(result.kind).toBe("bot_unavailable");
    if (result.kind !== "bot_unavailable") throw new Error("unreachable");
    expect(result.code).toBe("unreachable");
  });

  it("maps a refused destination onto the existing repo_unavailable recovery", async () => {
    fetchMock.mockResolvedValue(
      jsonResponse(403, { error: "not a tracker this endpoint posts to", code: "repo_not_allowed" }),
    );

    const result = await submitFeedback("A title", "A body", [], {
      useBot: true,
      repo: { owner: "someone", repo: "their-plugin" },
    });

    expect(result.kind).toBe("repo_unavailable");
    if (result.kind !== "repo_unavailable") throw new Error("unreachable");
    expect(result.repo).toBe("someone/their-plugin");
    expect(result.status).toBe(403);
  });

  it("stops at the named endpoint when one was named, rather than trying another host", async () => {
    fetchMock.mockResolvedValue(new Response("<html>not found</html>", { status: 404 }));

    await submitFeedback("A title", "A body", [], { useBot: true, repo: REPO });

    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect((fetchMock.mock.calls[0] as [string])[0]).toBe(ENDPOINT);
  });
});

/**
 * Feedback has its own public name. The registry path is kept behind it so a
 * deployment that predates the name, or a self-hosted one, still works, and so
 * a client does not go dark while DNS for the new host propagates.
 */
describe("which signing origin gets asked", () => {
  const fetchMock = vi.fn();

  const created = () =>
    jsonResponse(201, { ok: true, url: "https://example.test/issues/1", number: 1 });

  beforeEach(() => {
    delete process.env.UE_MCP_FEEDBACK_ENDPOINT;
    delete process.env.UE_MCP_FEEDBACK;
    delete process.env.UE_MCP_REGISTRY;
    fetchMock.mockReset();
    globalThis.fetch = fetchMock as unknown as typeof fetch;
  });

  afterEach(() => {
    globalThis.fetch = originalFetch;
    if (originalEndpointEnv === undefined) delete process.env.UE_MCP_FEEDBACK_ENDPOINT;
    else process.env.UE_MCP_FEEDBACK_ENDPOINT = originalEndpointEnv;
    if (originalFeedbackEnv === undefined) delete process.env.UE_MCP_FEEDBACK;
    else process.env.UE_MCP_FEEDBACK = originalFeedbackEnv;
    if (originalRegistryEnv === undefined) delete process.env.UE_MCP_REGISTRY;
    else process.env.UE_MCP_REGISTRY = originalRegistryEnv;
  });

  const urlsCalled = () => fetchMock.mock.calls.map((c) => (c as [string])[0]);

  it("posts to the feedback host by default", async () => {
    fetchMock.mockResolvedValue(created());

    await submitFeedback("A title", "A body", [], { useBot: true, repo: REPO });

    expect(urlsCalled()).toEqual([FEEDBACK_HOST]);
  });

  it("falls back to the registry path when the feedback host does not resolve", async () => {
    fetchMock
      .mockRejectedValueOnce(new Error("getaddrinfo ENOTFOUND feedback.ue-mcp.com"))
      .mockResolvedValueOnce(created());

    const result = await submitFeedback("A title", "A body", [], { useBot: true, repo: REPO });

    expect(urlsCalled()).toEqual([FEEDBACK_HOST, REGISTRY_PATH]);
    expect(result.kind).toBe("submitted");
  });

  it("falls back when the feedback host answers with something other than the endpoint", async () => {
    fetchMock
      .mockResolvedValueOnce(new Response("<html>bad gateway</html>", { status: 502 }))
      .mockResolvedValueOnce(created());

    const result = await submitFeedback("A title", "A body", [], { useBot: true, repo: REPO });

    expect(urlsCalled()).toEqual([FEEDBACK_HOST, REGISTRY_PATH]);
    expect(result.kind).toBe("submitted");
  });

  it("accepts a real answer from the first host instead of asking the second", async () => {
    fetchMock.mockResolvedValue(
      jsonResponse(503, { error: "not configured here", code: "signing_not_configured" }),
    );

    const result = await submitFeedback("A title", "A body", [], { useBot: true, repo: REPO });

    expect(urlsCalled()).toEqual([FEEDBACK_HOST]);
    expect(result.kind).toBe("bot_unavailable");
  });

  it("does not double-post when the first host rate limits the caller", async () => {
    fetchMock.mockResolvedValue(jsonResponse(429, { code: "rate_limited", retry_after: 600 }));

    await submitFeedback("A title", "A body", [], { useBot: true, repo: REPO });

    expect(urlsCalled()).toEqual([FEEDBACK_HOST]);
  });

  it("names both origins, and still degrades gracefully, when neither answers", async () => {
    fetchMock.mockRejectedValue(new Error("getaddrinfo ENOTFOUND"));

    const result = await submitFeedback("A title", "A body", [], { useBot: true, repo: REPO });

    expect(urlsCalled()).toEqual([FEEDBACK_HOST, REGISTRY_PATH]);
    expect(result.kind).toBe("bot_unavailable");
    if (result.kind !== "bot_unavailable") throw new Error("unreachable");
    expect(result.code).toBe("unreachable");
    expect(result.message).toContain(FEEDBACK_HOST);
    expect(result.message).toContain(REGISTRY_PATH);
  });

  it("honours UE_MCP_FEEDBACK as the origin", async () => {
    process.env.UE_MCP_FEEDBACK = "https://feedback.local.test/";
    fetchMock.mockResolvedValue(created());

    await submitFeedback("A title", "A body", [], { useBot: true, repo: REPO });

    expect(urlsCalled()[0]).toBe("https://feedback.local.test/");
  });

  it("asks a self-hosted registry first when only UE_MCP_REGISTRY is set", async () => {
    process.env.UE_MCP_REGISTRY = "https://registry.internal.test";
    fetchMock.mockResolvedValue(created());

    await submitFeedback("A title", "A body", [], { useBot: true, repo: REPO });

    expect(urlsCalled()).toEqual(["https://registry.internal.test/api/feedback"]);
  });
});

describe("the package ships no signing credential", () => {
  const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");

  it("has no credential blob under assets/", () => {
    expect(fs.existsSync(path.join(repoRoot, "assets", "installation.bin"))).toBe(false);
  });

  it("has no runtime loader for an embedded key", () => {
    expect(fs.existsSync(path.join(repoRoot, "src", "manifest-signature.ts"))).toBe(false);
  });

  it("never signs a JWT client-side", () => {
    const files = fs
      .readdirSync(path.join(repoRoot, "src"), { recursive: true, encoding: "utf-8" })
      .filter((f) => f.endsWith(".ts"));
    const offenders = files.filter((f) => {
      const text = fs.readFileSync(path.join(repoRoot, "src", f), "utf-8");
      return text.includes("createSign(") || text.includes("PRIVATE KEY-----");
    });
    // src/feedback/secret-scrub.ts matches "PRIVATE KEY-----" as a redaction pattern,
    // which is the opposite of shipping one.
    expect(offenders.filter((f) => !f.endsWith("secret-scrub.ts"))).toEqual([]);
  });
});
