#!/usr/bin/env node
/**
 * npx ue-mcp resolve <issue-number>
 *
 * Fetches a GitHub issue, creates a branch, launches Claude Code to
 * implement the fix, then pushes and opens a PR.  Never merges to main.
 */
import { execSync, spawn } from "node:child_process";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";

const RESET = "\x1b[0m";
const BOLD = "\x1b[1m";
const DIM = "\x1b[2m";
const GREEN = "\x1b[32m";
const RED = "\x1b[31m";
const CYAN = "\x1b[36m";

const ok = (msg: string) => console.log(`  ${GREEN}✓${RESET} ${msg}`);
const fail = (msg: string) => {
  console.error(`  ${RED}✗${RESET} ${msg}`);
  process.exit(1);
};

/* ── Helpers ─────────────────────────────────────────────────────── */

function hasCommand(cmd: string): boolean {
  try {
    execSync(`${cmd} --version`, { stdio: "ignore" });
    return true;
  } catch {
    return false;
  }
}

function run(cmd: string): string {
  return execSync(cmd, { encoding: "utf-8" }).trim();
}

function currentBranch(): string {
  return run("git rev-parse --abbrev-ref HEAD");
}

/* ── Main ────────────────────────────────────────────────────────── */

async function resolve() {
  const args = process.argv.slice(3);
  const ciMode = args.includes("--ci") || !!process.env.CI;
  const issueArg = args.find((a) => !a.startsWith("-"));
  const issueNum = Number(issueArg);

  if (!issueArg || isNaN(issueNum)) {
    console.log("");
    console.log(`  ${BOLD}Usage:${RESET} npx ue-mcp resolve <issue-number> [--ci]`);
    console.log(`  ${DIM}Example: npx ue-mcp resolve 16${RESET}`);
    console.log(`  ${DIM}   CI:   npx ue-mcp resolve 16 --ci${RESET}`);
    console.log("");
    process.exit(1);
  }

  // Prerequisites
  if (!hasCommand("gh")) fail("gh CLI is required. Install: https://cli.github.com");
  if (!hasCommand("claude")) fail("Claude Code is required. Install: npm i -g @anthropic-ai/claude-code");

  try {
    run("git rev-parse --git-dir");
  } catch {
    fail("Not a git repository. Clone ue-mcp first.");
  }

  // Detect repo owner/name from git remote
  let repo: string;
  try {
    const remote = run("git remote get-url origin");
    const match = remote.match(/[/:]([^/]+\/[^/.]+?)(?:\.git)?$/);
    repo = match ? match[1] : "db-lyon/ue-mcp";
  } catch {
    repo = "db-lyon/ue-mcp";
  }

  // Fetch issue
  console.log("");
  console.log(`  ${BOLD}${CYAN}ue-mcp resolve${RESET}`);
  console.log("");

  let issue: { title: string; body: string; labels: { name: string }[] };
  try {
    const raw = run(
      `gh issue view ${issueNum} --repo db-lyon/ue-mcp --json title,body,labels`,
    );
    issue = JSON.parse(raw);
  } catch {
    fail(`Could not fetch issue #${issueNum} from db-lyon/ue-mcp`);
    return; // unreachable, but satisfies TS
  }

  ok(`Issue #${issueNum}: ${issue.title}`);

  // Create branch from main
  const startBranch = currentBranch();
  const branch = `resolve/${issueNum}`;

  try {
    // Make sure we branch from latest main
    execSync("git fetch origin main", { stdio: "ignore" });
    execSync(`git checkout -b ${branch} origin/main`, { stdio: "pipe" });
  } catch {
    // Branch might already exist
    try {
      execSync(`git checkout ${branch}`, { stdio: "pipe" });
    } catch {
      fail(`Could not create or switch to branch ${branch}`);
    }
  }

  ok(`Branch: ${branch}`);
  console.log("");

  // Build prompt
  const prompt = [
    `You are resolving GitHub issue #${issueNum} in the ue-mcp codebase.`,
    ``,
    `## Issue: ${issue.title}`,
    ``,
    issue.body || "(no description)",
    ``,
    `## Codebase`,
    ``,
    `This is a TypeScript MCP server for Unreal Engine with a C++ plugin.`,
    `- src/tools/ — tool handlers (one file per category)`,
    `- src/ — core server (bridge.ts, types.ts, index.ts, instructions.ts)`,
    `- plugin/ — C++ Unreal Engine plugin source`,
    `- Read CLAUDE.md for project conventions.`,
    ``,
    `## Instructions`,
    ``,
    `1. Read the relevant source code to understand the problem`,
    `2. Implement the fix or feature`,
    `3. Verify with \`npx tsc --noEmit\``,
    `4. Commit your changes with a message referencing #${issueNum}`,
    `5. Do NOT push, create PRs, or make unrelated changes`,
  ].join("\n");

  // Write prompt to a temp file so shell escaping can't mangle it
  const promptFile = path.join(os.tmpdir(), `ue-mcp-resolve-${issueNum}.md`);
  fs.writeFileSync(promptFile, prompt);

  const claudeArgs = ["--print", "--dangerously-skip-permissions"];

  console.log(`  ${DIM}Launching Claude Code${ciMode ? " (CI mode)" : ""}...${RESET}`);
  console.log("");

  const exitCode = await new Promise<number | null>((resolve) => {
    const child = spawn("claude", claudeArgs, {
      stdio: ["pipe", "inherit", "inherit"],
      shell: true,
    });
    child.stdin!.write(prompt);
    child.stdin!.end();
    child.on("exit", resolve);
    child.on("error", () => resolve(1));
  });

  // Clean up
  try { fs.unlinkSync(promptFile); } catch { /* ignore */ }

  console.log("");

  if (exitCode !== 0) {
    console.log(`  ${DIM}Claude exited with code ${exitCode}. Branch ${branch} preserved.${RESET}`);
    console.log("");
    return;
  }

  // Check if there are commits on this branch beyond main
  let commitCount: number;
  try {
    const log = run(`git log origin/main..HEAD --oneline`);
    commitCount = log ? log.split("\n").length : 0;
  } catch {
    commitCount = 0;
  }

  if (commitCount === 0) {
    console.log(`  ${DIM}No commits on ${branch}. Nothing to push.${RESET}`);
    console.log("");
    return;
  }

  ok(`${commitCount} commit(s) ready`);

  // Push and create PR
  try {
    execSync(`git push -u origin ${branch}`, { stdio: "pipe" });
    ok("Pushed to origin");
  } catch (e) {
    fail(`Push failed: ${e instanceof Error ? e.message : e}`);
  }

  try {
    const prTitle = `Fix #${issueNum}: ${issue.title}`;
    const prBody = [
      `Resolves #${issueNum}`,
      ``,
      `---`,
      `*Automated with \`npx ue-mcp resolve ${issueNum}\`*`,
    ].join("\n");

    const prUrl = run(
      `gh pr create --title "${prTitle.replace(/"/g, '\\"')}" --body "${prBody.replace(/"/g, '\\"')}" --repo db-lyon/ue-mcp --head ${branch}`,
    );
    console.log("");
    ok(`PR created: ${prUrl}`);
  } catch {
    // PR creation might fail if they don't have push access to upstream
    // In that case, they can create the PR manually
    console.log(`  ${DIM}Tip: create a PR manually if you're working from a fork${RESET}`);
  }

  console.log("");
}

resolve().catch((e) => {
  console.error(`\n  ${RED}Fatal: ${e instanceof Error ? e.message : e}${RESET}\n`);
  process.exit(1);
});
