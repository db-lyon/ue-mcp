/**
 * The live parameter schema for one action.
 *
 * `project(search_tools)` finds an action by keyword but hands back only its
 * prose. An agent that has located `asset.set_property` still has to guess
 * whether the path parameter is `assetPath`, `path` or `asset`, and a guess
 * that is wrong does not fail loudly: a category's zod shape strips keys it
 * does not declare, so a misspelled parameter reaches the handler as
 * `undefined` and the call returns a plausible-looking result for a mutation
 * that never happened.
 *
 * This module answers the question directly, from three independent sources:
 *
 *   documented  the `Params:` clause authored in the action's description
 *   forwards    the keys the action's `mapParams` closure actually reads
 *   declared    the category's zod shape, which is what the wire accepts
 *
 * `declared` is the only one of the three that is load-bearing at runtime, so
 * a name present in either of the other two and absent from it is a silent
 * drop rather than a documentation nit. `schemaDrift()` reports exactly those,
 * and a unit test gates the whole surface on it.
 */
import { z } from "zod";
import type { ActionSpec, ToolDef } from "./types.js";
import { classifyActionClass, type ActionClass } from "./action-class.js";

/** A readable schema summary, not a substitute for runtime validation. */
export interface ValueSchema {
  /** Wire type, unwrapped through optional/default/nullable. */
  type: string;
  required: boolean;
  description?: string;
  /** Allowed values, when the parameter is an enum or a union of literals. */
  enumValues?: string[];
  /** Default applied by the schema when the caller omits the parameter. */
  default?: unknown;
  properties?: Record<string, ValueSchema>;
  items?: ValueSchema;
  variants?: ValueSchema[];
  /** Deeper fields were omitted to bound discovery output. */
  truncated?: boolean;
}

export interface ParamSchema extends ValueSchema {
  name: string;
  /**
   * Where this name was found. A parameter missing `declared` is stripped
   * before the handler sees it, whatever the description promises.
   */
  sources: Array<"documented" | "forwards" | "declared">;
  /**
   * Index into the action's `alternatives` when this parameter is one of a
   * choice. `required` is false for every member of a choice, because the
   * handler takes either one; what has to be satisfied is the group.
   */
  alternativeGroup?: number;
}

export interface ActionSchema {
  tool: string;
  action: string;
  description: string;
  /** The C++ bridge method this dispatches to, when it dispatches to one. */
  bridge?: string;
  /** True when the action runs in the server process with no editor call. */
  local: boolean;
  /** Longer wait this action declares for itself, in milliseconds. */
  timeoutMs?: number;
  /**
   * Whether this observes the editor or changes it (#817's taxonomy).
   *
   * MCP's own readOnlyHint is per TOOL, and every tool here is a category
   * holding both reads and mutations, so the manifest cannot carry this. A
   * harness that wants to auto-approve reads and prompt on writes reads it
   * from here instead of maintaining its own list.
   *
   *   read    observes; landing it in the wrong editor changes nothing
   *   mutate  may change the editor, its project on disk, or its process
   *   unknown decided by a parameter (an arbitrary python string, a wrapped
   *           tool name), and therefore gated exactly like mutate
   */
  class: ActionClass;
  params: ParamSchema[];
  /**
   * Choices the action offers, when it offers any.
   *
   * `level.delete_actor` takes `actorLabel OR actorPath`, and reporting both
   * as required would have a caller send two ways of naming one actor, while
   * reporting both as optional would have it send neither. Neither is
   * individually required and the group is, so the group is what is published:
   * each branch is a set of names that go together, and `required` says
   * whether one of the branches has to be supplied.
   */
  alternatives?: AlternativeGroup[];
  /**
   * Names promised by the description or read by `mapParams` that the category
   * does not declare. Passing one of these has no effect.
   */
  drift: string[];
}

/* ── zod introspection ─────────────────────────────────────────────── */

interface ZodDef {
  typeName?: string;
  innerType?: z.ZodTypeAny;
  schema?: z.ZodTypeAny;
  type?: z.ZodTypeAny;
  values?: unknown[];
  options?: z.ZodTypeAny[];
  defaultValue?: () => unknown;
  valueType?: z.ZodTypeAny;
}

function defOf(schema: z.ZodTypeAny): ZodDef {
  return (schema as unknown as { _def: ZodDef })._def ?? {};
}

/**
 * Peel the wrappers that only change optionality, keeping the first
 * description and default found on the way in. The description belongs to
 * whichever wrapper `.describe()` was called on, which for the prevailing
 * `z.string().optional().describe(...)` spelling is the outer one.
 */
function unwrap(schema: z.ZodTypeAny): {
  inner: z.ZodTypeAny;
  optional: boolean;
  description?: string;
  default?: unknown;
} {
  let cur = schema;
  let optional = false;
  let description = cur.description;
  let dflt: unknown;
  // Bounded: the wrapper chains in this codebase are two or three deep, and a
  // cycle would otherwise hang the server rather than fail a call.
  for (let i = 0; i < 16; i++) {
    const def = defOf(cur);
    const name = def.typeName;
    if (name === "ZodOptional" || name === "ZodNullable") {
      optional = true;
    } else if (name === "ZodDefault") {
      optional = true;
      if (dflt === undefined && typeof def.defaultValue === "function") dflt = def.defaultValue();
    } else if (name !== "ZodEffects" && name !== "ZodBranded" && name !== "ZodReadonly") {
      break;
    }
    const next = def.innerType ?? def.schema;
    if (!next) break;
    cur = next;
    description = description ?? cur.description;
  }
  return { inner: cur, optional, description, default: dflt };
}

/** A short, agent-readable name for a zod type. */
function typeName(schema: z.ZodTypeAny): string {
  const def = defOf(schema);
  switch (def.typeName) {
    case "ZodString": return "string";
    case "ZodNumber": return "number";
    case "ZodBoolean": return "boolean";
    case "ZodUnknown": case "ZodAny": return "any";
    case "ZodEnum": return "enum";
    case "ZodLiteral": return "literal";
    case "ZodRecord": return "object";
    case "ZodObject": return "object";
    case "ZodArray": {
      const el = def.type;
      return el ? `${typeName(unwrap(el).inner)}[]` : "array";
    }
    case "ZodUnion": {
      const opts = def.options ?? [];
      const names = [...new Set(opts.map((o) => typeName(unwrap(o).inner)))];
      return names.join("|") || "union";
    }
    default: return def.typeName ? def.typeName.replace(/^Zod/, "").toLowerCase() : "unknown";
  }
}

/** Allowed values for an enum, or a union made entirely of string literals. */
function enumValues(schema: z.ZodTypeAny): string[] | undefined {
  const def = defOf(schema);
  if (def.typeName === "ZodEnum" && Array.isArray(def.values)) {
    return def.values.map((v) => String(v));
  }
  if (def.typeName === "ZodUnion" && Array.isArray(def.options)) {
    const literals: string[] = [];
    for (const opt of def.options) {
      const od = defOf(unwrap(opt).inner);
      if (od.typeName !== "ZodLiteral") return undefined;
      literals.push(String((od as unknown as { value: unknown }).value));
    }
    return literals.length > 0 ? literals : undefined;
  }
  return undefined;
}

/** Reuse the declared shape so nested argument names never become another catalog. */
function valueSchema(schema: z.ZodTypeAny, depth = 0): ValueSchema {
  const { inner, description, default: dflt } = unwrap(schema);
  const result: ValueSchema = {
    type: typeName(inner), required: !schema.isOptional(), description,
    enumValues: enumValues(inner), default: dflt,
  };
  // ponytail: bound recursive discovery; deeper shapes remain runtime-validated.
  if (depth >= 6) return { ...result, truncated: true };
  if (inner instanceof z.ZodObject) {
    result.properties = Object.fromEntries(Object.entries(inner.shape).map(([name, child]) =>
      [name, valueSchema(child as z.ZodTypeAny, depth + 1)],
    ));
  } else if (inner instanceof z.ZodArray) {
    result.items = valueSchema(inner.element, depth + 1);
  } else if (inner instanceof z.ZodRecord) {
    result.items = valueSchema(inner.valueSchema, depth + 1);
  } else if (inner instanceof z.ZodUnion) {
    result.variants = inner.options.map((option: z.ZodTypeAny) => valueSchema(option, depth + 1));
  }
  return result;
}

/* ── description parsing ───────────────────────────────────────────── */

/**
 * A parameter named by an action's `Params:` clause.
 *
 * `group` is set when the clause offered the name as one of a choice
 * (`actorLabel OR actorPath`). A member of a choice is never individually
 * required, however the clause marks it, because the handler takes either.
 */
export interface DocumentedParam {
  name: string;
  optional: boolean;
  /** Index into the parse's `alternatives`, when this name is one of a choice. */
  group?: number;
}

/**
 * A choice the clause offered.
 *
 * Each branch is the set of names that go together, so `name + packagePath? OR
 * materialPath` has branches `[["name", "packagePath"], ["materialPath"]]`:
 * what the caller supplies is one branch, not one name.
 */
export interface AlternativeGroup {
  branches: string[][];
  /** True when the action needs one of the branches. */
  required: boolean;
}

export interface DocumentedParams {
  params: DocumentedParam[];
  alternatives: AlternativeGroup[];
}

/**
 * The grammar of a `Params:` clause, as the descriptions in this repo actually
 * write it rather than as one might wish they did.
 *
 * The clause is a list of ITEMS separated by `,` or `;` at bracket depth zero.
 * Each item names one parameter and then, optionally, commentary. Inside an
 * item the names are joined by separators:
 *
 *   OR or | either   a choice: supply one side, not both
 *   and/or           a choice where more than one side is also allowed
 *   + with plus      a conjunction: the names go together in one branch
 *   /                two spellings, or two keys, of the same idea
 *
 * An item may open with a quantifier (`at least one of`, `exactly one of`),
 * which makes the names behind it a choice, or with a connective (`plus`,
 * `then`, `EITHER`) continuing the previous item.
 *
 * Everything else at depth zero ends the item, because the prose has resumed:
 * a bare word that is not a separator (`renames[] where each entry is ...`),
 * or a character that cannot start a name (`all=true`, `- uses its agent`).
 *
 * The rule that matters most is what is NOT here. There is no list of English
 * words that are refused in parameter position. `value`, `all`, `from`, `to`,
 * `min`, `max` and `name` are real parameters on this surface, and a filter
 * that reads them as prose deletes a required field from the schema an agent
 * is handed. Position decides, not vocabulary: the head of an item, and
 * whatever follows a separator, is a parameter.
 */

/** Joins two names into a choice. */
const CHOICE_WORD = /^(?:or|either)$/i;
/** Joins two names into one branch of a choice. */
const JOIN_WORD = /^(?:and|plus|with)$/i;
/** Opens an item that continues the previous one rather than naming a parameter. */
const LEAD_WORD = /^(?:or|either|and|plus|with|then|also)$/i;
/** `at least one of a/b/c`, `exactly one of x/y`, `any one of ...`. */
const QUANTIFIER = /^\s*(?:at\s+least\s+|exactly\s+|any\s+)?(?:one|two)\s+of\s+/i;
/**
 * `Params: none` says the action takes nothing of its own. It is a sentinel,
 * not a terminator: `paged()` appends `cursor?, limit?` behind it, and those
 * are real parameters.
 */
const NONE_WORD = /^none$/i;
/**
 * Commentary that shares a bracket with real parameters, for the case where
 * the category's declared keys are not to hand to settle it.
 */
const BRACKET_PROSE = /^(?:default|defaults|e|g|i|see|note|optional|required|omit|when|flat|legacy|socket)$/i;

/** Cut a clause at the first match of `stop` that is not inside brackets. */
function cutAtDepthZero(text: string, stop: RegExp): string {
  let depth = 0;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (c === "(" || c === "[" || c === "{") depth++;
    else if (c === ")" || c === "]" || c === "}") depth = Math.max(0, depth - 1);
    else if (depth === 0) {
      stop.lastIndex = i;
      const m = stop.exec(text);
      if (m && m.index === i) return text.slice(0, i);
    }
  }
  return text;
}

/** The bracket group opening at `i`, and the index just past its close. */
function bracketAt(text: string, i: number): { inner: string; end: number } {
  let depth = 0;
  for (let j = i; j < text.length; j++) {
    const c = text[j];
    if (c === "(" || c === "[" || c === "{") depth++;
    else if (c === ")" || c === "]" || c === "}") {
      depth--;
      if (depth === 0) return { inner: text.slice(i + 1, j), end: j + 1 };
    }
  }
  return { inner: text.slice(i + 1), end: text.length };
}

/** Split on the given separators, ignoring any that sits inside brackets. */
function splitDepthZero(text: string, separators: string): Array<{ text: string; separator: string }> {
  const out: Array<{ text: string; separator: string }> = [];
  let depth = 0;
  let start = 0;
  let separator = "";
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (c === "(" || c === "[" || c === "{") depth++;
    else if (c === ")" || c === "]" || c === "}") depth = Math.max(0, depth - 1);
    else if (depth === 0 && separators.includes(c)) {
      out.push({ text: text.slice(start, i), separator });
      separator = c;
      start = i + 1;
    }
  }
  out.push({ text: text.slice(start), separator });
  return out;
}

interface Atom {
  name: string;
  optional: boolean;
}

interface ScannedItem {
  /** Names offered as a choice, one set per branch. Empty when there is none. */
  branches: Atom[][];
  /** Names the item adds outright, from a `(+ ...)` group. */
  extra: Atom[];
  /** True when the item's names are alternatives rather than a plain list. */
  choice: boolean;
  /** True when a quantifier said one of them is needed. */
  quantified: boolean;
}

interface ScanOptions {
  /** The category's declared keys, when the caller has them. */
  known?: ReadonlySet<string>;
  /** Every name must be declared, or the whole group is read as commentary. */
  strict?: boolean;
}

/** The atoms of a `(+ x?, y?)` group: further parameters, plus commentary. */
function scanPlusGroup(inner: string, known?: ReadonlySet<string>): Atom[] {
  const out: Atom[] = [];
  for (const part of splitDepthZero(inner.replace(/^\s*\+/, ""), ",")) {
    const m = /^\s*([A-Za-z_][A-Za-z0-9_]*)(\[\])?\s*(\?)?/.exec(part.text);
    if (!m) continue;
    const accepted = known ? known.has(m[1]) : !BRACKET_PROSE.test(m[1]);
    if (accepted) out.push({ name: m[1], optional: m[3] === "?" });
  }
  return out;
}

/**
 * Read one item of the clause.
 *
 * Scans left to right at depth zero, alternating between "a name may appear
 * here" and "a separator may appear here". The first thing that is neither
 * ends the item: the rest of it is prose about the parameter, not more
 * parameters.
 */
function scanItem(text: string, options: ScanOptions): ScannedItem {
  const { known, strict } = options;
  const branches: Atom[][] = [[]];
  const extra: Atom[] = [];
  let choice = false;
  let quantified = false;
  let expectName = true;
  let atHead = true;
  // A head connective (`plus shortcuts:`) is as often the prose resuming as it
  // is a continuation, so the name behind one has to be a declared key.
  let headGuarded = false;
  let rejected = false;

  // Set once a `+` (or `and`/`plus`/`with`) has joined something into the
  // current branch. After that a `/` is reading inside the branch rather than
  // starting a new one: `systemPath + emitterName?/emitterIndex?` is one way
  // of addressing the emitter, not two ways of addressing the system.
  let conjoined = false;

  const branch = (): Atom[] => branches[branches.length - 1];
  const openBranch = (markChoice: boolean): void => {
    if (markChoice) choice = true;
    if (branch().length > 0) branches.push([]);
    conjoined = false;
  };
  const addAtom = (atom: Atom): boolean => {
    if (known && (strict || headGuarded) && !known.has(atom.name)) return false;
    branch().push(atom);
    headGuarded = false;
    return true;
  };

  let i = 0;
  while (i < text.length) {
    const c = text[i];
    if (/\s/.test(c)) { i++; continue; }

    if (atHead) {
      const q = QUANTIFIER.exec(text.slice(i));
      if (q) {
        quantified = true;
        choice = true;
        i += q[0].length;
        continue;
      }
    }

    if (c === "(" || c === "[" || c === "{") {
      const { inner, end } = bracketAt(text, i);
      i = end;
      if (expectName) {
        // `EITHER (nodeId + inputName) OR graphInput`: a bracket where a name
        // was due groups names, it does not comment on one.
        const sub = scanItem(inner, { known, strict: true });
        const atoms = [...sub.branches.flat(), ...sub.extra];
        if (atoms.length > 0) {
          for (const atom of atoms) branch().push(atom);
          conjoined = atoms.length > 1;
          headGuarded = false;
          expectName = false;
        }
        continue;
      }
      const trimmed = inner.trim();
      if (trimmed.startsWith("+")) {
        extra.push(...scanPlusGroup(trimmed, known));
      } else if (/^or\b/i.test(trimmed)) {
        // `(or point, or worldX + worldY)` names further ways to say the same
        // thing. It is only ever read as parameters when every name in it is
        // declared, because the same shape carries prose (`(or socket name)`).
        const alt: Atom[][] = [];
        let ok = true;
        for (const part of splitDepthZero(trimmed, ",")) {
          const sub = scanItem(part.text, { known, strict: true });
          if (sub.branches.length === 0 && sub.extra.length === 0) { ok = false; break; }
          for (const b of sub.branches) alt.push(b);
          if (sub.extra.length > 0) alt.push(sub.extra);
        }
        if (ok && alt.length > 0) {
          choice = true;
          for (const b of alt) branches.push(b);
        }
      }
      continue;
    }

    if (c === "|") { openBranch(true); expectName = true; atHead = false; i++; continue; }
    if (c === "+") { conjoined = true; expectName = true; atHead = false; i++; continue; }
    if (c === "/") {
      // A slash separates: `target/targetLabel` are two spellings of one
      // parameter and `frames?/times?` are two ways of asking. Whether that
      // separation is a CHOICE is decided by the rest of the item - an item
      // with no `OR` in it is a plain list, and its branches never surface.
      if (!conjoined) openBranch(false);
      expectName = true;
      atHead = false;
      i++;
      continue;
    }
    if (c === ":") { i++; continue; }

    const m = /^([A-Za-z_][A-Za-z0-9_]*)(\[\])?\s*(\?)?/.exec(text.slice(i));
    if (!m) break;
    const word = m[1];

    if (atHead && NONE_WORD.test(word) && !known?.has(word)) return { branches: [], extra: [], choice: false, quantified: false };

    if (CHOICE_WORD.test(word) || JOIN_WORD.test(word) || (atHead && LEAD_WORD.test(word))) {
      if (CHOICE_WORD.test(word)) openBranch(true);
      else conjoined = true;
      if (atHead) headGuarded = true;
      expectName = true;
      atHead = false;
      i += m[1].length;
      continue;
    }

    if (!expectName) break;
    if (!addAtom({ name: word, optional: m[3] === "?" })) { rejected = true; break; }
    expectName = false;
    atHead = false;
    i += m[0].length;
  }

  if (rejected && branch().length === 0 && branches.length === 1) {
    return { branches: [], extra: [], choice: false, quantified: false };
  }
  const filled = branches.filter((b) => b.length > 0);
  // A branch is reached, or not, through its first name. Everything conjoined
  // behind an optional one is therefore optional too, however the clause spelt
  // it: `axisHorizontal?/axisVertical? + horizontalMin/horizontalMax/...` is a
  // back-compat spelling of an optional axis, not six required numbers.
  for (const b of filled) {
    if (b[0].optional) for (const atom of b) atom.optional = true;
  }
  // A quantifier is a choice even before a second branch turns up: `at least
  // one of a, b, c` spells its list with commas, so the rest of it arrives as
  // the items that follow.
  const isChoice = (choice && filled.length > 1) || (quantified && filled.length > 0);
  return { branches: filled, extra, choice: isChoice, quantified };
}

/**
 * Pull the parameters out of the `Params:` clause every action carries.
 *
 * `known`, when given, is the category's declared keys. It settles the two
 * places where the clause alone is ambiguous - a name behind a head connective
 * and a name inside a bracket - by asking whether the wire would accept it.
 * It only ever admits a name, never refuses one that stands in plain parameter
 * position, so a name the description invents is still reported and still
 * shows up as drift.
 */
export function parseParams(description: string, known?: ReadonlySet<string>): DocumentedParams {
  const at = description.search(/\bParams:/);
  if (at < 0) return { params: [], alternatives: [] };
  let clause = description.slice(at + "Params:".length);

  // The clause runs until the prose resumes. Two things end it: a `Returns`
  // section, whose names are result fields rather than parameters, and a
  // sentence break, after which the description is explaining rather than
  // listing. Both are only terminators at depth zero, so `(e.g. 'foot_l')`
  // and `{op:'set'}` stay attached to the parameter they document.
  clause = cutAtDepthZero(clause, /\.\s+|\bReturns\b|\bReturn:/g);
  // A trailing issue reference is not part of the list.
  clause = clause.replace(/\(#[\d/#\s,]+\)\s*$/, "").trim();

  const params: DocumentedParam[] = [];
  const byName = new Map<string, DocumentedParam>();
  const alternatives: AlternativeGroup[] = [];
  const add = (atom: Atom, group?: number): DocumentedParam => {
    const existing = byName.get(atom.name);
    if (existing) {
      if (group !== undefined && existing.group === undefined) existing.group = group;
      return existing;
    }
    const param: DocumentedParam = { name: atom.name, optional: atom.optional };
    if (group !== undefined) param.group = group;
    byName.set(atom.name, param);
    params.push(param);
    return param;
  };

  const items = splitDepthZero(clause, ",;");
  // A quantifier that ran out of item (`at least one of a, b, c`) keeps
  // collecting the bare names behind it until something else appears.
  let openGroup: { index: number; scanned: ScannedItem } | undefined;

  for (const item of items) {
    const bare = /^\s*([A-Za-z_][A-Za-z0-9_]*)\s*$/.exec(item.text);
    if (openGroup && item.separator === "," && bare) {
      alternatives[openGroup.index].branches.push([bare[1]]);
      add({ name: bare[1], optional: false }, openGroup.index);
      continue;
    }
    openGroup = undefined;

    const scanned = scanItem(item.text, { known });
    if (scanned.branches.length === 0 && scanned.extra.length === 0) continue;

    if (scanned.choice) {
      const index = alternatives.length;
      const required = scanned.quantified
        || scanned.branches.every((b) => b.some((a) => !a.optional));
      alternatives.push({ branches: scanned.branches.map((b) => b.map((a) => a.name)), required });
      for (const b of scanned.branches) for (const atom of b) add(atom, index);
      if (scanned.quantified) openGroup = { index, scanned };
    } else {
      for (const b of scanned.branches) for (const atom of b) add(atom);
    }
    for (const atom of scanned.extra) add(atom);
  }

  return { params, alternatives };
}

/** The parameters the `Params:` clause names, without the choice structure. */
export function parseParamsClause(
  description: string,
  known?: ReadonlySet<string>,
): DocumentedParam[] {
  return parseParams(description, known).params;
}

/**
 * The parameter keys an action's `mapParams` closure reads.
 *
 * Reading the compiled source is the only way to see this: `mapParams` is an
 * opaque function by the time the registry is built. It is a best-effort
 * signal - a closure that spreads its argument reads everything and shows
 * nothing here - so it only ever adds names, never removes them.
 */
export function forwardedParams(spec: ActionSpec): string[] {
  // A bridge action maps its parameters through `mapParams`; a local one
  // reads them out of its handler's second argument. Both are the same
  // question - which keys does this action look at - so both are scanned.
  const fn = spec.mapParams ?? spec.handler;
  if (!fn) return [];
  let src: string;
  try {
    src = fn.toString();
  } catch {
    return [];
  }
  const names = new Set<string>();

  // Bind the scan to the closure's own parameter, so `arr.length` and
  // `Array.isArray` are not mistaken for parameters the action reads.
  // For a handler that is `(ctx, p) => ...`, the bag is the second argument.
  const argsMatch = /^\s*(?:async\s*)?\(?\s*([^)=]*?)\s*\)?\s*=>/.exec(src);
  const argNames = (argsMatch?.[1] ?? "")
    .split(",")
    .map((s) => s.trim())
    .filter((s) => /^[A-Za-z_$][\w$]*$/.test(s));
  const bagName = spec.mapParams ? argNames[0] : argNames[1];
  if (bagName) {
    const bag = bagName.replace(/\$/g, "\\$");
    // The prevailing spelling is `(p) => ({ x: p.x, y: p.y ?? p.path })`.
    // Match the accessor rather than the key, so an alias like
    // `p.assetPath ?? p.path` reports both spellings the action accepts.
    for (const m of src.matchAll(new RegExp(`\\b${bag}\\.([A-Za-z_][A-Za-z0-9_]*)\\b`, "g"))) {
      names.add(m[1]);
    }
    for (const m of src.matchAll(new RegExp(`\\b${bag}\\[\\s*["']([A-Za-z_][A-Za-z0-9_]*)["']\\s*\\]`, "g"))) {
      names.add(m[1]);
    }
  }

  // Destructuring in the parameter position: `({ assetPath, save })`, or
  // `(ctx, { assetPath })` for a local handler.
  const destructured = spec.mapParams
    ? /^\s*\(?\s*\{([^{}]*)\}/.exec(src)
    : /^\s*(?:async\s*)?\([^,)]*,\s*\{([^{}]*)\}/.exec(src);
  if (destructured) {
    for (const part of destructured[1].split(",")) {
      const key = /^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(?::|=|$)/.exec(part);
      if (key) names.add(key[1]);
    }
  }
  names.delete("action");
  return [...names];
}

/* ── the schema itself ─────────────────────────────────────────────── */

/** Routing instructions the dispatcher consumes and strips before a handler
 *  ever sees them. They are real wire parameters, so they are reported, but
 *  they are never counted as drift against an action's own documentation. */
const ROUTING_PARAMS: ReadonlySet<string> = new Set(["action", "timeoutMs", "select", "omit", "editor", "toEditor"]);

/** Build the full schema for one action of one tool. */
export function actionSchema(tool: ToolDef, action: string): ActionSchema {
  const spec = tool.actions[action];
  if (!spec) {
    throw new Error(
      `Unknown action '${action}' on tool '${tool.name}'. Available: ${Object.keys(tool.actions).join(", ")}`,
    );
  }
  const description = spec.description ?? "";
  // The category's declared keys settle the two spots where the clause alone
  // is ambiguous, so the parse is done against them rather than in the dark.
  const declaredNames = new Set(Object.keys(tool.schema));
  const parsed = parseParams(description, declaredNames);
  const documented = parsed.params;
  const forwards = new Set(forwardedParams(spec));
  const documentedByName = new Map(documented.map((d) => [d.name, d]));

  // A choice only survives while more than one of its branches is reachable.
  // `asset.migrate` documents `toEditor OR destinationContentDir`, and with
  // one editor registered `toEditor` is not in the shape at all, which leaves
  // an ordinary required parameter rather than a choice.
  const alternatives: AlternativeGroup[] = [];
  const groupIndex = new Map<number, number>();
  parsed.alternatives.forEach((group, i) => {
    const branches = group.branches
      .map((b) => b.filter((n) => declaredNames.has(n)))
      .filter((b) => b.length > 0);
    if (branches.length < 2) return;
    groupIndex.set(i, alternatives.length);
    alternatives.push({ branches, required: group.required });
  });

  const params: ParamSchema[] = [];
  const covered = new Set<string>();

  for (const [name, schema] of Object.entries(tool.schema)) {
    if (name === "action") continue;
    const { inner, optional, description: paramDoc, default: dflt } = unwrap(schema);
    const doc = documentedByName.get(name);
    const group = doc?.group === undefined ? undefined : groupIndex.get(doc.group);
    const sources: ParamSchema["sources"] = ["declared"];
    if (doc) sources.unshift("documented");
    if (forwards.has(name)) sources.splice(sources.length - 1, 0, "forwards");

    // A category's shape is one flat bag shared by all its actions, so most of
    // its keys belong to some other action. Report the ones this action uses,
    // plus the routing parameters, which every action accepts.
    if (!doc && !forwards.has(name) && !ROUTING_PARAMS.has(name)) continue;
    covered.add(name);
    params.push({
      ...valueSchema(schema),
      name,
      type: typeName(inner),
      // The description's `?` marker wins: it is per-action, whereas the
      // category shape has to declare nearly everything optional to let its
      // other actions through.
      required: doc ? group === undefined && !doc.optional : !optional,
      description: paramDoc,
      enumValues: enumValues(inner),
      default: dflt,
      sources,
      alternativeGroup: group,
    });
  }

  const drift: string[] = [];
  const noteDrift = (name: string): void => {
    // Routing parameters are consumed by the dispatcher, and two of them are
    // injected into the shape only while this server drives more than one
    // editor, so their absence from a single-editor graph is not drift.
    if (ROUTING_PARAMS.has(name)) return;
    if (covered.has(name) || name in tool.schema || drift.includes(name)) return;
    drift.push(name);
  };
  for (const { name } of documented) noteDrift(name);
  for (const name of forwards) noteDrift(name);

  params.sort((a, b) => {
    if (a.required !== b.required) return a.required ? -1 : 1;
    return a.name.localeCompare(b.name);
  });

  return {
    tool: tool.name,
    action,
    description,
    bridge: spec.bridge,
    local: !spec.bridge,
    timeoutMs: spec.timeoutMs,
    class: classifyActionClass(tool.name, action).class,
    params,
    alternatives: alternatives.length > 0 ? alternatives : undefined,
    drift,
  };
}

/**
 * Resolve an action reference to the tools that provide it.
 *
 * Accepts `tool.action`, `tool:action`, `tool action`, or a bare action name,
 * which may be provided by more than one category (`list`, `save`, `create`).
 * All matches come back so the caller can disambiguate rather than being
 * handed whichever one sorted first.
 */
export function resolveActionRef(
  ref: string,
  tools: ToolDef[],
): Array<{ tool: ToolDef; action: string }> {
  const trimmed = (ref ?? "").trim();
  if (!trimmed) return [];
  const split = /^([A-Za-z_][A-Za-z0-9_]*)\s*[.: ]\s*([A-Za-z_][A-Za-z0-9_]*)$/.exec(trimmed);
  if (split) {
    const [, toolName, actionName] = split;
    const tool = tools.find((t) => t.name === toolName.toLowerCase());
    if (tool && tool.actions[actionName]) return [{ tool, action: actionName }];
    // A bare action name containing a dot is not a thing, so fall through only
    // when the qualified form found nothing at all.
    if (tool) return [];
  }
  const out: Array<{ tool: ToolDef; action: string }> = [];
  for (const tool of tools) {
    if (tool.actions[trimmed]) out.push({ tool, action: trimmed });
  }
  return out;
}

/** Close spellings for an action name that did not resolve. */
export function suggestActions(ref: string, tools: ToolDef[], limit = 8): string[] {
  const needle = (ref ?? "").trim().toLowerCase().replace(/^[a-z_]+[.:]/, "");
  if (!needle) return [];
  const scored: Array<{ label: string; score: number }> = [];
  for (const tool of tools) {
    for (const action of Object.keys(tool.actions)) {
      const score = similarity(needle, action.toLowerCase());
      if (score > 0) scored.push({ label: `${tool.name}.${action}`, score });
    }
  }
  return scored
    .sort((a, b) => b.score - a.score || a.label.localeCompare(b.label))
    .slice(0, limit)
    .map((s) => s.label);
}

/**
 * How close two action names are, on 0..1.
 *
 * Substring containment dominates, because the realistic miss is a caller who
 * remembers part of the name (`bones` for `list_skeleton_bones`) rather than
 * one who transposes two letters. Edit distance catches the typo case below
 * that, and anything under a third of the name matching scores zero so the
 * suggestion list stays short enough to read.
 */
export function similarity(a: string, b: string): number {
  if (a === b) return 1;
  if (b.includes(a)) return 0.9 * (a.length / b.length) + 0.05;
  if (a.includes(b)) return 0.85 * (b.length / a.length);
  const distance = editDistance(a, b);
  const longest = Math.max(a.length, b.length);
  const closeness = 1 - distance / longest;
  return closeness >= 0.6 ? closeness * 0.8 : 0;
}

function editDistance(a: string, b: string): number {
  // Two rows rather than the full matrix: this runs over every action name on
  // the surface for every miss.
  let prev = Array.from({ length: b.length + 1 }, (_, i) => i);
  let cur = new Array<number>(b.length + 1);
  for (let i = 1; i <= a.length; i++) {
    cur[0] = i;
    for (let j = 1; j <= b.length; j++) {
      const cost = a[i - 1] === b[j - 1] ? 0 : 1;
      cur[j] = Math.min(cur[j - 1] + 1, prev[j] + 1, prev[j - 1] + cost);
    }
    [prev, cur] = [cur, prev];
  }
  return prev[b.length];
}

/**
 * The closest spellings to a missed action name, out of a plain name list.
 *
 * Separate from `suggestActions` because the dispatcher has only its own
 * category's keys at the point it fails, and importing the whole graph there
 * would tie a per-call error path to the session registry.
 */
export function nearestActions(ref: string, available: string[], limit = 5): string[] {
  const needle = (ref ?? "").trim().toLowerCase();
  if (!needle) return [];
  return available
    .map((a) => ({ a, score: similarity(needle, a.toLowerCase()) }))
    .filter((x) => x.score > 0)
    .sort((x, y) => y.score - x.score || x.a.localeCompare(y.a))
    .slice(0, limit)
    .map((x) => x.a);
}

/**
 * Every action on the surface, with the drift each one carries. Used by the
 * schema-drift unit test and by `project(describe_action)` when it is asked
 * for a whole category rather than one action.
 */
export function allActionSchemas(tools: ToolDef[]): ActionSchema[] {
  const out: ActionSchema[] = [];
  for (const tool of tools) {
    for (const action of Object.keys(tool.actions)) {
      out.push(actionSchema(tool, action));
    }
  }
  return out;
}
