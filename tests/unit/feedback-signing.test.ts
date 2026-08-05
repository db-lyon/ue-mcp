import { describe, it, expect, beforeEach, afterEach, vi } from "vitest";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";

/**
 * The anonymous feedback path posts to a hosted signing service that holds the
 * GitHub App key. Two things are under test: that the client speaks that
 * protocol correctly, and that it carries no credential of its own.
 */

const { submitFeedback } = await import("../../src/github-app.js");

const REPO = { owner: "db-lyon", repo: "ue-mcp" };
const ENDPOINT = "https://signing.example.test/api/feedback";

const originalFetch = globalThis.fetch;
const originalEndpointEnv = process.env.UE_MCP_FEEDBACK_ENDPOINT;

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
    fetchMock.mockReset();
    globalThis.fetch = fetchMock as unknown as typeof fetch;
  });

  afterEach(() => {
    globalThis.fetch = originalFetch;
    if (originalEndpointEnv === undefined) delete process.env.UE_MCP_FEEDBACK_ENDPOINT;
    else process.env.UE_MCP_FEEDBACK_ENDPOINT = originalEndpointEnv;
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

  it("defaults to the registry origin when no endpoint override is set", async () => {
    delete process.env.UE_MCP_FEEDBACK_ENDPOINT;
    fetchMock.mockResolvedValue(
      jsonResponse(201, { ok: true, url: "https://example.test/issues/1", number: 1 }),
    );

    await submitFeedback("A title", "A body", [], { useBot: true, repo: REPO });

    const [url] = fetchMock.mock.calls[0] as [string];
    expect(url).toBe("https://plugins.ue-mcp.com/api/feedback");
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
    // src/secret-scrub.ts matches "PRIVATE KEY-----" as a redaction pattern,
    // which is the opposite of shipping one.
    expect(offenders.filter((f) => !f.endsWith("secret-scrub.ts"))).toEqual([]);
  });
});
