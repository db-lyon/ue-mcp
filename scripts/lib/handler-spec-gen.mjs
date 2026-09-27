// Rendering the generated category modules from tests/golden/handler-specs.json
// (#1057). Shared by scripts/generate-handler-specs.mjs, which writes them, and
// tests/unit/handler-specs.test.ts, which asserts the checked-in files are what
// the recording renders to. Run under tsx, so the validation is the server's own.

import { specProblems, clauseItems, renderChoice, categorySchema } from "../../src/surface/handler-spec.js";

/**
 * The `Params:` clause for one handler, in the grammar tests/helpers/params-clause.ts reads:
 * required names bare, optional ones with `?`, aliases as `(or alias)`, and a
 * choice where its first member is declared, written as renderChoice writes it
 * (`actorLabel OR actorPath`, `at least one of labelPrefix/tag`).
 */
export function paramsClause(spec) {
  if (spec.params.length === 0) return "Params: none";
  const items = clauseItems(spec).map((item) => {
    if (item.kind === "choice") return renderChoice(item.choice, spec.params);
    const p = item.param;
    const aliases = p.aliases?.length ? ` (${p.aliases.map((a) => `or ${a}`).join(", ")})` : "";
    return `${p.name}${p.required ? "" : "?"}${aliases}`;
  });
  return `Params: ${items.join(", ")}`;
}

/** Handlers of one category, sorted by method. */
export function handlersByCategory(snapshot) {
  const problems = specProblems(snapshot.handlers ?? {});
  if (problems.length > 0) throw new Error(`handler-specs.json cannot generate a surface:\n  ${problems.join("\n  ")}`);
  const out = new Map();
  for (const method of Object.keys(snapshot.handlers).sort()) {
    const spec = snapshot.handlers[method];
    const category = spec.category;
    if (!category) throw new Error(`${method}: no category, so no module to generate it into`);
    if (!out.has(category)) out.set(category, []);
    out.get(category).push([method, spec]);
  }
  return out;
}

const HEADER = `// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).`;

/** The generated module for one category. */
export function renderCategoryModule(category, handlers) {
  const specs = Object.fromEntries(handlers);
  // Built here too, so a key declared with two shapes fails the generator
  // rather than the server's first load.
  categorySchema(specs);
  const clauses = handlers.map(([method, spec]) => `  ${method}: ${JSON.stringify(paramsClause(spec))},`);
  return `${HEADER}
import { categorySchema, makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd ${category} handler. */
export const handlerSpecs: HandlerSpecs = ${JSON.stringify(specs, null, 2)};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
${clauses.join("\n")}
};

/** Every key the spec'd ${category} handlers declare, aliases included. */
export const schema = categorySchema(handlerSpecs);

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
`;
}

/** The index that gathers every category's recorded specs. */
export function renderIndexModule(categories) {
  const imports = categories.map((c) => `import { handlerSpecs as ${c} } from "./${c}.generated.js";`);
  return `${HEADER}
import type { HandlerSpecs } from "../../surface/handler-spec.js";
${imports.join("\n")}

/** Every recorded handler spec, across categories. */
export const RECORDED_HANDLER_SPECS: HandlerSpecs = {
${categories.map((c) => `  ...${c},`).join("\n")}
};
`;
}

/** Every generated file, as relative path to contents. */
export function renderAll(snapshot) {
  const byCategory = handlersByCategory(snapshot);
  const categories = [...byCategory.keys()].sort();
  const files = new Map();
  for (const category of categories) {
    files.set(`src/tools/specs/${category}.generated.ts`, renderCategoryModule(category, byCategory.get(category)));
  }
  files.set("src/tools/specs/index.ts", renderIndexModule(categories));
  return files;
}
