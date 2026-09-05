/**
 * Cumulative release notes for a stable version that had prereleases.
 *
 * The release flow gives every version its own hand-authored notes file, which
 * is right for a prerelease: someone reading the 1.2.0-beta.3 page wants what
 * changed since beta.2, not a growing wall of everything the line has shipped.
 * It is wrong for the stable release those betas lead up to. Written the same
 * way, the 1.2.0 page describes the last few days of work and buries the
 * months the betas carried, and the betas are prereleases so they never take
 * the "Latest" badge or answer /releases/latest. The work becomes invisible.
 *
 * So a stable release's notes are the union of every prerelease of the same
 * X.Y.Z, plus whatever landed after the last one. This script builds that
 * union from published releases:
 *
 *   node scripts/compose-release-notes.mjs --version 1.2.0 \
 *     --notes-file since-last-beta.md --out /tmp/v1.2.0.md
 *
 * What it merges:
 *
 *   - Bodies, section by section, on the `### Heading` level the repo already
 *     uses (Server, Bug fixes, Internals, Multi-editor, Bridge protocol, and
 *     anything else that shows up). Heading order is order of first
 *     appearance; a section that only exists in the newest input lands at the
 *     end.
 *   - Bullets, deduplicated. A fix often gets reworded between betas, so
 *     matching the text alone is not enough: two bullets are also the same
 *     bullet when they cite the same issue or PR number. The longer variant
 *     wins, because it is usually the one carrying the detail. Nothing fuzzier
 *     than that, on purpose. A bullet that merely reads like another one is
 *     kept, and a human deleting a duplicate is cheap next to this script
 *     silently eating a shipped change.
 *   - Headline frontmatter, from the union of the prerelease headlines. The
 *     published body has its frontmatter stripped by CI, so the headline is
 *     read back from the `landing/headline` commit status CI posts on the
 *     tagged commit, which is the validated, ` · `-joined copy.
 *
 * Dedupe is global rather than per section, since the beta that called
 * something a bug fix and the beta that called it a server change are still
 * describing one change. The first section it appeared in is the one it keeps.
 *
 * A stable release with no prereleases is a supported case: there is nothing
 * to merge, so the local notes file passes straight through and is validated.
 *
 * The composed file is run through scripts/release-headline.mjs before this
 * script exits, so a body it produced cannot fail the publish gate later.
 *
 * No shebang: vitest re-imports this file and its loader rejects the shebang
 * line as "Invalid or unexpected token". Invoke as
 * `node scripts/compose-release-notes.mjs ...` or `npm run release:notes --`.
 */
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { execFileSync } from "node:child_process";
import { fileURLToPath, pathToFileURL } from "node:url";

import { parseVersion } from "./release-version.mjs";
import {
  ITEM_RE,
  MAX_ITEMS,
  MAX_ITEM_LEN,
  MAX_JOINED_LEN,
  MIN_ITEMS,
  MIN_ITEM_LEN,
  SEP,
  ValidationError,
  processBody,
  splitFrontmatter,
  parseHeadlineArray,
} from "./release-headline.mjs";

/** The commit status CI posts with the validated, joined headline. */
export const HEADLINE_STATUS_CONTEXT = "landing/headline";

export class ComposeError extends Error {}

/* ------------------------------------------------------------------ *
 * Version ordering
 * ------------------------------------------------------------------ */

/** Strips a leading `v` from a tag so it can be parsed as a version. */
export function versionOfTag(tag) {
  return String(tag).trim().replace(/^v/, "");
}

/**
 * Semver precedence for two prerelease suffixes, per the spec's rule 11.
 *
 * Lexical sorting is wrong here and quietly so: `v1.2.0-beta.10` sorts before
 * `v1.2.0-beta.2` as a string, which would merge the betas out of order and
 * hand the older wording priority over the newer.
 *
 * `null` means "no prerelease suffix", which outranks every suffix.
 */
export function comparePrereleaseIds(a, b) {
  if (a === null && b === null) return 0;
  if (a === null) return 1;
  if (b === null) return -1;
  const left = a.split(".");
  const right = b.split(".");
  for (let i = 0; i < Math.max(left.length, right.length); i++) {
    const l = left[i];
    const r = right[i];
    // A shorter set of identifiers has lower precedence when all the leading
    // ones are equal, so `beta` sorts before `beta.2`.
    if (l === undefined) return -1;
    if (r === undefined) return 1;
    const lNum = /^\d+$/.test(l);
    const rNum = /^\d+$/.test(r);
    if (lNum && rNum) {
      const d = Number(l) - Number(r);
      if (d !== 0) return d < 0 ? -1 : 1;
      continue;
    }
    // Numeric identifiers always have lower precedence than alphanumeric ones.
    if (lNum !== rNum) return lNum ? -1 : 1;
    if (l !== r) return l < r ? -1 : 1;
  }
  return 0;
}

/** Full semver precedence, used to sort the prerelease tags of one version. */
export function compareVersions(a, b) {
  const x = parseVersion(a);
  const y = parseVersion(b);
  for (const key of ["major", "minor", "patch"]) {
    if (x[key] !== y[key]) return x[key] < y[key] ? -1 : 1;
  }
  return comparePrereleaseIds(x.prerelease, y.prerelease);
}

/**
 * The tags that are prereleases of `version`, oldest first.
 *
 * Anything that is not parseable semver, belongs to another X.Y.Z, or is the
 * stable release itself is dropped rather than thrown on: the tag list comes
 * from a repo that may carry tags this pipeline never created.
 */
export function prereleaseTagsFor(version, tags) {
  const target = parseVersion(version);
  const matches = [];
  for (const tag of tags ?? []) {
    let parsed;
    try {
      parsed = parseVersion(versionOfTag(tag));
    } catch {
      continue;
    }
    if (parsed.prerelease === null) continue;
    if (
      parsed.major !== target.major ||
      parsed.minor !== target.minor ||
      parsed.patch !== target.patch
    ) {
      continue;
    }
    matches.push(tag);
  }
  matches.sort((a, b) => compareVersions(versionOfTag(a), versionOfTag(b)));
  return matches;
}

/* ------------------------------------------------------------------ *
 * Body parsing
 * ------------------------------------------------------------------ */

function toLines(text) {
  return String(text ?? "").replace(/\r\n/g, "\n").split("\n");
}

function trimBlank(lines) {
  const out = [...lines];
  while (out.length && out[0].trim() === "") out.shift();
  while (out.length && out[out.length - 1].trim() === "") out.pop();
  return out;
}

/**
 * Splits a release body into the text above the first `### ` heading and the
 * sections below it. `## vX.Y.Z` and the summary paragraph stay in the
 * preamble; a `#### ` sub-heading stays inside its section.
 */
export function parseSections(body) {
  const preamble = [];
  let current = null;
  const sections = [];
  for (const line of toLines(body)) {
    const m = line.match(/^###\s+(.+?)\s*$/);
    if (m) {
      current = { heading: m[1], lines: [] };
      sections.push(current);
      continue;
    }
    (current ? current.lines : preamble).push(line);
  }
  return {
    preamble: trimBlank(preamble).join("\n"),
    sections: sections.map((s) => ({
      heading: s.heading,
      body: trimBlank(s.lines).join("\n"),
    })),
  };
}

/**
 * Splits section text into any prose above the bullets and the bullets
 * themselves. A bullet starts with a marker in column 0; indented lines and
 * nested bullets belong to the bullet above them.
 */
export function parseBullets(sectionBody) {
  const lead = [];
  const bullets = [];
  let current = null;
  for (const line of toLines(sectionBody)) {
    if (/^[-*]\s+\S/.test(line)) {
      current = [line];
      bullets.push(current);
      continue;
    }
    if (current) current.push(line);
    else lead.push(line);
  }
  return {
    lead: trimBlank(lead).join("\n"),
    bullets: bullets.map((b) => trimBlank(b).join("\n")),
  };
}

/** A bullet's text with its marker removed and whitespace collapsed. */
export function bulletText(bullet) {
  return String(bullet).replace(/^[-*]\s+/, "").replace(/\s+/g, " ").trim();
}

/** The exact-match dedupe key: case and trailing punctuation do not count. */
export function bulletKey(bullet) {
  return bulletText(bullet).toLowerCase().replace(/[.\s]+$/, "");
}

/**
 * Issue and PR numbers cited by a bullet.
 *
 * The `#` has to open a word so a heading marker or a mid-word hash is not
 * read as a citation, and `#5.7` is left alone because a dot followed by more
 * digits makes that a version rather than an issue.
 */
export function issueRefs(bullet) {
  const refs = new Set();
  for (const m of bulletText(bullet).matchAll(/(?:^|[\s([])#(\d+)\b(?!\.\d)/g)) {
    refs.add(`#${m[1]}`);
  }
  return refs;
}

/* ------------------------------------------------------------------ *
 * Merging
 * ------------------------------------------------------------------ */

/**
 * Merges release bodies in the order given, oldest input first.
 *
 * Returns the merged sections and the bullets that were folded away, each
 * naming the input it came from so the run can report what it did.
 */
export function mergeBodies(inputs) {
  const order = [];
  const buckets = new Map();
  const byExactKey = new Map();
  const byIssueRef = new Map();
  const duplicates = [];

  for (const input of inputs ?? []) {
    const { sections } = parseSections(input.body);
    for (const section of sections) {
      const key = section.heading.trim().toLowerCase();
      let bucket = buckets.get(key);
      if (!bucket) {
        bucket = { heading: section.heading.trim(), lead: "", bullets: [] };
        buckets.set(key, bucket);
        order.push(key);
      }
      const { lead, bullets } = parseBullets(section.body);
      if (!bucket.lead && lead) bucket.lead = lead;

      for (const bullet of bullets) {
        const exact = bulletKey(bullet);
        const refs = issueRefs(bullet);
        let hit = byExactKey.get(exact) ?? null;
        let reason = hit ? "same text" : null;
        if (!hit) {
          for (const ref of refs) {
            const candidate = byIssueRef.get(ref);
            if (candidate) {
              hit = candidate;
              reason = `same issue ${ref}`;
              break;
            }
          }
        }
        if (hit) {
          // The longer wording usually carries the extra detail a later beta
          // added, so it is the one that survives. The position does not move.
          const replaced = bulletText(bullet).length > bulletText(hit.text).length;
          duplicates.push({
            source: input.label,
            reason,
            kept: replaced ? bulletText(bullet) : bulletText(hit.text),
            dropped: replaced ? bulletText(hit.text) : bulletText(bullet),
          });
          if (replaced) hit.text = bullet;
          byExactKey.set(exact, hit);
          for (const ref of refs) if (!byIssueRef.has(ref)) byIssueRef.set(ref, hit);
          continue;
        }
        const entry = { text: bullet };
        bucket.bullets.push(entry);
        byExactKey.set(exact, entry);
        for (const ref of refs) if (!byIssueRef.has(ref)) byIssueRef.set(ref, entry);
      }
    }
  }

  return {
    sections: order.map((key) => {
      const bucket = buckets.get(key);
      return {
        heading: bucket.heading,
        lead: bucket.lead,
        bullets: bucket.bullets.map((e) => e.text),
      };
    }),
    duplicates,
  };
}

/**
 * Unions the headline items in order of first appearance and truncates to what
 * the publish gate accepts.
 *
 * Every drop is reported rather than applied quietly: the union of five betas
 * routinely exceeds six items, and which five survive is an editorial call a
 * human may want to overrule.
 */
export function mergeHeadlines(lists) {
  const seen = new Set();
  const ordered = [];
  for (const list of lists ?? []) {
    for (const raw of list ?? []) {
      const item = String(raw).trim();
      if (!item) continue;
      const norm = item.toLowerCase().replace(/\s+/g, " ");
      if (seen.has(norm)) continue;
      seen.add(norm);
      ordered.push(item);
    }
  }

  const items = [];
  const dropped = [];
  for (const item of ordered) {
    if (item.length < MIN_ITEM_LEN || item.length > MAX_ITEM_LEN) {
      dropped.push({
        item,
        reason: `${item.length} chars, allowed ${MIN_ITEM_LEN}-${MAX_ITEM_LEN}`,
      });
      continue;
    }
    if (!ITEM_RE.test(item)) {
      dropped.push({ item, reason: "forbidden characters" });
      continue;
    }
    if (items.length >= MAX_ITEMS) {
      dropped.push({ item, reason: `over the ${MAX_ITEMS} item cap` });
      continue;
    }
    if ([...items, item].join(SEP).length > MAX_JOINED_LEN) {
      dropped.push({ item, reason: `joined headline would pass ${MAX_JOINED_LEN} chars` });
      continue;
    }
    items.push(item);
  }
  return { items, dropped };
}

/** Reads a headline array out of a notes file, tolerating one that has none. */
export function headlineOf(body) {
  try {
    const { yaml } = splitFrontmatter(String(body ?? ""));
    return parseHeadlineArray(yaml);
  } catch {
    return [];
  }
}

/** Drops frontmatter if the body still carries it. Published bodies do not. */
export function stripFrontmatter(body) {
  try {
    return splitFrontmatter(String(body ?? "")).rest.replace(/^\s+/, "");
  } catch {
    return String(body ?? "");
  }
}

/** Points the preamble's `## vX.Y.Z` heading at the version being cut. */
export function retitle(preamble, version) {
  const lines = toLines(preamble);
  const idx = lines.findIndex((l) => /^##\s+v?\d/.test(l));
  if (idx === -1) return trimBlank([`## v${version}`, "", ...lines]).join("\n");
  lines[idx] = `## v${version}`;
  return trimBlank(lines).join("\n");
}

/** True when a preamble carries something beyond its version heading. */
export function hasSummary(preamble) {
  return toLines(preamble)
    .filter((l) => !/^##\s+v?\d/.test(l))
    .some((l) => l.trim() !== "");
}

/**
 * The four top-level sections a release body is made of, in reading order.
 * Merged prerelease sections land under one of them by heading, so a section
 * a late beta introduced cannot end up printed after the internals.
 */
export const TOP_SECTIONS = ["Features", "Fixes", "Mentions", "Contributions"];

const FIX_HEADING = /\b(fix|fixes|bug|bugs|regression|correctness|stability|crash|dialog)/i;
const MENTION_HEADING =
  /\b(breaking|internal|internals|deprecat|upgrade|migration|known|engine range|follow-up)/i;

/**
 * Which top-level section a merged heading belongs under. Fixes are tested
 * first so "Bug fixes" does not read as a mention, and anything unrecognised
 * is a feature, because that is what a new category section is.
 */
export function classifySection(heading) {
  const h = String(heading ?? "");
  if (FIX_HEADING.test(h)) return "Fixes";
  if (MENTION_HEADING.test(h)) return "Mentions";
  return "Features";
}

/** The thanks line, in the shape every large repo writes it. */
export function renderContributions(logins) {
  const unique = [...new Set((logins ?? []).filter(Boolean))];
  const links = unique.map((l) => `[@${l}](https://github.com/${l})`);
  const noun = unique.length === 1 ? "contributor" : "contributors";
  return `Thanks to ${unique.length} ${noun}: ${links.join(", ")}.`;
}

/** Renders the composed notes file, frontmatter first. */
export function renderBody({ version, headline, preamble, sections, contributors = [] }) {
  const out = ["---", "headline:"];
  for (const item of headline) out.push(`  - ${item}`);
  out.push("---", "");
  out.push(retitle(preamble, version), "");

  const grouped = new Map(TOP_SECTIONS.map((name) => [name, []]));
  for (const section of sections) {
    if (!section.lead && section.bullets.length === 0) continue;
    grouped.get(classifySection(section.heading)).push(section);
  }

  for (const name of TOP_SECTIONS) {
    if (name === "Contributions") continue;
    const inSection = grouped.get(name);
    if (inSection.length === 0) continue;
    out.push(`## ${name}`, "");
    for (const section of inSection) {
      out.push(`### ${section.heading}`, "");
      if (section.lead) out.push(section.lead, "");
      for (const bullet of section.bullets) out.push(bullet);
      out.push("");
    }
  }

  if ((contributors ?? []).length > 0) {
    out.push("## Contributions", "", renderContributions(contributors), "");
  }
  return `${out.join("\n").replace(/\n{3,}/g, "\n\n").trimEnd()}\n`;
}

/* ------------------------------------------------------------------ *
 * Composition
 * ------------------------------------------------------------------ */

/**
 * Builds the stable release body.
 *
 * `prereleases` is `[{ tag, body, headline }]`, oldest first. `localNotes` is
 * the raw contents of a notes file covering whatever landed after the last
 * prerelease, frontmatter and all.
 *
 * With no prereleases there is nothing to accumulate, so the local file is the
 * answer as written and is returned unchanged.
 */
export function composeReleaseNotes({ version, prereleases = [], localNotes = null, contributors = [] }) {
  parseVersion(version);
  if (parseVersion(version).prerelease !== null) {
    throw new ComposeError(
      `${version} is itself a prerelease. Prerelease notes stay incremental; compose ` +
        `only when cutting the stable X.Y.Z they lead up to.`
    );
  }
  if (prereleases.length === 0 && localNotes === null) {
    throw new ComposeError(
      `No published prereleases of ${version} and no --notes-file. There is nothing to compose.`
    );
  }

  if (prereleases.length === 0) {
    const body = String(localNotes);
    validateBody(body);
    return {
      body,
      passthrough: true,
      merged: [],
      duplicates: [],
      headline: { items: headlineOf(body), dropped: [] },
      preambleFrom: null,
    };
  }

  const inputs = prereleases.map((r) => ({ label: r.tag, body: stripFrontmatter(r.body) }));
  const localBody = localNotes === null ? null : stripFrontmatter(localNotes);
  if (localBody !== null) inputs.push({ label: "local notes", body: localBody });

  const { sections, duplicates } = mergeBodies(inputs);

  // The summary belongs to the release being cut. A local file's summary wins.
  // Inheriting the newest prerelease's used to keep the body reading, but that
  // text was written about one beta and describes it, so a stable cut carried a
  // sentence about three fixes over a release with hundreds of changes. With no
  // local summary the heading stands alone and the run says so.
  const localPreamble = localBody === null ? "" : parseSections(localBody).preamble;
  const newest = prereleases[prereleases.length - 1];
  const usingLocal = hasSummary(localPreamble);
  const preamble = usingLocal ? localPreamble : `## v${version}`;
  const preambleFrom = usingLocal ? "local notes" : null;

  const headline = mergeHeadlines([
    ...prereleases.map((r) => r.headline ?? []),
    localNotes === null ? [] : headlineOf(localNotes),
  ]);
  if (headline.items.length < MIN_ITEMS) {
    throw new ComposeError(
      `No usable headline items. The prereleases of ${version} carry no landing/headline ` +
        `status and the notes file has no headline frontmatter; add one to the notes file.`
    );
  }

  const body = renderBody({ version, headline: headline.items, preamble, sections, contributors });
  validateBody(body);
  return {
    body,
    passthrough: false,
    merged: prereleases.map((r) => r.tag),
    duplicates,
    headline,
    preambleFrom,
    contributors,
    newestTag: newest.tag,
  };
}

/** Runs the composed body through the same parser the publish gate runs. */
export function validateBody(body) {
  try {
    return processBody(body);
  } catch (e) {
    if (e instanceof ValidationError) {
      throw new ComposeError(`Composed body would fail the publish gate: ${e.message}`);
    }
    throw e;
  }
}

/* ------------------------------------------------------------------ *
 * GitHub reads
 * ------------------------------------------------------------------ */

function gh(args) {
  return execFileSync("gh", args, { encoding: "utf8", maxBuffer: 64 * 1024 * 1024 });
}

/**
 * Reads a tag's headline back off the `landing/headline` commit status.
 *
 * CI strips the frontmatter before it publishes, so the status is the only
 * machine-readable copy of a released headline. A tag without one contributes
 * nothing rather than failing the run: the status only exists for versions
 * published after the gate was added.
 */
export function fetchHeadline(repo, tag, run = gh) {
  let raw;
  try {
    raw = run([
      "api",
      `repos/${repo}/commits/${tag}/status`,
      "--jq",
      `[.statuses[] | select(.context=="${HEADLINE_STATUS_CONTEXT}") | .description] | first // ""`,
    ]);
  } catch {
    return [];
  }
  return String(raw)
    .trim()
    .split(SEP.trim())
    .map((s) => s.trim())
    .filter(Boolean);
}

/** Every published prerelease of `version`, oldest first, bodies included. */
export function fetchPrereleases(repo, version, run = gh) {
  const listed = JSON.parse(
    run(["release", "list", "--repo", repo, "--limit", "200", "--json", "tagName,isDraft"])
  );
  const tags = prereleaseTagsFor(
    version,
    listed.filter((r) => !r.isDraft).map((r) => r.tagName)
  );
  return tags.map((tag) => ({
    tag,
    body: JSON.parse(run(["release", "view", tag, "--repo", repo, "--json", "body"])).body ?? "",
    headline: fetchHeadline(repo, tag, run),
  }));
}

function currentRepo(run = gh) {
  return JSON.parse(run(["repo", "view", "--json", "nameWithOwner"])).nameWithOwner;
}

/**
 * GitHub logins that landed a commit between two tags, maintainers dropped.
 *
 * Reads the compare API rather than `git log`, because a release credits
 * handles and a commit only carries an email. A tag that does not exist yet,
 * or an API that will not answer, yields nobody rather than failing the cut.
 */
export function contributorsBetween(repo, base, head, { exclude = [], run = gh } = {}) {
  let payload;
  try {
    payload = JSON.parse(run(["api", `repos/${repo}/compare/${base}...${head}`, "--paginate"]));
  } catch {
    return [];
  }
  const skip = new Set(exclude.map((s) => String(s).toLowerCase()));
  const seen = [];
  for (const commit of payload.commits ?? []) {
    const login = commit?.author?.login;
    if (!login || skip.has(login.toLowerCase())) continue;
    if (!seen.some((l) => l.toLowerCase() === login.toLowerCase())) seen.push(login);
  }
  return seen;
}

/** The stable release immediately before `version`, or null when it is the first. */
export function previousStableTag(repo, version, run = gh) {
  const listed = JSON.parse(
    run(["release", "list", "--repo", repo, "--limit", "200", "--json", "tagName,isDraft"])
  );
  const stables = listed
    .filter((r) => !r.isDraft)
    .map((r) => r.tagName)
    .map((tag) => {
      try {
        const v = versionOfTag(tag);
        return parseVersion(v).prerelease === null ? { tag, v } : null;
      } catch {
        return null;
      }
    })
    .filter(Boolean)
    .filter((e) => compareVersions(e.v, version) < 0)
    .sort((a, b) => compareVersions(a.v, b.v));
  return stables.length ? stables[stables.length - 1].tag : null;
}

/* ------------------------------------------------------------------ *
 * CLI
 * ------------------------------------------------------------------ */

const USAGE = `Usage: node scripts/compose-release-notes.mjs --version X.Y.Z [options]

  --version X.Y.Z     the stable version being cut (required)
  --notes-file PATH    notes covering what landed after the last prerelease
  --out PATH           where to write the composed body (default: temp dir)
  --repo OWNER/NAME    repository to read releases from (default: the checkout)`;

function main() {
  const args = process.argv.slice(2);
  let version = null;
  let notesFile = null;
  let out = null;
  let repo = null;
  for (let i = 0; i < args.length; i++) {
    if (args[i] === "--version") version = args[++i];
    else if (args[i] === "--notes-file") notesFile = args[++i];
    else if (args[i] === "--out") out = args[++i];
    else if (args[i] === "--repo") repo = args[++i];
    else if (args[i] === "--help" || args[i] === "-h") {
      console.log(USAGE);
      return;
    } else if (!args[i].startsWith("-") && version === null) version = args[i];
  }
  if (!version) {
    console.error(USAGE);
    process.exit(2);
  }

  try {
    const slug = repo ?? currentRepo();
    const prereleases = fetchPrereleases(slug, version);
    const localNotes = notesFile === null ? null : fs.readFileSync(notesFile, "utf8");
    // Handles come from the commit range, so the section cannot be written
    // from memory and cannot quietly omit somebody who landed a PR.
    const previous = previousStableTag(slug, version);
    const contributors = previous
      ? contributorsBetween(slug, previous, "HEAD", { exclude: [slug.split("/")[0]] })
      : [];
    const result = composeReleaseNotes({ version, prereleases, localNotes, contributors });

    const target =
      out ??
      path.join(fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-compose-")), `v${version}.md`);
    fs.mkdirSync(path.dirname(path.resolve(target)), { recursive: true });
    fs.writeFileSync(target, result.body);

    report(version, notesFile, target, result);

    // Prove the file the releaser is about to hand to `gh release create`
    // survives the exact script the publish job runs on it.
    const self = path.dirname(fileURLToPath(import.meta.url));
    execFileSync(
      process.execPath,
      [path.join(self, "release-headline.mjs"), "--body-file", target],
      { stdio: "inherit" }
    );
  } catch (e) {
    if (e instanceof ComposeError || e instanceof ValidationError) {
      console.error(`::error::${e.message}`);
      process.exit(1);
    }
    throw e;
  }
}

function report(version, notesFile, target, result) {
  console.log(`Composed release notes for v${version}`);
  if (result.passthrough) {
    console.log(`  No published prereleases of ${version}; ${notesFile} passed through as written.`);
  } else {
    console.log(`  Merged ${result.merged.length} prerelease(s): ${result.merged.join(", ")}`);
    if (notesFile) console.log(`  Plus local notes: ${notesFile}`);
    console.log(`  Deduped ${result.duplicates.length} bullet(s)`);
    for (const dupe of result.duplicates) {
      console.log(`    [${dupe.reason}] kept: ${clip(dupe.kept)}`);
      console.log(`                  folded: ${clip(dupe.dropped)}`);
    }
    console.log(`  Headline: ${result.headline.items.join(SEP)}`);
    for (const drop of result.headline.dropped) {
      console.log(`  Dropped headline item (${drop.reason}): ${drop.item}`);
    }
    if (result.headline.dropped.length) {
      console.log("  Edit the frontmatter by hand if a dropped item should have won.");
    }
    if (result.preambleFrom !== "local notes") {
      console.log(
        "  No summary written. The body opens on its version heading alone; " +
          "pass --notes-file with an opening paragraph to give it one."
      );
    }
    console.log(
      result.contributors.length
        ? `  Contributors: ${result.contributors.map((c) => `@${c}`).join(", ")}`
        : "  Contributors: none found in the range."
    );
  }
  console.log(`NOTES_PATH=${target}`);
}

function clip(text, limit = 90) {
  return text.length > limit ? `${text.slice(0, limit)} [trimmed]` : text;
}

const invokedAsScript =
  process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href;
if (invokedAsScript) main();
