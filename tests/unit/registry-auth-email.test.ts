import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import {
  authBase,
  displayAccount,
  EMAIL_CODE_TRIES,
  fetchAuthMethods,
  loginWithEmail,
  readRegistryAuth,
} from "../../src/extensions/registry-auth.js";
import { chooseLoginMethod, parseLoginFlags } from "../../src/cli/login-cli.js";

const REGISTRY = "https://registry.test";
const WRONG = "That code is wrong or has expired. Request a new one.";
let dir: string;

const res = (status: number, body: unknown, headers: Record<string, string> = {}) =>
  new Response(JSON.stringify(body), { status, headers: { "content-type": "application/json", ...headers } });

/** A fetch stub answering by path, recording every call. */
function stubRegistry(routes: Record<string, Array<() => Response>>) {
  const calls: Array<{ url: string; body: unknown }> = [];
  const fetchMock = vi.fn(async (url: string, init?: RequestInit) => {
    const p = new URL(url).pathname;
    calls.push({ url: p, body: init?.body ? JSON.parse(String(init.body)) : undefined });
    const queue = routes[p];
    if (!queue || queue.length === 0) return new Response("not found", { status: 404 });
    return (queue.length > 1 ? queue.shift()! : queue[0])();
  });
  vi.stubGlobal("fetch", fetchMock);
  return calls;
}

/** Scripted answers to prompts; null once they run out. */
const answers = (...lines: string[]) => {
  const asked: string[] = [];
  return { asked, ask: async (q: string) => (asked.push(q), lines.length ? lines.shift()! : null) };
};

const started = () => res(200, { ok: true, expiresInSeconds: 600 });
const verified = (extra: Record<string, unknown> = {}) =>
  res(200, { ok: true, token: "uemcp_email", prefix: "uemcp_em", login: "dev@example.com", email: "dev@example.com", created: false, ...extra });

beforeEach(() => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-regauth-"));
  vi.stubEnv("UE_MCP_AUTH_DIR", dir);
  vi.stubEnv("UE_MCP_REGISTRY", REGISTRY);
  vi.stubEnv("UE_MCP_AUTH_URL", undefined);
});

afterEach(() => {
  vi.unstubAllEnvs();
  vi.unstubAllGlobals();
  fs.rmSync(dir, { recursive: true, force: true });
});

describe("loginWithEmail", () => {
  it("sends a code, verifies the typed code and caches the account with its email", async () => {
    const calls = stubRegistry({ "/api/auth/email/start": [started], "/api/auth/email/verify": [verified] });
    const logs: string[] = [];
    const a = answers("123 456");

    const auth = await loginWithEmail((m) => logs.push(m), { email: " dev@example.com ", ask: a.ask });

    expect(calls).toEqual([
      { url: "/api/auth/email/start", body: { email: "dev@example.com" } },
      { url: "/api/auth/email/verify", body: { email: "dev@example.com", code: "123456" } },
    ]);
    expect(auth).toMatchObject({ token: "uemcp_email", login: "dev@example.com", email: "dev@example.com", method: "email", registry: REGISTRY });
    expect(await readRegistryAuth()).toMatchObject({ token: "uemcp_email", method: "email" });
    expect(logs).toContain("Sent a sign-in code to dev@example.com. It is valid for 10 minutes.");
    expect(displayAccount(auth)).toBe("dev@example.com");
  });

  it("says when the sign-in created the account", async () => {
    stubRegistry({ "/api/auth/email/start": [started], "/api/auth/email/verify": [() => verified({ created: true })] });
    const logs: string[] = [];
    await loginWithEmail((m) => logs.push(m), { email: "dev@example.com", ask: answers("123456").ask });
    expect(logs).toContain("Created a UE-MCP account for dev@example.com.");
  });

  it("lets a wrong code be retried, and does not send malformed codes", async () => {
    const calls = stubRegistry({
      "/api/auth/email/start": [started],
      "/api/auth/email/verify": [() => res(400, { error: WRONG }), verified],
    });
    const logs: string[] = [];
    await loginWithEmail((m) => logs.push(m), { email: "dev@example.com", ask: answers("12ab", "111111", "222222").ask });
    expect(logs).toContain("The code is 6 digits.");
    expect(logs).toContain(WRONG);
    expect(calls.filter((c) => c.url === "/api/auth/email/verify").map((c) => (c.body as { code: string }).code)).toEqual(["111111", "222222"]);
  });

  it("stops accepting codes after the registry's limit and sends a new one on r", async () => {
    const wrongs = Array.from({ length: EMAIL_CODE_TRIES }, () => () => res(400, { error: WRONG }));
    const calls = stubRegistry({ "/api/auth/email/start": [started], "/api/auth/email/verify": [...wrongs, verified] });
    const codes = Array.from({ length: EMAIL_CODE_TRIES }, (_, i) => String(100000 + i));
    const a = answers(...codes, "r", "999999");

    const auth = await loginWithEmail(() => undefined, { email: "dev@example.com", ask: a.ask });

    expect(auth.token).toBe("uemcp_email");
    expect(a.asked[EMAIL_CODE_TRIES]).toContain("can no longer be used");
    expect(calls.filter((c) => c.url === "/api/auth/email/start")).toHaveLength(2);
  });

  it("gives up when the limit is hit and the user stops", async () => {
    const wrongs = Array.from({ length: EMAIL_CODE_TRIES }, () => () => res(400, { error: WRONG }));
    stubRegistry({ "/api/auth/email/start": [started], "/api/auth/email/verify": wrongs });
    const codes = Array.from({ length: EMAIL_CODE_TRIES }, (_, i) => String(100000 + i));
    await expect(loginWithEmail(() => undefined, { email: "dev@example.com", ask: answers(...codes, "").ask })).rejects.toThrow(/sign-in stopped/);
    expect(await readRegistryAuth()).toBeNull();
  });

  it("prints the wait on a 429 from start, from the body or the Retry-After header", async () => {
    stubRegistry({ "/api/auth/email/start": [() => res(429, { error: "slow down", retryAfter: 42 })] });
    await expect(loginWithEmail(() => undefined, { email: "dev@example.com", ask: answers().ask })).rejects.toThrow("try again in 42 seconds");

    stubRegistry({ "/api/auth/email/start": [() => res(429, { error: "slow down" }, { "retry-after": "17" })] });
    await expect(loginWithEmail(() => undefined, { email: "dev@example.com", ask: answers().ask })).rejects.toThrow("try again in 17 seconds");
  });

  it("prints the wait on a 429 from verify", async () => {
    stubRegistry({ "/api/auth/email/start": [started], "/api/auth/email/verify": [() => res(429, { error: "slow down", retryAfter: 30 })] });
    await expect(loginWithEmail(() => undefined, { email: "dev@example.com", ask: answers("123456").ask })).rejects.toThrow("try again in 30 seconds");
  });

  it("points at GitHub on a 503", async () => {
    stubRegistry({ "/api/auth/email/start": [() => res(503, { error: "Email sign-in is not available." })] });
    await expect(loginWithEmail(() => undefined, { email: "dev@example.com", ask: answers().ask })).rejects.toThrow(/not available right now.*ue-mcp login --github/);
  });

  it("refuses something that is not an email address without calling the registry", async () => {
    const calls = stubRegistry({});
    await expect(loginWithEmail(() => undefined, { email: "nope", ask: answers().ask })).rejects.toThrow("not an email address");
    expect(calls).toHaveLength(0);
  });

  it("reports the cached account instead of signing in again unless forced", async () => {
    stubRegistry({ "/api/auth/email/start": [started], "/api/auth/email/verify": [verified] });
    await loginWithEmail(() => undefined, { email: "dev@example.com", ask: answers("123456").ask });
    const logs: string[] = [];
    await loginWithEmail((m) => logs.push(m), { email: "dev@example.com", ask: answers().ask });
    expect(logs[0]).toBe(`Already logged in to ${REGISTRY} as dev@example.com.`);
  });
});

describe("fetchAuthMethods", () => {
  it("reads the methods, and is null on an older registry without the endpoint", async () => {
    stubRegistry({ "/api/auth/methods": [() => res(200, { email: true, github: true })] });
    expect(await fetchAuthMethods()).toEqual({ email: true, github: true });
    stubRegistry({});
    expect(await fetchAuthMethods()).toBeNull();
  });
});

describe("displayAccount", () => {
  it("prefixes @ only for a GitHub sign-in, including files written before email sign-in", () => {
    expect(displayAccount({ login: "octo", method: "github" })).toBe("@octo");
    expect(displayAccount({ login: "octo" })).toBe("@octo");
    expect(displayAccount({ login: "dev@example.com", method: "email" })).toBe("dev@example.com");
  });
});

describe("chooseLoginMethod", () => {
  const env = (isTTY: boolean, methods: { email: boolean; github: boolean } | null, ...lines: string[]) => {
    const a = answers(...lines);
    return { a, env: { isTTY, methods: vi.fn(async () => methods), ask: a.ask, log: () => undefined } };
  };

  it("parses --email with a value, --email=value and a bare --email", () => {
    expect(parseLoginFlags(["--email", "a@b.co", "--force"])).toMatchObject({ email: "a@b.co", force: true });
    expect(parseLoginFlags(["--email=a@b.co"])).toMatchObject({ email: "a@b.co" });
    expect(parseLoginFlags(["--email", "--force"])).toMatchObject({ emailWithoutAddress: true, force: true });
    expect(parseLoginFlags(["--github"])).toMatchObject({ github: true });
  });

  it("follows the flags without asking the registry", async () => {
    const e = env(true, { email: true, github: true });
    expect(await chooseLoginMethod(parseLoginFlags(["--github"]), e.env)).toEqual({ method: "github" });
    expect(await chooseLoginMethod(parseLoginFlags(["--email", "a@b.co"]), e.env)).toEqual({ method: "email", email: "a@b.co" });
    expect(e.env.methods).not.toHaveBeenCalled();
    await expect(chooseLoginMethod(parseLoginFlags(["--email", "a@b.co", "--github"]), e.env)).rejects.toThrow("not both");
  });

  it("uses GitHub without a terminal so scripts never wait", async () => {
    const e = env(false, { email: true, github: true });
    expect(await chooseLoginMethod(parseLoginFlags([]), e.env)).toEqual({ method: "github" });
    expect(e.a.asked).toEqual([]);
    await expect(chooseLoginMethod(parseLoginFlags(["--email"]), e.env)).rejects.toThrow("needs an address");
  });

  it("on a terminal asks, defaulting to email when the registry offers it", async () => {
    const e = env(true, { email: true, github: true }, "", "dev@example.com");
    expect(await chooseLoginMethod(parseLoginFlags([]), e.env)).toEqual({ method: "email", email: "dev@example.com" });
    const g = env(true, { email: true, github: true }, "g");
    expect(await chooseLoginMethod(parseLoginFlags([]), g.env)).toEqual({ method: "github" });
  });

  it("on a terminal falls back to GitHub when email is off or the registry predates it", async () => {
    for (const methods of [{ email: false, github: true }, null]) {
      const e = env(true, methods);
      expect(await chooseLoginMethod(parseLoginFlags([]), e.env)).toEqual({ method: "github" });
      expect(e.a.asked).toEqual([]);
    }
  });
});

describe("the sign-in host", () => {
  const verifyBody = { ok: true, token: "uemcp_email", prefix: "uemcp_em", login: "dev@example.com", email: "dev@example.com", created: false };

  it("defaults to accounts.ue-mcp.com and keeps the registry in the cached file", async () => {
    const hosts: string[] = [];
    vi.stubGlobal("fetch", vi.fn(async (url: string) => {
      const u = new URL(url);
      hosts.push(u.origin);
      return u.pathname.endsWith("/start") ? res(200, { ok: true, expiresInSeconds: 600 }) : res(200, verifyBody);
    }));
    expect(authBase()).toBe("https://accounts.ue-mcp.com");
    const auth = await loginWithEmail(() => undefined, { email: "dev@example.com", ask: answers("123456").ask });
    expect(hosts).toEqual(["https://accounts.ue-mcp.com", "https://accounts.ue-mcp.com"]);
    expect(auth.registry).toBe(REGISTRY);
  });

  it("retries once on the registry when the default host fails at the network level", async () => {
    const hosts: string[] = [];
    vi.stubGlobal("fetch", vi.fn(async (url: string) => {
      const u = new URL(url);
      hosts.push(u.origin);
      if (u.origin === "https://accounts.ue-mcp.com") throw new TypeError("fetch failed");
      return res(200, { email: true, github: true });
    }));
    expect(await fetchAuthMethods()).toEqual({ email: true, github: true });
    expect(hosts).toEqual(["https://accounts.ue-mcp.com", REGISTRY]);
  });

  it("does not retry an HTTP answer from the default host", async () => {
    const hosts: string[] = [];
    vi.stubGlobal("fetch", vi.fn(async (url: string) => {
      hosts.push(new URL(url).origin);
      return res(503, { error: "down" });
    }));
    await expect(loginWithEmail(() => undefined, { email: "dev@example.com", ask: answers().ask })).rejects.toThrow(/not available right now/);
    expect(hosts).toEqual(["https://accounts.ue-mcp.com"]);
  });

  it("uses UE_MCP_AUTH_URL when set and never falls back from it", async () => {
    vi.stubEnv("UE_MCP_AUTH_URL", "https://auth.test/");
    const hosts: string[] = [];
    vi.stubGlobal("fetch", vi.fn(async (url: string) => {
      hosts.push(new URL(url).origin);
      throw new TypeError("fetch failed");
    }));
    expect(authBase()).toBe("https://auth.test");
    await expect(loginWithEmail(() => undefined, { email: "dev@example.com", ask: answers().ask })).rejects.toThrow("fetch failed");
    expect(hosts).toEqual(["https://auth.test"]);
  });
});
