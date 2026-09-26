// Reading the category tools out of src/tools/*.ts, for the audits that
// compare the shipped surface against docs/tool-reference.md.
//
// Two audits used to parse those files with their own regex, and both were
// blind in ways that made them report zero:
//
//   audit-docs   `categoryTool\(\s*"[^"]+",\s*"[^"]*",\s*\{` failed on any
//                category whose description contained an escaped quote, so
//                foliage read as src=0 and its fifteen documented actions were
//                reported as EXTRA.
//   audit-params captured only the FIRST string literal of a `description:`,
//                so every multi-line or concatenated description lost its
//                `Params:` clause. Fifteen actions were reported as the docs
//                inventing parameters the source did not declare, and the
//                audit meant to enforce the TS/C++ parameter-name rule was
//                blind for that entire class.
//
// Both come from pattern-matching TypeScript rather than reading it. So this
// masks the source instead: string, template and regex literal CONTENTS are
// blanked while the delimiters and every character position are kept, which
// leaves the braces, parens and commas that are real code standing on their
// own. Offsets into the mask are offsets into the source, so a span located in
// one can be read out of the other.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
export const TOOLS_DIR = path.join(ROOT, "src", "tools");

// Where a `/` can only be starting a regex literal, never dividing. The list is
// deliberately short: everything the tool sources actually do (`matchAll(/../)`,
// `= /../`, `replace(x, /../)`, `key: /../`) lands on one of these, and leaving
// `)`, an identifier and a digit out means a real division is never mistaken
// for a regex.
const REGEX_PRECEDES = new Set(["", "(", ",", "=", ":", "[", "!", "&", "|", "?", "{", ";", ">"]);
const REGEX_KEYWORDS = /(?:^|[^\w$])(return|typeof|case|in|of|do|else|yield|await|new|throw)$/;

/**
 * Blank out comments and the contents of every string, template and regex
 * literal, keeping the delimiters and the length.
 *
 * Regex literals matter as much as strings: `project.ts` holds
 * `/(\n\s*\}\s*\n\s*\})\s*$/`, whose two escaped closing braces are invisible
 * to a brace counter that does not know it is inside a regex. Left unmasked
 * they close the actions object early, and every action declared after
 * `add_module_dependency` silently drops out.
 */
export function maskLiterals(src) {
  const out = src.split("");
  let i = 0;
  let prev = "";
  const blank = (from, to) => {
    for (let k = from; k < to; k++) if (out[k] !== "\n") out[k] = " ";
  };
  while (i < src.length) {
    const ch = src[i];
    if (ch === "/" && src[i + 1] === "/") {
      const end = src.indexOf("\n", i);
      blank(i, end === -1 ? src.length : end);
      i = end === -1 ? src.length : end;
      continue;
    }
    if (ch === "/" && src[i + 1] === "*") {
      const end = src.indexOf("*/", i + 2);
      const stop = end === -1 ? src.length : end + 2;
      blank(i, stop);
      i = stop;
      continue;
    }
    if (ch === '"' || ch === "'" || ch === "`") {
      let j = i + 1;
      while (j < src.length) {
        if (src[j] === "\\") {
          j += 2;
          continue;
        }
        if (src[j] === ch) break;
        j++;
      }
      blank(i + 1, Math.min(j, src.length));
      i = Math.min(j, src.length) + 1;
      prev = ch;
      continue;
    }
    if (ch === "/" && (REGEX_PRECEDES.has(prev) || REGEX_KEYWORDS.test(src.slice(Math.max(0, i - 12), i)))) {
      let j = i + 1;
      let inClass = false;
      let closed = false;
      while (j < src.length && src[j] !== "\n") {
        if (src[j] === "\\") {
          j += 2;
          continue;
        }
        if (src[j] === "[") inClass = true;
        else if (src[j] === "]") inClass = false;
        else if (src[j] === "/" && !inClass) {
          closed = true;
          break;
        }
        j++;
      }
      if (closed) {
        blank(i + 1, j);
        i = j + 1;
        prev = "/";
        continue;
      }
    }
    if (!/\s/.test(ch)) prev = ch;
    i++;
  }
  return out.join("");
}

/** Split the argument list opened at `openParen` on top-level commas, as
 *  [start, end) spans. Returns null when the list never closes. */
export function topLevelArgs(masked, openParen) {
  const args = [];
  let depth = 0;
  let start = openParen + 1;
  for (let i = openParen; i < masked.length; i++) {
    const ch = masked[i];
    if (ch === "(" || ch === "{" || ch === "[") depth++;
    else if (ch === ")" || ch === "}" || ch === "]") {
      depth--;
      if (depth === 0) {
        args.push([start, i]);
        return args;
      }
    } else if (ch === "," && depth === 1) {
      args.push([start, i]);
      start = i + 1;
    }
  }
  return null;
}

const ESCAPES = { n: "\n", t: "\t", r: "\r", b: "\b", f: "\f", v: "\v", "0": "\0" };

function unescape(raw) {
  let out = "";
  for (let i = 0; i < raw.length; i++) {
    if (raw[i] !== "\\") {
      out += raw[i];
      continue;
    }
    const next = raw[++i];
    if (next === undefined) break;
    out += ESCAPES[next] ?? next;
  }
  return out;
}

/**
 * Every string literal inside a span, unescaped and joined.
 *
 * This is what makes a concatenated description readable: `"one " + "two"`,
 * a description split across six lines, and one wrapped in a helper like
 * `paged("...")` all come back as the single sentence a reader sees at
 * runtime. The literals are located in the MASK, where a quote is always a
 * real delimiter, and read out of the SOURCE, where the text still is.
 *
 * A `${}` substitution inside a template literal comes through as its own
 * source text, which is wrong but harmless: no category description uses one,
 * and the alternative is evaluating TypeScript.
 */
export function concatenatedLiterals(src, masked, start, end) {
  let out = "";
  for (let i = start; i < end; i++) {
    const ch = masked[i];
    if (ch !== '"' && ch !== "'" && ch !== "`") continue;
    let j = i + 1;
    while (j < end && masked[j] !== ch) j++;
    out += unescape(src.slice(i + 1, j));
    i = j;
  }
  return out;
}

/**
 * One category tool: its name, its action keys in declared order, and each
 * action's full description.
 *
 * Returns null when the file cannot be read, which callers must treat as a
 * failure rather than as an empty result. Reporting nothing is not the same as
 * finding nothing, and conflating the two is how both audits came to pass
 * while blind.
 */
/**
 * The actions a spread inside a category's action record folds in.
 *
 * Two shapes exist. `...epicActions`, imported from
 * `src/tools/epic/<category>.generated.ts`, declares an `actions` object in the
 * same `key: bp(...)` form a hand-written category uses. A group record a
 * category imports from a module of its own (`./project/sessions.js`) is read
 * the same way, with that module's spec builders. Reading both with the same
 * walker is what puts them in front of the same audits as the rest.
 *
 * An unknown spread returns nothing rather than throwing: a category is free
 * to grow another one, and an audit that fell over on it would be worse than
 * one that reports the actions it can see. `spreadsOnly` still records that a
 * spread was there.
 */
function resolveSpreadActions(categoryFile, spreadName) {
  if (spreadName !== "epicActions") return resolveImportedActions(categoryFile, spreadName);
  const category = path.basename(categoryFile, ".ts");
  const generated = path.join(path.dirname(categoryFile), "epic", `${category}.generated.ts`);
  if (!fs.existsSync(generated)) return [];

  const src = fs.readFileSync(generated, "utf8");
  const masked = maskLiterals(src);
  const decl = masked.indexOf("export const actions");
  if (decl === -1) return [];
  const brace = masked.indexOf("{", decl);
  if (brace === -1) return [];
  const end = matchingBrace(masked, brace);
  if (end === -1) return [];

  return walkActionKeys(src, masked, brace, end).map((a) => ({
    ...a,
    description: describeAction(src, masked, a.start, a.end),
    // Carried so a caller can tell a generated action from a hand-written one
    // without re-deriving it from the name.
    generated: true,
  }));
}

/**
 * The actions of a record a category imports from one of its own modules:
 * `import { sessionActions } from "./project/sessions.js"`, where that module
 * declares `export const sessionActions ... = { key: value, ... }`.
 */
function resolveImportedActions(categoryFile, localName) {
  const categorySrc = fs.readFileSync(categoryFile, "utf8");
  let modulePath = null;
  let exported = localName;
  for (const m of categorySrc.matchAll(/import\s*\{([^}]*)\}\s*from\s*"(\.\.?\/[^"]+)\.js"/g)) {
    for (const spec of m[1].split(",").map((x) => x.trim()).filter(Boolean)) {
      const [name, alias] = spec.split(/\s+as\s+/);
      if ((alias ?? name) !== localName) continue;
      exported = name;
      modulePath = path.resolve(path.dirname(categoryFile), `${m[2]}.ts`);
    }
  }
  if (!modulePath || !fs.existsSync(modulePath)) return [];

  const src = fs.readFileSync(modulePath, "utf8");
  const masked = maskLiterals(src);
  const decl = masked.search(new RegExp(`export const ${exported}\\b`));
  if (decl === -1) return [];
  const eq = masked.indexOf("=", decl);
  const brace = masked.indexOf("{", eq);
  if (eq === -1 || brace === -1) return [];
  const end = matchingBrace(masked, brace);
  if (end === -1) return [];

  const specBuilders = readSpecBuilders(modulePath, src);
  return walkActionKeys(src, masked, brace, end).map((a) => ({
    ...a,
    // Read in the module that declares it, as for a generated action: the
    // span points into that file, not the category's.
    description: describeValue(modulePath, src, masked, a.start, a.end, specBuilders),
    paged: /(?:^|[^\w$])paged\s*\(/.test(masked.slice(a.start, a.end)),
    external: true,
  }));
}

/**
 * The generated `Params:` clause of every spec'd bridge method in a category
 * (#1057), read out of `src/tools/specs/<category>.generated.ts`.
 *
 * An action declared with `specBp(effect, summary, method)` carries only its
 * summary in the category source; the clause is appended from the recorded
 * C++ spec at load time. Reading it from the generated module is what keeps
 * those actions in front of the same audits as the rest.
 */
function readSpecClauses(specsDir, category) {
  const generated = path.join(specsDir, `${category}.generated.ts`);
  const clauses = new Map();
  if (!fs.existsSync(generated)) return clauses;
  const src = fs.readFileSync(generated, "utf8").replace(/\r\n/g, "\n");
  const decl = src.indexOf("export const paramsClauses");
  if (decl === -1) return clauses;
  const body = src.slice(decl, src.indexOf("\n};", decl));
  for (const m of body.matchAll(/^\s+([a-z_][a-z0-9_]*): ("(?:[^"\\]|\\.)*"),$/gm)) {
    clauses.set(m[1], JSON.parse(m[2]));
  }
  return clauses;
}

/**
 * Every spec builder a category file imports, as local name to the clauses of
 * the category it came from. A tool can expose a handler another category
 * registered, importing that category's builder under its own name:
 * `import { specBp as reflectionSpecBp } from "./specs/reflection.generated.js"`.
 * A group module one directory down imports from `../specs/`.
 */
function readSpecBuilders(categoryFile, src) {
  const builders = new Map();
  for (const m of src.matchAll(/import\s*\{([^}]*)\}\s*from\s*"((?:\.|\.\.)\/specs)\/([a-z_]+)\.generated\.js"/g)) {
    const binding = m[1].match(/(?:^|,)\s*specBp(?:\s+as\s+([A-Za-z_$][\w$]*))?\s*(?:,|$)/);
    if (binding) builders.set(binding[1] ?? "specBp", readSpecClauses(path.resolve(path.dirname(categoryFile), m[2]), m[3]));
  }
  return builders;
}

/** Offset of the `}` closing the `{` at `open`, or -1. */
function matchingBrace(masked, open) {
  let depth = 0;
  for (let i = open; i < masked.length; i++) {
    if (masked[i] === "{") depth++;
    else if (masked[i] === "}") {
      depth--;
      if (depth === 0) return i;
    }
  }
  return -1;
}

/** `key: value` entries of an object literal, as name + value span. */
function walkActionKeys(src, masked, brace, bodyEnd) {
  const found = [];
  let depth = 0;
  let pending = null;
  for (let i = brace; i < bodyEnd; i++) {
    const ch = masked[i];
    if (ch === "{" || ch === "(" || ch === "[") {
      depth++;
      if (depth === 1 && !pending) pending = { seeking: true };
      continue;
    }
    if (ch === "}" || ch === ")" || ch === "]") {
      depth--;
      if (depth === 0 && pending && !pending.seeking) {
        found.push({ name: pending.name, start: pending.start, end: i });
        pending = null;
      }
      continue;
    }
    if (depth !== 1) continue;
    if (ch === ",") {
      if (pending && !pending.seeking) found.push({ name: pending.name, start: pending.start, end: i });
      pending = { seeking: true };
      continue;
    }
    if (!pending?.seeking || /\s/.test(ch)) continue;
    const key = masked.slice(i, i + 120).match(/^([a-z_][a-z0-9_]*)\s*:/);
    if (key) {
      pending = { name: key[1], start: i + key[0].length };
      i += key[0].length - 1;
      continue;
    }
    pending.seeking = false;
  }
  return found;
}

export function readCategory(file) {
  const src = fs.readFileSync(file, "utf8");
  const masked = maskLiterals(src);
  const specBuilders = readSpecBuilders(file, src);
  const call = masked.indexOf("categoryTool(");
  if (call === -1) return null;
  const args = topLevelArgs(masked, masked.indexOf("(", call));
  if (!args || args.length < 3) return null;

  const nameMatch = src.slice(args[0][0], args[0][1]).trim().match(/^["'`]([a-z_][a-z0-9_]*)["'`]$/);
  if (!nameMatch) return null;

  const [bodyStart, bodyEnd] = args[2];
  const brace = masked.indexOf("{", bodyStart);
  if (brace === -1 || brace >= bodyEnd) return null;

  // Walk the actions object at depth 1, taking each key and the span of its
  // value up to the next top-level comma.
  const actions = [];
  const spreads = [];
  let depth = 0;
  let pending = null;
  let sawSpread = false;
  for (let i = brace; i < bodyEnd; i++) {
    const ch = masked[i];
    if (ch === "{" || ch === "(" || ch === "[") {
      depth++;
      if (depth === 1 && !pending) pending = { seeking: true };
      continue;
    }
    if (ch === "}" || ch === ")" || ch === "]") {
      depth--;
      if (depth === 0 && pending && !pending.seeking) {
        actions.push({ name: pending.name, start: pending.start, end: i });
        pending = null;
      }
      continue;
    }
    if (depth !== 1) continue;
    if (ch === ",") {
      if (pending && !pending.seeking) {
        actions.push({ name: pending.name, start: pending.start, end: i });
      }
      pending = { seeking: true };
      continue;
    }
    if (!pending?.seeking || /\s/.test(ch)) continue;
    // A spread is not a key, but it is still a declaration. `...epicActions`
    // folds in the generated engine-tool module for this category, and reading
    // it as an action with an empty name made every category that has one
    // report a phantom missing doc row. It is RESOLVED rather than skipped:
    // those 830 actions are declared, advertised and documented like any
    // other, so an audit that could not see them would be exempting the
    // largest block of the surface from the checks everything else passes.
    if (masked.startsWith("...", i)) {
      sawSpread = true;
      // To the next comma at this depth: a spread of a call, such as
      // `...inAdvertisedOrder(sessionActions, configActions)`, names every
      // record it passes, and each is resolved.
      let j = i + 3;
      let inner = 0;
      while (j < bodyEnd) {
        const c = masked[j];
        if (c === "(" || c === "[" || c === "{") inner++;
        else if (c === ")" || c === "]" || c === "}") {
          if (inner === 0) break;
          inner--;
        } else if (c === "," && inner === 0) break;
        j++;
      }
      for (const m of masked.slice(i + 3, j).matchAll(/[A-Za-z_$][A-Za-z0-9_$]*/g)) spreads.push(m[0]);
      pending = { seeking: true };
      i = j - 1;
      continue;
    }
    const key = masked.slice(i, i + 80).match(/^([a-z_][a-z0-9_]*)\s*:/);
    if (key) {
      pending = { name: key[1], start: i + key[0].length };
      i += key[0].length - 1;
      continue;
    }
    pending.seeking = false;
  }

  for (const name of spreads) {
    actions.push(...resolveSpreadActions(file, name));
  }

  return {
    name: nameMatch[1],
    file,
    /** True when every entry in the actions record was a spread. */
    spreadsOnly: actions.length === 0 && sawSpread,
    actions: actions.map((a) => ({
      name: a.name,
      // A spread-resolved action's span points into the GENERATED module, not
      // into this file, so its description was read there and is carried
      // through. Re-deriving it here would read whatever happens to sit at
      // that offset in the category source, which pairs an engine tool's name
      // with a native action's prose.
      description: a.generated || a.external ? a.description : describeValue(file, src, masked, a.start, a.end, specBuilders),
      generated: a.generated === true,
      // `paged()` rewrites the description at runtime to add `cursor?, limit?`
      // to its Params clause, so the generated doc row lists two parameters the
      // literal in the source does not. Recorded as a fact about the action
      // rather than replayed here: reimplementing that string surgery in a
      // second place is a copy that would drift the first time the wrapper
      // changed, and the only thing a caller of this needs to know is that the
      // two names are expected.
      // Same offset problem as the description: a spread-resolved action's
      // span is into the generated module, so testing this file at those
      // offsets reports whatever native action happens to sit there. A
      // generated action never uses `paged()`, so the answer is simply no.
      paged: a.generated
        ? false
        : a.external
          ? a.paged
          : /(?:^|[^\w$])paged\s*\(/.test(masked.slice(a.start, a.end)),
    })),
  };
}

/**
 * The description of one action, from its value expression.
 *
 * Three shapes ship:
 *
 *   bp("effect", <description>, "bridge_method", ...)  effect first, then the
 *                                                      description
 *   { ...bp("effect", <description>, ...), timeoutMs: N }  the same call,
 *                                                spread into an object that
 *                                                overrides a field
 *   { kind, effect, description: <expr>, handler }  a local action, named field
 *
 * The middle one is why `bp(` is looked for anywhere in the value rather than
 * only at its start. Reading only the leading form left every action carrying
 * a timeout override with an empty description, which then reported its whole
 * documented parameter list as invented: landscape alone lost fourteen.
 *
 * In all three the expression may be a concatenation, a multi-line literal, or
 * wrapped in a helper such as `paged(...)`, so the whole argument span is read
 * for literals rather than the first one matched.
 */
function describeAction(src, masked, start, end, specBuilders = new Map()) {
  const head = masked.slice(start, end);
  // specBp("effect", "summary", "method"), under whichever name the file
  // imported it: the summary, then the clause that builder's generated spec
  // module appends for that method at load time (#1057).
  for (const [builder, specClauses] of specBuilders) {
    const spec = head.match(new RegExp(`(?:^|[^\\w$])${builder.replace(/\$/g, "\\$")}\\s*\\(`));
    if (!spec) continue;
    const args = topLevelArgs(masked, start + spec.index + spec[0].length - 1);
    if (!args || args.length < 3) return "";
    const summary = concatenatedLiterals(src, masked, args[1][0], args[1][1]);
    const method = concatenatedLiterals(src, masked, args[2][0], args[2][1]);
    const clause = specClauses.get(method);
    return clause === undefined ? summary : `${summary} ${clause}`;
  }
  const bp = head.match(/(?:^|[^\w$])bp\s*\(/);
  if (bp) {
    const args = topLevelArgs(masked, start + bp.index + bp[0].length - 1);
    if (!args || args.length === 0) return "";
    // The effect is the first argument and is never the description. It is
    // always one of three bare literals, so it is recognised by value rather
    // than by position.
    const first = src.slice(args[0][0], args[0][1]).trim();
    const i = /^"(?:read|mutate|unknown)"$/.test(first) ? 1 : 0;
    if (i >= args.length) return "";
    return concatenatedLiterals(src, masked, args[i][0], args[i][1]);
  }
  const field = head.match(/(?:^|[{,])\s*description\s*:/);
  if (field) {
    const from = start + field.index + field[0].length;
    // Up to the next comma at the object's own depth.
    let depth = 0;
    let to = end;
    for (let i = from; i < end; i++) {
      const ch = masked[i];
      if (ch === "{" || ch === "(" || ch === "[") depth++;
      else if (ch === "}" || ch === ")" || ch === "]") depth--;
      else if (ch === "," && depth === 0) {
        to = i;
        break;
      }
    }
    return concatenatedLiterals(src, masked, from, to);
  }
  return "";
}

/**
 * An action's description, following a bare reference to a spec declared
 * elsewhere (`build_project: buildProjectAction`) to the object that declares
 * it, in this file or in the module this file imports it from.
 */
function describeValue(file, src, masked, start, end, specBuilders) {
  const direct = describeAction(src, masked, start, end, specBuilders);
  const ref = masked.slice(start, end).trim();
  if (direct !== "" || !/^[A-Za-z_$][\w$]*$/.test(ref)) return direct;

  let target = { file, src, masked, name: ref };
  const declared = (m, name) => m.search(new RegExp(`(?:^|\\n)(?:export\\s+)?const\\s+${name}\\b`));
  if (declared(masked, ref) === -1) {
    for (const m of src.matchAll(/import\s*\{([^}]*)\}\s*from\s*"(\.\.?\/[^"]+)\.js"/g)) {
      for (const spec of m[1].split(",").map((x) => x.trim()).filter(Boolean)) {
        const [name, alias] = spec.split(/\s+as\s+/);
        if ((alias ?? name) !== ref) continue;
        const modulePath = path.resolve(path.dirname(file), `${m[2]}.ts`);
        if (!fs.existsSync(modulePath)) return "";
        const msrc = fs.readFileSync(modulePath, "utf8");
        target = { file: modulePath, src: msrc, masked: maskLiterals(msrc), name };
      }
    }
  }
  const decl = declared(target.masked, target.name);
  if (decl === -1) return "";
  const brace = target.masked.indexOf("{", target.masked.indexOf("=", decl));
  const close = brace === -1 ? -1 : matchingBrace(target.masked, brace);
  if (close === -1) return "";
  return describeAction(target.src, target.masked, brace, close, readSpecBuilders(target.file, target.src));
}

/**
 * Every category tool in src/tools, discovered from the tree rather than from
 * a hard-coded list. Both audits used to carry a list of twenty that had
 * stopped growing with the surface, so chooser, plugins, epic and fab were
 * never checked at all.
 *
 * Returns `{ categories, blind }`. A file that declares a categoryTool and
 * cannot be read lands in `blind`, and a caller that ignores it is back to
 * reporting zero while seeing nothing.
 */
export function readCategories() {
  const categories = [];
  const blind = [];
  const files = fs
    .readdirSync(TOOLS_DIR)
    .filter((f) => f.endsWith(".ts"))
    .map((f) => path.join(TOOLS_DIR, f))
    .filter((f) => fs.readFileSync(f, "utf8").includes("categoryTool("))
    .sort();
  for (const file of files) {
    const parsed = readCategory(file);
    if (!parsed) {
      blind.push({ file: path.relative(ROOT, file), reason: "categoryTool call not parsed" });
      continue;
    }
    // A category whose actions come entirely from a generated module has no
    // hand-written keys to read, and that is a fact about it rather than a
    // failure to read it. `dataflow` and `conversation` are wrapped engine
    // tools and nothing else. The distinction matters: reporting zero because
    // there is nothing to see must stay different from reporting zero because
    // the parser went blind, which is the whole reason this function
    // distinguishes them at all.
    if (parsed.actions.length === 0) {
      if (!parsed.spreadsOnly) {
        blind.push({ file: path.relative(ROOT, file), reason: "no action keys read" });
        continue;
      }
      continue;
    }
    categories.push(parsed);
  }
  return { categories, blind };
}
