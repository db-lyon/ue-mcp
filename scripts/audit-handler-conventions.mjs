/**
 * Convention audit over every registered C++ handler.
 *
 * The repo has a house style that is load-bearing rather than cosmetic:
 *
 *   idempotency  a mutation says whether it actually changed anything, so a
 *                replayed flow step or a retried call after a timeout does not
 *                double-apply and does not report success for a no-op.
 *   rollback     a mutation emits the inverse call that undoes it, which is
 *                what `rollback_on_failure` in the flow engine consumes. A
 *                mutation with no rollback silently makes a flow unrecoverable.
 *   reachability every handler is callable from the TS surface. An orphan is a
 *                capability nobody can use.
 *
 * 198 rollback call sites and 101 idempotency markers say the conventions are
 * real. What was missing was anything that fails when a new handler skips one,
 * which is how the gaps this audit reports accumulated.
 *
 * This is a source-level audit, not a runtime one: it reads each handler's
 * function body out of the .cpp and looks for the markers. That cannot prove a
 * rollback is CORRECT, only that one is emitted. Correctness is what the live
 * suite is for. Catching the omissions is still most of the value, because the
 * common failure is not a wrong rollback, it is no rollback at all.
 *
 * ## Two things it gets right that it used to get wrong
 *
 * **It follows one level of file-local helper call.** A handler that funnels
 * its write through a shared helper emits its markers from inside that helper,
 * and a scanner that only reads the handler's own braces reports it as having
 * neither. The landscape sculpt handlers are the clean case: seven height and
 * weight writers all go through MCPLscWriteHeights / MCPLscWriteWeights, which
 * is where MCPSetRollback and the idempotency markers actually fire. Splitting
 * the emission back out to the call sites to satisfy a scanner would be exactly
 * the duplication whose ninth copy quietly stops emitting a rollback, so the
 * scanner is what improves. ONE level only, and no recursion: that covers the
 * real cases and keeps the reading predictable. Every credit granted this way
 * is reported in `idempotentVia` / `rollbackVia`, so a number can always be
 * traced back to the line that produced it.
 *
 * **It keys handlers on the class-qualified name.** Matching on the bare method
 * name means two classes with the same method name collide and the audit reads
 * the wrong body. FStateTreeHandlers::ListNodeTypes hit exactly that against
 * FBlueprintHandlers::ListNodeTypes. The registering class is taken from the
 * enclosing RegisterHandlers definition, or from the explicit qualification
 * when the registration writes `&FFoo::Bar`.
 *
 * Run: node scripts/audit-handler-conventions.mjs [--json]
 * Gated by tests/unit/handler-conventions.test.ts.
 */
import { readdirSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
export const HANDLERS_DIR = join(
  here, "..", "plugin", "ue_mcp_bridge", "Source", "UE_MCP_Bridge", "Private", "Handlers",
);

/** Markers that say a handler reports whether it actually changed anything. */
const IDEMPOTENCY_MARKERS = [
  "MCPSetCreated", "MCPSetExisted", "MCPSetUpdated",
  // Hand-rolled equivalents, all of which answer the same question in the
  // result body: did this call actually change anything? Collected from what
  // handlers really emit rather than from what the helpers offer, because a
  // marker list narrower than the codebase reports false violations.
  "alreadyDeleted", "alreadyRevoked", "alreadyRemoved", "alreadyDetached",
  "alreadyExists", "alreadyRunning", "alreadyStopped", "alreadyPaused",
  "alreadySet", "alreadyOpen", "unchanged", "noChange", "wasAlready",
  "skipped", "nested", "wasActive", "alreadyClosed", "alreadyApplied",
  "SetBoolField(TEXT(\"changed\")",
];

/** Markers that say a handler emits the call that undoes it. */
const ROLLBACK_MARKERS = [
  "MCPSetRollback", "MCPSetDeleteAssetRollback", "SetObjectField(TEXT(\"rollback\")",
];

/**
 * Markers that say a handler has CONSIDERED its inverse and reported that
 * there is not one.
 *
 * This is the difference between a mutation that forgot and a mutation that
 * decided. Both emit no rollback, and until this existed the audit could not
 * tell them apart, so an honest `rollbackPossible: false` with a note read
 * exactly like an omission. A caller reading the result can tell them apart,
 * which is the whole point of emitting the field.
 *
 * `MCPSetNoRollback(Result, Reason)` in Public/HandlerUtils.h is the helper
 * spelling, and it sets the field and its note together. This list carried the
 * name before the helper was written, which made it a marker that could never
 * match: the audit advertised two accepted spellings and only ever recognised
 * one. Both are recognised now because both exist.
 */
const NO_ROLLBACK_MARKERS = [
  "rollbackPossible", "MCPSetNoRollback",
];

/**
 * Markers that say a handler CANNOT READ whether it changed anything, and said
 * so.
 *
 * The same distinction as NO_ROLLBACK_MARKERS, one convention over. A handler
 * that dispatches somebody else's code cannot measure its effect: epic(call_tool)
 * runs a wrapped engine tool whose writes it never sees, and the Fab module
 * publishes no authentication or library state this build can read back. An
 * invented `unchanged: false` there would be a claim rather than a reading.
 *
 * This list exists because the spelling was ALREADY in the codebase, in three
 * Fab handlers, written deliberately and explained in comments, and this audit
 * recognised none of it and counted all three as omissions. That is the exact
 * failure the NO_ROLLBACK_MARKERS comment above describes happening to
 * `rollbackPossible`, repeated: an idiom spreading by copy while the audit
 * scores it as debt, which makes the ledger wrong in the direction that costs
 * somebody a day writing markers for handlers that already answered.
 *
 * `MCPSetIdempotencyUnobservable(Result, Reason)` in Public/HandlerUtils.h is
 * the helper spelling, and it sets the flag and its note together so the pair
 * cannot come apart. Both spellings are recognised because both exist.
 */
const NO_IDEMPOTENCY_MARKERS = [
  "idempotencyObservable", "MCPSetIdempotencyUnobservable",
];

/**
 * Words that take a parenthesised head and a brace body without being a
 * function definition. Without these the definition scan below reads every
 * `if (...) {` as a function named `if`.
 */
const NOT_A_FUNCTION_NAME = new Set([
  "if", "for", "while", "switch", "catch", "do", "else", "return", "sizeof",
  "alignof", "decltype", "noexcept", "constexpr", "static_cast", "const_cast",
  "dynamic_cast", "reinterpret_cast", "new", "delete", "throw", "case",
]);

/**
 * What may sit between the start of a line and the name of a function being
 * DEFINED: a return type, optionally class-qualified, and nothing else. An
 * empty prefix is allowed for a definition whose return type is on the line
 * above. Anything carrying `(`, `=`, `.` or `->` is a call, not a definition.
 */
const DEFINITION_PREFIX = /^[ \t]*(?:[A-Za-z_][\w:<>,\s*&]*)?$/;

/* ── Source scanning ─────────────────────────────────────────────── */

/**
 * Index of the character closing the bracket opened at `open`, skipping
 * comments and string/char literals so a brace inside TEXT("{") cannot end a
 * body early. Returns -1 when the file runs out first.
 */
function matchBracket(text, open) {
  const opener = text[open];
  const closer = opener === "(" ? ")" : "}";
  let depth = 0;
  for (let i = open; i < text.length; i++) {
    const c = text[i];
    if (c === "/" && text[i + 1] === "/") {
      const nl = text.indexOf("\n", i);
      if (nl < 0) return -1;
      i = nl;
      continue;
    }
    if (c === "/" && text[i + 1] === "*") {
      const close = text.indexOf("*/", i + 2);
      if (close < 0) return -1;
      i = close + 1;
      continue;
    }
    if (c === '"' || c === "'") {
      for (i++; i < text.length; i++) {
        if (text[i] === "\\") { i++; continue; }
        if (text[i] === c) break;
      }
      continue;
    }
    if (c === opener) depth++;
    else if (c === closer) {
      depth--;
      if (depth === 0) return i;
    }
  }
  return -1;
}

/**
 * Every function DEFINITION that starts at column 0 of a line, with the class
 * it belongs to. Definitions in this tree are written at column 0 and calls are
 * indented, which is what separates `void FFoo::Bar(...)` from a call to it.
 */
function memberDefinitions(text) {
  const re = /^[A-Za-z_][^\n;{}()]*?\b(F\w+)::(\w+)\s*\(/gm;
  const out = [];
  for (const m of text.matchAll(re)) {
    out.push({ index: m.index, className: m[1], method: m[2] });
  }
  return out;
}

/** Read every `Registry.RegisterHandler(TEXT("x"), &Fn)` in the tree.
 *
 *  The registering class comes from the enclosing definition, so two classes
 *  that both define `ListNodeTypes` stay apart. An explicitly qualified
 *  `&FFoo::Bar` names its own class and is taken at its word. */
export function readRegistrations() {
  const re = /Registry\.RegisterHandler(?:WithTimeout)?\(\s*TEXT\("([^"]+)"\)\s*,\s*&(?:(F\w+)::)?(\w+)/g;
  const out = new Map();
  for (const entry of readdirSync(HANDLERS_DIR)) {
    if (!entry.endsWith(".cpp")) continue;
    const body = readFileSync(join(HANDLERS_DIR, entry), "utf8");
    const defs = memberDefinitions(body);
    for (const m of body.matchAll(re)) {
      let className = m[2] ?? null;
      if (!className) {
        // The last definition opened before this line is the one we are inside.
        for (const def of defs) {
          if (def.index > m.index) break;
          className = def.className;
        }
      }
      out.set(m[1], { method: m[3], className, registeredIn: entry });
    }
  }
  return out;
}

/**
 * The body of one handler function, found by brace matching from its
 * definition. Handlers are split across many files, so every file is searched.
 *
 * `className` is required to disambiguate two classes sharing a method name.
 * When no definition carries that class the search widens to any class and the
 * row says so through `classFallback`, because a body found under the wrong
 * class is still more auditable than no body at all - and the flag makes the
 * ambiguity countable rather than silent.
 */
export function findHandlerBody(className, methodName, sources) {
  const qualified = className
    ? new RegExp(`TSharedPtr<FJsonValue>\\s+${className}::${methodName}\\s*\\([^)]*\\)\\s*\\{`)
    : null;
  const anyClass = new RegExp(
    `TSharedPtr<FJsonValue>\\s+F\\w+::${methodName}\\s*\\([^)]*\\)\\s*\\{`,
  );

  for (const pattern of [qualified, anyClass]) {
    if (!pattern) continue;
    for (const [file, text] of sources) {
      const m = pattern.exec(text);
      if (!m) continue;
      const start = m.index + m[0].length - 1;
      const end = matchBracket(text, start);
      return {
        file,
        start,
        end: end < 0 ? text.length : end + 1,
        body: text.slice(start, end < 0 ? text.length : end + 1),
        classFallback: pattern === anyClass,
      };
    }
  }
  return null;
}

/** Every handler source, read once. */
export function readSources() {
  const out = new Map();
  for (const entry of readdirSync(HANDLERS_DIR)) {
    if (!entry.endsWith(".cpp")) continue;
    out.set(entry, readFileSync(join(HANDLERS_DIR, entry), "utf8"));
  }
  return out;
}

/**
 * Every function defined in one translation unit, by name, as [start, end)
 * ranges. Overloads and same-named statics are all kept: a call site cannot be
 * resolved to one of them from text alone, so the marker check below accepts
 * any of them and names which one it read.
 */
function fileFunctionRanges(text) {
  const out = new Map();
  const re = /([A-Za-z_]\w*)\s*\(/g;
  for (const m of text.matchAll(re)) {
    const name = m[1];
    if (NOT_A_FUNCTION_NAME.has(name)) continue;

    // A preprocessor line can carry a braced body that is not a function.
    const lineStart = text.lastIndexOf("\n", m.index) + 1;
    if (text[lineStart] === "#") continue;

    // Everything before the name on its own line has to look like a return
    // type (or nothing at all, for a definition whose return type is on the
    // line above). That rejects `X.Foo(`, `Bar(Baz(` and `if (Cond(` before
    // any bracket matching happens, which is most of the work.
    if (!DEFINITION_PREFIX.test(text.slice(lineStart, m.index))) continue;

    const open = m.index + m[0].length - 1;
    const closeParen = matchBracket(text, open);
    if (closeParen < 0) continue;

    // Only a `{` (possibly after `const` / `noexcept`) makes this a definition.
    const tail = /^\s*(?:const\s*)?(?:noexcept\s*)?\{/.exec(text.slice(closeParen + 1, closeParen + 32));
    if (!tail) continue;

    const braceOpen = closeParen + tail[0].length;
    const braceClose = matchBracket(text, braceOpen);
    if (braceClose < 0) continue;

    if (!out.has(name)) out.set(name, []);
    out.get(name).push({ start: braceOpen, end: braceClose + 1 });
  }
  return out;
}

const rangeCache = new Map();
function functionRangesFor(file, text) {
  if (!rangeCache.has(file)) rangeCache.set(file, fileFunctionRanges(text));
  return rangeCache.get(file);
}

/**
 * Strip C++ comments, preserving string and character literals.
 *
 * Without this the marker scan reads prose. Every marker below is a bare word
 * that occurs naturally in English - "unchanged", "skipped", "nested" - so a
 * rollbackNote explaining that a value was left unchanged earned a handler the
 * idempotency credit it was being audited for. An adversarial audit found a
 * handler drawing its ONLY credit from a comment, which is the failure this
 * whole file exists to catch, happening inside the catcher.
 *
 * String literals are kept because real markers live inside them:
 * `SetBoolField(TEXT("changed")` and `SetStringField(TEXT("alreadyRemoved")`
 * are code, not prose. So this cannot be a regex; it has to know which quotes
 * it is inside. A `//` inside a string stays, a `"` inside a comment is
 * ignored, and an escaped quote does not end its literal.
 */
// Written as a code point so the literal survives every layer of quoting
// between here and the file on disk.
const NEWLINE = String.fromCharCode(10);

export function stripComments(text) {
  let out = "";
  let i = 0;
  const n = text.length;
  while (i < n) {
    const c = text[i];
    const next = text[i + 1];
    if (c === "/" && next === "/") {
      while (i < n && text[i] !== NEWLINE) i++;
      continue;
    }
    if (c === "/" && next === "*") {
      i += 2;
      while (i < n && !(text[i] === "*" && text[i + 1] === "/")) i++;
      i += 2;
      // Keep a space so two tokens either side of a comment do not fuse.
      out += " ";
      continue;
    }
    if (c === '"' || c === "'") {
      const quote = c;
      out += c;
      i++;
      while (i < n) {
        if (text[i] === "\\") { out += text.slice(i, i + 2); i += 2; continue; }
        out += text[i];
        if (text[i] === quote) { i++; break; }
        i++;
      }
      continue;
    }
    out += c;
    i++;
  }
  return out;
}

const has = (body, markers) => {
  const code = stripComments(body);
  return markers.some((marker) => code.includes(marker));
};

/** Names this body calls as free or static functions, deduped, in call order.
 *
 *  `Obj->Foo(...)` and `Obj.Foo(...)` are dropped: those call a method on some
 *  other object, and crediting them would follow any engine method whose name
 *  happens to collide with a function defined in this file. That is not
 *  hypothetical - `PrimComp->SetCollisionEnabled(...)` collides with
 *  FPhysicsHandlers::SetCollisionEnabled in the same translation unit. */
function calledNames(body) {
  const out = [];
  const seen = new Set();
  for (const m of body.matchAll(/([A-Za-z_]\w*)\s*\(/g)) {
    const name = m[1];
    if (NOT_A_FUNCTION_NAME.has(name) || seen.has(name)) continue;
    const before = body.slice(Math.max(0, m.index - 2), m.index);
    if (before.endsWith(".") || before.endsWith("->")) continue;
    seen.add(name);
    out.push(name);
  }
  return out;
}

/**
 * ONE level of indirection: the first function defined in the same translation
 * unit that this handler calls and that emits the marker itself. Deliberately
 * not recursive - a helper that calls a helper that emits a rollback is a
 * chain this audit does not claim to read, and pretending otherwise would make
 * the number harder to trust rather than easier.
 */
function markerViaLocalHelper(found, text, markers) {
  const ranges = functionRangesFor(found.file, text);
  for (const name of calledNames(found.body)) {
    const defs = ranges.get(name);
    if (!defs) continue;
    for (const def of defs) {
      // The handler's own body is not indirection.
      if (def.start >= found.start && def.end <= found.end) continue;
      if (has(text.slice(def.start, def.end), markers)) return name;
    }
  }
  return null;
}

/**
 * Audit every registered handler.
 *
 * `classify` maps a bridge method name to "read" | "mutate" | "unknown". It is
 * injected rather than imported so this script stays runnable on its own,
 * without pulling the TS graph in.
 */
export function auditHandlers(classify) {
  const registrations = readRegistrations();
  const sources = readSources();
  const rows = [];

  for (const [action, { method, className, registeredIn }] of registrations) {
    const found = findHandlerBody(className, method, sources);
    const text = found ? sources.get(found.file) : null;

    const idempotentDirect = found ? has(found.body, IDEMPOTENCY_MARKERS) : false;
    const rollbackDirect = found ? has(found.body, ROLLBACK_MARKERS) : false;
    const idempotentVia = found && !idempotentDirect
      ? markerViaLocalHelper(found, text, IDEMPOTENCY_MARKERS) : null;
    const rollbackVia = found && !rollbackDirect
      ? markerViaLocalHelper(found, text, ROLLBACK_MARKERS) : null;
    // Only meaningful when no rollback is emitted, but recorded either way so
    // a handler that emits both can be spotted as the contradiction it is.
    const declaresNoRollback = found ? has(found.body, NO_ROLLBACK_MARKERS) : false;
    const declaresNoIdempotency = found ? has(found.body, NO_IDEMPOTENCY_MARKERS) : false;

    rows.push({
      action,
      method,
      className,
      registeredIn,
      file: found?.file ?? null,
      bodyFound: Boolean(found),
      classFallback: found?.classFallback ?? false,
      class: classify(action),
      idempotent: idempotentDirect || Boolean(idempotentVia),
      rollback: rollbackDirect || Boolean(rollbackVia),
      declaresNoRollback,
      declaresNoIdempotency,
      idempotentVia,
      rollbackVia,
    });
  }
  rows.sort((a, b) => a.action.localeCompare(b.action));
  return rows;
}

/* ── CLI ──────────────────────────────────────────────────────────── */

if (process.argv[1] && import.meta.url.endsWith(process.argv[1].replace(/\\/g, "/").split("/").pop())) {
  // Standalone: everything not obviously a read is treated as a mutation, so
  // the raw numbers are an upper bound. The unit test uses the real classifier.
  const READ_PREFIX = /^(get_|list_|read_|find_|search_|describe_|inspect_|is_|has_|validate_|check_|measure_|compare_|diagnose_|trace_|count_|export_|reflect_|resolve_|sample_|analyze_|run_eqs_)/;
  const rows = auditHandlers((a) => (READ_PREFIX.test(a) ? "read" : "mutate"));
  const mutations = rows.filter((r) => r.class !== "read");
  const noRollback = mutations.filter((r) => !r.rollback);
  const noIdempotency = mutations.filter((r) => !r.idempotent);
  const viaHelperIdem = mutations.filter((r) => r.idempotentVia);
  const viaHelperRollback = mutations.filter((r) => r.rollbackVia);
  const fallbackClass = rows.filter((r) => r.classFallback);

  if (process.argv.includes("--json")) {
    console.log(JSON.stringify({ rows, mutations: mutations.length }, null, 2));
  } else {
    console.log(`handlers registered      : ${rows.length}`);
    console.log(`bodies not found         : ${rows.filter((r) => !r.bodyFound).length}`);
    console.log(`bodies via class fallback: ${fallbackClass.length}`);
    console.log(`classified as mutations  : ${mutations.length}`);
    console.log(`mutations w/o rollback   : ${noRollback.length}`);
    console.log(`mutations w/o idempotency: ${noIdempotency.length}`);
    console.log(`credited via a helper    : ${viaHelperRollback.length} rollback, ${viaHelperIdem.length} idempotency`);
    console.log(`\nno rollback:\n  ${noRollback.map((r) => r.action).join("\n  ")}`);
  }
}
