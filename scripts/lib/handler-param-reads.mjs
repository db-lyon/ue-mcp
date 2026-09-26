// The parameter names a C++ handler's source reads (#1057).
//
// The contract test proves a spec'd handler reads exactly what it declares by
// calling it. A handler registered with MCPSpec::ContractExempt cannot be
// called under contract values, so tests/unit/handler-spec-exempt.test.ts holds
// it to its spec from its source instead, with this reader.
//
// It reads the handler body the way the read tracking counts reads: a key is
// read when it reaches MCPNoteParamRead, which every HandlerUtils.h helper
// calls. The helpers are known by name and position; any other function the
// bag is handed to is found among the handler sources and headers and read the
// same way, with its parameters bound to what the call passed, up to a fixed
// depth. A key it cannot resolve to a literal is reported as opaque rather
// than guessed, so the test fails loudly instead of passing on a partial read.

import { readFileSync } from "node:fs";
import { join } from "node:path";
import { listHandlerFiles } from "./cpp-registrations.mjs";
import {
  HANDLERS_DIR,
  findHandlerBody,
  readRegistrations,
  readSources,
  stripComments,
} from "../audit-handler-conventions.mjs";

const MODULE_ROOT = join(HANDLERS_DIR, "..", "..");

/** Every handler source plus the module headers, subfolders included, where shared helpers live. */
export function readAllSources() {
  const out = new Map(readSources());
  for (const dir of [join(MODULE_ROOT, "Public"), join(MODULE_ROOT, "Private")]) {
    for (const { name, path } of listHandlerFiles(dir, [".h"])) out.set(name, readFileSync(path, "utf8"));
  }
  return out;
}

/** HandlerUtils.h helpers whose key sits at a fixed argument. */
const KEY_ARGS = new Map([
  ...[
    "MCPNoteParamRead", "RequireString", "OptionalString", "OptionalInt", "OptionalNumber", "OptionalBool",
    "HasParam", "TryGetParam", "TryGetStringParam", "TryGetNumberParam", "TryGetBoolParam", "TryGetArrayParam",
    "TryGetObjectParam", "OptionalVec3", "RequireVec3", "OptionalRotator", "RequireRotator", "OptionalLinearColor",
    "OptionalTransform", "MCPReadFunctionArgs", "MCPReadPythonArgs",
  ].map((name) => [name, [1]]),
  ["RequireStringAlt", [1, 2]],
]);

/** Helpers that read fixed keys whatever they are passed. */
const FIXED_KEYS = new Map([
  ["ResolveWorldFromParams", ["world", "pieInstance"]],
  ["ReadPageRequest", ["cursor", "limit"]],
]);

const DIRECT_READ = /(TryGet\w*Field|Get\w*Field|HasField|HasTypedField|TryGetField)\s*\(/;

/** Split an argument list at depth-zero commas. */
function splitArgs(text) {
  const out = [];
  let depth = 0;
  let start = 0;
  let quote = null;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (quote) {
      if (c === "\\") { i++; continue; }
      if (c === quote) quote = null;
      continue;
    }
    if (c === '"' || c === "'") { quote = c; continue; }
    if (c === "(" || c === "[" || c === "{") depth++;
    else if (c === ")" || c === "]" || c === "}") depth = Math.max(0, depth - 1);
    else if (c === "," && depth === 0) {
      out.push(text.slice(start, i).trim());
      start = i + 1;
    }
  }
  const last = text.slice(start).trim();
  if (last) out.push(last);
  return out;
}

/** The index just past the bracket closing the one at `open`, skipping strings. */
function closeOf(text, open) {
  let depth = 0;
  let quote = null;
  for (let i = open; i < text.length; i++) {
    const c = text[i];
    if (quote) {
      if (c === "\\") { i++; continue; }
      if (c === quote) quote = null;
      continue;
    }
    if (c === '"' || c === "'") { quote = c; continue; }
    if (c === "(" || c === "{" || c === "[") depth++;
    else if (c === ")" || c === "}" || c === "]") {
      depth--;
      if (depth === 0) return i + 1;
    }
  }
  return text.length;
}

/** Every call in `body`: its name and its arguments. `Obj.Foo(` and `Obj->Foo(` are kept with their receiver. */
function callsIn(body) {
  const out = [];
  for (const m of body.matchAll(/((?:\w+\s*(?:->|\.)\s*)?)([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)\s*\(/g)) {
    const open = m.index + m[0].length - 1;
    const end = closeOf(body, open);
    out.push({ receiver: m[1].replace(/\s*(->|\.)\s*$/, "").trim(), name: m[2], args: splitArgs(body.slice(open + 1, end - 1)), index: m.index });
  }
  return out;
}

/** A key argument as a literal: `TEXT("x")`, `"x"`, or a bound parameter that is one. */
function literalKey(arg, bindings) {
  const trimmed = (arg ?? "").trim();
  const text = /^TEXT\(\s*"([^"]*)"\s*\)$/.exec(trimmed) ?? /^"([^"]*)"$/.exec(trimmed);
  if (text) return text[1];
  if (/^[A-Za-z_]\w*$/.test(trimmed) && bindings.has(trimmed)) return literalKey(bindings.get(trimmed), new Map());
  return null;
}

// Stripped sources and found definitions, per source set: the calibration test
// reads hundreds of handlers against the same files.
const strippedCache = new WeakMap();
const definitionCache = new WeakMap();

function strippedSources(sources) {
  if (!strippedCache.has(sources)) {
    strippedCache.set(sources, new Map([...sources].map(([file, raw]) => [file, stripComments(raw)])));
  }
  return strippedCache.get(sources);
}

/** The body and parameter names of a function named `name`, from any source. */
function findDefinition(name, sources) {
  if (!definitionCache.has(sources)) definitionCache.set(sources, new Map());
  const cache = definitionCache.get(sources);
  if (!cache.has(name)) cache.set(name, findDefinitionUncached(name, sources));
  return cache.get(name);
}

function findDefinitionUncached(name, sources) {
  const bare = name.split("::").pop();
  const pattern = new RegExp(`(?:^|[\\s*&>:])${bare}\\s*\\(`, "g");
  for (const [file, text] of strippedSources(sources)) {
    for (const m of text.matchAll(pattern)) {
      const open = m.index + m[0].length - 1;
      const close = closeOf(text, open);
      const tail = /^\s*(?:const\s*)?(?:noexcept\s*)?\{/.exec(text.slice(close, close + 40));
      if (!tail) continue;
      // A call statement followed by a block (`if (Foo(x)) {`) is not a definition.
      const lineStart = text.lastIndexOf("\n", m.index) + 1;
      const prefix = text.slice(lineStart, m.index + m[0].length - bare.length - 1);
      if (/\b(if|while|for|switch|return)\b|[=(!]/.test(prefix)) continue;
      const params = splitArgs(text.slice(open + 1, close - 1)).map((decl) => {
        const withoutDefault = decl.replace(/=.*$/s, "").trim();
        const id = /([A-Za-z_]\w*)\s*(?:\[\s*\])?$/.exec(withoutDefault);
        return id ? id[1] : null;
      });
      const bodyOpen = close + tail[0].length - 1;
      return { file, params, body: text.slice(bodyOpen, closeOf(text, bodyOpen)) };
    }
  }
  return null;
}

/** Selector key overrides (`Sel.LabelKey = TEXT("x")`) written in a body, by variable. */
function selectorOverrides(body) {
  const out = new Map();
  for (const m of body.matchAll(/(\w+)\.(LabelKey|PathKey|AltLabelKey)\s*=\s*TEXT\(\s*"([^"]+)"\s*\)/g)) {
    if (!out.has(m[1])) out.set(m[1], {});
    out.get(m[1])[m[2]] = m[3];
  }
  return out;
}

/**
 * The keys `body` reads off the parameter named `bag`.
 * Returns { keys: Set, opaque: string[] }; an opaque entry names a read whose
 * key is not a literal, or a function the bag reached that could not be found.
 */
export function paramReadsInBody(body, bag, sources, bindings = new Map(), depth = 0) {
  const keys = new Set();
  const opaque = [];
  const code = stripComments(body);
  const overrides = selectorOverrides(code);
  const addKey = (arg, what) => {
    const key = literalKey(arg, bindings);
    if (key !== null) keys.add(key);
    else opaque.push(`${what}(${arg})`);
  };

  for (const call of callsIn(code)) {
    if (call.receiver === bag && DIRECT_READ.test(`${call.name}(`)) {
      addKey(call.args[0], `${bag}->${call.name}`);
      continue;
    }
    if (call.receiver) continue;
    const at = call.args.findIndex((a) => a === bag);
    if (at < 0) continue;
    const bareName = call.name.split("::").pop();

    if (KEY_ARGS.has(bareName)) {
      for (const index of KEY_ARGS.get(bareName)) addKey(call.args[index], bareName);
      continue;
    }
    if (bareName === "MCPReadParamsAhead") {
      for (const m of (call.args[1] ?? "").matchAll(/TEXT\(\s*"([^"]+)"\s*\)/g)) keys.add(m[1]);
      continue;
    }
    if (FIXED_KEYS.has(bareName)) {
      for (const key of FIXED_KEYS.get(bareName)) keys.add(key);
      continue;
    }
    if (bareName === "MCPResolveActor") {
      const selector = overrides.get((call.args[3] ?? "").trim()) ?? {};
      keys.add(selector.PathKey ?? "actorPath");
      keys.add(selector.LabelKey ?? "actorLabel");
      if (selector.AltLabelKey) keys.add(selector.AltLabelKey);
      continue;
    }

    if (depth >= 4) {
      opaque.push(`${call.name} (too deep)`);
      continue;
    }
    const def = findDefinition(call.name, sources);
    if (!def || !def.params[at]) {
      opaque.push(`${call.name} (definition not found)`);
      continue;
    }
    const inner = new Map();
    def.params.forEach((name, index) => {
      if (name && call.args[index] !== undefined) {
        const bound = call.args[index].trim();
        inner.set(name, bindings.has(bound) ? bindings.get(bound) : bound);
      }
    });
    const nested = paramReadsInBody(def.body, def.params[at], sources, inner, depth + 1);
    for (const key of nested.keys) keys.add(key);
    opaque.push(...nested.opaque.map((o) => `${call.name} > ${o}`));
  }
  return { keys, opaque };
}

/** The keys one registered bridge method's handler reads, or null when its body is not found. */
export function handlerParamReads(method, { registrations = readRegistrations(), sources = readAllSources() } = {}) {
  const reg = registrations.get(method);
  if (!reg) return null;
  const found = findHandlerBody(reg.className, reg.method, sources);
  if (!found) return null;
  const signature = /\(\s*const\s+TSharedPtr<FJsonObject>\s*&\s*(\w+)\s*\)/.exec(
    sources.get(found.file).slice(Math.max(0, found.start - 300), found.start + 1),
  );
  return { ...paramReadsInBody(found.body, signature ? signature[1] : "Params", sources), file: found.file };
}
