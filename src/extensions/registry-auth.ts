/**
 * Registry login for `ue-mcp login`.
 *
 * The account is the registry user. It signs in with an email address and a
 * one-time code (POST /api/auth/email/start, then /verify), or with GitHub.
 * Sign-in calls go to authBase() (accounts.ue-mcp.com); the cached token's
 * `registry` stays registryBase(), since the token is valid on both hosts.
 *
 * Publishing to the plugin registry needs a per-author token. Rather than make
 * authors visit the site, mint a token, and paste it into an env var, this runs
 * the GitHub device flow already used for feedback authorship (src/feedback/github-auth.ts) and
 * trades the resulting GitHub identity for a registry token via
 * POST /api/cli/exchange.
 *
 * The registry token is cached at ~/.ue-mcp/registry.json (mode 0600) and read
 * automatically by `ue-mcp plugin publish`, so publishing needs no env var and
 * no shared secret. UE_MCP_PUBLISH_TOKEN and --token still win when set, so CI
 * can inject a token without logging in.
 */
import { promises as fs } from "node:fs";
import { join } from "node:path";
import {
  authDir,
  readUserAuth,
  startDeviceFlow,
  pollDeviceFlow,
  type UserAuth,
} from "../feedback/github-auth.js";
import { registryBase } from "./registry-catalog.js";
import { readEnv } from "../core/env.js";
import { writePrivateFile } from "../core/private-file.js";

const registryFile = () => join(authDir(), "registry.json");

export interface RegistryAuth {
  /** Registry publish token (uemcp_...). */
  token: string;
  /** Account login as the registry reports it: a GitHub login, or the email for an email-only account. */
  login: string;
  /** The account's email address, when the registry returned one. */
  email?: string;
  /** How this token was obtained. Absent in files written before email sign-in, which were all GitHub. */
  method?: "email" | "github";
  registry: string;
  authorized_at: string;
}


export async function readRegistryAuth(): Promise<RegistryAuth | null> {
  try {
    const raw = await fs.readFile(registryFile(), "utf-8");
    const a = JSON.parse(raw) as RegistryAuth;
    // A cached token for a different registry (e.g. a local dev instance) must
    // not leak into prod publishes.
    if (a.registry && a.registry !== registryBase()) return null;
    return a.token ? a : null;
  } catch {
    return null;
  }
}

/**
 * The account as shown to the user. Only a GitHub sign-in is a GitHub login, so
 * only that gets the "@" prefix; an email sign-in is shown as the registry gave it.
 */
export function displayAccount(a: Pick<RegistryAuth, "login" | "method">): string {
  return a.method === "email" ? a.login : `@${a.login}`;
}

async function writeRegistryAuth(a: RegistryAuth): Promise<void> {
  await writePrivateFile(registryFile(), JSON.stringify(a, null, 2));
}

export async function clearRegistryAuth(): Promise<void> {
  await fs.unlink(registryFile()).catch(() => {});
}

/**
 * Resolve the token `plugin publish` should use, in precedence order:
 * explicit flag, env var, then the cached login. Returns null when the author
 * has never logged in.
 */
export async function resolvePublishToken(flagToken?: string): Promise<string | null> {
  if (flagToken) return flagToken;
  const env = readEnv("publishToken") ?? process.env.REGISTRY_PUBLISH_TOKEN;
  if (env) return env;
  const cached = await readRegistryAuth();
  return cached?.token ?? null;
}

const DEFAULT_AUTH_BASE = "https://accounts.ue-mcp.com";

/** Origin of the sign-in endpoints: UE_MCP_AUTH_URL, else https://accounts.ue-mcp.com. */
export function authBase(): string {
  return (readEnv("authUrl") ?? DEFAULT_AUTH_BASE).replace(/\/+$/, "");
}

/**
 * Fetch a sign-in endpoint. With UE_MCP_AUTH_URL unset, a network failure on
 * the default host (no DNS record yet, connection refused) is retried once on
 * the registry, which serves the same routes and tokens. An HTTP status is an
 * answer and is never retried.
 */
async function authFetch(path: string, init?: RequestInit): Promise<Response> {
  try {
    return await fetch(`${authBase()}${path}`, init);
  } catch (e) {
    if (readEnv("authUrl")) throw e;
    return fetch(`${registryBase()}${path}`, init);
  }
}

/** Trade a GitHub token for a registry publish token. */
async function exchange(githubToken: string): Promise<{ token: string; login: string; email?: string }> {
  const res = await authFetch("/api/cli/exchange", {
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
  const data = JSON.parse(text) as { token?: string; login?: string; email?: string | null };
  if (!data.token) throw new Error("registry did not return a token");
  return { token: data.token, login: data.login ?? "unknown", ...(data.email ? { email: data.email } : {}) };
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
      log(`Already logged in to ${existing.registry} as ${displayAccount(existing)}.`);
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

    const result = await pollDeviceFlow(pending);
    if (result.kind === "denied") throw new Error("authorization denied on GitHub");
    if (result.kind !== "auth") throw new Error("device code expired; run 'ue-mcp login' again");
    gh = result.auth;
    log(`Authorized as @${gh.login}.`);
  }

  const { token, login, email } = await exchange(gh.token);
  const auth: RegistryAuth = {
    token,
    login,
    ...(email ? { email } : {}),
    method: "github",
    registry: registryBase(),
    authorized_at: new Date().toISOString(),
  };
  await writeRegistryAuth(auth);
  return auth;
}

/** The sign-in methods a registry offers. */
export interface AuthMethods {
  email: boolean;
  github: boolean;
}

/**
 * GET /api/auth/methods. Null when the registry does not answer it (an older
 * registry, or unreachable), which callers treat as GitHub only.
 */
export async function fetchAuthMethods(): Promise<AuthMethods | null> {
  try {
    const res = await authFetch("/api/auth/methods");
    if (!res.ok) return null;
    const data = (await res.json()) as { email?: unknown; github?: unknown };
    return { email: data.email === true, github: data.github !== false };
  } catch {
    return null;
  }
}

/** A refusal from the email sign-in endpoints, with the HTTP status and any wait. */
export class EmailSignInError extends Error {
  constructor(
    message: string,
    readonly status: number,
    /** Seconds to wait before asking again, from a 429. */
    readonly retryAfter?: number,
  ) {
    super(message);
    this.name = "EmailSignInError";
  }
}

async function postEmail<T>(path: string, body: Record<string, string>): Promise<T> {
  const res = await authFetch(path, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(body),
  });
  const text = await res.text();
  let data: Record<string, unknown> = {};
  try {
    data = JSON.parse(text) as Record<string, unknown>;
  } catch {
    // Non-JSON body (a proxy error page); the status still says what happened.
  }
  if (res.ok) return data as T;
  const error = typeof data.error === "string" ? data.error : text || `HTTP ${res.status}`;
  if (res.status === 429) {
    const fromBody = typeof data.retryAfter === "number" ? data.retryAfter : undefined;
    const header = Number(res.headers.get("retry-after"));
    const wait = fromBody ?? (Number.isFinite(header) && header > 0 ? header : undefined);
    throw new EmailSignInError(error, 429, wait);
  }
  if (res.status === 404 || res.status === 405) {
    throw new EmailSignInError("this registry does not offer email sign-in; run 'ue-mcp login --github'", res.status);
  }
  throw new EmailSignInError(error, res.status);
}

/** POST /api/auth/email/start: ask the registry to email a one-time code. */
export async function startEmailSignIn(email: string): Promise<{ expiresInSeconds: number }> {
  const data = await postEmail<{ expiresInSeconds?: number }>("/api/auth/email/start", { email });
  return { expiresInSeconds: typeof data.expiresInSeconds === "number" ? data.expiresInSeconds : 600 };
}

/** POST /api/auth/email/verify: trade the code for a registry token. */
export async function verifyEmailCode(
  email: string,
  code: string,
): Promise<{ token: string; login: string; email: string; created: boolean }> {
  const data = await postEmail<{ token?: string; login?: string; email?: string; created?: boolean }>(
    "/api/auth/email/verify",
    { email, code },
  );
  if (!data.token) throw new EmailSignInError("registry did not return a token", 200);
  return { token: data.token, login: data.login ?? email, email: data.email ?? email, created: data.created === true };
}

/** Wrong tries the registry allows on one code before it stops accepting it. */
export const EMAIL_CODE_TRIES = 5;

/** A user-facing message for an email sign-in refusal. */
export function describeEmailError(e: EmailSignInError): string {
  if (e.status === 429) {
    return e.retryAfter
      ? `too many requests; try again in ${e.retryAfter} seconds`
      : "too many requests; try again later";
  }
  if (e.status === 503) return `email sign-in is not available right now (${e.message}); run 'ue-mcp login --github' instead`;
  return e.message;
}

/** True for something shaped like an email address. The registry has the final say. */
export function looksLikeEmail(s: string): boolean {
  return /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(s.trim());
}

/**
 * Email sign-in: send a code, read it with `ask`, verify, and cache the token.
 *
 * `ask` returns the next line the user typed, or null at end of input. A wrong
 * or expired code may be retried up to the registry's limit, and typing "r"
 * sends a new code.
 */
export async function loginWithEmail(
  log: (msg: string) => void,
  opts: { email: string; ask: (question: string) => Promise<string | null>; force?: boolean },
): Promise<RegistryAuth> {
  if (!opts.force) {
    const existing = await readRegistryAuth();
    if (existing) {
      log(`Already logged in to ${existing.registry} as ${displayAccount(existing)}.`);
      log(`Re-run with --force to mint a fresh token.`);
      return existing;
    }
  }
  const email = opts.email.trim();
  if (!looksLikeEmail(email)) throw new Error(`not an email address: ${email}`);

  const send = async (): Promise<void> => {
    try {
      const { expiresInSeconds } = await startEmailSignIn(email);
      const minutes = Math.max(1, Math.round(expiresInSeconds / 60));
      log(`Sent a sign-in code to ${email}. It is valid for ${minutes} minutes.`);
    } catch (e) {
      throw e instanceof EmailSignInError ? new Error(describeEmailError(e)) : e;
    }
  };

  await send();
  let wrong = 0;
  for (;;) {
    const exhausted = wrong >= EMAIL_CODE_TRIES;
    const answer = await opts.ask(
      exhausted
        ? "That code can no longer be used. Type r to send a new one, or press Enter to stop: "
        : "Enter the 6-digit code (or r to send a new one): ",
    );
    if (answer === null) throw new Error("no code entered");
    const typed = answer.trim();
    if (typed.toLowerCase() === "r") {
      await send();
      wrong = 0;
      continue;
    }
    if (exhausted) throw new Error("sign-in stopped; run 'ue-mcp login' again");
    if (typed === "") continue;
    const code = typed.replace(/\s+/g, "");
    if (!/^\d{6}$/.test(code)) {
      log("The code is 6 digits.");
      continue;
    }
    try {
      const v = await verifyEmailCode(email, code);
      const auth: RegistryAuth = {
        token: v.token,
        login: v.login,
        email: v.email,
        method: "email",
        registry: registryBase(),
        authorized_at: new Date().toISOString(),
      };
      await writeRegistryAuth(auth);
      if (v.created) log(`Created a UE-MCP account for ${v.email}.`);
      return auth;
    } catch (e) {
      if (!(e instanceof EmailSignInError) || e.status !== 400) {
        throw e instanceof EmailSignInError ? new Error(describeEmailError(e)) : e;
      }
      wrong++;
      log(e.message);
    }
  }
}
