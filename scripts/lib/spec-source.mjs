// A fingerprint of the C++ source the handler parameter specs are written in,
// so a spec edit that was never re-recorded is visible without an editor.
//
// tests/golden/handler-specs.json can only be recorded from a running editor
// with the rebuilt plugin (scripts/record-handler-specs.mjs), and CI has no
// Unreal install. What CI can read is the source: the spec arguments of each
// `Registry.RegisterHandler(TEXT("x"), &Fn, { ...spec... })` call, and every
// line outside a registration that builds a spec (`MCPParam::`, `MCPSpec::`,
// `FMCPParamSpec` and so on, for the shared parameters a category declares
// once). The recorder stores the fingerprint beside the recording, and
// tests/unit/handler-spec-source.test.ts fails when the source has moved on.
//
// It is a fingerprint, not a parser. A spec built some other way (a helper
// returning a parameter list, a reference to a shared parameter added on a line
// of its own) can change the published spec without changing it; the live
// test tests/live/handler-specs.test.ts still compares a running editor against
// the recording. Whitespace, comments, file names and file order do not count,
// so moving a handler between files is not drift.

import { createHash } from "node:crypto";
import { existsSync, readFileSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { HANDLERS_DIR, REGISTRATION_RE, listHandlerFiles } from "./cpp-registrations.mjs";

export const SPEC_SOURCE_FILE = join(
  dirname(fileURLToPath(import.meta.url)), "..", "..", "tests", "golden", "handler-specs.source.json",
);

const SPEC_LINE_RE = /MCPParam::|MCPSpec::|MCPSharedParam::|FMCPParamSpec|FMCPSpecRules|FMCPHandlerSpec/;

/** The parameters every category shares, declared outside the handler tree. */
const SHARED_PARAMS_HEADER = "HandlerSharedParams.h";

const hash = (text) => createHash("sha256").update(text).digest("hex").slice(0, 12);

/** Drop comments and whitespace, keeping string literals intact. */
function normalize(text) {
  let out = "";
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (c === '"' || c === "'") {
      const start = i;
      for (i++; i < text.length && text[i] !== c; i++) if (text[i] === "\\") i++;
      out += text.slice(start, i + 1);
    } else if (c === "/" && text[i + 1] === "/") {
      while (i < text.length && text[i] !== "\n") i++;
    } else if (c === "/" && text[i + 1] === "*") {
      i = text.indexOf("*/", i + 2);
      if (i < 0) break;
      i++;
    } else if (!/\s/.test(c)) {
      out += c;
    }
  }
  return out;
}

/** The offset of the `)` closing a call whose arguments start at `from`. */
function closingParen(text, from) {
  let depth = 1;
  for (let i = from; i < text.length; i++) {
    const c = text[i];
    if (c === '"' || c === "'") {
      for (i++; i < text.length && text[i] !== c; i++) if (text[i] === "\\") i++;
    } else if (c === "/" && text[i + 1] === "/") {
      while (i < text.length && text[i] !== "\n") i++;
    } else if (c === "/" && text[i + 1] === "*") {
      i = text.indexOf("*/", i + 2);
      if (i < 0) return text.length;
    } else if (c === "(" || c === "[" || c === "{") depth++;
    else if (c === ")" || c === "]" || c === "}") {
      depth--;
      if (depth === 0) return i;
    }
  }
  return text.length;
}

/**
 * `{ handlers: { method: hash }, shared: hash }` for the plugin's handler tree.
 * Only registrations that pass a spec are listed.
 */
export function specSourceFingerprint(dir = HANDLERS_DIR) {
  const perMethod = new Map();
  const shared = [];
  for (const { path } of listHandlerFiles(dir, [".cpp", ".h"])) {
    const text = readFileSync(path, "utf8").replace(/\r\n/g, "\n");
    const spans = [];
    for (const m of text.matchAll(new RegExp(REGISTRATION_RE.source, "g"))) {
      const start = m.index + m[0].length;
      const end = closingParen(text, start);
      spans.push([m.index, end]);
      // RegisterHandlerWithTimeout's timeout is not part of the spec.
      const args = normalize(text.slice(start, end)).replace(/^,[\d.]+f?/, "");
      if (args === "") continue;
      perMethod.set(m[1], [...(perMethod.get(m[1]) ?? []), args]);
    }
    let offset = 0;
    for (const line of text.split("\n")) {
      const inside = spans.some(([a, b]) => offset >= a && offset <= b);
      if (!inside && SPEC_LINE_RE.test(line)) {
        const fragment = normalize(line);
        if (fragment) shared.push(fragment);
      }
      offset += line.length + 1;
    }
  }
  const sharedHeader = join(dir, "..", SHARED_PARAMS_HEADER);
  if (existsSync(sharedHeader)) {
    // The whole header: a description or a bound on a line of its own changes every handler that uses it.
    const fragment = normalize(readFileSync(sharedHeader, "utf8").replace(/\r\n/g, "\n"));
    if (fragment) shared.push(fragment);
  }
  const handlers = Object.fromEntries(
    [...perMethod.keys()].sort().map((method) => [method, hash(perMethod.get(method).join("\n"))]),
  );
  return { handlers, shared: hash(shared.sort().join("\n")) };
}

/** What changed between a stored fingerprint and the source, as readable lines. */
export function specSourceDrift(stored, current) {
  const lines = [];
  const before = stored?.handlers ?? {};
  for (const [method, h] of Object.entries(current.handlers)) {
    if (!(method in before)) lines.push(`${method}: registered with a spec since the recording`);
    else if (before[method] !== h) lines.push(`${method}: its spec arguments changed`);
  }
  for (const method of Object.keys(before)) {
    if (!(method in current.handlers)) lines.push(`${method}: no longer registered with a spec`);
  }
  if (stored?.shared !== current.shared) lines.push("a shared parameter or spec helper line changed");
  return lines;
}

/** The file the recorder writes beside tests/golden/handler-specs.json. */
export function serializeSpecSource(fingerprint) {
  return `${JSON.stringify(
    {
      about:
        "Written by scripts/record-handler-specs.mjs from the C++ spec source the recording was made from. "
        + "tests/unit/handler-spec-source.test.ts fails when the source has changed since; do not edit by hand.",
      ...fingerprint,
    },
    null,
    2,
  )}\n`;
}

export function readSpecSource(file = SPEC_SOURCE_FILE) {
  try {
    return JSON.parse(readFileSync(file, "utf8"));
  } catch {
    return null;
  }
}
