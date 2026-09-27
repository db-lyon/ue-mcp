/**
 * Reads the `Params:` clause every built-in action's description carries.
 *
 * The server does not parse prose: every action declares its parameters
 * (options, a C++ spec, an Epic schema or a plugin manifest schema). The
 * clause still ships in each description an agent reads, so the unit tests
 * use this to hold that prose to what is declared.
 */
import type { AlternativeGroup, DocumentedParam, DocumentedParams } from "../../src/surface/action-schema.js";

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
