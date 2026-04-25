#!/usr/bin/env node
/**
 * Parses YAML frontmatter from a release-notes markdown body, validates the
 * `headline` array, and emits the joined string + a path to the
 * frontmatter-stripped body. Used by CI to gate the publish job: invalid
 * format = publish fails before npm.
 *
 * Frontmatter accepted (block array form):
 *
 *   ---
 *   headline:
 *     - First feature
 *     - Second feature
 *     - Third feature
 *   ---
 *
 *   ## v1.2.3
 *   ...
 *
 * Inline array form (`headline: [a, b, c]`) is also accepted.
 *
 * Validation:
 *   - 1-6 items
 *   - each item 3-50 chars
 *   - each item matches /^[A-Za-z0-9][A-Za-z0-9 _\-/().,+]*$/ (no sentence
 *     punctuation : ; ? ! and no embedded middle-dots)
 *   - joined string (items.join(" · ")) <= 140 chars
 */
import fs from "node:fs";
import path from "node:path";
import os from "node:os";
import { pathToFileURL } from "node:url";

export const ITEM_RE = /^[A-Za-z0-9][A-Za-z0-9 _\-/().,+]*$/;
export const MIN_ITEMS = 1;
export const MAX_ITEMS = 6;
export const MIN_ITEM_LEN = 3;
export const MAX_ITEM_LEN = 60;
export const MAX_JOINED_LEN = 140;
export const SEP = " · ";

export class ValidationError extends Error {}

export function splitFrontmatter(body) {
  const m = body.match(/^---\r?\n([\s\S]*?)\r?\n---\r?\n([\s\S]*)$/);
  if (!m) {
    throw new ValidationError(
      "Release notes body must start with YAML frontmatter delimited by '---' lines, " +
        "containing a `headline:` array. Example:\n" +
        "---\n" +
        "headline:\n" +
        "  - First feature\n" +
        "  - Second feature\n" +
        "---\n\n" +
        "## v1.2.3\n..."
    );
  }
  return { yaml: m[1], rest: m[2] };
}

export function parseHeadlineArray(yamlText) {
  const lines = yamlText.split(/\r?\n/);
  let i = 0;
  while (i < lines.length && (lines[i].trim() === "" || lines[i].trim().startsWith("#"))) i++;
  if (i >= lines.length) {
    throw new ValidationError("Frontmatter is empty (no `headline:` key found).");
  }
  const keyLine = lines[i];

  const inline = keyLine.match(/^headline:\s*\[(.*)\]\s*$/);
  if (inline) {
    const inner = inline[1].trim();
    if (inner === "") return [];
    return inner
      .split(",")
      .map((s) => s.trim().replace(/^["']|["']$/g, ""));
  }

  if (!/^headline:\s*$/.test(keyLine)) {
    throw new ValidationError(
      `First non-empty frontmatter line must be 'headline:' (got: ${keyLine.slice(0, 80)})`
    );
  }
  i++;
  const items = [];
  while (i < lines.length) {
    const line = lines[i];
    if (line.trim() === "") {
      i++;
      continue;
    }
    const m = line.match(/^\s+-\s*(.+?)\s*$/);
    if (!m) {
      throw new ValidationError(
        `Unexpected frontmatter line (expected '  - <item>' under 'headline:'): ${line.slice(0, 80)}`
      );
    }
    items.push(m[1].replace(/^["']|["']$/g, ""));
    i++;
  }
  return items;
}

export function validate(items) {
  if (!Array.isArray(items) || items.length < MIN_ITEMS) {
    throw new ValidationError(`headline must have at least ${MIN_ITEMS} item(s).`);
  }
  if (items.length > MAX_ITEMS) {
    throw new ValidationError(`headline has ${items.length} items; max is ${MAX_ITEMS}.`);
  }
  for (const [idx, item] of items.entries()) {
    if (typeof item !== "string") {
      throw new ValidationError(`headline[${idx}] is not a string.`);
    }
    if (item.length < MIN_ITEM_LEN || item.length > MAX_ITEM_LEN) {
      throw new ValidationError(
        `headline[${idx}] is ${item.length} chars; must be ${MIN_ITEM_LEN}-${MAX_ITEM_LEN}. Item: '${item}'`
      );
    }
    if (!ITEM_RE.test(item)) {
      throw new ValidationError(
        `headline[${idx}] '${item}' contains forbidden characters. ` +
          `Allowed: letters, digits, spaces, and the punctuation _ - / ( ) . , +. ` +
          `Sentence punctuation (: ; ? !) and the joiner (·) are forbidden — ` +
          `headlines are noun-phrase lists, not sentences.`
      );
    }
  }
  const joined = items.join(SEP);
  if (joined.length > MAX_JOINED_LEN) {
    throw new ValidationError(
      `Joined headline is ${joined.length} chars; GitHub status descriptions cap at ${MAX_JOINED_LEN}.`
    );
  }
  return joined;
}

export function processBody(body) {
  const { yaml, rest } = splitFrontmatter(body);
  const items = parseHeadlineArray(yaml);
  const headline = validate(items);
  const strippedBody = rest.replace(/^\s+/, "");
  return { headline, strippedBody };
}

function main() {
  const args = process.argv.slice(2);
  let bodyFile = null;
  for (let i = 0; i < args.length; i++) {
    if (args[i] === "--body-file") bodyFile = args[++i];
  }
  let body;
  try {
    body = bodyFile ? fs.readFileSync(bodyFile, "utf8") : fs.readFileSync(0, "utf8");
  } catch (e) {
    console.error(`::error::Failed to read body: ${e.message}`);
    process.exit(1);
  }
  let result;
  try {
    result = processBody(body);
  } catch (e) {
    if (e instanceof ValidationError) {
      console.error(`::error::${e.message}`);
      process.exit(1);
    }
    throw e;
  }
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-relnotes-"));
  const stripped = path.join(tmp, "body.md");
  fs.writeFileSync(stripped, result.strippedBody);
  console.log(`HEADLINE=${result.headline}`);
  console.log(`BODY_PATH=${stripped}`);
}

const invokedAsScript =
  process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href;
if (invokedAsScript) main();
