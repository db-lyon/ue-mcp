#!/usr/bin/env node
/**
 * `npx ue-mcp auth` — interactive GitHub device-flow auth so feedback(submit)
 * can author issues as the user's real GitHub account.
 *
 * The same flow is offered inside `npx ue-mcp init` when the user opts into
 * the feedback prompt hook. This standalone entry point exists for people
 * who skipped that step at init time and want to authorize later without
 * re-running the full setup wizard.
 */

import { readUserAuth, startDeviceFlow, tryExchangeDeviceCode } from "./auth.js";
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

export async function runFeedbackAuthStep(): Promise<void> {
  console.log("");
  console.log(
    `  ${BOLD}${CYAN}Feedback authorship${RESET}  ${DIM}(GitHub OAuth)${RESET}`,
  );

  const cached = await readUserAuth();
  if (cached) {
    ok(`Feedback issues will author as @${cached.login} (cached token reused)`);
    return;
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

  const choice = await singleSelect("Authorize now?", [
    "Yes - run device flow now (recommended)",
    `Skip - feedback submissions will refuse until I run \`npx ue-mcp auth\` or call with author="bot"`,
  ]);
  if (choice !== 0) {
    info(`Skipped. Run npx ue-mcp auth to set this up later, or pass author="bot" at submit time.`);
    return;
  }

  let pending;
  try {
    pending = await startDeviceFlow();
  } catch (e) {
    warn(`Device flow start failed: ${e instanceof Error ? e.message : e}`);
    info(`Submissions will refuse until you run \`npx ue-mcp auth\` or call with author="bot".`);
    return;
  }

  console.log("");
  console.log(`  ${BOLD}1.${RESET} Open: ${CYAN}${pending.verification_uri}${RESET}`);
  console.log(`  ${BOLD}2.${RESET} Enter code: ${BOLD}${YELLOW}${pending.user_code}${RESET}`);
  console.log(`  ${BOLD}3.${RESET} Authorize the ue-mcp-feedback app`);
  console.log("");
  console.log(`  ${DIM}Polling every ${pending.interval}s. Code expires in ~15 min. Ctrl-C to skip.${RESET}`);

  const deadline = pending.expires_at * 1000;
  process.stdout.write("  ");
  while (Date.now() < deadline) {
    await new Promise((r) => setTimeout(r, pending.interval * 1000));
    let result;
    try {
      result = await tryExchangeDeviceCode(pending);
    } catch (e) {
      console.log("");
      warn(`Auth failed: ${e instanceof Error ? e.message : e}`);
      info(`Submissions will refuse until you re-run \`npx ue-mcp auth\` or call with author="bot".`);
      return;
    }
    if (result.kind === "auth") {
      console.log("");
      ok(`Authorized as @${result.auth.login}`);
      info(`Token cached at ~/.ue-mcp/auth.json (mode 600)`);
      return;
    }
    if (result.kind === "expired") {
      console.log("");
      warn("Device code expired. Re-run npx ue-mcp auth to retry.");
      return;
    }
    if (result.kind === "denied") {
      console.log("");
      warn(`Authorization denied. Submissions will refuse until you re-run auth or call with author="bot".`);
      return;
    }
    process.stdout.write(".");
  }
  console.log("");
  warn(`Timed out waiting for authorization. Submissions will refuse until you re-run auth or call with author="bot".`);
}

// Only run when invoked as a CLI subcommand (not when imported by init.ts).
if (import.meta.url === `file://${process.argv[1].replace(/\\/g, "/")}` ||
    process.argv[1].endsWith("auth-cli.js") ||
    process.argv[1].endsWith("auth-cli.ts")) {
  runFeedbackAuthStep().catch((e) => {
    console.error(
      `\n  ${RED}Fatal error: ${e instanceof Error ? e.message : e}${RESET}\n`,
    );
    process.exit(1);
  });
}
