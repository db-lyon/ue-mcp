/**
 * The parser must recover every parameter the description actually names.
 *
 * `tests/unit/action-schema.test.ts` holds the other direction: a name the
 * parser recovers must be declared by the category. That catches a typo in a
 * description, and nothing else. It is blind to the failure that matters more,
 * which is a parameter the description names, the category declares and the
 * handler requires, that the parser drops on the floor - `describe_action`
 * then reports a schema that is missing a required field, and an agent
 * obeying it makes a call the handler rejects, or worse, one the handler
 * accepts with an empty value.
 *
 * The oracle here is deliberately independent of the parser's own grammar.
 * For every action:
 *
 *   dropped   a word that appears in the `Params:` clause OUTSIDE any bracket,
 *             and is a declared key of the category's own zod shape, and is
 *             not in the parser's output. A prose word is never a declared
 *             key, so this cannot fire on commentary, and it never consults
 *             the parser's prose rules, so a prose rule that swallows a real
 *             parameter cannot hide from it.
 *
 *   invented  a name the parser reports that does not occur anywhere in the
 *             clause text. The parser may only recover what is written.
 */
import { describe, it, expect } from "vitest";
import { ALL_TOOLS } from "../../src/tools.js";
import { actionSchema } from "../../src/action-schema.js";

/** The `Params:` clause of a description, cut where the prose resumes. */
function paramsClause(description: string): string | undefined {
  const at = description.search(/\bParams:/);
  if (at < 0) return undefined;
  const text = description.slice(at + "Params:".length);
  let depth = 0;
  const stop = /\.\s+|\bReturns\b|\bReturn:/g;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if ("([{".includes(c)) depth++;
    else if (")]}".includes(c)) depth = Math.max(0, depth - 1);
    else if (depth === 0) {
      stop.lastIndex = i;
      const m = stop.exec(text);
      if (m && m.index === i) return text.slice(0, i);
    }
  }
  return text;
}

/** Identifier-shaped words of `clause` that sit outside every bracket. */
function topLevelWords(clause: string): string[] {
  const words: string[] = [];
  let depth = 0;
  for (let i = 0; i < clause.length; i++) {
    const c = clause[i];
    if ("([{".includes(c)) { depth++; continue; }
    if (")]}".includes(c)) { depth = Math.max(0, depth - 1); continue; }
    if (depth > 0) continue;
    const m = /^[A-Za-z_][A-Za-z0-9_]*/.exec(clause.slice(i));
    if (m) { words.push(m[0]); i += m[0].length - 1; }
  }
  return words;
}

/** Consumed by the dispatcher, accepted by every action, documented nowhere. */
const ROUTING: ReadonlySet<string> = new Set(["action", "timeoutMs", "select", "omit", "editor", "toEditor"]);

/**
 * The four places on the surface where the oracle's assumption breaks: a word
 * of ordinary English that some category also happens to declare as a key.
 *
 * Each is quoted with the prose it sits in, because that is the whole
 * justification. Adding a line here says "this word is not a parameter in this
 * sentence", and a line that cannot be justified that way is a real drop being
 * waved through.
 */
const ENGLISH_NOT_A_PARAMETER: Record<string, string[]> = {
  // "renames[] where each entry is {sourcePath, destinationPath} OR ..."
  "asset.bulk_rename": ["where"],
  // "parameters?: [{name, type}] where type is bool/int/float/..."
  "blueprint.add_event_dispatcher": ["type"],
  // "... OR pathfindingContextPath? (object path) - uses its agent + filter"
  "gameplay.find_nav_path": ["filter"],
  // "parameters[] ({name, type}) where type is float|int32|bool|string|name|double"
  "statetree.set_root_parameters": ["name"],
  // "... onConflict? (skip | overwrite), override? ..." reads as prose here:
  // the word is English in this clause and became a collision only once the
  // wrapped engine tools declared `override` as a real material parameter.
  // Declaring 549 new parameter names across the surface is bound to turn a
  // few existing English words into collisions; this is the mechanism for it.
  "material.build_material": ["override"],
};

describe("the parser recovers what the clause names", () => {
  it("drops no declared parameter the clause names, and invents none", () => {
    const offenders: string[] = [];
    for (const tool of ALL_TOOLS) {
      const declared = new Set(Object.keys(tool.schema));
      for (const action of Object.keys(tool.actions)) {
        const description = tool.actions[action].description ?? "";
        const clause = paramsClause(description);
        if (clause === undefined) continue;
        const params = actionSchema(tool, action).params;
        const reported = new Set(params.map((p) => p.name));

        const english = new Set(ENGLISH_NOT_A_PARAMETER[`${tool.name}.${action}`] ?? []);
        const dropped = topLevelWords(clause).filter(
          (w) => declared.has(w) && !ROUTING.has(w) && !english.has(w) && !reported.has(w),
        );
        // Only a name the parser claims to have READ has to be in the clause.
        // A name it reports because `mapParams` forwards it, or because the
        // dispatcher routes on it, was never claimed to come from the prose.
        const invented = params
          .filter((p) => p.sources.includes("documented"))
          .map((p) => p.name)
          .filter((n) => !new RegExp(String.raw`\b` + n + String.raw`\b`).test(clause));
        if (dropped.length > 0 || invented.length > 0) {
          offenders.push(
            `${tool.name}.${action}:`
              + (dropped.length ? ` dropped ${[...new Set(dropped)].join(", ")}` : "")
              + (invented.length ? ` invented ${invented.join(", ")}` : ""),
          );
        }
      }
    }
    expect(
      offenders,
      "These actions' Params: clauses name a parameter the category declares that\n"
        + "describe_action does not report, or report a name the clause never wrote.\n"
        + "The first is a required field an agent will omit; the second is a field it\n"
        + "will pass for nothing:\n  " + offenders.join("\n  "),
    ).toEqual([]);
  });
});

describe("the English-collision list", () => {
  it("names only words the category really declares and the clause really writes", () => {
    // A stale entry is a hole in the test above, so every line has to still be
    // doing the job it was added for.
    const stale: string[] = [];
    for (const [ref, words] of Object.entries(ENGLISH_NOT_A_PARAMETER)) {
      const [toolName, action] = ref.split(".");
      const tool = ALL_TOOLS.find((t) => t.name === toolName);
      const spec = tool?.actions[action];
      if (!tool || !spec) { stale.push(`${ref}: no such action`); continue; }
      const clause = paramsClause(spec.description ?? "") ?? "";
      for (const word of words) {
        if (!(word in tool.schema)) stale.push(`${ref}: '${word}' is not a declared key`);
        else if (!topLevelWords(clause).includes(word)) stale.push(`${ref}: '${word}' is not in the clause`);
      }
    }
    expect(stale).toEqual([]);
  });
});
