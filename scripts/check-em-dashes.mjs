/**
 * Style gate: no em dash (U+2014) in tracked files or in commit messages.
 *
 * CLAUDE.md has banned the character in public artifacts for a long time, and
 * the ban held only as long as someone remembered it. One cleanup pass removed
 * 695 of them from the tree, and two commits landing during that same pass put
 * new ones back into tests/smoke/. Nothing noticed. This is the thing that
 * notices.
 *
 * Scope decision: the whole tracked tree, not the added lines of a diff.
 * A diff-scoped check passes the moment the offending commit is rebased,
 * squashed, or merged from a branch that forked before the check existed, and
 * it can never tell you the tree is clean today. A tree scan states an
 * invariant, and the invariant holds on every commit that CI sees.
 *
 * Two escape hatches, both deliberately narrow:
 *
 *   1. ALLOW MARKER. A line carrying the marker `em-dash-allowed` (on the line
 *      itself or on the line directly above it) is exempt. This is for the
 *      handful of places where the character is the data: the rule statement in
 *      CLAUDE.md, the sanitiser regexes that strip it out of Epic's catalog
 *      text, and the fixture plus negative assertion that prove the sanitiser
 *      works. It is per line, so it exempts the one literal that needs
 *      exempting and nothing else in the file. Grep the marker to see every
 *      exemption in the repo.
 *
 *   2. EXCLUDED_PATHS. Whole paths that this repo does not author. Each entry
 *      carries its reason in the table below. Adding one is a visible,
 *      reviewable change to this file. Nothing here is excluded by wildcard.
 *
 * No shebang: vitest re-imports this file and its loader rejects the shebang
 * line as "Invalid or unexpected token".
 *
 * Usage:
 *   node scripts/check-em-dashes.mjs                      # tracked tree
 *   node scripts/check-em-dashes.mjs --commits A..B       # commit messages too
 *   node scripts/check-em-dashes.mjs --files a.ts b.md    # named files only
 *   node scripts/check-em-dashes.mjs --explain            # print the policy
 *
 * Exit 0 clean, 1 on any finding, 2 on bad usage.
 */
import { execFileSync } from "node:child_process";
import { readFileSync, statSync } from "node:fs";
import { pathToFileURL } from "node:url";

/**
 * The character under ban, written as an escape so this file is not itself a
 * finding and does not need its own exemption.
 */
export const EM_DASH = "\u2014";

/** Per-line opt-out, matched literally anywhere on a line. */
export const ALLOW_MARKER = "em-dash-allowed";

/**
 * Paths this gate does not read, each with the reason it does not.
 *
 * `path` is matched as an exact file path or as a directory prefix. These are
 * the only two, and both are content the repo receives rather than writes.
 */
export const EXCLUDED_PATHS = [
  {
    path: "assets/epic-catalog.snapshot.json",
    reason:
      "Harvested snapshot of Epic's tool catalog. The em dashes are upstream prose we do " +
      "not author; src/epic-enrich.ts strips them at load, which is what the sanitiser " +
      "regex and its test exist for. Editing the snapshot to please a style rule would " +
      "make it stop matching the engine it came from.",
  },
  {
    path: "tests/ue_mcp/Content/Python/",
    reason:
      "Superseded copy of an older bridge source tree carried inside the test project. " +
      "plugin/ue_mcp_bridge/ is the source of truth and is covered by this gate. Rewriting " +
      "a dead copy would produce 40 files of diff noise and protect nothing.",
  },
];

/** True when `file` is excluded by the table above. */
export function isExcluded(file) {
  const p = file.replace(/\\/g, "/");
  return EXCLUDED_PATHS.some((e) => (e.path.endsWith("/") ? p.startsWith(e.path) : p === e.path));
}

/**
 * Every em dash in `text` that is not covered by an allow marker.
 *
 * Returns `{ line, column, text }` per finding, 1-based, so the caller can
 * print something a person can click.
 */
export function scanText(text) {
  const lines = text.split(/\r?\n/);
  const findings = [];
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    if (!line.includes(EM_DASH)) continue;
    const allowed = line.includes(ALLOW_MARKER) || (i > 0 && lines[i - 1].includes(ALLOW_MARKER));
    if (allowed) continue;
    let col = line.indexOf(EM_DASH);
    while (col !== -1) {
      findings.push({ line: i + 1, column: col + 1, text: line.trim() });
      col = line.indexOf(EM_DASH, col + 1);
    }
  }
  return findings;
}

/** Files git is tracking, minus the excluded paths. */
export function trackedFiles(cwd = process.cwd()) {
  const out = execFileSync("git", ["ls-files", "-z"], { cwd, maxBuffer: 64 * 1024 * 1024 });
  return out
    .toString("utf8")
    .split("\0")
    .filter((f) => f.length > 0 && !isExcluded(f));
}

/**
 * Reads a file as UTF-8 text, or returns null when it should not be scanned.
 *
 * Anything holding a NUL byte is binary (the repo tracks .uasset, .png, LFS
 * pointers and friends), and a byte sequence that is not UTF-8 cannot contain
 * U+2014 in the encoding this gate is about.
 */
export function readTextFile(path) {
  let buf;
  try {
    if (statSync(path).isDirectory()) return null;
    buf = readFileSync(path);
  } catch {
    return null; // deleted between ls-files and here, or unreadable
  }
  if (buf.includes(0)) return null;
  return buf.toString("utf8");
}

/** Scans the given files. Returns `{ file, line, column, text }` findings. */
export function scanFiles(files, cwd = process.cwd()) {
  const findings = [];
  for (const file of files) {
    const text = readTextFile(`${cwd}/${file}`);
    if (text === null) continue;
    for (const f of scanText(text)) findings.push({ file, ...f });
  }
  return findings;
}

/**
 * Scans the commit messages in a git range.
 *
 * The tree scan cannot see these: a commit message is not a file, and it is
 * exactly where the character slips through, because nobody re-reads a message
 * after typing it.
 */
export function scanCommitMessages(range, cwd = process.cwd()) {
  const shas = execFileSync("git", ["rev-list", range], { cwd })
    .toString("utf8")
    .split("\n")
    .map((s) => s.trim())
    .filter(Boolean);
  const findings = [];
  for (const sha of shas) {
    const msg = execFileSync("git", ["log", "-1", "--format=%B", sha], { cwd }).toString("utf8");
    for (const f of scanText(msg)) {
      findings.push({ file: `commit ${sha.slice(0, 12)} (message)`, ...f });
    }
  }
  return findings;
}

function explain() {
  console.log(`Style gate: the em dash (U+2014) is banned in tracked files and commit messages.`);
  console.log(`Replace it with " - ", a colon, parentheses, or two sentences.\n`);
  console.log(`Per-line exemption: put "${ALLOW_MARKER}" on the line or the line above it.`);
  console.log(`Use it only where the character is the data being handled.\n`);
  console.log(`Excluded paths:`);
  for (const e of EXCLUDED_PATHS) console.log(`  ${e.path}\n    ${e.reason}`);
}

function main() {
  const args = process.argv.slice(2);
  let range = null;
  let named = null;
  for (let i = 0; i < args.length; i++) {
    if (args[i] === "--explain") {
      explain();
      return;
    } else if (args[i] === "--commits") {
      range = args[++i];
      if (!range) {
        console.error("::error::--commits needs a git range, for example origin/main..HEAD");
        process.exit(2);
      }
    } else if (args[i] === "--files") {
      named = args.slice(i + 1).filter((f) => !isExcluded(f));
      break;
    } else {
      console.error(`::error::Unknown argument '${args[i]}'. See --explain.`);
      process.exit(2);
    }
  }

  const files = named ?? trackedFiles();
  const findings = scanFiles(files);
  if (range) findings.push(...scanCommitMessages(range));

  if (findings.length === 0) {
    const scope = range ? `${files.length} tracked files and commits in ${range}` : `${files.length} tracked files`;
    console.log(`No em dashes in ${scope}.`);
    return;
  }

  for (const f of findings) {
    console.error(`::error file=${f.file},line=${f.line},col=${f.column}::em dash (U+2014)`);
    console.error(`${f.file}:${f.line}:${f.column}: ${f.text}`);
  }
  console.error(
    `\n${findings.length} em dash(es) found. Replace with " - ", a colon, or two sentences.` +
      `\nIf the character is genuinely the data, mark that line with "${ALLOW_MARKER}".` +
      `\nRun 'npm run audit:em-dash -- --explain' for the policy.`
  );
  process.exit(1);
}

const invokedAsScript = process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href;
if (invokedAsScript) main();
