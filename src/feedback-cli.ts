#!/usr/bin/env node
/**
 * `npx ue-mcp feedback <list|show|approve|discard|review>` — review and act on
 * submissions that ue-mcp deferred to disk while running in
 * `feedback.mode = "defer"`. Pairs with src/feedback-deferred.ts.
 *
 * Argv layout after index.ts splices "feedback" out:
 *   argv[2] = subcommand (list, show, approve, discard, review)
 *   argv[3] = id (for show/approve/discard)
 */

import * as readline from "node:readline";
import {
  deleteDeferred,
  getPendingDir,
  listDeferred,
  loadDeferred,
  type DeferredFeedback,
} from "./feedback-deferred.js";
import { submitFeedback } from "./github-app.js";
import {
  getFeedbackMode,
  setFeedbackMode,
  getUserStatePath,
  type FeedbackMode,
} from "./user-state.js";
import {
  BOLD,
  CYAN,
  DIM,
  GREEN,
  RED,
  RESET,
  YELLOW,
  fail,
  info,
  ok,
  warn,
} from "./ui/ansi.js";

function printHelp(): void {
  console.log("");
  console.log(`  ${BOLD}${CYAN}ue-mcp feedback${RESET}`);
  console.log("");
  console.log(`  Review submissions deferred while \`feedback.mode = "defer"\` was active.`);
  console.log(`  Pending submissions are stored in: ${DIM}${getPendingDir()}${RESET}`);
  console.log("");
  console.log(`  ${BOLD}mode${RESET}              Print the current feedback approval mode`);
  console.log(`  ${BOLD}mode${RESET} <mode>       Set the mode (interactive | auto-approve | defer)`);
  console.log(`  ${BOLD}list${RESET}              List every pending submission (id, project, title)`);
  console.log(`  ${BOLD}show${RESET} <id>         Print the full body and metadata for one entry`);
  console.log(`  ${BOLD}approve${RESET} <id>      POST the entry to GitHub, then delete it locally`);
  console.log(`  ${BOLD}discard${RESET} <id>      Delete the entry without posting`);
  console.log(`  ${BOLD}review${RESET}            Walk the queue one entry at a time, approve/discard/skip per item ${DIM}(experimental)${RESET}`);
  console.log("");
}

function cmdMode(arg: string | undefined): void {
  if (arg === undefined) {
    const env = (process.env.UE_MCP_FEEDBACK_MODE ?? "").trim().toLowerCase();
    const pref = getFeedbackMode();
    const effective: FeedbackMode =
      env === "interactive" || env === "auto-approve" || env === "defer"
        ? env
        : pref ?? "interactive";

    console.log("");
    console.log(`  ${BOLD}effective:${RESET} ${effective}`);
    console.log(`  ${DIM}preference (~/.ue-mcp/state.json): ${pref ?? "(not set; default interactive)"}${RESET}`);
    if (env === "interactive" || env === "auto-approve" || env === "defer") {
      console.log(`  ${DIM}UE_MCP_FEEDBACK_MODE env override: ${env}${RESET}`);
    }
    console.log("");
    console.log(`  ${DIM}Set with: npx ue-mcp feedback mode <interactive|auto-approve|defer>${RESET}`);
    console.log(`  ${DIM}Clear preference: npx ue-mcp feedback mode default${RESET}`);
    console.log("");
    return;
  }

  if (arg === "default" || arg === "clear" || arg === "unset") {
    setFeedbackMode(undefined);
    ok(`Cleared mode preference. Effective mode will default to "interactive" until set again or overridden by UE_MCP_FEEDBACK_MODE.`);
    info(getUserStatePath());
    return;
  }

  if (arg !== "interactive" && arg !== "auto-approve" && arg !== "defer") {
    fail(`Unknown mode "${arg}". Allowed: interactive, auto-approve, defer, default.`);
    process.exit(1);
  }
  setFeedbackMode(arg);
  ok(`Feedback mode set to "${arg}" (per-user, stored in ~/.ue-mcp/state.json).`);
  if (process.env.UE_MCP_FEEDBACK_MODE) {
    warn(`UE_MCP_FEEDBACK_MODE=${process.env.UE_MCP_FEEDBACK_MODE} is set in your env and will override this preference for processes started from that shell.`);
  }
}

function cmdList(): void {
  const entries = listDeferred();
  if (entries.length === 0) {
    info("No pending feedback submissions.");
    info(getPendingDir());
    return;
  }
  console.log("");
  console.log(`  ${BOLD}${entries.length} pending feedback submission(s)${RESET}`);
  console.log("");
  const idWidth = Math.max(...entries.map((e) => e.id.length));
  const projWidth = Math.max(
    "project".length,
    ...entries.map((e) => (e.project ?? "(none)").length),
  );
  console.log(
    `  ${DIM}${"id".padEnd(idWidth)}  ${"project".padEnd(projWidth)}  ${"author".padEnd(4)}  title${RESET}`,
  );
  for (const e of entries) {
    const projLabel = (e.project ?? "(none)").padEnd(projWidth);
    const titlePreview = e.title.length > 70 ? `${e.title.slice(0, 67)}...` : e.title;
    console.log(`  ${e.id.padEnd(idWidth)}  ${projLabel}  ${e.author.padEnd(4)}  ${titlePreview}`);
  }
  console.log("");
  console.log(`  ${DIM}Approve with: npx ue-mcp feedback approve <id>${RESET}`);
  console.log(`  ${DIM}Discard with: npx ue-mcp feedback discard <id>${RESET}`);
  console.log("");
}

function renderEntry(entry: DeferredFeedback, header?: string): void {
  console.log("");
  if (header) console.log(`  ${header}`);
  console.log(`  ${BOLD}id      ${RESET}${entry.id}`);
  console.log(`  ${BOLD}created ${RESET}${entry.createdAt}`);
  console.log(`  ${BOLD}project ${RESET}${entry.project ?? "(none)"}`);
  console.log(`  ${BOLD}author  ${RESET}${entry.author}`);
  console.log(`  ${BOLD}labels  ${RESET}${entry.labels.join(", ") || "(none)"}`);
  console.log(`  ${BOLD}title   ${RESET}${entry.title}`);
  console.log("");
  console.log(`  ${BOLD}── BODY ─────────────────────────────────────${RESET}`);
  console.log(entry.body);
  console.log(`  ${BOLD}── END BODY ─────────────────────────────────${RESET}`);
  console.log("");
}

function cmdShow(id: string): void {
  const entry = loadDeferred(id);
  if (!entry) {
    fail(`No pending feedback with id "${id}".`);
    process.exit(1);
  }
  renderEntry(entry);
}

/** Outcome an interactive caller needs to distinguish from a bare success. */
type ApproveOutcome =
  | { kind: "posted"; number: number; url: string }
  | { kind: "auth_required"; verification_uri: string; user_code: string }
  | { kind: "error"; message: string };

async function approveEntry(entry: DeferredFeedback): Promise<ApproveOutcome> {
  let result: Awaited<ReturnType<typeof submitFeedback>>;
  try {
    result = await submitFeedback(entry.title, entry.body, entry.labels, {
      useBot: entry.author === "bot",
    });
  } catch (e) {
    return { kind: "error", message: e instanceof Error ? e.message : String(e) };
  }
  if (result.kind === "auth_required") {
    return { kind: "auth_required", verification_uri: result.verification_uri, user_code: result.user_code };
  }
  deleteDeferred(entry.id);
  return { kind: "posted", number: result.number, url: result.url };
}

async function cmdApprove(id: string): Promise<void> {
  const entry = loadDeferred(id);
  if (!entry) {
    fail(`No pending feedback with id "${id}".`);
    process.exit(1);
  }
  console.log("");
  info(`Posting ${id} to GitHub as ${entry.author === "bot" ? "ue-mcp-feedback bot" : "your GitHub user"}...`);
  const outcome = await approveEntry(entry);
  if (outcome.kind === "error") {
    fail(`GitHub POST failed: ${outcome.message}`);
    info(`Entry left on disk; retry with: npx ue-mcp feedback approve ${id}`);
    process.exit(1);
  }
  if (outcome.kind === "auth_required") {
    warn("GitHub auth required to post as user.");
    console.log(`  ${BOLD}1.${RESET} Open: ${CYAN}${outcome.verification_uri}${RESET}`);
    console.log(`  ${BOLD}2.${RESET} Enter code: ${BOLD}${YELLOW}${outcome.user_code}${RESET}`);
    console.log(`  ${BOLD}3.${RESET} Authorize, then re-run: npx ue-mcp feedback approve ${id}`);
    info(`Or discard with: npx ue-mcp feedback discard ${id}`);
    process.exit(1);
  }
  ok(`Posted as issue #${outcome.number} (${outcome.url})`);
  info(`Removed local entry ${id}.`);
  console.log("");
}

async function cmdReview(): Promise<void> {
  const entries = listDeferred();
  if (entries.length === 0) {
    info("No pending feedback submissions.");
    info(getPendingDir());
    return;
  }
  if (!process.stdin.isTTY) {
    fail("`feedback review` requires an interactive terminal. Use list/show/approve/discard for non-interactive workflows.");
    process.exit(1);
  }

  console.log("");
  console.log(`  ${BOLD}Reviewing ${entries.length} pending submission(s).${RESET}`);
  console.log(`  ${DIM}Choices per item: [a]pprove  [d]iscard  [s]kip  [q]uit${RESET}`);

  const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
  const ask = (prompt: string): Promise<string> =>
    new Promise((resolve) => rl.question(prompt, (a) => resolve(a.trim().toLowerCase())));

  let approved = 0;
  let discarded = 0;
  let skipped = 0;
  let quit = false;

  try {
    for (let i = 0; i < entries.length; i++) {
      const entry = entries[i];
      renderEntry(entry, `${BOLD}[${i + 1}/${entries.length}]${RESET}`);

      let action: "a" | "d" | "s" | "q" | null = null;
      while (action === null) {
        const answer = await ask(`  ${BOLD}?${RESET} [a]pprove  [d]iscard  [s]kip  [q]uit > `);
        if (answer === "a" || answer === "approve") action = "a";
        else if (answer === "d" || answer === "discard") action = "d";
        else if (answer === "s" || answer === "skip" || answer === "") action = "s";
        else if (answer === "q" || answer === "quit" || answer === "exit") action = "q";
        else warn(`Unknown choice "${answer}". Type a, d, s, or q.`);
      }

      if (action === "q") { quit = true; break; }

      if (action === "s") {
        info(`Skipped (left on disk).`);
        skipped++;
        continue;
      }

      if (action === "d") {
        if (deleteDeferred(entry.id)) {
          ok(`Discarded ${entry.id}.`);
          discarded++;
        } else {
          fail(`Failed to delete ${entry.id}.`);
        }
        continue;
      }

      info(`Posting ${entry.id} to GitHub as ${entry.author === "bot" ? "ue-mcp-feedback bot" : "your GitHub user"}...`);
      const outcome = await approveEntry(entry);
      if (outcome.kind === "posted") {
        ok(`Posted as issue #${outcome.number} (${outcome.url})`);
        approved++;
        continue;
      }
      if (outcome.kind === "auth_required") {
        warn("GitHub auth required to post as user.");
        console.log(`  ${BOLD}1.${RESET} Open: ${CYAN}${outcome.verification_uri}${RESET}`);
        console.log(`  ${BOLD}2.${RESET} Enter code: ${BOLD}${YELLOW}${outcome.user_code}${RESET}`);
        console.log(`  ${BOLD}3.${RESET} Authorize, then re-run: npx ue-mcp feedback review`);
        info(`This entry and any remaining are left on disk. Stopping.`);
        quit = true;
        break;
      }
      // outcome.kind === "error"
      fail(`GitHub POST failed: ${outcome.message}`);
      info(`Entry left on disk. Stopping the review loop so you can investigate; re-run to resume.`);
      quit = true;
      break;
    }
  } finally {
    rl.close();
  }

  const leftover = listDeferred().length;
  console.log("");
  console.log(
    `  ${BOLD}Done.${RESET}  approved: ${GREEN}${approved}${RESET}  discarded: ${YELLOW}${discarded}${RESET}  skipped: ${skipped}  remaining: ${leftover}`,
  );
  if (quit && leftover > 0) {
    info(`Re-run with: npx ue-mcp feedback review`);
  }
  console.log("");
}

function cmdDiscard(id: string): void {
  const entry = loadDeferred(id);
  if (!entry) {
    fail(`No pending feedback with id "${id}".`);
    process.exit(1);
  }
  if (deleteDeferred(id)) {
    ok(`Discarded ${id} (was: "${entry.title}").`);
  } else {
    fail(`Failed to delete ${id}.`);
    process.exit(1);
  }
}

async function main(): Promise<void> {
  const sub = process.argv[2];
  const id = process.argv[3];

  switch (sub) {
    case "mode":
      cmdMode(id);
      return;
    case "list":
      cmdList();
      return;
    case "show":
      if (!id) { fail("Usage: npx ue-mcp feedback show <id>"); process.exit(1); }
      cmdShow(id);
      return;
    case "approve":
      if (!id) { fail("Usage: npx ue-mcp feedback approve <id>"); process.exit(1); }
      await cmdApprove(id);
      return;
    case "discard":
      if (!id) { fail("Usage: npx ue-mcp feedback discard <id>"); process.exit(1); }
      cmdDiscard(id);
      return;
    case "review":
      await cmdReview();
      return;
    case undefined:
    case "help":
    case "--help":
    case "-h":
      printHelp();
      return;
    default:
      fail(`Unknown subcommand: feedback ${sub}`);
      printHelp();
      process.exit(1);
  }
}

main().catch((e) => {
  console.error(
    `\n  ${RED}Fatal error: ${e instanceof Error ? e.message : e}${RESET}\n`,
  );
  process.exit(1);
});
