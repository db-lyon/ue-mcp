#!/usr/bin/env node
/**
 * `ue-mcp login` / `ue-mcp logout` - sign this machine in to the UE-MCP account
 * (the plugin registry user) and cache its token at ~/.ue-mcp/registry.json.
 *
 * The account signs in with an email address and a one-time code, or with the
 * GitHub device flow. Studio and `ue-mcp plugin publish` read the cached token.
 */
import { createInterface, type Interface } from "node:readline";
import {
  clearRegistryAuth,
  displayAccount,
  fetchAuthMethods,
  loginToRegistry,
  loginWithEmail,
  readRegistryAuth,
  type AuthMethods,
  type RegistryAuth,
} from "../extensions/registry-auth.js";
import { registryBase } from "../extensions/registry-catalog.js";

function log(msg: string): void {
  console.log(msg ? `[ue-mcp] ${msg}` : "");
}

const HELP = `Usage: ue-mcp login [--email <address> | --github] [--force]
       ue-mcp logout

Signs this machine in to your UE-MCP account and caches the account token in
~/.ue-mcp/registry.json. Studio and \`ue-mcp plugin publish\` use it.

  --email <address>  Sign in with an email address. The registry emails a
                     6-digit code; type it when asked. No password.
  --github           Sign in with GitHub (device flow).
  --force            Sign in again and mint a fresh token, replacing the cached one.

With neither flag, an interactive terminal asks which to use (email is the
default when the registry offers it). Without a terminal, GitHub is used.

CI: set UE_MCP_PUBLISH_TOKEN instead (mint a token at the registry /account page).
Sign-in goes to https://accounts.ue-mcp.com; override it with UE_MCP_AUTH_URL.
Override the registry with UE_MCP_REGISTRY.`;

/** Reads answers line by line from stdin, buffered so piped input is not lost between prompts. */
class LineReader {
  private rl: Interface | null = null;
  private lines: AsyncIterator<string> | null = null;

  async ask(question: string): Promise<string | null> {
    if (!this.rl) {
      this.rl = createInterface({ input: process.stdin, terminal: false });
      this.lines = this.rl[Symbol.asyncIterator]();
    }
    process.stdout.write(question);
    const next = await this.lines!.next();
    if (!process.stdin.isTTY) process.stdout.write("\n");
    return next.done ? null : next.value;
  }

  close(): void {
    this.rl?.close();
  }
}

export interface LoginFlags {
  email?: string;
  /** --email given without an address. */
  emailWithoutAddress: boolean;
  github: boolean;
  force: boolean;
}

export function parseLoginFlags(args: string[]): LoginFlags {
  const flags: LoginFlags = { emailWithoutAddress: false, github: false, force: false };
  for (let i = 0; i < args.length; i++) {
    const a = args[i];
    if (a === "--force") flags.force = true;
    else if (a === "--github") flags.github = true;
    else if (a.startsWith("--email=")) {
      const v = a.slice("--email=".length);
      if (v) flags.email = v;
      else flags.emailWithoutAddress = true;
    } else if (a === "--email") {
      const v = args[i + 1];
      if (v && !v.startsWith("-")) {
        flags.email = v;
        i++;
      } else flags.emailWithoutAddress = true;
    }
  }
  return flags;
}

export type LoginChoice = { method: "github" } | { method: "email"; email: string };

/**
 * Pick the sign-in method. A flag decides; otherwise a terminal is asked
 * (email by default when the registry offers it), and without one, or on a
 * registry without email sign-in, it is GitHub so scripts never wait on input.
 */
export async function chooseLoginMethod(
  flags: LoginFlags,
  env: {
    isTTY: boolean;
    methods: () => Promise<AuthMethods | null>;
    ask: (question: string) => Promise<string | null>;
    log: (msg: string) => void;
  },
): Promise<LoginChoice> {
  const wantsEmail = flags.email !== undefined || flags.emailWithoutAddress;
  if (wantsEmail && flags.github) throw new Error("use --email or --github, not both");
  if (flags.github) return { method: "github" };

  const askEmail = async (): Promise<string> => {
    if (!env.isTTY) throw new Error("--email needs an address, as in: ue-mcp login --email you@example.com");
    const answer = await env.ask("Email address: ");
    const email = answer?.trim();
    if (!email) throw new Error("no email address entered");
    return email;
  };

  if (wantsEmail) return { method: "email", email: flags.email ?? (await askEmail()) };
  if (!env.isTTY) return { method: "github" };

  const methods = await env.methods();
  if (!methods?.email) return { method: "github" };
  for (;;) {
    const answer = await env.ask("Sign in with email or GitHub? [E/g]: ");
    if (answer === null) throw new Error("no sign-in method chosen");
    const a = answer.trim().toLowerCase();
    if (a === "" || a === "e" || a === "email") return { method: "email", email: await askEmail() };
    if (a === "g" || a === "github") return { method: "github" };
    env.log("Type e for email or g for GitHub.");
  }
}

async function login(args: string[]): Promise<void> {
  if (args.includes("--help") || args.includes("-h")) {
    console.log(HELP);
    return;
  }
  const flags = parseLoginFlags(args);
  const reader = new LineReader();
  const ask = (q: string) => reader.ask(q);
  try {
    if (!flags.force) {
      const existing = await readRegistryAuth();
      if (existing) {
        log(`Already logged in to ${existing.registry} as ${displayAccount(existing)}.`);
        log(`Re-run with --force to mint a fresh token.`);
        return;
      }
    }
    const choice = await chooseLoginMethod(flags, {
      isTTY: Boolean(process.stdin.isTTY && process.stdout.isTTY),
      methods: fetchAuthMethods,
      ask,
      log,
    });
    const auth: RegistryAuth =
      choice.method === "email"
        ? await loginWithEmail(log, { email: choice.email, ask, force: true })
        : await loginToRegistry(log, { force: true });
    log(`Logged in to ${auth.registry} as ${displayAccount(auth)}.`);
    log(`Publish with: ue-mcp plugin publish <dir>`);
  } catch (e) {
    console.error(`[ue-mcp] login failed: ${e instanceof Error ? e.message : e}`);
    process.exitCode = 1;
  } finally {
    reader.close();
  }
}

async function logout(args: string[]): Promise<void> {
  if (args.includes("--help") || args.includes("-h")) {
    console.log(HELP);
    return;
  }
  const existing = await readRegistryAuth();
  await clearRegistryAuth();
  if (existing) {
    log(`Logged out of ${existing.registry} (was ${displayAccount(existing)}).`);
    log(`The token remains valid until you revoke it at ${registryBase()}/account.`);
  } else {
    log("Not logged in.");
  }
}

/** Entry point for `ue-mcp login [--email <address> | --github] [--force]`. */
export async function runLogin(argv: string[]): Promise<number | void> {
  await login(argv);
  return process.exitCode === 1 ? 1 : undefined;
}

/** Entry point for `ue-mcp logout`. */
export async function runLogout(argv: string[]): Promise<number | void> {
  await logout(argv);
}
