// Issue triage for .github/workflows/issue-triage.yml.
//
//   node scripts/issue-triage.mjs targets                  issue numbers to triage, from the event
//   node scripts/issue-triage.mjs prompt <n>               the agent prompt for issue n
//   node scripts/issue-triage.mjs apply <n> <agent.json>   verify the agent's decision and apply it
//
// The agent runs read-only and returns a decision. Everything that writes to
// GitHub happens here, after each claim the decision makes (a duplicate
// target, a fixing commit, a destination) is checked against GitHub and git.

import { execFileSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

export const MARKER = "<!-- ue-mcp-triage -->";
export const MODES = ["act", "propose", "dry-run"];
export const MAINTAINERS = new Set(["OWNER", "MEMBER", "COLLABORATOR"]);

// Where an issue goes when it is not ours. A destination without a repo is a
// tracker GitHub cannot transfer into (private, or issues disabled), so the
// issue stays here under its label.
export const DESTINATIONS = {
  "ue-mcp-studio": { repo: "db-lyon/ue-mcp-studio" },
  "ue-mcp-studio-plugin": { repo: "db-lyon/ue-mcp-studio-plugin" },
  "voxel-plugin-tools": { repo: "db-lyon/voxel-plugin-tools" },
  perforce: { label: "plugin:perforce", name: "Perforce" },
  meshy: { label: "plugin:meshy", name: "Meshy" },
  "fab-editor": { label: "plugin:fab-editor", name: "Fab Editor" },
};

// Labels triage manages or that carry a maintainer decision. Everything else
// in the repo is an area label the agent may pick.
export const NON_AREA_LABELS = new Set([
  "bug", "documentation", "duplicate", "enhancement", "good first issue", "help wanted",
  "invalid", "question", "wontfix", "agent-feedback", "resolve", "limitation", "planned",
  "fixed-pending-release", "merge-blocked", "triaged", "needs-triage", "needs-info",
]);

export const TRIAGE_LABELS = {
  triaged: { color: "C2E0C6", description: "Automated triage settled this issue" },
  "needs-triage": { color: "FBCA04", description: "Automated triage left this for a maintainer" },
  "needs-info": { color: "D876E3", description: "Waiting on details from the reporter" },
  "plugin:perforce": { color: "BFD4F2", description: "The Perforce plugin's area" },
  "plugin:meshy": { color: "BFD4F2", description: "The Meshy plugin's area" },
  "plugin:fab-editor": { color: "BFD4F2", description: "The Fab Editor plugin's area" },
};

const MAX_SUMMARY = 700;
// Built from code points so the banned characters never appear in this file.
const DASHES = new RegExp(`\\s*[${String.fromCharCode(0x2014, 0x2013)}]\\s*`, "g");
const ELLIPSIS = new RegExp(`${String.fromCharCode(0x2026)}|\\.{3,}`, "g");

/** Public text from the agent: no pings, no markup, no outside links, house punctuation. */
export function sanitize(text) {
  let t = String(text ?? "");
  t = t.replace(/<!--[\s\S]*?-->/g, "").replace(/<\/?[a-z][^>]*>/gi, "");
  t = t.replace(/\[([^\]]*)\]\(([^)]*)\)/g, (m, label, url) =>
    url.startsWith("https://github.com/db-lyon/") ? m : label);
  t = t.replace(/https?:\/\/\S+/g, (url) => (url.startsWith("https://github.com/db-lyon/") ? url : ""));
  t = t.replace(/(^|[^\w`])@([\w-]+)/g, "$1$2");
  t = t.replace(DASHES, ", ").replace(ELLIPSIS, ".").replace(/\s--\s/g, ", ");
  t = t.replace(/[ \t]+/g, " ").trim();
  if (t.length > MAX_SUMMARY) t = `${t.slice(0, MAX_SUMMARY).replace(/\s+\S*$/, "")}.`;
  return t;
}

/** Issue numbers to triage for this event. Maintainers' own issues are skipped on open. */
export function targetsFor(eventName, payload, dispatchNumbers) {
  if (eventName === "workflow_dispatch") {
    return String(dispatchNumbers ?? "")
      .split(/[\s,]+/)
      .map((s) => Number.parseInt(s.replace(/^#/, ""), 10))
      .filter((n) => Number.isInteger(n) && n > 0);
  }
  const issue = payload?.issue;
  if (!issue || issue.pull_request || issue.state !== "open") return [];
  if (MAINTAINERS.has(issue.author_association)) return [];
  return [issue.number];
}

/**
 * Turn the agent's decision into the outcome that will actually be applied.
 * Any claim the facts do not back, and any closure below high confidence,
 * becomes needs-human with the reason, so nothing closes on a guess.
 */
export function resolveDecision(decision, facts, mode) {
  if (!decision) return { outcome: "needs-human", reason: "the agent returned no decision", silent: true };
  const proposed = decision.outcome;
  const downgrade = (reason) => ({ outcome: "needs-human", reason, proposed });
  const extra = {};

  if (proposed === "duplicate") {
    const dup = facts.duplicate;
    if (!dup || dup.number === facts.issue.number || dup.isPullRequest) {
      return downgrade(`#${decision.duplicateOf ?? "?"} is not another issue in this repository`);
    }
    extra.duplicate = dup;
  }
  if (proposed === "fixed") {
    if (!facts.fix?.onMain) return downgrade(`commit ${decision.fixCommit ?? "(none)"} is not on main`);
    extra.fix = facts.fix;
  }
  if (proposed === "move") {
    const dest = DESTINATIONS[decision.moveTo];
    if (!dest) return downgrade(`unknown destination ${decision.moveTo ?? "(none)"}`);
    if (dest.repo && !facts.canTransfer) return downgrade(`moving to ${dest.repo} needs the transfer token`);
    extra.dest = dest;
  }
  if (proposed === "limitation" && !sanitize(decision.summary)) return downgrade("a limitation needs a named blocker");
  if (proposed === "needs-info" && !(decision.questions ?? []).some((q) => sanitize(q))) {
    return downgrade("needs-info without questions");
  }

  const closes =
    proposed === "duplicate" ||
    proposed === "invalid" ||
    (proposed === "move" && Boolean(extra.dest.repo)) ||
    (proposed === "fixed" && Boolean(extra.fix.releasedIn));
  if (closes && decision.confidence !== "high") {
    return downgrade(`${decision.confidence} confidence; closing needs high`);
  }
  if (closes && mode === "propose") return { outcome: "proposed", proposed, closes, ...extra };
  return { outcome: proposed, closes, ...extra };
}

function sentence(...parts) {
  return parts.map((p) => (p ?? "").trim()).filter(Boolean).join(" ");
}

/** The public comment for an outcome, without the marker. Null means no comment. */
export function outcomeText(outcome, r, decision, ctx) {
  const s = sanitize(decision?.summary);
  switch (outcome) {
    case "duplicate": {
      const dup = r.duplicate;
      const shipping = dup.state === "open" && dup.labels.includes("fixed-pending-release")
        ? "The fix is on main and ships in the next release." : "";
      return sentence(`Duplicate of #${dup.number}.`, shipping, s);
    }
    case "fixed": {
      const { short, releasedIn, prerelease } = r.fix;
      if (releasedIn) {
        return sentence(`Fixed in ${short}, released in ${releasedIn}.`, s,
          `Comment here if it still reproduces on ${ctx.latestRelease ?? releasedIn} or later.`);
      }
      const beta = prerelease ? `, available in ${prerelease} (\`npx ue-mcp@beta\`)` : "";
      return sentence(`Fixed on main in ${short}${beta}.`, s, "This issue closes when the next stable release ships.");
    }
    case "move":
      return r.dest.repo
        ? sentence(`This belongs in ${r.dest.repo}, so it is moving there.`, s)
        : sentence(`This is the ${r.dest.name} plugin's area; it stays here under the \`${r.dest.label}\` label.`, s);
    case "limitation":
      return s;
    case "needs-info": {
      const qs = (decision.questions ?? []).map(sanitize).filter(Boolean).map((q) => `- ${q}`);
      return [s, qs.join("\n")].filter(Boolean).join("\n\n");
    }
    case "invalid":
      return s || "Closed: this is not about ue-mcp.";
    default:
      return null;
  }
}

/**
 * What to do to the issue: labels, the comment, a close or a transfer.
 * Pure, so the rules are under unit test; `applyPlan` does the writing.
 */
export function planActions(r, decision, ctx) {
  const areas = new Set(
    (decision?.areas ?? []).filter((a) => ctx.repoLabels.has(a) && !NON_AREA_LABELS.has(a) && !a.startsWith("plugin:")),
  );
  const add = new Set();
  const remove = new Set(["needs-triage"]);
  let comment = null;
  let close = null;
  let transfer = null;

  if (decision && ctx.repoLabels.has(decision.kind)) add.add(decision.kind);
  for (const a of [...areas].slice(0, 3)) add.add(a);

  switch (r.outcome) {
    case "accept":
      add.add("triaged");
      break;
    case "duplicate":
      add.add("duplicate");
      comment = outcomeText("duplicate", r, decision, ctx);
      close = "duplicate";
      break;
    case "fixed":
      add.add("triaged");
      comment = outcomeText("fixed", r, decision, ctx);
      if (r.fix.releasedIn) close = "completed";
      else add.add("fixed-pending-release");
      break;
    case "move":
      add.add("triaged");
      comment = outcomeText("move", r, decision, ctx);
      if (r.dest.repo) transfer = r.dest.repo;
      else add.add(r.dest.label);
      break;
    case "limitation":
      add.add("limitation");
      add.add("triaged");
      comment = outcomeText("limitation", r, decision, ctx);
      break;
    case "needs-info":
      add.add("needs-info");
      comment = outcomeText("needs-info", r, decision, ctx);
      break;
    case "invalid":
      add.add("invalid");
      comment = outcomeText("invalid", r, decision, ctx);
      close = "not_planned";
      break;
    case "proposed": {
      add.add("needs-triage");
      remove.delete("needs-triage");
      const text = outcomeText(r.proposed, r, decision, ctx);
      comment = `**Triage proposal**, applied only when a maintainer confirms it:\n\n${quote(text)}`;
      break;
    }
    case "needs-human": {
      add.add("needs-triage");
      remove.delete("needs-triage");
      if (!r.silent) {
        const note = sanitize(decision?.summary);
        comment = sentence("Automated triage left this for a maintainer.", note) +
          `\n\n<details><summary>Triage notes</summary>\n\nProposed: \`${r.proposed}\` (${decision?.confidence} confidence). Not applied: ${r.reason}.\n\n</details>`;
      }
      break;
    }
  }
  for (const l of add) remove.delete(l);
  return { add: [...add], remove: [...remove], comment, close, transfer };
}

function quote(text) {
  return String(text ?? "").split("\n").map((l) => `> ${l}`).join("\n");
}

// ---------------------------------------------------------------------------
// GitHub and git

function run(cmd, args, opts = {}) {
  return execFileSync(cmd, args, { encoding: "utf8", stdio: ["pipe", "pipe", "pipe"], ...opts }).trim();
}
function tryRun(cmd, args, opts) {
  try { return run(cmd, args, opts); } catch { return null; }
}
function succeeds(cmd, args) {
  return tryRun(cmd, args) !== null;
}
function ghJson(args, opts) {
  const out = tryRun("gh", args, opts);
  return out ? JSON.parse(out) : null;
}

function issueFacts(repo, n) {
  const i = ghJson(["api", `repos/${repo}/issues/${n}`]);
  if (!i) return null;
  return {
    number: i.number,
    state: i.state,
    locked: i.locked,
    isPullRequest: Boolean(i.pull_request),
    labels: i.labels.map((l) => (typeof l === "string" ? l : l.name)),
  };
}

function releases(repo) {
  return (ghJson(["release", "list", "--repo", repo, "--limit", "200", "--json", "tagName,isDraft,isPrerelease,isLatest"]) ?? [])
    .filter((r) => !r.isDraft);
}

/** Where a commit stands: on main, and the first stable and pre-release tags that contain it. */
export function fixFacts(sha, published) {
  if (!sha || !succeeds("git", ["cat-file", "-e", `${sha}^{commit}`])) return null;
  const full = run("git", ["rev-parse", sha]);
  const onMain = succeeds("git", ["merge-base", "--is-ancestor", full, "origin/main"]);
  const tags = (tryRun("git", ["tag", "--contains", full, "--sort=version:refname"]) ?? "").split("\n").filter(Boolean);
  const byTag = new Map(published.map((r) => [r.tagName, r]));
  const firstStable = tags.find((t) => byTag.get(t) && !byTag.get(t).isPrerelease) ?? null;
  const firstPre = tags.find((t) => byTag.get(t)?.isPrerelease) ?? null;
  return { sha: full, short: full.slice(0, 8), onMain, releasedIn: firstStable, prerelease: firstStable ? null : firstPre };
}

function ensureLabels(repo, names) {
  for (const name of names) {
    const spec = TRIAGE_LABELS[name];
    if (!spec) continue;
    tryRun("gh", ["label", "create", name, "--repo", repo, "--color", spec.color, "--description", spec.description]);
  }
}

function upsertComment(repo, n, body) {
  const comments = ghJson(["api", "--paginate", "--slurp", `repos/${repo}/issues/${n}/comments`]) ?? [];
  const mine = comments.flat().find((c) => c.body?.includes(MARKER));
  const full = `${MARKER}\n${body}`;
  if (mine) run("gh", ["api", "-X", "PATCH", `repos/${repo}/issues/comments/${mine.id}`, "-f", `body=${full}`]);
  else run("gh", ["api", "-X", "POST", `repos/${repo}/issues/${n}/comments`, "-f", `body=${full}`]);
}

function applyPlan(repo, n, plan) {
  ensureLabels(repo, plan.add);
  if (plan.add.length) run("gh", ["issue", "edit", String(n), "--repo", repo, "--add-label", plan.add.join(",")]);
  for (const l of plan.remove) tryRun("gh", ["issue", "edit", String(n), "--repo", repo, "--remove-label", l]);
  if (plan.comment) upsertComment(repo, n, plan.comment);
  if (plan.close) {
    const close = (reason) => run("gh", ["api", "-X", "PATCH", `repos/${repo}/issues/${n}`, "-f", "state=closed", "-f", `state_reason=${reason}`]);
    try { close(plan.close); } catch { close(plan.close === "duplicate" ? "not_planned" : "completed"); }
  }
  if (plan.transfer) {
    run("gh", ["issue", "transfer", String(n), plan.transfer, "--repo", repo], {
      env: { ...process.env, GH_TOKEN: process.env.TRANSFER_TOKEN },
    });
  }
}

/** Area labels the agent may choose from, for the prompt. */
function areaLabels(repo) {
  return (ghJson(["label", "list", "--repo", repo, "--limit", "200", "--json", "name"]) ?? [])
    .map((l) => l.name)
    .filter((l) => !NON_AREA_LABELS.has(l) && !l.startsWith("plugin:"));
}

function summaryLine(file, text) {
  if (file) fs.appendFileSync(file, `${text}\n`);
  else console.log(text);
}

function readAgentResult(file) {
  try {
    const raw = JSON.parse(fs.readFileSync(file, "utf8"));
    const result = Array.isArray(raw) ? raw[raw.length - 1] : raw;
    return { decision: result?.structured_output ?? null, cost: result?.total_cost_usd ?? null, turns: result?.num_turns ?? null };
  } catch {
    return { decision: null, cost: null, turns: null };
  }
}

function main() {
  const [cmd, ...args] = process.argv.slice(2);
  const repo = process.env.GITHUB_REPOSITORY ?? "db-lyon/ue-mcp";

  if (cmd === "targets") {
    const payload = process.env.GITHUB_EVENT_PATH ? JSON.parse(fs.readFileSync(process.env.GITHUB_EVENT_PATH, "utf8")) : {};
    const list = targetsFor(process.env.GITHUB_EVENT_NAME, payload, process.env.ISSUE_NUMBERS);
    console.log(`issues=${JSON.stringify(list)}`);
    return;
  }

  if (cmd === "prompt") {
    const n = Number.parseInt(args[0], 10);
    if (!Number.isInteger(n)) throw new Error("prompt needs an issue number");
    const template = fs.readFileSync(path.join(import.meta.dirname, "..", ".github", "triage", "prompt.md"), "utf8");
    process.stdout.write(template
      .replaceAll("{{ISSUE}}", String(n))
      .replaceAll("{{REPO}}", repo)
      .replaceAll("{{AREAS}}", areaLabels(repo).map((l) => `\`${l}\``).join(", ")));
    return;
  }

  if (cmd === "apply") {
    const n = Number.parseInt(args[0], 10);
    const mode = MODES.includes(process.env.TRIAGE_MODE) ? process.env.TRIAGE_MODE : "propose";
    const { decision, cost, turns } = readAgentResult(args[1]);
    const out = process.env.GITHUB_STEP_SUMMARY;

    const issue = issueFacts(repo, n);
    if (!issue || issue.state !== "open" || issue.locked || issue.isPullRequest) {
      summaryLine(out, `#${n}: skipped, the issue is no longer open.`);
      return;
    }
    const published = releases(repo);
    const facts = {
      issue,
      duplicate: decision?.duplicateOf ? issueFacts(repo, decision.duplicateOf) : null,
      fix: decision?.fixCommit ? fixFacts(decision.fixCommit, published) : null,
      canTransfer: Boolean(process.env.TRANSFER_TOKEN),
    };
    const ctx = {
      repoLabels: new Set((ghJson(["label", "list", "--repo", repo, "--limit", "200", "--json", "name"]) ?? []).map((l) => l.name)),
      latestRelease: published.find((r) => r.isLatest)?.tagName ?? null,
    };
    const resolved = resolveDecision(decision, facts, mode);
    const plan = planActions(resolved, decision, ctx);

    summaryLine(out, [
      `### #${n}: ${resolved.outcome}${resolved.proposed && resolved.proposed !== resolved.outcome ? ` (proposed ${resolved.proposed})` : ""}`,
      "",
      `Mode \`${mode}\`. Confidence ${decision?.confidence ?? "n/a"}. Cost ${cost == null ? "n/a" : `$${cost.toFixed(3)}`}, ${turns ?? "?"} turns.`,
      resolved.reason ? `\nNot applied as proposed: ${resolved.reason}.` : "",
      "",
      "```json",
      JSON.stringify({ decision, plan }, null, 2),
      "```",
      "",
    ].join("\n"));

    if (mode === "dry-run") return;
    applyPlan(repo, n, plan);
    return;
  }

  console.error("usage: issue-triage.mjs targets | prompt <n> | apply <n> <agent.json>");
  process.exit(2);
}

const invokedAsScript = process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href;
if (invokedAsScript) main();
