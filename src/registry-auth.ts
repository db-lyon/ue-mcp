/**
 * Registry login for `ue-mcp login`.
 *
 * Publishing to the plugin registry needs a per-author token. Rather than make
 * authors visit the site, mint a token, and paste it into an env var, this runs
 * the GitHub device flow already used for feedback authorship (src/auth.ts) and
 * trades the resulting GitHub identity for a registry token via
 * POST /api/cli/exchange.
 *
 * The registry token is cached at ~/.ue-mcp/registry.json (mode 0600) and read
 * automatically by `ue-mcp plugin publish`, so publishing needs no env var and
 * no shared secret. UE_MCP_PUBLISH_TOKEN and --token still win when set, so CI
 * can inject a token without logging in.
 */
import { promises as fs } from "node:fs";
import { homedir } from "node:os";
import { join } from "node:path";
import {
  readUserAuth,
  startDeviceFlow,
  tryExchangeDeviceCode,
  writeUserAuth,
  type UserAuth,
} from "./auth.js";

const AUTH_DIR = process.env.UE_MCP_AUTH_DIR || join(homedir(), ".ue-mcp");
const REGISTRY_FILE = join(AUTH_DIR, "registry.json");

export interface RegistryAuth {
  /** Registry publish token (uemcp_...). */
  token: string;
  /** GitHub login this token was minted for - shown by `plugin publish`. */
  login: string;
  registry: string;
  authorized_at: string;
}

export function registryBase(): string {
  return (process.env.UE_MCP_REGISTRY ?? "https://plugins.ue-mcp.com").replace(/\/+$/, "");
}

export async function readRegistryAuth(): Promise<RegistryAuth | null> {
  try {
    const raw = await fs.readFile(REGISTRY_FILE, "utf-8");
    const a = JSON.parse(raw) as RegistryAuth;
    // A cached token for a different registry (e.g. a local dev instance) must
    // not leak into prod publishes.
    if (a.registry && a.registry !== registryBase()) return null;
    return a.token ? a : null;
  } catch {
    return null;
  }
}

export async function writeRegistryAuth(a: RegistryAuth): Promise<void> {
  await fs.mkdir(AUTH_DIR, { recursive: true });
  await fs.writeFile(REGISTRY_FILE, JSON.stringify(a, null, 2), { mode: 0o600 });
}

export async function clearRegistryAuth(): Promise<void> {
  await fs.unlink(REGISTRY_FILE).catch(() => {});
}

/**
 * Resolve the token `plugin publish` should use, in precedence order:
 * explicit flag, env var, then the cached login. Returns null when the author
 * has never logged in.
 */
export async function resolvePublishToken(flagToken?: string): Promise<string | null> {
  if (flagToken) return flagToken;
  const env = process.env.UE_MCP_PUBLISH_TOKEN ?? process.env.REGISTRY_PUBLISH_TOKEN;
  if (env) return env;
  const cached = await readRegistryAuth();
  return cached?.token ?? null;
}

/** Trade a GitHub token for a registry publish token. */
async function exchange(githubToken: string): Promise<{ token: string; login: string }> {
  const base = registryBase();
  const res = await fetch(`${base}/api/cli/exchange`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ githubToken }),
  });
  const text = await res.text();
  if (!res.ok) {
    let msg = text;
    try {
      msg = (JSON.parse(text) as { error?: string }).error ?? text;
    } catch {
      // Non-JSON body (a proxy error page); surface it raw.
    }
    throw new Error(`registry exchange failed (HTTP ${res.status}): ${msg}`);
  }
  const data = JSON.parse(text) as { token?: string; login?: string };
  if (!data.token) throw new Error("registry did not return a token");
  return { token: data.token, login: data.login ?? "unknown" };
}

/**
 * Run the full login: reuse a cached GitHub token when present, otherwise walk
 * the device flow, then exchange for a registry token and cache it.
 *
 * `log` receives user-facing progress so callers own their own formatting.
 */
export async function loginToRegistry(
  log: (msg: string) => void,
  opts: { force?: boolean } = {},
): Promise<RegistryAuth> {
  if (!opts.force) {
    const existing = await readRegistryAuth();
    if (existing) {
      log(`Already logged in to ${existing.registry} as @${existing.login}.`);
      log(`Re-run with --force to mint a fresh token.`);
      return existing;
    }
  }

  // The feedback flow may already hold a usable GitHub token; reuse it so a
  // second browser round-trip is not forced on the author.
  let gh: UserAuth | null = await readUserAuth();
  if (gh) {
    log(`Using your existing GitHub authorization (@${gh.login}).`);
  } else {
    const pending = await startDeviceFlow();
    log("");
    log(`  Open:  ${pending.verification_uri}`);
    log(`  Code:  ${pending.user_code}`);
    log("");
    log("Waiting for GitHub authorization...");

    const deadline = pending.expires_at * 1000;
    for (;;) {
      if (Date.now() > deadline) throw new Error("device code expired; run 'ue-mcp login' again");
      await new Promise((r) => setTimeout(r, pending.interval * 1000));
      const result = await tryExchangeDeviceCode(pending);
      if (result.kind === "auth") {
        gh = result.auth;
        await writeUserAuth(gh);
        break;
      }
      if (result.kind === "denied") throw new Error("authorization denied on GitHub");
      if (result.kind === "expired") throw new Error("device code expired; run 'ue-mcp login' again");
      // pending: keep polling.
    }
    log(`Authorized as @${gh!.login}.`);
  }

  const { token, login } = await exchange(gh!.token);
  const auth: RegistryAuth = {
    token,
    login,
    registry: registryBase(),
    authorized_at: new Date().toISOString(),
  };
  await writeRegistryAuth(auth);
  return auth;
}
