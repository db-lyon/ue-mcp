/**
 * The gates a release has to clear, as checks instead of as rules to remember.
 *
 * Each one replaced a note that said "always do X before bumping". A note is
 * only as good as whoever read it last, and every one of these was broken at
 * least once by somebody who had read it.
 *
 * Usage:
 *   node scripts/check-release-gates.mjs version-delta   patch-only bumps
 *   node scripts/check-release-gates.mjs count-markers   stamped counts are current
 *   node scripts/check-release-gates.mjs docs-freshness  handlers moved, docs did not
 *   node scripts/check-release-gates.mjs all             every gate above
 */
import fs from "node:fs";
import path from "node:path";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

const git = (...args) =>
  execFileSync("git", args, { cwd: REPO, encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] }).trim();

/** A semver as numbers, prerelease tail kept separate. */
export function parseVersion(raw) {
  const m = /^(\d+)\.(\d+)\.(\d+)(?:-(.+))?$/.exec(raw.trim());
  if (!m) return null;
  return { major: +m[1], minor: +m[2], patch: +m[3], prerelease: m[4] ?? null, raw: raw.trim() };
}

/**
 * Whether a version move is allowed without somebody saying so.
 *
 * Patch-only, in both directions of the rule: the major and minor must not
 * move, and the patch must not go backwards. A prerelease on the same X.Y.Z is
 * allowed, because that is how a beta is cut.
 *
 * Returns null when the move is fine, or a sentence saying what is wrong.
 */
export function versionDeltaProblem(fromRaw, toRaw) {
  const from = parseVersion(fromRaw);
  const to = parseVersion(toRaw);
  if (!to) return `'${toRaw}' is not a version this project can publish.`;
  if (!from) return null; // Nothing to compare against, so nothing to refuse.
  if (from.raw === to.raw) return null;

  if (to.major !== from.major) {
    return (
      `Version went from ${from.raw} to ${to.raw}, which moves the MAJOR. `
      + "Bumps in this repo are patch-only unless the owner says otherwise, so this needs "
      + "their sign-off rather than a merge."
    );
  }
  if (to.minor !== from.minor) {
    return (
      `Version went from ${from.raw} to ${to.raw}, which moves the MINOR. `
      + "Bumps in this repo are patch-only unless the owner says otherwise, so this needs "
      + "their sign-off rather than a merge."
    );
  }
  if (to.patch < from.patch) {
    return `Version went backwards, from ${from.raw} to ${to.raw}.`;
  }
  // A prerelease sorts BELOW the release of the same number, so cutting
  // 1.3.5-beta.1 after 1.3.5 is published puts it behind what is already out.
  // A beta belongs on the next patch.
  if (to.prerelease && !from.prerelease && to.patch === from.patch) {
    return (
      `Version went from ${from.raw} to ${to.raw}, which sorts BELOW the version already `
      + "published. A prerelease belongs on the next patch, not on the one that shipped."
    );
  }
  if (to.patch > from.patch + 1) {
    return (
      `Version skipped from ${from.raw} to ${to.raw}. `
      + "Every published number is burned permanently, so a skip is either a mistake or a "
      + "number nobody can ever use."
    );
  }
  return null;
}

/**
 * What this branch is being compared against, and why.
 *
 * Three situations, and the obvious answer is wrong in two of them.
 *
 *   pull request  the base branch, so the gate sees the delta the merge
 *                 introduces
 *   push to main  the PREVIOUS commit on main. origin/main is HEAD here, so
 *                 comparing against it compares a version to itself and every
 *                 gate passes whatever the bump was
 *   local branch  the merge base with main, so a stale local main does not
 *                 make an untouched version look changed
 *
 * Returns null only when there genuinely is no baseline, which is a fresh
 * repository with one commit.
 */
export function baselineRef(env = process.env, run = git) {
  const tryRev = (ref) => {
    try {
      run("rev-parse", "--verify", "--quiet", `${ref}^{commit}`);
      return ref;
    } catch {
      return null;
    }
  };

  if (env.GITHUB_BASE_REF) {
    return tryRev(`origin/${env.GITHUB_BASE_REF}`) ?? tryRev(env.GITHUB_BASE_REF);
  }

  const head = (() => {
    try {
      return run("rev-parse", "--abbrev-ref", "HEAD");
    } catch {
      return "";
    }
  })();

  if (head === "main" || env.GITHUB_REF === "refs/heads/main") {
    return tryRev("HEAD~1");
  }

  for (const main of ["origin/main", "main"]) {
    if (!tryRev(main)) continue;
    try {
      return run("merge-base", main, "HEAD");
    } catch {
      return main;
    }
  }
  return null;
}

function versionAt(ref) {
  try {
    return JSON.parse(git("show", `${ref}:package.json`)).version;
  } catch {
    return null;
  }
}

function checkVersionDelta() {
  const here = JSON.parse(fs.readFileSync(path.join(REPO, "package.json"), "utf8")).version;
  const base = baselineRef();
  const there = base === null ? null : versionAt(base);
  if (there === null) {
    // Loud, not silent. A gate that cannot find its baseline used to report
    // "skipped" and exit 0, which is indistinguishable from passing.
    console.error("version-delta   - no baseline commit to compare against, so the bump is unchecked.");
    console.error("    On a pull request this means the checkout had no history: set fetch-depth: 0.");
    return 1;
  }
  const problem = versionDeltaProblem(there, here);
  if (problem) {
    console.error(`version-delta   - ${problem}`);
    return 1;
  }
  console.log(`version-delta   - ${there} to ${here}, allowed`);
  return 0;
}

/**
 * The stamped counts in CLAUDE.md and the docs are generated. If regenerating
 * them changes anything, they were stamped from a surface that has since moved
 * and every number a reader trusts is stale.
 */
function checkCountMarkers() {
  // Compared before and after rather than demanding a clean tree: this runs
  // mid-branch, where an untracked file is normal and says nothing about
  // whether the stamped numbers are current.
  const before = new Set(git("status", "--porcelain").split("\n").filter(Boolean));
  try {
    execFileSync("npm", ["run", "--silent", "generate:metadata"], {
      cwd: REPO,
      stdio: ["ignore", "pipe", "pipe"],
      shell: process.platform === "win32",
    });
  } catch (e) {
    console.error(`count-markers   - could not regenerate: ${e.message}`);
    return 1;
  }
  const after = git("status", "--porcelain").split("\n").filter(Boolean);
  const moved = after.filter((line) => !before.has(line));
  if (moved.length > 0) {
    console.error(
      "count-markers   - regenerating changed these, so the stamped numbers were stale:\n"
        + moved.map((l) => `      ${l}`).join("\n")
        + "\n    Run `npm run generate:metadata` and commit what it changes.",
    );
    return 1;
  }
  console.log("count-markers   - stamped counts match the surface");
  return 0;
}

/**
 * Docs go stale silently, and stale docs is the complaint that comes back
 * most. Handlers are where the user-visible surface lives, so a change there
 * with no docs change in the same range is the shape of the problem.
 */
export function docsFreshnessProblem(changedFiles) {
  const touchesHandlers = changedFiles.some(
    (f) => /^plugin\/.*Handlers.*\.(cpp|h)$/.test(f) || /^src\/tools\/.*\.ts$/.test(f),
  );
  if (!touchesHandlers) return null;
  const touchesDocs = changedFiles.some((f) => f.startsWith("docs/"));
  if (touchesDocs) return null;
  return (
    "Handler or tool sources changed and nothing under docs/ did. The published docs are "
    + "the thing users read; a surface change that does not reach them is the stale-docs "
    + "complaint that keeps coming back. Update docs/, or say in the PR why this change is "
    + "invisible to a reader."
  );
}

function checkDocsFreshness() {
  const base = process.env.GITHUB_BASE_REF ? `origin/${process.env.GITHUB_BASE_REF}` : "origin/main";
  let changed;
  try {
    changed = git("diff", "--name-only", `${base}...HEAD`).split("\n").filter(Boolean);
  } catch {
    console.log("docs-freshness  - no base to compare against, skipped");
    return 0;
  }
  if (changed.length === 0) {
    console.log("docs-freshness  - nothing changed");
    return 0;
  }
  const problem = docsFreshnessProblem(changed);
  if (problem) {
    console.error(`docs-freshness  - ${problem}`);
    return 1;
  }
  console.log("docs-freshness  - ok");
  return 0;
}

const GATES = {
  "version-delta": checkVersionDelta,
  "count-markers": checkCountMarkers,
  "docs-freshness": checkDocsFreshness,
};

function main(argv) {
  const which = argv[0] ?? "all";
  const names = which === "all" ? Object.keys(GATES) : [which];
  let bad = 0;
  for (const name of names) {
    const gate = GATES[name];
    if (!gate) {
      console.error(`No such gate: ${name}. Known: ${Object.keys(GATES).join(", ")}, all`);
      return 2;
    }
    bad += gate();
  }
  return bad > 0 ? 1 : 0;
}

if (process.argv[1]?.endsWith("check-release-gates.mjs")) {
  process.exit(main(process.argv.slice(2)));
}
