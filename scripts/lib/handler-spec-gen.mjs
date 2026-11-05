// Rendering the generated category modules from tests/golden/handler-specs.json
// (#1057). Shared by scripts/generate-handler-specs.mjs, which writes them, and
// tests/unit/handler-specs.test.ts, which asserts the checked-in files are what
// the recording renders to. Run under tsx, so the validation is the server's own.

import { specProblems, paramsClause, formsMessage } from "../../src/surface/handler-spec.js";

export { paramsClause };

const ZOD_BY_TYPE = {
  string: "z.string()",
  number: "z.number()",
  integer: "z.number().int()",
  boolean: "z.boolean()",
  object: "z.record(z.string(), z.unknown())",
  array: "z.array(z.unknown())",
  vec3: "z.object({ x: z.number(), y: z.number(), z: z.number() })",
  rotator: "z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() })",
  color: "z.object({ r: z.number(), g: z.number(), b: z.number(), a: z.number().optional() })",
  any: "z.unknown()",
};

function baseExpression(type, name) {
  const expr = ZOD_BY_TYPE[type];
  if (!expr) throw new Error(`unknown parameter type '${type}' on '${name}'`);
  return expr;
}

// The value forms. Written inline at every use, so the JSON Schema converter
// never emits a `$ref`, and with no z.unknown() member, which converts to the
// empty schema a client can read as "nothing validates" (#811).
const ARG_SCALAR = "z.union([z.string(), z.number(), z.boolean(), z.null()])";
const ARG_STRUCT = "z.object({}).passthrough().meta({ additionalProperties: true })";
const ARG_VALUE = `z.union([${ARG_SCALAR}, ${ARG_STRUCT}, z.array(z.union([${ARG_SCALAR}, ${ARG_STRUCT}, z.array(${ARG_SCALAR})]))])`;
const FORM_EXPRESSION = {
  argMap: `z.record(z.string(), ${ARG_VALUE})`,
  argEntryList: `z.array(z.object({ name: z.string(), value: ${ARG_VALUE}.optional() }))`,
  scalarMap: `z.record(z.string(), ${ARG_SCALAR})`,
  stringList: "z.array(z.string())",
  string: "z.string()",
};

/** A value that takes one of several named forms, refused with a message naming them. */
function formsExpression(forms, name) {
  const members = forms.map((form) => {
    const expr = FORM_EXPRESSION[form];
    if (!expr) throw new Error(`unknown value form '${form}' on '${name}'`);
    return expr;
  });
  if (members.length === 1) return members[0];
  return `z.union([${members.join(", ")}], { error: ${JSON.stringify(formsMessage(name, forms))} })`;
}

/** A scalar of one type with its enum or range applied. The written twin of ruledZod. */
function ruledExpression(type, rules, name) {
  if (type === "string" && rules.enum?.length) return `z.enum(${JSON.stringify(rules.enum)})`;
  if ((type === "number" || type === "integer") && (rules.minimum !== undefined || rules.maximum !== undefined)) {
    let expr = baseExpression(type, name);
    if (rules.minimum !== undefined) expr += `.min(${rules.minimum})`;
    if (rules.maximum !== undefined) expr += `.max(${rules.maximum})`;
    return expr;
  }
  return baseExpression(type, name);
}

function fieldExpression(f, owner) {
  const at = `${owner}.${f.name}`;
  if (f.forms?.length) return formsExpression(f.forms, f.name);
  if (f.fields) return f.type === "array" ? `z.array(${fieldsExpression(f.fields, at)})` : fieldsExpression(f.fields, at);
  return f.type === "array" ? `z.array(${ruledExpression(f.items ?? "any", f, at)})` : ruledExpression(f.type, f, at);
}

function fieldEntries(fields, owner) {
  return fields.map((f) => `${f.name}: ${fieldExpression(f, owner)}${f.required ? "" : ".optional()"}.describe(${JSON.stringify(f.description)})`);
}

/** An object with declared fields, each described. */
function fieldsExpression(fields, name) {
  return `z.object({ ${fieldEntries(fields, name).join(", ")} })`;
}

/** A tagged union: one strict object per variant, its tag field a literal. */
function oneOfExpression(oneOf, name) {
  const variants = oneOf.variants.map((v) => {
    const entries = [`${oneOf.key}: z.literal(${JSON.stringify(v.tag)})`, ...fieldEntries(v.fields, `${name}[${v.tag}]`)];
    return `z.object({ ${entries.join(", ")} }).strict().describe(${JSON.stringify(v.description)})`;
  });
  return `z.discriminatedUnion(${JSON.stringify(oneOf.key)}, [${variants.join(", ")}])`;
}

/**
 * The zod expression a parameter renders to, without optionality. The written
 * twin of paramZod in src/surface/handler-spec.ts: a literal, a value's forms, an
 * object or array element with declared fields or variants, the alternative
 * types of a union, then null.
 */
export function zodExpression(param) {
  let expr;
  const element = () => param.oneOf
    ? oneOfExpression(param.oneOf, param.name)
    : param.fields ? fieldsExpression(param.fields, param.name) : ruledExpression(param.items ?? "any", param, param.name);
  if (param.literal !== undefined) expr = `z.literal(${JSON.stringify(param.literal)})`;
  else if (param.forms?.length) expr = formsExpression(param.forms, param.name);
  else if (param.type === "array") expr = `z.array(${element()})`;
  else if (param.type === "object" && (param.fields || param.oneOf)) expr = element();
  else expr = ruledExpression(param.type, param, param.name);
  if (param.orTypes?.length) {
    expr = `z.union([${[expr, ...param.orTypes.map((t) => baseExpression(t, param.name))].join(", ")}])`;
  }
  return param.nullable ? `${expr}.nullable()` : expr;
}


/**
 * One zod entry per key across a category. A key several handlers declare must
 * accept the same thing in every one of them, because the category's shape is
 * shared; the descriptions are merged, naming which handlers each belongs to
 * when they differ.
 */
/** The value with its enums and ranges removed, at every depth: what it is as a type. */
function withoutRules(value) {
  const { enum: _enum, minimum: _min, maximum: _max, ...rest } = value;
  if (rest.fields) rest.fields = rest.fields.map(withoutRules);
  if (rest.oneOf) rest.oneOf = { ...rest.oneOf, variants: rest.oneOf.variants.map((v) => ({ ...v, fields: v.fields.map(withoutRules) })) };
  return rest;
}

function categoryKeys(handlers) {
  const keys = new Map();
  const claim = (key, expr, base, description, method) => {
    const entry = keys.get(key);
    if (!entry) {
      keys.set(key, { expr, base, descriptions: new Map([[description, [method]]]) });
      return;
    }
    // Declarations that differ only by enum or range share the type; the
    // shared key takes the type and each action's contract enforces its own rules.
    if (entry.expr !== expr && entry.base === base) entry.expr = base;
    else if (entry.expr !== expr) {
      throw new Error(`'${key}' is declared as ${entry.expr} and as ${expr} (${method}); one category key has one type`);
    }
    const owners = entry.descriptions.get(description);
    if (owners) owners.push(method);
    else entry.descriptions.set(description, [method]);
  };
  for (const [method, spec] of handlers) {
    for (const param of spec.params) {
      const expr = zodExpression(param);
      const base = zodExpression(withoutRules(param));
      claim(param.name, expr, base, param.description, method);
      for (const alias of param.aliases ?? []) claim(alias, expr, base, `Alias for ${param.name}`, method);
    }
  }
  return [...keys.entries()].sort(([a], [b]) => a.localeCompare(b)).map(([key, entry]) => {
    const described = [...entry.descriptions.entries()];
    const description = described.length === 1
      ? described[0][0]
      : described.map(([text, owners]) => `${text} (${owners.join(", ")})`).join(". ");
    return { key, expr: entry.expr, description };
  });
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
  const clauses = handlers.map(([method, spec]) => `  ${method}: ${JSON.stringify(paramsClause(spec))},`);
  const keys = categoryKeys(handlers).map(
    ({ key, expr, description }) => `  ${key}: ${expr}.optional().describe(${JSON.stringify(description)}),`,
  );
  return `${HEADER}
import { z } from "zod";
import { makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd ${category} handler. */
export const handlerSpecs: HandlerSpecs = ${JSON.stringify(specs, null, 2)};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
${clauses.join("\n")}
};

/** Every key the spec'd ${category} handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
${keys.join("\n")}
};

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
