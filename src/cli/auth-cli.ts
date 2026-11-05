#!/usr/bin/env node
/**
 * `npx ue-mcp auth` - interactive GitHub device-flow auth so feedback(submit)
 * can author issues as the user's real GitHub account.
 *
 * The same flow is offered inside `npx ue-mcp init` when the user opts into
 * the feedback prompt hook. This standalone entry point exists for people
 * who skipped that step at init time and want to authorize later without
 * re-running the full setup wizard.
 */

import { authFile, pollDeviceFlow, readUserAuth, startDeviceFlow } from "../feedback/github-auth.js";
import {
  BOLD,
  CYAN,
  DIM,
  RED,
  RESET,
  YELLOW,
  info,
  ok,
  warn,
} from "./ui/ansi.js";
import { singleSelect } from "./ui/select.js";
import { isMainModule, type CliUsage } from "./cli-main.js";

/**
 * Sign in, or reuse a cached sign-in. True when a token is cached afterwards.
 * `ask` offers a skip first; `ue-mcp auth` is itself the answer, so it does not.
 */
export async function runFeedbackAuthStep(opts: { ask?: boolean } = {}): Promise<boolean> {
  console.log("");
  console.log(
    `  ${BOLD}${CYAN}Feedback authorship${RESET}  ${DIM}(GitHub OAuth)${RESET}`,
  );

  const cached = await readUserAuth();
  if (cached) {
    ok(`Feedback issues will author as @${cached.login} (cached token reused)`);
    return true;
  }

  console.log(
    `  ${DIM}feedback(submit) defaults to authoring issues as your GitHub user.${RESET}`,
  );
  console.log(
    `  ${DIM}Without a cached OAuth token, every submission will refuse until${RESET}`,
  );
  console.log(
    `  ${DIM}you either authorize here, or call feedback(submit) with author="bot"${RESET}`,
  );
  console.log(
    `  ${DIM}to post anonymously as the ue-mcp-feedback bot.${RESET}`,
  );
  console.log("");

  const choice = opts.ask === false
    ? 0
    : await singleSelect("Authorize now?", [
        "Yes - run device flow now (recommended)",
        `Skip - feedback submissions will refuse until I run \`npx ue-mcp auth\` or call with author="bot"`,
      ], "Run ue-mcp auth, which signs in without asking, or init --yes, which skips the sign-in.");
  if (choice !== 0) {
    info(`Skipped. Run npx ue-mcp auth to set this up later, or pass author="bot" at submit time.`);
    return false;
  }

  let pending;
  try {
    pending = await startDeviceFlow();
  } catch (e) {
    warn(`Device flow start failed: ${e instanceof Error ? e.message : e}`);
    info(`Submissions will refuse until you run \`npx ue-mcp auth\` or call with author="bot".`);
    return false;
  }

  console.log("");
  console.log(`  ${BOLD}1.${RESET} Open: ${CYAN}${pending.verification_uri}${RESET}`);
  console.log(`  ${BOLD}2.${RESET} Enter code: ${BOLD}${YELLOW}${pending.user_code}${RESET}`);
  console.log(`  ${BOLD}3.${RESET} Authorize the ue-mcp-feedback app`);
  console.log("");
  console.log(`  ${DIM}Polling every ${pending.interval}s. Code expires in ~15 min. Ctrl-C to skip.${RESET}`);

  process.stdout.write("  ");
  let result;
  try {
    result = await pollDeviceFlow(pending, () => process.stdout.write("."));
  } catch (e) {
    console.log("");
    warn(`Auth failed: ${e instanceof Error ? e.message : e}`);
    info(`Submissions will refuse until you re-run \`npx ue-mcp auth\` or call with author="bot".`);
    return false;
  }
  console.log("");
  if (result.kind === "auth") {
    ok(`Authorized as @${result.auth.login}`);
    info(`Token cached at ${authFile()} (mode 600)`);
    return true;
  }
  if (result.kind === "expired") {
    warn("Device code expired. Re-run npx ue-mcp auth to retry.");
  } else if (result.kind === "denied") {
    warn(`Authorization denied. Submissions will refuse until you re-run auth or call with author="bot".`);
  } else {
    warn(`Timed out waiting for authorization. Submissions will refuse until you re-run auth or call with author="bot".`);
  }
  return false;
}

/** One line of `ue-mcp auth --json`. */
export type AuthEvent =
  | { event: "code"; verificationUri: string; userCode: string; expiresAt: number; interval: number }
  | { event: "signed-in"; login: string; cached: boolean }
  | { event: "failed"; reason: "denied" | "expired" | "timeout" | "error"; message?: string };

/**
 * The device flow for a caller with no terminal, such as an editor's setup page:
 * one JSON object per line, the code to enter first, then how it ended.
 */
export async function runFeedbackAuthJson(emit: (e: AuthEvent) => void): Promise<boolean> {
  const cached = await readUserAuth();
  if (cached) {
    emit({ event: "signed-in", login: cached.login, cached: true });
    return true;
  }
  const failed = (e: unknown): false => {
    emit({ event: "failed", reason: "error", message: e instanceof Error ? e.message : String(e) });
    return false;
  };
  let pending;
  try {
    pending = await startDeviceFlow();
  } catch (e) {
    return failed(e);
  }
  emit({ event: "code", verificationUri: pending.verification_uri, userCode: pending.user_code, expiresAt: pending.expires_at, interval: pending.interval });
  let result;
  try {
    result = await pollDeviceFlow(pending);
  } catch (e) {
    return failed(e);
  }
  if (result.kind === "auth") {
    emit({ event: "signed-in", login: result.auth.login, cached: false });
    return true;
  }
  emit({ event: "failed", reason: result.kind });
  return false;
}

export const usage: CliUsage = {
  usage: "ue-mcp auth [--json]",
  summary: "Sign in to GitHub so feedback issues author as your account.",
  flags: [
    { name: "--json", description: "Print one JSON object per line: the code to enter, then the outcome" },
  ],
};

/** Entry point for `ue-mcp auth`. init.ts runs the same step inline. */
export async function run(argv: string[] = []): Promise<number | void> {
  const authorized = argv.includes("--json")
    ? await runFeedbackAuthJson((e) => process.stdout.write(JSON.stringify(e) + "\n"))
    : await runFeedbackAuthStep({ ask: false });
  return authorized ? 0 : 1;
}

// Also runnable as `node dist/cli/auth-cli.js`, only when executed directly.
if (isMainModule(import.meta.url)) {
  runFeedbackAuthStep({ ask: false }).then((authorized) => {
    if (!authorized) process.exitCode = 1;
  }, (e) => {
    console.error(
      `\n  ${RED}Fatal error: ${e instanceof Error ? e.message : e}${RESET}\n`,
    );
    process.exit(1);
  });
}
