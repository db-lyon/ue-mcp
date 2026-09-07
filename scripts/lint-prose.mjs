/**
 * One prose gate over everything this project publishes.
 *
 * Commit messages, docs/, src/, dist/ and release bodies all reach somebody
 * outside this repo. dist/ is in that list because it is how a leak escaped
 * once: the string was taken out of src/ and shipped anyway in a generated
 * .d.ts that nothing was checking.
 *
 * Every rule here used to be a rule somebody had to remember. A rule nobody
 * can grep is a rule that comes back, so each one is a check with a test
 * behind it.
 *
 * EXEMPTIONS. A line carrying `lint-prose-allow: <rule-id>` (on the line
 * itself, or on the line directly above it) is exempt from that one rule on
 * that one line. It exists because some text has to contain the thing it
 * bans: a rule that forbids a character has to show the character, and a list
 * that stops a command running has to name the command. Grep the marker to
 * see every exemption in the tree.
 *
 * Usage:
 *   node scripts/lint-prose.mjs --all                 every tracked file
 *   node scripts/lint-prose.mjs --files a.ts b.md     these files
 *   node scripts/lint-prose.mjs --commit-msg FILE     a commit message
 *   node scripts/lint-prose.mjs --release-body FILE   a release body
 *   node scripts/lint-prose.mjs --explain             what each rule is for
 */
import fs from "node:fs";
import path from "node:path";
import { execFileSync } from "node:child_process";

export const ALLOW_MARKER = "lint-prose-allow";

/**
 * Where a rule applies.
 *
 * `tracked` is everything. The narrower scopes exist because two of these
 * rules would otherwise fire on the code that implements them.
 */
const SCOPES = {
  published: (rel) => /^(docs|src|dist)\//.test(rel),
  docs: (rel) => /^docs\//.test(rel),
  scripts: (rel) => /^scripts\//.test(rel),
  code: (rel) => /^(scripts|src)\//.test(rel),
  tracked: () => true,
};

/**
 * Competitor and comparison projects.
 *
 * Split by what a false positive costs. Handles are unambiguous and match
 * anywhere, case-insensitively. The two that are also ordinary English words
 * match only capitalised and only whole, so "a monolithic handler" and "the
 * flop case" stay writable.
 */
const COMPETITOR_HANDLES = [
  "chongdashu",
  "GenOrca",
  "runreal",
  "mcp-unreal",
  "ChiR24",
  "unreal-analyzer",
  "Soverance",
  "Claireon",
];
const COMPETITOR_WORDS = ["Monolith", "Flop"];

const WORD = /[A-Za-z0-9]/;

/** A whole-word hit, without building a regex out of the needle. */
function containsWord(haystack, needle, { caseSensitive = false } = {}) {
  const hay = caseSensitive ? haystack : haystack.toLowerCase();
  const nee = caseSensitive ? needle : needle.toLowerCase();
  let from = 0;
  for (;;) {
    const i = hay.indexOf(nee, from);
    if (i === -1) return false;
    const before = i === 0 ? "" : hay[i - 1];
    const after = hay[i + nee.length] ?? "";
    // A hyphen inside the needle itself is part of the name, so only
    // alphanumerics either side disqualify a hit.
    if (!WORD.test(before || " ") && !WORD.test(after || " ")) return true;
    from = i + 1;
  }
}

/**
 * Session URLs and trailers, for any vendor.
 *
 * A session link is a private transcript. It names who wrote the change and
 * with what, it rots the moment the session is gone, and it means nothing to
 * anybody reading the history later.
 */
const SESSION_VENDORS = ["claude", "chatgpt", "openai", "copilot", "cursor", "gemini", "codex"];

function looksLikeSessionLink(line) {
  for (const v of SESSION_VENDORS) {
    // The word has to be IN the URL, not merely on the same line. Requiring
    // only that both appear somewhere flagged ordinary prose about MCP
    // clients, which mentions a vendor domain and the word "session" in one
    // breath all the time.
    if (new RegExp(`${v}\\.(?:ai|com)/[^\\s)"']*session`, "i").test(line)) return `${v} session url`;
    if (new RegExp(`^\\s*\\*?\\s*${v}[-_ ]session\\s*:`, "i").test(line)) return `${v} session trailer`;
  }
  if (/\bsession_[0-9A-Za-z]{16,}\b/.test(line)) return "session id";
  return null;
}

export const RULES = [
  {
    id: "em-dash",
    scope: "tracked",
    why: "Em dashes. Use a spaced hyphen, a colon, parentheses, or two sentences.",
    // A double hyphen is deliberately NOT banned here. `npm run x -- --flag` is
    // how npm forwards arguments, so the sequence is unavoidable in any doc
    // that shows a command, and banning it flags 300 lines of correct CLI.
    legacyMarker: "em-dash-allowed",
    test: (line) => (line.includes("—") ? "em dash" : null),
  },
  {
    id: "tier",
    scope: "tracked",
    why: 'The word "tier" describes nothing. Say "the live tests", "the unit tests".',
    test: (line) => (/\btiers?\b/i.test(line) ? "the word tier" : null),
  },
  {
    id: "competitor",
    scope: "tracked",
    why: "Never name a competitor or comparison project in anything public. Describe the work on its own terms.",
    test: (line) => {
      // A handle matches through its own separators and through casing, so
      // `unreal-analyzer`, `unreal_analyzer` and "Unreal Analyzer" all count,
      // and it matches when something follows it, so `runrealClient` counts
      // too. It must still START at a boundary: flattening the whole line
      // instead made `binDir, "UnrealEditor"` contain one of these names
      // across the gap between two unrelated words.
      for (const h of COMPETITOR_HANDLES) {
        const body = h.replace(/[^A-Za-z0-9]+/g, "[-_ ]?");
        // Start of line, or after a non-alphanumeric.
        if (new RegExp(`(^|[^A-Za-z0-9])${body}`, "i").test(line)) return h;
        // Or the start of a camelCase word: syncWithChongdashuBridge. This one
        // is case-SENSITIVE on the first letter, so it cannot fire across the
        // gap between two unrelated lowercase words the way a flattened match
        // did.
        const camel = body.replace(/^./, (c) => c.toUpperCase());
        if (new RegExp(`[a-z0-9]${camel}`).test(line)) return h;
      }
      // These two are also ordinary English, so they match as whole words
      // only. Any casing counts: a heading is as public as a sentence. The
      // cost is the occasional false positive on GPU prose, which is one
      // marker; the cost of missing one is a competitor named in a release.
      for (const w of COMPETITOR_WORDS) if (containsWord(line, w)) return w;
      return null;
    },
  },
  {
    id: "session-link",
    scope: "tracked",
    why: "Agent session URLs and session trailers. Private, vendor specific, and dead the moment the session is.",
    test: looksLikeSessionLink,
  },
  {
    id: "dead-ref",
    scope: "docs",
    why: "Published docs must not point at something a reader cannot open: plans/, comms/, scratch/, or anything gitignored.",
    test: (line) => {
      // A repository-root path, in either separator, in either case. What must
      // NOT match is the same word deeper in some other path: a content folder
      // (/Game/scratch/), a URL (example.com/comms/) and a nested doc path
      // (roadmaps/plans/overview) are all things a reader can open.
      for (const dir of ["plans", "comms", "scratch"]) {
        const re = new RegExp(`(^|[\\s(\`"'])(\\./)?${dir}[/\\\\]`, "i");
        if (re.test(line)) return `${dir}/`;
      }
      if (containsWord(line, "gitignored")) return "gitignored";
      return null;
    },
  },
  {
    id: "range-hedge",
    scope: "tracked",
    why: "The engine range is 5.4 to 5.8. Never annotate it with which versions were compiled, tested or gated.",
    // Deliberately narrow, and it will miss rewordings.
    //
    // The obvious generalisation, any assurance verb near a version number,
    // cannot tell a hedge about the SUPPORTED RANGE from a per-feature version
    // requirement, and the second is necessary documentation: "timing regions
    // need UE 5.7 or newer" has to be sayable, as does a comment explaining
    // which engine a code path is gated at. A rule that flags those trains
    // people to add markers without reading, which is worse than a rule with
    // gaps.
    test: (line) => {
      const lower = line.toLowerCase();
      if (!/5\.[4-8]\s*(to|-|through|and)\s*5\.[4-8]/.test(lower)) return null;
      // Only when the SPAN itself is being qualified.
      for (const hedge of [
        "compiled and verified",
        "compiled and tested",
        "built and tested",
        "tested on",
        "verified on",
        "compiled for",
        "compiled on",
        "version-gated but not",
        "gated but not built",
        "but not built",
        "untested",
        "unverified",
      ]) {
        if (lower.includes(hedge)) return hedge;
      }
      return null;
    },
  },
  {
    id: "no-kill",
    // Not just scripts/. src/ launches and stops editors too, and a kill there
    // reaches exactly the same processes.
    scope: "code",
    why: "Never kill a process the user is running. Ask the editor to close, addressed to one project.",
    test: (line) => {
      if (containsWord(line, "taskkill")) return "taskkill";
      if (/\bStop-Process\b/i.test(line)) return "Stop-Process";
      if (/\bpkill\b/.test(line)) return "pkill";
      if (/\bkill\s+-9\b/.test(line)) return "kill -9";
      // Signal 0 sends nothing: it is the standard way to ask whether a
      // process id is still alive, which is the opposite of killing it.
      const kills = /\bprocess\.kill\s*\(/.test(line);
      const probesLiveness = /\bprocess\.kill\s*\([^,)]*,\s*0\s*\)/.test(line);
      if (kills && !probesLiveness) return "process.kill";
      return null;
    },
  },
  {
    id: "no-hot-reload",
    // scripts/ only. src/ names these because the server EXPOSES them as
    // actions, which is not the same as using one to decide a build is good.
    scope: "scripts",
    why: "Never validate a build with Live Coding or a hot reload. A full build with the editor stopped is the authoritative one.",
    test: (line) => {
      for (const bad of ["live_coding_compile", "hot_reload"]) {
        if (containsWord(line, bad)) return bad;
      }
      return null;
    },
  },
];

/**
 * Whole paths this repo does not author, or authors only as a dead copy.
 *
 * An exclusion is a path plus a reason, so the table reads as an argument
 * rather than a list. Anything not on it is in scope.
 */
export const EXCLUDED_PATHS = [
  {
    path: "assets/epic-catalog.snapshot.json",
    reason:
      "Harvested snapshot of Epic's tool catalog. The prose in it is upstream and not ours; "
      + "src/epic-enrich.ts strips what needs stripping at load, which is what the sanitiser "
      + "and its test exist for. Editing the snapshot to please a style rule would make it "
      + "stop matching the engine it came from.",
  },
  {
    path: "tests/ue_mcp/Content/Python/",
    reason:
      "Superseded copy of an older bridge source tree carried inside the test project. "
      + "plugin/ue_mcp_bridge/ is the source of truth and is covered. Rewriting a dead copy "
      + "would produce 40 files of diff noise and protect nothing.",
  },
  {
    path: "MEMORIES.md",
    reason:
      "A verbatim dump of retired agent notes, kept as the record of what each rule said "
      + "before it became a check. Rewriting quoted history to satisfy the rules it "
      + "describes would destroy the only evidence of where the rules came from.",
  },
  {
    path: "scripts/lint-prose.mjs",
    reason:
      "This file. It names every banned string, because a rule that forbids a word has to "
      + "hold the word somewhere. Exempting the whole file rather than marking forty lines "
      + "keeps the rule table readable; the exemption is two paths wide and both are here.",
  },
  {
    path: "tests/unit/lint-prose.test.ts",
    reason:
      "The proof. Each rule is tested against a deliberate violation, so the fixtures are "
      + "themselves violations by construction. A test that could not hold one would be a "
      + "test that never saw the rule fire.",
  },
  {
    path: "tests/golden/",
    reason:
      "Recordings of the advertised surface, produced by npm run golden:record and never "
      + "hand-edited. Their prose comes from src/tools/, which is linted; editing a recording "
      + "to please a style rule would make it stop matching what the server actually serves.",
  },
  {
    path: "dist/ue-mcp.default.yml",
    reason:
      "Generated by scripts/generate-default-config.ts from src/tools/, which is linted. The "
      + "allow markers in that source are line comments and do not survive into the generated "
      + "YAML, so the gate belongs on the source and not on its output.",
  },
  {
    path: "dist/tool-counts.json",
    reason:
      "Generated by scripts/generate-tool-metadata.ts from the tool graph. Numbers and action "
      + "names, with no authored prose in it.",
  },
  {
    path: "docs/tool-reference.md",
    reason:
      "Generated by scripts/generate-tool-metadata.ts from src/tools/, which is linted. "
      + "Editing it directly is overwritten by the next generate, so the gate belongs on the "
      + "source and not on its output.",
  },
];

/** True when `file` is excluded by the table above. */
export function isExcluded(file) {
  const p = file.split("\\").join("/");
  return EXCLUDED_PATHS.some((e) => (e.path.endsWith("/") ? p.startsWith(e.path) : p === e.path));
}

const BINARY = /\.(png|jpg|jpeg|gif|ico|pdf|zip|uasset|umap|dll|pdb|lib|exp|ttf|woff2?)$/i;

function trackedFiles() {
  return execFileSync("git", ["ls-files"], { encoding: "utf8" })
    .split("\n")
    .filter((f) => f && !BINARY.test(f) && !isExcluded(f));
}

/** Every violation in one blob, as {rule, why, line, text, found}. */
export function lintText(text, { rel = "", only = null } = {}) {
  const lines = text.split(/\r?\n/);
  const rules = RULES.filter((r) => {
    if (only) return only.includes(r.id);
    // No path means a commit message or a release body: those are prose, so
    // only the rules that apply to prose everywhere apply to them.
    return rel === "" ? r.scope === "tracked" : SCOPES[r.scope](rel);
  });
  const out = [];
  for (const rule of rules) {
    // The em-dash rule predates this file and its exemptions are already
    // written with their own marker. Honour both rather than rewriting them.
    const markers = [`${ALLOW_MARKER}: ${rule.id}`];
    if (rule.legacyMarker) markers.push(rule.legacyMarker);
    const exempt = (l) => l !== undefined && markers.some((m) => l.includes(m));
    for (let i = 0; i < lines.length; i++) {
      const line = lines[i];
      if (exempt(line) || (i > 0 && exempt(lines[i - 1]))) continue;
      const found = rule.test(line);
      if (found) out.push({ rule: rule.id, why: rule.why, line: i + 1, text: line.trim(), found });
    }
  }
  return out;
}

function report(where, findings) {
  for (const f of findings) {
    console.error(`${where}:${f.line}  [${f.rule}] ${f.found}`);
    console.error(`    ${f.text.slice(0, 160)}`);
    console.error(`    ${f.why}`);
  }
}

function main(argv) {
  if (argv.includes("--explain")) {
    for (const r of RULES) console.log(`${r.id.padEnd(14)} (${r.scope})  ${r.why}`);
    console.log(`\nExempt one line with a comment carrying: ${ALLOW_MARKER}: <rule-id>`);
    return 0;
  }

  let total = 0;
  const one = (file, opts) => {
    let text;
    try {
      text = fs.readFileSync(file, "utf8");
    } catch {
      return;
    }
    const findings = lintText(text, opts);
    total += findings.length;
    report(opts.label ?? file, findings);
  };

  const at = (flag) => {
    const i = argv.indexOf(flag);
    return i === -1 ? null : argv[i + 1];
  };

  // What is being committed, not what is on disk. The hook used to pass staged
  // PATHS and read each file from the working tree, so a violation staged and
  // then edited away in the working copy sailed through and was committed.
  if (argv.includes("--staged")) {
    const names = execFileSync("git", ["diff", "--cached", "--name-only", "--diff-filter=ACMR"], {
      encoding: "utf8",
    })
      .split(/\r?\n/)
      .filter(Boolean);
    for (const rel of names) {
      if (BINARY.test(rel) || isExcluded(rel)) continue;
      let text;
      try {
        text = execFileSync("git", ["show", `:${rel}`], { encoding: "utf8", maxBuffer: 64 * 1024 * 1024 });
      } catch {
        continue;
      }
      const findings = lintText(text, { rel });
      total += findings.length;
      report(rel, findings);
    }
    if (total > 0) {
      console.error(`\n${total} prose violation${total === 1 ? "" : "s"} in what is staged.`);
      return 1;
    }
    console.log("lint:prose - clean");
    return 0;
  }

  const commitMsg = at("--commit-msg");
  const releaseBody = at("--release-body");

  if (commitMsg) {
    one(commitMsg, { rel: "", label: "commit message" });
  } else if (releaseBody) {
    one(releaseBody, { rel: "", label: "release body" });
  } else if (argv.includes("--files")) {
    const given = argv.slice(argv.indexOf("--files") + 1).filter((a) => !a.startsWith("--"));
    for (const f of given) {
      const rel = f.split(path.sep).join("/");
      if (!BINARY.test(f) && !isExcluded(rel) && fs.existsSync(f)) one(f, { rel });
    }
  } else {
    for (const f of trackedFiles()) one(f, { rel: f });
  }

  if (total > 0) {
    console.error(
      `\n${total} prose violation${total === 1 ? "" : "s"}. `
        + "Run `node scripts/lint-prose.mjs --explain` for what each rule is for, "
        + `or exempt one line with \`${ALLOW_MARKER}: <rule-id>\`.`,
    );
    return 1;
  }
  console.log("lint:prose - clean");
  return 0;
}

if (process.argv[1]?.endsWith("lint-prose.mjs")) {
  process.exit(main(process.argv.slice(2)));
}
