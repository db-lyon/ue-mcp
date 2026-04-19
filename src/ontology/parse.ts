/**
 * Parser for the .kant subset ue-mcp emits.
 *
 * We do not implement the full kantext grammar. We implement exactly
 * what `serializeFragment` produces, plus the kernel vocabulary files
 * we author by hand:
 *
 *   - Comments: lines starting with #, and # trailing comments on
 *     scalar lines (comment treated as a marker hint for Signals).
 *   - Top-level blocks: `⛩️:` and `<Anchor>@<Root>:`.
 *   - Nested mappings, two-space indent.
 *   - Scalars: quoted strings ("..."), numbers, bare identifiers.
 *   - Signal values: `<number> # <marker>` on the value line.
 *   - No arrays, no nulls, no booleans. (If encountered we raise.)
 *
 * Output is a KantPoint tree per top-level block. The ⛩️ block yields
 * a sidecar Meaning index keyed by anchor name.
 */

import * as fs from "node:fs";
import type { KantPoint, KantScalar, KantSignal } from "./types.js";

export interface MeaningEntry {
  readonly category?: string;
  readonly meaning?: string;
  readonly purpose?: string;
}

export interface ParsedFragment {
  readonly source: string;
  readonly meanings: Readonly<Record<string, MeaningEntry>>;
  readonly blocks: Readonly<Record<string, KantPoint>>;
}

interface RawLine {
  readonly indent: number;
  readonly content: string;
  readonly lineNo: number;
}

const INDENT_WIDTH = 2;
const SHRINE = "⛩️";

function tokenize(text: string): RawLine[] {
  const out: RawLine[] = [];
  const lines = text.split(/\r?\n/);
  for (let i = 0; i < lines.length; i++) {
    const raw = lines[i];
    if (!raw.trim() || raw.trimStart().startsWith("#")) continue;
    const match = raw.match(/^( *)(.*)$/)!;
    const spaces = match[1].length;
    if (spaces % INDENT_WIDTH !== 0) {
      throw new Error(`kant parse: line ${i + 1}: indentation must be multiples of ${INDENT_WIDTH}`);
    }
    out.push({ indent: spaces / INDENT_WIDTH, content: match[2], lineNo: i + 1 });
  }
  return out;
}

function parseScalar(raw: string, lineNo: number): { value: KantScalar | KantSignal; marker?: string } {
  // Strip trailing comment (preserve marker if this is a signal).
  const commentIdx = findUnquotedHash(raw);
  let valuePart = commentIdx >= 0 ? raw.slice(0, commentIdx).trim() : raw.trim();
  const markerPart = commentIdx >= 0 ? raw.slice(commentIdx + 1).trim() : undefined;

  if (valuePart.startsWith('"') && valuePart.endsWith('"')) {
    return { value: JSON.parse(valuePart) as string };
  }
  if (valuePart === "true" || valuePart === "false") {
    throw new Error(`kant parse: line ${lineNo}: booleans are not permitted`);
  }
  if (valuePart === "null" || valuePart === "~") {
    throw new Error(`kant parse: line ${lineNo}: nulls are not permitted`);
  }
  if (valuePart.startsWith("[") || valuePart.startsWith("{")) {
    throw new Error(`kant parse: line ${lineNo}: inline lists/maps are not permitted`);
  }
  const num = Number(valuePart);
  if (!Number.isNaN(num) && valuePart !== "") {
    if (markerPart !== undefined) {
      return { value: { kind: "signal", value: num, marker: markerPart }, marker: markerPart };
    }
    return { value: num };
  }
  // Bare identifier or path.
  return { value: valuePart };
}

function findUnquotedHash(s: string): number {
  let inStr = false;
  for (let i = 0; i < s.length; i++) {
    const c = s[i];
    if (c === '"' && s[i - 1] !== "\\") inStr = !inStr;
    if (c === "#" && !inStr) return i;
  }
  return -1;
}

interface ParseCursor {
  i: number;
}

function parseMapping(lines: RawLine[], cursor: ParseCursor, parentIndent: number): {
  meaning?: string;
  purpose?: string;
  category?: string;
  fields?: Record<string, KantScalar | KantSignal>;
  children?: Record<string, KantPoint>;
} {
  const result: {
    meaning?: string;
    purpose?: string;
    category?: string;
    fields?: Record<string, KantScalar | KantSignal>;
    children?: Record<string, KantPoint>;
  } = {};

  while (cursor.i < lines.length) {
    const line = lines[cursor.i];
    if (line.indent <= parentIndent) break;
    if (line.indent !== parentIndent + 1) {
      throw new Error(`kant parse: line ${line.lineNo}: unexpected indent ${line.indent} (expected ${parentIndent + 1})`);
    }
    const colonIdx = indexOfKeyColon(line.content);
    if (colonIdx < 0) throw new Error(`kant parse: line ${line.lineNo}: expected 'key:' got '${line.content}'`);
    const key = line.content.slice(0, colonIdx).trim();
    const rest = line.content.slice(colonIdx + 1).trim();
    cursor.i += 1;

    if (rest === "") {
      // Nested mapping. If next line is deeper we recurse, else empty placeholder.
      const next = lines[cursor.i];
      if (!next || next.indent <= line.indent) {
        // Empty placeholder (e.g. `Classes:` with nothing below).
        if (key === "meaning" || key === "purpose" || key === "Category") {
          throw new Error(`kant parse: line ${line.lineNo}: '${key}' requires a value`);
        }
        (result.children ??= {})[key] = {};
        continue;
      }
      const nested = parseMapping(lines, cursor, line.indent);
      if (key === "meaning" || key === "purpose" || key === "Category") {
        throw new Error(`kant parse: line ${line.lineNo}: '${key}' must be a scalar`);
      }
      (result.children ??= {})[key] = pointFromMapping(nested);
    } else {
      const { value } = parseScalar(rest, line.lineNo);
      if (key === "meaning") {
        if (typeof value !== "string") throw new Error(`kant parse: line ${line.lineNo}: meaning must be string`);
        result.meaning = value;
      } else if (key === "purpose") {
        if (typeof value !== "string") throw new Error(`kant parse: line ${line.lineNo}: purpose must be string`);
        result.purpose = value;
      } else if (key === "Category") {
        if (typeof value !== "string") throw new Error(`kant parse: line ${line.lineNo}: Category must be string`);
        result.category = value;
      } else {
        (result.fields ??= {})[key] = value;
      }
    }
  }

  return result;
}

function indexOfKeyColon(s: string): number {
  let inStr = false;
  for (let i = 0; i < s.length; i++) {
    const c = s[i];
    if (c === '"' && s[i - 1] !== "\\") inStr = !inStr;
    if (c === ":" && !inStr) return i;
  }
  return -1;
}

function pointFromMapping(m: {
  meaning?: string;
  purpose?: string;
  category?: string;
  fields?: Record<string, KantScalar | KantSignal>;
  children?: Record<string, KantPoint>;
}): KantPoint {
  return {
    meaning: m.meaning,
    purpose: m.purpose,
    category: m.category,
    fields: m.fields,
    children: m.children,
  };
}

export function parseKant(text: string, sourceLabel: string): ParsedFragment {
  const lines = tokenize(text);
  const cursor: ParseCursor = { i: 0 };
  const meanings: Record<string, MeaningEntry> = {};
  const blocks: Record<string, KantPoint> = {};

  while (cursor.i < lines.length) {
    const line = lines[cursor.i];
    if (line.indent !== 0) {
      throw new Error(`kant parse: line ${line.lineNo}: top-level block must be unindented`);
    }
    const colon = indexOfKeyColon(line.content);
    if (colon < 0) throw new Error(`kant parse: line ${line.lineNo}: expected 'key:' got '${line.content}'`);
    const key = line.content.slice(0, colon).trim();
    cursor.i += 1;

    const nested = parseMapping(lines, cursor, 0);

    if (key === SHRINE) {
      // ⛩️ block: each child is a Meaning@X / Space@X / Mapping@X entry.
      for (const [anchorKey, point] of Object.entries(nested.children ?? {})) {
        const atIdx = anchorKey.indexOf("@");
        if (atIdx < 0) continue;
        const kind = anchorKey.slice(0, atIdx);
        const name = anchorKey.slice(atIdx + 1);
        if (kind === "Meaning") {
          meanings[name] = {
            category: point.category,
            meaning: point.meaning,
            purpose: point.purpose,
          };
        }
        // Space@ and Mapping@ entries are retained under ⛩️ as structural
        // markers but do not feed the instance tree. Future: surface them
        // to a separate index when selector evaluation needs them.
      }
      continue;
    }

    // Non-shrine top-level block: `<Anchor>@<Root>:` produces an instance tree.
    const atIdx = key.indexOf("@");
    const blockName = atIdx >= 0 ? key.slice(atIdx + 1) : key;
    blocks[blockName] = pointFromMapping(nested);
  }

  return { source: sourceLabel, meanings, blocks };
}

export function parseKantFile(filePath: string): ParsedFragment {
  const text = fs.readFileSync(filePath, "utf-8");
  return parseKant(text, filePath);
}
