import * as fs from "node:fs";
import * as path from "node:path";
import { randomBytes } from "node:crypto";
import {
  deferSubmission,
  getPendingDir,
  listDeferred,
  type DeferredFeedback,
} from "./feedback-deferred.js";
import { repoSlug, type GitHubRepo } from "../extensions/registry-catalog.js";

/**
 * What feedback(submit) does when the elicitation gate cannot reach a human
 * (#991).
 *
 * The gate is the only approval channel the server has, and some clients
 * answer it themselves: the VS Code Claude Code extension auto-declines in
 * 15 to 75ms, before anything is rendered. The "too fast to be human"
 * detection catches that correctly, but until now the report died there. A
 * team ran into it eight times in a week and had to retype every report by
 * hand somewhere else.
 *
 * The fallback needs nothing from the client, which is the whole point:
 *
 *   1. The scrubbed payload is written to disk, in the same pending-feedback
 *      store `npx ue-mcp feedback list/approve/discard` already reads. So the
 *      report survives the session even if nobody clicks anything.
 *   2. A prefilled `github.com/.../issues/new?title=&body=` URL is handed
 *      back. One click lands the user in the issue form with the body already
 *      written, no auth, no elicitation, no CLI.
 *   3. A short confirmation token is written into the report file. If the
 *      user says "yes, submit it" in plain text, they can read the token out
 *      of the file and have the agent pass it back on a second
 *      feedback(submit) call, which posts the exact stored bytes.
 *
 * Door 3 is a human-confirmation speed bump, not an authorization boundary:
 * an agent with filesystem access can read the token itself. It exists so a
 * user who WANTS to approve has a path that ends in a posted issue, and it is
 * deliberately the last of the three. Doors 1 and 2 need no trust at all.
 */

/**
 * Cap on the whole prefilled URL, measured AFTER percent-encoding.
 *
 * Browsers and GitHub both start truncating somewhere north of 8KB, and a
 * truncated query string produces a half-written issue body rather than an
 * error, which is worse than no link. 6000 leaves headroom for the origin,
 * the path, and any redirect GitHub adds on the way to the login wall.
 *
 * Measuring the encoded length is the part that matters: a body of 5000 plain
 * characters full of newlines and backticks encodes to well over 15000.
 */
export const MaxIssueUrlChars = 6000;

export interface PrefilledIssueUrl {
  /** The URL, or null when even a body-less link would blow the cap. */
  url: string | null;
  /** True when the body in the URL is a prefix of the real body. */
  truncated: boolean;
  /** Encoded length of the URL that was produced, for diagnostics/tests. */
  length: number;
}

function issueUrlFor(repo: GitHubRepo, title: string, body: string): string {
  const qs = `title=${encodeURIComponent(title)}&body=${encodeURIComponent(body)}`;
  return `https://github.com/${repo.owner}/${repo.repo}/issues/new?${qs}`;
}

/**
 * Build a prefilled new-issue URL that is guaranteed to fit under the cap.
 *
 * Full body if it fits. Otherwise the longest prefix that still fits once the
 * "rest of the report is on disk" pointer is appended. If not even that fits,
 * `url` is null and the caller tells the user to open a blank issue and paste
 * from the file, which beats handing out a link that silently loses half the
 * report.
 */
export function buildPrefilledIssueUrl(
  repo: GitHubRepo,
  title: string,
  body: string,
  reportPath?: string,
): PrefilledIssueUrl {
  const full = issueUrlFor(repo, title, body);
  if (full.length <= MaxIssueUrlChars) {
    return { url: full, truncated: false, length: full.length };
  }

  const pointer = reportPath
    ? `\n\n_(truncated. The full report is at ${reportPath} - paste the rest in.)_`
    : `\n\n_(truncated. Ask the agent for the full report text and paste the rest in.)_`;

  // A body-less link is the floor. If the title alone cannot fit, no amount
  // of trimming helps and there is no honest link to hand out.
  const floor = issueUrlFor(repo, title, pointer);
  if (floor.length > MaxIssueUrlChars) {
    return { url: null, truncated: true, length: floor.length };
  }

  // Longest prefix that fits, by bisection. Encoded length is not linear in
  // the plain length, so scanning by character would be O(n) URL builds.
  let lo = 0;
  let hi = body.length;
  while (lo < hi) {
    const mid = Math.ceil((lo + hi) / 2);
    if (issueUrlFor(repo, title, body.slice(0, mid) + pointer).length <= MaxIssueUrlChars) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }
  const url = issueUrlFor(repo, title, body.slice(0, lo) + pointer);
  return { url, truncated: true, length: url.length };
}

/* ── local report file ─────────────────────────────────────────────── */

export interface FallbackReport {
  /** Pending-store id. Also `npx ue-mcp feedback approve <id>`. */
  id: string;
  /** Human-readable markdown copy of the report, token at the top. */
  path: string;
  /** JSON entry the CLI reads. */
  jsonPath: string;
  /** Confirmation token, written into `path` and NEVER into a tool result. */
  token: string;
  /** Prefilled issue URL, or null when the body cannot fit in one. */
  url: string | null;
  /** True when the URL carries a prefix of the body rather than all of it. */
  urlTruncated: boolean;
}

function generateToken(): string {
  // Short enough for a human to read off a file and retype, long enough that
  // it is not guessable in the handful of pending entries that ever exist.
  return randomBytes(4).toString("hex");
}

export interface FallbackInput {
  title: string;
  body: string;
  labels: string[];
  repo: GitHubRepo;
  routing: string;
  project: string | null;
  author: "user" | "bot";
  /** Why the elicitation gate could not be used. Recorded in the file. */
  reason: string;
}

/**
 * Persist the payload and produce the click-through URL.
 *
 * Writing lands in the SAME store as `feedback.mode = "defer"`, on purpose:
 * the CLI review commands already understand it, so an unreachable
 * elicitation gate degrades into the deferred flow rather than into a new
 * parallel one nobody knows how to drain.
 */
export function writeFallbackReport(input: FallbackInput): FallbackReport {
  const token = generateToken();
  const entry = deferSubmission(
    {
      title: input.title,
      body: input.body,
      labels: input.labels,
      repo: repoSlug(input.repo),
      routing: input.routing,
      confirmToken: token,
      blockedReason: input.reason,
    },
    input.project,
    input.author,
  );

  const mdPath = path.join(getPendingDir(), `${entry.id}.md`);
  const md = [
    `# ${input.title}`,
    "",
    `Confirmation token: ${token}`,
    "",
    `This report was NOT posted. The MCP client could not show the approval`,
    `form (${input.reason}), so nothing was sent anywhere.`,
    "",
    `Three ways to land it, pick one:`,
    "",
    `  1. Open the prefilled link the agent printed and click Submit.`,
    `  2. Run \`npx ue-mcp feedback approve ${entry.id}\`.`,
    `  3. Tell the agent to call feedback(submit) with confirmToken="${token}",`,
    `     which posts exactly the body below.`,
    "",
    `Tracker: ${repoSlug(input.repo)}`,
    `Labels : ${input.labels.join(", ")}`,
    `Author : ${input.author}`,
    `Routing: ${input.routing}`,
    "",
    "---",
    "",
    input.body,
    "",
  ].join("\n");
  fs.writeFileSync(mdPath, md, { mode: 0o600 });

  const prefilled = buildPrefilledIssueUrl(input.repo, input.title, input.body, mdPath);

  return {
    id: entry.id,
    path: mdPath,
    jsonPath: path.join(getPendingDir(), `${entry.id}.json`),
    token,
    url: prefilled.url,
    urlTruncated: prefilled.truncated,
  };
}

/** Look up a pending entry by its confirmation token. */
export function findByConfirmToken(token: string): DeferredFeedback | null {
  const wanted = token.trim().toLowerCase();
  if (!wanted) return null;
  for (const entry of listDeferred()) {
    if ((entry.confirmToken ?? "").toLowerCase() === wanted) return entry;
  }
  return null;
}

/** Drop the markdown copy that accompanies a pending entry, if any. */
export function deleteFallbackReport(id: string): void {
  const mdPath = path.join(getPendingDir(), `${id}.md`);
  try {
    if (fs.existsSync(mdPath)) fs.unlinkSync(mdPath);
  } catch {
    // Best effort. A leftover .md is noise, not a failure.
  }
}
