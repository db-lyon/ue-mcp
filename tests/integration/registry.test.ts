/**
 * The plugin registry as the user meets it: `ue-mcp login` and `logout`
 * trading a GitHub token or an emailed code for the account token, and the catalog the server
 * fetches and keeps on disk for when the registry cannot be reached.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeHttp, json, McpServer, plain, runCli, Sandbox } from "./harness.js";
import { writePluginPackage } from "./plugin-fixtures.js";

let sb: Sandbox;
let registry: FakeHttp;
const exchanges: Array<{ githubToken: string }> = [];
const posts: Array<string | undefined> = [];
const emailCalls: Array<{ url: string; body: Record<string, string> }> = [];

const CATALOG = [
  { slug: "clifix", name: "CLI Fixture", packageName: "ue-mcp-clifix", repoUrl: "https://github.com/example/ue-mcp-clifix", provides: ["tools"], status: "published" },
  { slug: "draft", name: "Draft Plugin", packageName: "ue-mcp-draft", repoUrl: "https://github.com/example/ue-mcp-draft", status: "draft" },
];

beforeAll(async () => {
  sb = new Sandbox();
  registry = await FakeHttp.start((req) => {
    if (req.url === "/api/cli/exchange" && req.method === "POST") {
      const body = JSON.parse(req.body) as { githubToken: string };
      exchanges.push(body);
      if (body.githubToken === "gho_revoked") return { status: 403, json: { error: "GitHub token rejected" } };
      return { json: { token: "uemcp_minted", login: "registry-octo" } };
    }
    if (req.url === "/api/auth/email/start" && req.method === "POST") {
      const body = JSON.parse(req.body) as { email: string };
      emailCalls.push({ url: req.url, body });
      if (body.email === "busy@example.com") return { status: 429, json: { error: "Too many requests.", retryAfter: 45 } };
      return { json: { ok: true, expiresInSeconds: 600 } };
    }
    if (req.url === "/api/auth/email/verify" && req.method === "POST") {
      const body = JSON.parse(req.body) as { email: string; code: string };
      emailCalls.push({ url: req.url, body });
      if (body.code !== "246810") return { status: 400, json: { error: "That code is wrong or has expired. Request a new one." } };
      return { json: { ok: true, token: "uemcp_email", prefix: "uemcp_em", login: body.email, email: body.email, created: true } };
    }
    if (req.url === "/api/plugins") return { json: { plugins: CATALOG } };
    if (req.url === "/api/publish" && req.method === "POST") {
      posts.push(req.headers.authorization);
      return { json: { ok: true } };
    }
    return { status: 404, body: "not found" };
  });
}, 30_000);

afterAll(async () => {
  await registry?.stop().catch(() => undefined);
  sb?.cleanup();
});

describe("login and logout", () => {
  const authDir = () => path.join(sb.home, ".ue-mcp");
  const registryJson = () => path.join(authDir(), "registry.json");
  const cli = (args: string[], env: Record<string, string> = {}) => runCli(sb, args, { env: { UE_MCP_REGISTRY: registry.origin, UE_MCP_AUTH_URL: registry.origin, ...env } });

  beforeAll(() => {
    fs.mkdirSync(authDir(), { recursive: true });
    fs.writeFileSync(path.join(authDir(), "auth.json"), JSON.stringify({ token: "gho_cached", login: "octo", authorized_at: "2026-01-01T00:00:00Z" }));
  });

  it("login trades the cached GitHub token for a publish token and caches it for this registry", async () => {
    covers("cli.login");
    const r = await cli(["login"]);
    expect(r.code, r.stderr + r.stdout).toBe(0);
    const out = plain(r.stdout);
    expect(out).toContain("Using your existing GitHub authorization (@octo)");
    expect(out).toContain(`Logged in to ${registry.origin} as @registry-octo`);
    expect(exchanges).toEqual([{ githubToken: "gho_cached" }]);
    const saved = JSON.parse(fs.readFileSync(registryJson(), "utf8"));
    expect(saved).toMatchObject({ token: "uemcp_minted", login: "registry-octo", registry: registry.origin });
  });

  it("login again reports the cached login and mints nothing", async () => {
    covers("cli.login");
    const r = await cli(["login"]);
    expect(r.code, r.stderr).toBe(0);
    expect(plain(r.stdout)).toContain(`Already logged in to ${registry.origin} as @registry-octo`);
    expect(exchanges).toHaveLength(1);
  });

  it("plugin publish uses the token login cached", async () => {
    covers("cli.login");
    const dir = writePluginPackage(sb.path("pub"), { name: "ue-mcp-pub", version: "0.1.0", manifest: "actionPrefix: pub\n" });
    const r = await cli(["plugin", "publish", dir]);
    expect(r.code, r.stderr + r.stdout).toBe(0);
    expect(posts).toEqual(["Bearer uemcp_minted"]);
  });

  it("login reports a refused exchange and caches nothing", async () => {
    covers("cli.login");
    const other = sb.path("other-auth");
    fs.mkdirSync(other, { recursive: true });
    fs.writeFileSync(path.join(other, "auth.json"), JSON.stringify({ token: "gho_revoked", login: "octo", authorized_at: "2026-01-01T00:00:00Z" }));
    const r = await cli(["login"], { UE_MCP_AUTH_DIR: other });
    expect(r.code).toBe(1);
    expect(r.stderr).toContain("registry exchange failed (HTTP 403): GitHub token rejected");
    expect(fs.existsSync(path.join(other, "registry.json"))).toBe(false);
  });

  it("logout forgets the cached token, and says so when there is none", async () => {
    covers("cli.logout");
    const r = await cli(["logout"]);
    expect(r.code, r.stderr).toBe(0);
    expect(plain(r.stdout)).toContain(`Logged out of ${registry.origin} (was @registry-octo)`);
    expect(fs.existsSync(registryJson())).toBe(false);
    expect(fs.existsSync(path.join(authDir(), "auth.json"))).toBe(true);

    const again = await cli(["logout"]);
    expect(again.code).toBe(0);
    expect(plain(again.stdout)).toContain("Not logged in.");
  });
});

describe("email login", () => {
  const cli = (args: string[], authDir: string, input?: string) =>
    runCli(sb, args, { env: { UE_MCP_REGISTRY: registry.origin, UE_MCP_AUTH_URL: registry.origin, UE_MCP_AUTH_DIR: authDir }, input });

  it("login --email sends a code, takes a retry after a wrong code, and caches the account without an @", async () => {
    covers("cli.login.email");
    const dir = sb.path("email-auth");
    fs.mkdirSync(dir, { recursive: true });
    const r = await cli(["login", "--email", "dev@example.com"], dir, "111111\n246810\n");
    expect(r.code, r.stderr + r.stdout).toBe(0);
    const out = plain(r.stdout);
    expect(out).toContain("Sent a sign-in code to dev@example.com. It is valid for 10 minutes.");
    expect(out).toContain("That code is wrong or has expired. Request a new one.");
    expect(out).toContain("Created a UE-MCP account for dev@example.com.");
    expect(out).toContain(`Logged in to ${registry.origin} as dev@example.com.`);
    expect(out).not.toContain("@dev@example.com");
    expect(out).not.toContain("uemcp_email");
    expect(emailCalls.map((c) => [c.url, c.body.code])).toEqual([
      ["/api/auth/email/start", undefined],
      ["/api/auth/email/verify", "111111"],
      ["/api/auth/email/verify", "246810"],
    ]);
    const saved = JSON.parse(fs.readFileSync(path.join(dir, "registry.json"), "utf8"));
    expect(saved).toMatchObject({ token: "uemcp_email", login: "dev@example.com", email: "dev@example.com", method: "email", registry: registry.origin });

    const out2 = await cli(["logout"], dir);
    expect(plain(out2.stdout)).toContain(`Logged out of ${registry.origin} (was dev@example.com)`);
  });

  it("login --email prints the wait when the registry rate limits and caches nothing", async () => {
    covers("cli.login.email");
    const dir = sb.path("email-busy");
    fs.mkdirSync(dir, { recursive: true });
    const r = await cli(["login", "--email", "busy@example.com"], dir);
    expect(r.code).toBe(1);
    expect(r.stderr).toContain("too many requests; try again in 45 seconds");
    expect(fs.existsSync(path.join(dir, "registry.json"))).toBe(false);
  });

  it("plain login without a terminal keeps the GitHub flow and never asks the registry for methods", async () => {
    covers("cli.login");
    const dir = sb.path("plain-auth");
    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(path.join(dir, "auth.json"), JSON.stringify({ token: "gho_plain", login: "octo", authorized_at: "2026-01-01T00:00:00Z" }));
    const r = await cli(["login"], dir);
    expect(r.code, r.stderr + r.stdout).toBe(0);
    expect(plain(r.stdout)).toContain(`Logged in to ${registry.origin} as @registry-octo`);
    expect(registry.requests.some((q) => q.url === "/api/auth/methods")).toBe(false);
  });
});

describe("the plugin catalog", () => {
  it("is fetched from the registry, cached on disk, and used from the cache when the registry is down", async () => {
    covers("registry.catalog");
    const uproject = sb.makeProject("Game");
    const cache = path.join(sb.home, ".ue-mcp", "registry-catalog.json");
    const origin = registry.origin;
    const route = { action: "route", title: "Ping returns nothing", summary: "The clifix ping action answers empty", repo: "example/ue-mcp-clifix" };

    const online = await McpServer.start(sb, { projects: [uproject], strategy: "full", env: { UE_MCP_REGISTRY: origin } });
    try {
      const r = json<{ target: string; target_repo: string; registry_reachable: boolean; matched_plugin: { slug: string } | null }>(await online.call("feedback", route));
      expect(r).toMatchObject({ target: "plugin", target_repo: "example/ue-mcp-clifix", registry_reachable: true });
      expect(r.matched_plugin?.slug).toBe("clifix");
    } finally {
      await online.close();
    }
    expect(registry.requests.some((q) => q.method === "GET" && q.url === "/api/plugins")).toBe(true);
    const cached = JSON.parse(fs.readFileSync(cache, "utf8")) as { base: string; plugins: Array<{ slug: string }> };
    expect(cached.base).toBe(origin);
    expect(cached.plugins.map((p) => p.slug)).toEqual(["clifix"]);

    await registry.stop();
    const offline = await McpServer.start(sb, { projects: [uproject], strategy: "full", env: { UE_MCP_REGISTRY: origin } });
    try {
      const r = json<{ target: string; target_repo: string; registry_reachable: boolean }>(await offline.call("feedback", route));
      expect(r).toMatchObject({ target: "plugin", target_repo: "example/ue-mcp-clifix", registry_reachable: true });
    } finally {
      await offline.close();
    }
  });
});
