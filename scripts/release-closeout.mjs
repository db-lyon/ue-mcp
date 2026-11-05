// Closes fixed-pending-release issues whose fix shipped in a published release.
//
//   node scripts/release-closeout.mjs --tag v1.4.8 [--dry-run]
//
// An issue's fix is every commit on main that cites it: commit messages naming
// #N, the merge commits of merged PRs that reference it, and commit SHAs cited
// in its comments. A stable release containing all of them closes the issue.
// A prerelease containing all of them gets one comment saying where to get it.
// An issue with no commit found is listed in the summary and left alone.

import { execFileSync } from "node:child_process";
import fs from "node:fs";
import { pathToFileURL } from "node:url";

export const PRERELEASE_MARKER = "<!-- ue-mcp-release-closeout:prerelease -->";
const LABEL = "fixed-pending-release";

/** released: every fix commit is in the tag. pending: none is. partial: some are. unknown: none found. */
export function classify(fixCommits, inTag) {
  if (!fixCommits.length) return "unknown";
  const shipped = fixCommits.filter((c) => inTag(c)).length;
  if (shipped === fixCommits.length) return "released";
  return shipped ? "partial" : "pending";
}

const NOT_RELEASED = {
  unknown: "no fix commit found",
  pending: "fix is not in this release",
  partial: "only some fix commits are in this release",
};

export function closingComment(tag, commits) {
  const shas = commits.map((c) => c.slice(0, 8)).join(", ");
  return `Fixed in ${shas}, released in ${tag}.`;
}

export function prereleaseComment(tag, commits) {
  const shas = commits.map((c) => c.slice(0, 8)).join(", ");
  return `${PRERELEASE_MARKER}\nAvailable in ${tag} (fix: ${shas}); install it with \`npx ue-mcp@beta\`. This issue closes when the stable release ships.`;
}

/** Hex runs in comment text that may be commit SHAs; callers keep only real commits on main. */
export function citedShas(text) {
  return [...String(text ?? "").matchAll(/(?<![0-9a-zA-Z/#])([0-9a-f]{7,40})(?![0-9a-zA-Z])/g)]
    .map((m) => m[1])
    .filter((s) => /[a-f]/.test(s) && /[0-9]/.test(s));
}

function run(cmd, args) {
  return execFileSync(cmd, args, { encoding: "utf8", stdio: ["pipe", "pipe", "pipe"] }).trim();
}
function succeeds(cmd, args) {
  try { run(cmd, args); return true; } catch { return false; }
}
function ghJson(args) {
  return JSON.parse(run("gh", args));
}

function onMain(sha) {
  return succeeds("git", ["merge-base", "--is-ancestor", sha, "origin/main"]);
}

function fixCommits(repo, n) {
  const found = new Set();
  const log = run("git", ["log", "origin/main", "-E", `--grep=#${n}([^0-9]|$)`, "--format=%H"]);
  for (const sha of log.split("\n").filter(Boolean)) found.add(sha);

  const [owner, name] = repo.split("/");
  const query = `query($o:String!,$r:String!,$n:Int!){repository(owner:$o,name:$r){issue(number:$n){
    comments(last:50){nodes{body}}
    timelineItems(first:100,itemTypes:[CROSS_REFERENCED_EVENT,CONNECTED_EVENT]){nodes{
      ... on CrossReferencedEvent{source{... on PullRequest{merged mergeCommit{oid}}}}
      ... on ConnectedEvent{subject{... on PullRequest{merged mergeCommit{oid}}}}}}}}}`;
  const data = ghJson(["api", "graphql", "-f", `query=${query}`, "-F", `o=${owner}`, "-F", `r=${name}`, "-F", `n=${n}`]);
  const issue = data.data.repository.issue;
  for (const node of issue.timelineItems.nodes) {
    const pr = node.source ?? node.subject;
    if (pr?.merged && pr.mergeCommit?.oid) found.add(pr.mergeCommit.oid);
  }
  for (const c of issue.comments.nodes) {
    for (const s of citedShas(c.body)) {
      if (!succeeds("git", ["cat-file", "-e", `${s}^{commit}`])) continue;
      found.add(run("git", ["rev-parse", s]));
    }
  }
  return [...found].filter(onMain);
}

function main() {
  const args = process.argv.slice(2);
  const tag = args[args.indexOf("--tag") + 1];
  const dryRun = args.includes("--dry-run");
  const repo = process.env.GITHUB_REPOSITORY ?? "db-lyon/ue-mcp";
  const out = (line) => (process.env.GITHUB_STEP_SUMMARY ? fs.appendFileSync(process.env.GITHUB_STEP_SUMMARY, `${line}\n`) : console.log(line));
  if (!tag || !tag.startsWith("v")) {
    console.error("usage: release-closeout.mjs --tag vX.Y.Z [--dry-run]");
    process.exit(2);
  }

  let release;
  try {
    release = ghJson(["release", "view", tag, "--repo", repo, "--json", "isDraft,isPrerelease"]);
  } catch {
    out(`${tag}: no release, nothing to close.`);
    return;
  }
  if (release.isDraft) {
    out(`${tag}: still a draft, nothing to close.`);
    return;
  }

  const issues = ghJson(["issue", "list", "--repo", repo, "--state", "open", "--label", LABEL, "--limit", "500", "--json", "number,title"]);
  out(`### ${tag} (${release.isPrerelease ? "prerelease" : "stable"})${dryRun ? ", dry run" : ""}\n`);

  for (const { number: n, title } of issues) {
    const commits = fixCommits(repo, n);
    const state = classify(commits, (c) => succeeds("git", ["merge-base", "--is-ancestor", c, tag]));
    if (state !== "released") {
      out(`- #${n} ${state}: ${NOT_RELEASED[state]}. ${title}`);
      continue;
    }
    if (!release.isPrerelease) {
      out(`- #${n} closed. ${title}`);
      if (dryRun) continue;
      run("gh", ["issue", "comment", String(n), "--repo", repo, "--body", closingComment(tag, commits)]);
      run("gh", ["issue", "edit", String(n), "--repo", repo, "--remove-label", LABEL]);
      run("gh", ["issue", "close", String(n), "--repo", repo, "--reason", "completed"]);
      continue;
    }
    const comments = ghJson(["issue", "view", String(n), "--repo", repo, "--json", "comments"]).comments;
    if (comments.some((c) => c.body.includes(PRERELEASE_MARKER))) {
      out(`- #${n} already announced in an earlier prerelease. ${title}`);
      continue;
    }
    out(`- #${n} announced. ${title}`);
    if (!dryRun) run("gh", ["issue", "comment", String(n), "--repo", repo, "--body", prereleaseComment(tag, commits)]);
  }
}

const invokedAsScript = process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href;
if (invokedAsScript) main();
