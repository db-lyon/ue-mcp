#!/usr/bin/env node
/**
 * `npx ue-mcp feedback <list|show|approve|discard>` — review and act on
 * submissions that ue-mcp deferred to disk while running in
 * `feedback.mode = "defer"`. Pairs with src/feedback-deferred.ts.
 *
 * Argv layout after index.ts splices "feedback" out:
 *   argv[2] = subcommand (list, show, approve, discard)
 *   argv[3] = id (for show/approve/discard)
 */

import {
  deleteDeferred,
  getPendingDir,
  listDeferred,
  loadDeferred,
  type DeferredFeedback,
} from "./feedback-deferred.js";
import { submitFeedback } from "./github-app.js";
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
  console.log(`  ${BOLD}list${RESET}              List every pending submission (id, project, title)`);
  console.log(`  ${BOLD}show${RESET} <id>         Print the full body and metadata for one entry`);
  console.log(`  ${BOLD}approve${RESET} <id>      POST the entry to GitHub, then delete it locally`);
  console.log(`  ${BOLD}discard${RESET} <id>      Delete the entry without posting`);
  console.log("");
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

function cmdShow(id: string): void {
  const entry = loadDeferred(id);
  if (!entry) {
    fail(`No pending feedback with id "${id}".`);
    process.exit(1);
  }
  console.log("");
  console.log(`  ${BOLD}id      ${RESET}${entry.id}`);
  console.log(`  ${BOLD}created ${RESET}${entry.createdAt}`);
  console.log(`  ${BOLD}project ${RESET}${entry.project ?? "(none)"}`);
  console.log(`  ${BOLD}author  ${RESET}${entry.author}`);
  console.log(`  ${BOLD}labels  ${RESET}${entry.labels.join(", ")}`);
  console.log(`  ${BOLD}title   ${RESET}${entry.title}`);
  console.log("");
  console.log(`  ${BOLD}── BODY ─────────────────────────────────────${RESET}`);
  console.log(entry.body);
  console.log(`  ${BOLD}── END BODY ─────────────────────────────────${RESET}`);
  console.log("");
}

async function cmdApprove(id: string): Promise<void> {
  const entry = loadDeferred(id);
  if (!entry) {
    fail(`No pending feedback with id "${id}".`);
    process.exit(1);
  }
  console.log("");
  info(`Posting ${id} to GitHub as ${entry.author === "bot" ? "ue-mcp-feedback bot" : "your GitHub user"}...`);
  let result: Awaited<ReturnType<typeof submitFeedback>>;
  try {
    result = await submitFeedback(entry.title, entry.body, entry.labels, {
      useBot: entry.author === "bot",
    });
  } catch (e) {
    fail(`GitHub POST failed: ${e instanceof Error ? e.message : String(e)}`);
    info(`Entry left on disk; retry with: npx ue-mcp feedback approve ${id}`);
    process.exit(1);
  }
  if (result.kind === "auth_required") {
    warn("GitHub auth required to post as user.");
    console.log(`  ${BOLD}1.${RESET} Open: ${CYAN}${result.verification_uri}${RESET}`);
    console.log(`  ${BOLD}2.${RESET} Enter code: ${BOLD}${YELLOW}${result.user_code}${RESET}`);
    console.log(`  ${BOLD}3.${RESET} Authorize, then re-run: npx ue-mcp feedback approve ${id}`);
    info(`Or discard with: npx ue-mcp feedback discard ${id}`);
    process.exit(1);
  }
  ok(`Posted as issue #${result.number} (${result.url})`);
  if (deleteDeferred(id)) {
    info(`Removed local entry ${id}.`);
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
