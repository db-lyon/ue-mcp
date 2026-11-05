/**
 * The plugin registry as the user meets it: `ue-mcp login` and `logout`
 * trading a GitHub token for a publish token, and the catalog the server
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
  const cli = (args: string[], env: Record<string, string> = {}) => runCli(sb, args, { env: { UE_MCP_REGISTRY: registry.origin, ...env } });

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
