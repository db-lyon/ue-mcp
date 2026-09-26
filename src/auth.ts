import { promises as fs } from "node:fs";
import { join } from "node:path";
import { userDir } from "./core/user-dir.js";
import { readEnv } from "./core/env.js";

// OAuth Client ID for the ue-mcp-feedback GitHub App. This is NOT a secret -
// device flow client IDs are designed to be public. Override at runtime via
// UE_MCP_OAUTH_CLIENT_ID for testing or for users running a fork.
const DEFAULT_CLIENT_ID = "Iv23li9lpE9A0FqmXlJH";
/** Read per call, like every other variable, so an override is seen when it is set. */
function clientId(): string {
  return readEnv("oauthClientId") || DEFAULT_CLIENT_ID;
}

/** Where the GitHub and registry tokens are kept: UE_MCP_AUTH_DIR, else ~/.ue-mcp. */
export function authDir(): string {
  return readEnv("authDir") || userDir();
}
const authFile = () => join(authDir(), "auth.json");
const pendingFile = () => join(authDir(), "device-pending.json");

export interface UserAuth {
  token: string;
  login: string;
  authorized_at: string;
}

export interface PendingDeviceFlow {
  device_code: string;
  user_code: string;
  verification_uri: string;
  expires_at: number;
  interval: number;
}

export type AuthState =
  | { kind: "auth"; auth: UserAuth }
  | { kind: "pending"; pending: PendingDeviceFlow };


export async function readUserAuth(): Promise<UserAuth | null> {
  try {
    const raw = await fs.readFile(authFile(), "utf-8");
    return JSON.parse(raw) as UserAuth;
  } catch {
    return null;
  }
}

export async function writeUserAuth(auth: UserAuth): Promise<void> {
  await fs.mkdir(authDir(), { recursive: true });
  await fs.writeFile(authFile(), JSON.stringify(auth, null, 2), { mode: 0o600 });
}

export async function clearUserAuth(): Promise<void> {
  await fs.unlink(authFile()).catch(() => {});
}

async function readPending(): Promise<PendingDeviceFlow | null> {
  try {
    const raw = await fs.readFile(pendingFile(), "utf-8");
    const p = JSON.parse(raw) as PendingDeviceFlow;
    if (p.expires_at < Math.floor(Date.now() / 1000)) {
      await fs.unlink(pendingFile()).catch(() => {});
      return null;
    }
    return p;
  } catch {
    return null;
  }
}

async function writePending(p: PendingDeviceFlow): Promise<void> {
  await fs.mkdir(authDir(), { recursive: true });
  await fs.writeFile(pendingFile(), JSON.stringify(p, null, 2), { mode: 0o600 });
}

async function clearPending(): Promise<void> {
  await fs.unlink(pendingFile()).catch(() => {});
}

export async function startDeviceFlow(): Promise<PendingDeviceFlow> {
  const res = await fetch("https://github.com/login/device/code", {
    method: "POST",
    headers: {
      Accept: "application/json",
      "Content-Type": "application/json",
      "User-Agent": "ue-mcp",
    },
    body: JSON.stringify({ client_id: clientId() }),
  });
  if (!res.ok) {
    throw new Error(`Device code request failed: ${res.status} ${await res.text()}`);
  }
  const data = (await res.json()) as {
    device_code: string;
    user_code: string;
    verification_uri: string;
    expires_in: number;
    interval: number;
  };
  const pending: PendingDeviceFlow = {
    device_code: data.device_code,
    user_code: data.user_code,
    verification_uri: data.verification_uri,
    expires_at: Math.floor(Date.now() / 1000) + data.expires_in,
    interval: Math.max(data.interval, 5),
  };
  await writePending(pending);
  return pending;
}

export type ExchangeResult =
  | { kind: "auth"; auth: UserAuth }
  | { kind: "pending" }
  | { kind: "expired" }
  | { kind: "denied" };

export async function tryExchangeDeviceCode(
  pending: PendingDeviceFlow,
): Promise<ExchangeResult> {
  const res = await fetch("https://github.com/login/oauth/access_token", {
    method: "POST",
    headers: {
      Accept: "application/json",
      "Content-Type": "application/json",
      "User-Agent": "ue-mcp",
    },
    body: JSON.stringify({
      client_id: clientId(),
      device_code: pending.device_code,
      grant_type: "urn:ietf:params:oauth:grant-type:device_code",
    }),
  });
  const data = (await res.json()) as {
    access_token?: string;
    token_type?: string;
    error?: string;
  };
  if (data.error === "authorization_pending" || data.error === "slow_down") {
    return { kind: "pending" };
  }
  if (data.error === "expired_token") {
    await clearPending();
    return { kind: "expired" };
  }
  if (data.error === "access_denied") {
    await clearPending();
    return { kind: "denied" };
  }
  if (!data.access_token) {
    throw new Error(`Device code exchange failed: ${data.error || "unknown"}`);
  }

  const userRes = await fetch("https://api.github.com/user", {
    headers: {
      Authorization: `token ${data.access_token}`,
      Accept: "application/vnd.github+json",
      "User-Agent": "ue-mcp",
    },
  });
  if (!userRes.ok) {
    throw new Error(`Failed to fetch user info: ${userRes.status}`);
  }
  const user = (await userRes.json()) as { login: string };

  const auth: UserAuth = {
    token: data.access_token,
    login: user.login,
    authorized_at: new Date().toISOString(),
  };
  await writeUserAuth(auth);
  await clearPending();
  return { kind: "auth", auth };
}

/** How a polled device flow ended. `timeout` means the code outlived its expiry unanswered. */
export type DeviceFlowOutcome = Exclude<ExchangeResult, { kind: "pending" }> | { kind: "timeout" };

/**
 * Poll GitHub at the flow's interval until the code is authorized, denied or
 * expired. An authorization is already cached by tryExchangeDeviceCode.
 * `onPending` runs after each poll that is still waiting.
 */
export async function pollDeviceFlow(
  pending: PendingDeviceFlow,
  onPending?: () => void,
): Promise<DeviceFlowOutcome> {
  const deadline = pending.expires_at * 1000;
  while (Date.now() < deadline) {
    await new Promise((r) => setTimeout(r, pending.interval * 1000));
    const result = await tryExchangeDeviceCode(pending);
    if (result.kind !== "pending") return result;
    onPending?.();
  }
  return { kind: "timeout" };
}

/** Resolve user auth, initiating or resuming device flow as needed.
 *  Returns either a usable auth or a pending state for the caller to surface. */
export async function resolveUserAuth(): Promise<AuthState> {
  const existing = await readUserAuth();
  if (existing) return { kind: "auth", auth: existing };

  const pending = await readPending();
  if (pending) {
    const result = await tryExchangeDeviceCode(pending);
    if (result.kind === "auth") return { kind: "auth", auth: result.auth };
    if (result.kind === "pending") return { kind: "pending", pending };
    if (result.kind === "denied") {
      // User explicitly declined - start a fresh flow next call so they get
      // another chance, but surface the denial first.
      throw new Error("User denied authorization. Re-run feedback.submit to try again, or pass useBot=true.");
    }
    // expired: fall through to fresh start
  }

  const fresh = await startDeviceFlow();
  return { kind: "pending", pending: fresh };
}
