/**
 * Parameter contracts authored in C++ (#1057).
 *
 * A handler registered with a spec declares its parameters once, at its
 * `RegisterHandler` call. The bridge publishes every spec in
 * `get_bridge_capabilities.handlerSpecs`; `npm run specs:record` writes that to
 * `tests/golden/handler-specs.json`, and `npm run specs:generate` turns the
 * recording into `src/tools/specs/<category>.generated.ts`: the zod entries and
 * the `Params:` clause of every spec'd action.
 *
 * The advertised surface always comes from the recording, whether or not an
 * editor is connected, so the startup contract never depends on which editor
 * answered. A connected editor whose live specs differ from the recording is
 * reported as drift by `project(get_status)`.
 */
import { z } from "zod";
import type { ActionEffect, BridgeActionSpec } from "../core/types.js";
import { ROUTING_PARAM_NAMES } from "./routing-params.js";

/** Wire types a spec can declare. Mirrors EMCPParamType in HandlerRegistry.h. */
export const PARAM_TYPES = ["string", "number", "integer", "boolean", "object", "array", "vec3", "rotator", "color", "any"] as const;
export type ParamType = (typeof PARAM_TYPES)[number];

/**
 * Named value shapes a multi-form value can take. Mirrors EMCPValueForm. Each
 * has one fixed, fully typed schema, so a value accepting several of them is
 * advertised without an untyped member a client could read as "nothing
 * validates" (#811).
 */
export const VALUE_FORMS = ["argMap", "argEntryList", "stringList", "string", "scalarMap"] as const;
export type ValueForm = (typeof VALUE_FORMS)[number];

/** One field of an object parameter, or of each element of an array of objects. */
export interface ParamField {
  name: string;
  type: ParamType;
  required: boolean;
  description: string;
  /** Element type of an `array` field. */
  items?: ParamType;
  /** The named shapes an `any` field takes. */
  forms?: ValueForm[];
  /** The only values a string field, or each string of an array field, may take. */
  enum?: string[];
  /** Inclusive lower bound of a number or integer field, or of each element of an array field. */
  minimum?: number;
  /** Inclusive upper bound, as minimum. */
  maximum?: number;
  /** The shape of an `object` field, or of each element of an array-of-objects field. */
  fields?: ParamField[];
}

/** One shape of a tagged union: its tag value and the fields that shape has. */
export interface ParamVariant {
  tag: string;
  description: string;
  fields: ParamField[];
}

/** A tagged union: the field `key` holds a tag that picks one of `variants`. */
export interface ParamOneOf {
  key: string;
  variants: ParamVariant[];
}

/** One declared parameter, as the bridge publishes it. */
export interface ParamSpec {
  name: string;
  type: ParamType;
  required: boolean;
  description: string;
  /** Other names the registry renames to `name` before the handler runs. */
  aliases?: string[];
  /** Element type of an `array` parameter. */
  items?: ParamType;
  /** JSON null is a value of its own, such as "clear the reference". */
  nullable?: boolean;
  /** Further types the value may take instead of `type`. */
  orTypes?: ParamType[];
  /** The one value the parameter accepts. */
  literal?: boolean | string | number;
  /** The shape of an `object` parameter, or of each element of an array of objects. */
  fields?: ParamField[];
  /** The named shapes an `any` parameter takes. */
  forms?: ValueForm[];
  /** The variants of a tagged `object` parameter, or of each element of a tagged array of objects. */
  oneOf?: ParamOneOf;
  /** The only values a string parameter, or each string of an array parameter, may take. */
  enum?: string[];
  /** Inclusive lower bound of a number or integer parameter, or of each element of an array parameter. */
  minimum?: number;
  /** Inclusive upper bound, as minimum. */
  maximum?: number;
}

/** How many branches of a choice a call supplies. Mirrors EMCPChoiceMode. */
export const CHOICE_MODES = ["exactlyOne", "atLeastOne"] as const;
export type ChoiceMode = (typeof CHOICE_MODES)[number];

/**
 * A required choice between parameters. Each branch is a set of names that go
 * together: `settings OR propertyName + propertyValue` has the branches
 * `[["settings"], ["propertyName", "propertyValue"]]`. Every name is a
 * declared, optional parameter; the group is what is required.
 */
export interface ParamChoice {
  mode: ChoiceMode;
  branches: string[][];
}

/** One handler's contract. */
export interface HandlerSpec {
  category?: string;
  params: ParamSpec[];
  /** Required choices between the declared parameters. */
  choices?: ParamChoice[];
  /**
   * Why the C++ contract test does not call this handler: its contract values
   * would reach a create, spawn, save or run. The surface is generated from it
   * like any other; tests/unit/handler-spec-exempt.test.ts holds the handler
   * source to it instead.
   */
  contractExempt?: string;
}

/** Every spec'd handler, keyed by bridge method. */
export type HandlerSpecs = Record<string, HandlerSpec>;

const IDENTIFIER = /^[A-Za-z_][A-Za-z0-9_]*$/;

/**
 * Why a set of specs cannot generate a surface, one line per problem. Mirrors
 * FMCPHandlerRegistry::ValidateParamSpecs, and refuses the dispatcher's routing
 * names for the same reason: a parameter called `action` would land on top of
 * the parameter that selects the action.
 */
export function specProblems(specs: HandlerSpecs): string[] {
  const routing = new Set(ROUTING_PARAM_NAMES);
  const problems: string[] = [];
  for (const [method, spec] of Object.entries(specs)) {
    if (!IDENTIFIER.test(method)) problems.push(`${method}: not a method name`);
    if (!Array.isArray(spec?.params)) {
      problems.push(`${method}: params is not an array`);
      continue;
    }
    const seen = new Set<string>();
    for (const param of spec.params) {
      const names = [param.name, ...(param.aliases ?? [])];
      for (const name of names) {
        if (typeof name !== "string" || !IDENTIFIER.test(name)) {
          problems.push(`${method}: '${String(name)}' is not an identifier`);
        } else if (routing.has(name)) {
          problems.push(`${method}: '${name}' is a routing name the dispatcher consumes`);
        } else if (seen.has(name)) {
          problems.push(`${method}: '${name}' is declared twice`);
        }
        seen.add(name);
      }
      if (!PARAM_TYPES.includes(param.type)) problems.push(`${method}.${param.name}: unknown type '${param.type}'`);
      if (param.items !== undefined) {
        if (param.type !== "array") problems.push(`${method}.${param.name}: items on a non-array`);
        else if (!PARAM_TYPES.includes(param.items)) problems.push(`${method}.${param.name}: unknown item type '${param.items}'`);
      }
      if (typeof param.required !== "boolean") problems.push(`${method}.${param.name}: required is not a boolean`);
      if (typeof param.description !== "string") problems.push(`${method}.${param.name}: description is not a string`);
      problems.push(...shapeProblems(method, param));
    }
    problems.push(...choiceProblems(method, spec));
    if (spec.contractExempt !== undefined && (typeof spec.contractExempt !== "string" || spec.contractExempt.trim() === "")) {
      problems.push(`${method}: a contract exemption needs a reason`);
    }
  }
  return problems;
}

/** Mirrors FMCPHandlerRegistry::ValidateValueShape. */
function shapeProblems(method: string, param: ParamSpec): string[] {
  const at = `${method}.${param.name}`;
  const problems: string[] = [];
  if (param.nullable !== undefined && param.nullable !== true) problems.push(`${at}: nullable is written only as true`);
  const orTypes = param.orTypes ?? [];
  orTypes.forEach((orType, index) => {
    if (!PARAM_TYPES.includes(orType)) problems.push(`${at}: unknown alternative type '${orType}'`);
    else if (orType === "any" || param.type === "any") problems.push(`${at}: a union with any, which already accepts everything`);
    else if (orType === "array") problems.push(`${at}: array as an alternative type; declare it as the parameter's own type`);
    else if (orType === param.type) problems.push(`${at}: its own type listed as an alternative`);
    else if (orTypes.indexOf(orType) !== index) problems.push(`${at}: an alternative type listed twice`);
  });
  problems.push(...valueRuleProblems(at, param));
  if (param.enum !== undefined && (param.literal !== undefined || orTypes.length > 0)) {
    problems.push(`${at}: an enum that is also a literal or a union`);
  }
  if (param.literal !== undefined) {
    const fits = (param.type === "boolean" && typeof param.literal === "boolean")
      || (param.type === "string" && typeof param.literal === "string")
      || ((param.type === "number" || param.type === "integer") && typeof param.literal === "number");
    if (!fits) problems.push(`${at}: a literal that is not a value of its type`);
    if (orTypes.length > 0) problems.push(`${at}: both a literal and a union`);
  }
  const objectShaped = param.type === "object" || (param.type === "array" && param.items === "object");
  if (param.fields !== undefined) {
    if (!objectShaped) problems.push(`${at}: fields on something that is neither an object nor an array of objects`);
    problems.push(...fieldListProblems(at, param.fields));
  }
  if (param.forms !== undefined) {
    if (param.type !== "any") problems.push(`${at}: forms on a parameter that is not of type any; the forms are its type`);
    if (param.literal !== undefined || param.fields !== undefined || param.oneOf !== undefined) {
      problems.push(`${at}: forms together with a literal, fields or variants`);
    }
    problems.push(...formProblems(at, param.forms));
  }
  if (param.oneOf !== undefined) {
    const { key, variants } = param.oneOf;
    if (!objectShaped) problems.push(`${at}: a tagged union on something that is neither an object nor an array of objects`);
    if (param.fields !== undefined || orTypes.length > 0 || param.literal !== undefined) {
      problems.push(`${at}: a tagged union that also declares fields, a union or a literal`);
    }
    if (typeof key !== "string" || !IDENTIFIER.test(key)) problems.push(`${at}: tag field '${String(key)}' is not an identifier`);
    if (!Array.isArray(variants) || variants.length < 2) {
      problems.push(`${at}: a tagged union with fewer than two variants`);
    } else {
      const tags = new Set<string>();
      for (const variant of variants) {
        if (typeof variant.tag !== "string" || variant.tag === "" || tags.has(variant.tag)) {
          problems.push(`${at}: variant tag '${String(variant.tag)}' is empty or declared twice`);
        }
        tags.add(variant.tag);
        if (typeof variant.description !== "string") problems.push(`${at}[${key}=${variant.tag}]: description is not a string`);
        if (!Array.isArray(variant.fields)) {
          problems.push(`${at}[${key}=${variant.tag}]: fields is not an array`);
          continue;
        }
        if (variant.fields.some((f) => f.name === key)) problems.push(`${at}[${key}=${variant.tag}]: a field named after its tag`);
        problems.push(...fieldListProblems(`${at}[${key}=${variant.tag}]`, variant.fields));
      }
    }
  }
  return problems;
}

/** Mirrors FMCPHandlerRegistry::ValidateField, over a list that must not repeat a name. */
function fieldListProblems(at: string, fields: readonly ParamField[]): string[] {
  const problems: string[] = [];
  const seen = new Set<string>();
  for (const field of fields) {
    if (typeof field.name !== "string" || !IDENTIFIER.test(field.name)) problems.push(`${at}: field '${String(field.name)}' is not an identifier`);
    else if (seen.has(field.name)) problems.push(`${at}: field '${field.name}' is declared twice`);
    seen.add(field.name);
    if (!PARAM_TYPES.includes(field.type)) problems.push(`${at}.${field.name}: unknown type '${field.type}'`);
    if (field.items !== undefined && field.type !== "array") problems.push(`${at}.${field.name}: items on a non-array`);
    if (typeof field.required !== "boolean") problems.push(`${at}.${field.name}: required is not a boolean`);
    if (typeof field.description !== "string") problems.push(`${at}.${field.name}: description is not a string`);
    if (field.forms !== undefined) {
      if (field.type !== "any") problems.push(`${at}.${field.name}: forms on a field that is not of type any`);
      problems.push(...formProblems(`${at}.${field.name}`, field.forms));
    }
    problems.push(...valueRuleProblems(`${at}.${field.name}`, field));
    if (field.fields !== undefined) {
      if (field.type !== "object" && !(field.type === "array" && field.items === "object")) {
        problems.push(`${at}.${field.name}: fields on something that is neither an object nor an array of objects`);
      }
      problems.push(...fieldListProblems(`${at}.${field.name}`, field.fields));
    }
  }
  return problems;
}

/** Mirrors FMCPHandlerRegistry::ValidateValueRules. Rules on an array apply to each element. */
function valueRuleProblems(
  at: string,
  value: { type: ParamType; items?: ParamType; enum?: string[]; minimum?: number; maximum?: number },
): string[] {
  const problems: string[] = [];
  const valueType = value.type === "array" ? value.items ?? "any" : value.type;
  if (value.enum !== undefined) {
    if (valueType !== "string") problems.push(`${at}: enum values on something that is not a string or an array of strings`);
    if (!Array.isArray(value.enum) || value.enum.length === 0) problems.push(`${at}: enum is not a non-empty array`);
    else if (value.enum.some((v, i) => typeof v !== "string" || v === "" || value.enum!.indexOf(v) !== i)) {
      problems.push(`${at}: an enum value listed twice, or an empty one`);
    } else if (value.enum.some((v) => /[,{}]/.test(v))) {
      problems.push(`${at}: an enum value containing ',', '{' or '}', which a signature cannot write`);
    }
  }
  for (const bound of ["minimum", "maximum"] as const) {
    if (value[bound] === undefined) continue;
    if (typeof value[bound] !== "number" || !Number.isFinite(value[bound])) problems.push(`${at}: ${bound} is not a finite number`);
    if (valueType !== "number" && valueType !== "integer") problems.push(`${at}: a numeric range on something that is not a number or an integer`);
  }
  if (value.minimum !== undefined && value.maximum !== undefined && value.minimum > value.maximum) {
    problems.push(`${at}: a minimum above its maximum`);
  }
  return problems;
}

function formProblems(at: string, forms: readonly ValueForm[]): string[] {
  if (!Array.isArray(forms) || forms.length === 0) return [`${at}: forms is not a non-empty array`];
  const problems: string[] = [];
  forms.forEach((form, index) => {
    if (!VALUE_FORMS.includes(form)) problems.push(`${at}: unknown form '${String(form)}'`);
    else if (forms.indexOf(form) !== index) problems.push(`${at}: form '${form}' listed twice`);
  });
  return problems;
}

/**
 * Mirrors FMCPHandlerRegistry::ValidateHandlerSpec's choice rules, plus one the
 * clause grammar needs: an `at least one of a/b` group keeps collecting the bare
 * names that follow it, so a required parameter may not come right after one.
 */
function choiceProblems(method: string, spec: HandlerSpec): string[] {
  if (spec.choices === undefined) return [];
  if (!Array.isArray(spec.choices)) return [`${method}: choices is not an array`];
  const problems: string[] = [];
  const byName = new Map(spec.params.map((p) => [p.name, p]));
  const chosen = new Set<string>();
  spec.choices.forEach((choice, index) => {
    if (!CHOICE_MODES.includes(choice.mode)) problems.push(`${method}: choice ${index} has an unknown mode '${choice.mode}'`);
    if (!Array.isArray(choice.branches) || choice.branches.length < 2) {
      problems.push(`${method}: choice ${index} offers fewer than two branches`);
      return;
    }
    for (const branch of choice.branches) {
      if (!Array.isArray(branch) || branch.length === 0) {
        problems.push(`${method}: choice ${index} has an empty branch`);
        continue;
      }
      for (const name of branch) {
        const param = byName.get(name);
        if (!param) problems.push(`${method}: choice ${index} names '${name}', which is not a declared parameter`);
        else if (param.required) problems.push(`${method}: '${name}' is required and also a side of choice ${index}`);
        if (chosen.has(name)) problems.push(`${method}: '${name}' appears in more than one branch or choice`);
        chosen.add(name);
      }
    }
  });
  if (problems.length > 0) return problems;
  const items = clauseItems(spec);
  items.forEach((item, index) => {
    const next = items[index + 1];
    if (item.kind === "choice" && item.choice.mode === "atLeastOne" && next?.kind === "param"
      && next.param.required && !next.param.aliases?.length) {
      problems.push(
        `${method}: required '${next.param.name}' follows an at-least-one choice, and the clause would read it as one `
        + "more branch; declare it before the choice's first member",
      );
    }
  });
  return problems;
}

/** The items of a Params clause, in spec order: a choice sits where its first member is declared. */
type ClauseItem = { kind: "param"; param: ParamSpec } | { kind: "choice"; choice: ParamChoice };

export function clauseItems(spec: HandlerSpec): ClauseItem[] {
  const choiceOf = new Map<string, ParamChoice>();
  for (const choice of spec.choices ?? []) for (const branch of choice.branches) for (const name of branch) choiceOf.set(name, choice);
  const placed = new Set<ParamChoice>();
  const items: ClauseItem[] = [];
  for (const param of spec.params) {
    const choice = choiceOf.get(param.name);
    if (!choice) items.push({ kind: "param", param });
    else if (!placed.has(choice)) {
      placed.add(choice);
      items.push({ kind: "choice", choice });
    }
  }
  return items;
}

/**
 * Why a call's parameters do not satisfy the spec's choices, or undefined when
 * they do. A name counts as supplied under any of its aliases, since the
 * registry resolves them only after this check, and a null counts as supplied.
 */
export function choiceViolation(
  spec: { params: readonly ParamSpec[]; choices?: readonly ParamChoice[] },
  supplied: Readonly<Record<string, unknown>>,
): string | undefined {
  if (!spec.choices?.length) return undefined;
  const aliases = new Map(spec.params.map((p) => [p.name, p.aliases ?? []]));
  const given = (name: string): boolean =>
    [name, ...(aliases.get(name) ?? [])].some((key) => Object.prototype.hasOwnProperty.call(supplied, key) && supplied[key] !== undefined);
  for (const choice of spec.choices) {
    const rendered = renderChoice(choice, spec.params);
    const touched = choice.branches.filter((branch) => branch.some(given));
    if (touched.length === 0) {
      return `needs ${rendered}, and none was given`;
    }
    if (choice.mode === "exactlyOne" && touched.length > 1) {
      return `takes one side of ${rendered}, and got ${touched.map((b) => b.filter(given).join(" + ")).join(" and ")}`;
    }
    for (const branch of touched) {
      const missing = branch.filter((name) => !given(name));
      if (missing.length > 0) {
        return `got ${branch.filter(given).join(" + ")} without ${missing.join(" + ")}; ${branch.join(" + ")} go together in ${rendered}`;
      }
    }
  }
  return undefined;
}

/** A name as a clause writes it: with its aliases as `(or alias)`. */
function clauseName(param: ParamSpec | undefined, name: string): string {
  const aliases = param?.aliases?.length ? ` (${param.aliases.map((a) => `or ${a}`).join(", ")})` : "";
  return `${name}${aliases}`;
}

/**
 * One choice as the Params clause writes it. `exactlyOne` joins its branches
 * with OR (`actorLabel OR actorPath`); `atLeastOne` is quantified
 * (`at least one of labelPrefix/tag`), with OR between branches only when a
 * branch holds more than one name. Names inside a branch join with ` + `.
 */
export function renderChoice(choice: ParamChoice, params: readonly ParamSpec[]): string {
  const byName = new Map(params.map((p) => [p.name, p]));
  const branch = (names: string[]): string => names.map((n) => clauseName(byName.get(n), n)).join(" + ");
  if (choice.mode === "exactlyOne") return choice.branches.map(branch).join(" OR ");
  const single = choice.branches.every((b) => b.length === 1);
  return `at least one of ${choice.branches.map(branch).join(single ? "/" : " OR ")}`;
}

/**
 * The `Params:` clause for one handler, in the grammar parseParams reads:
 * required names bare, optional ones with `?`, aliases as `(or alias)`, and a
 * choice where its first member is declared, written as renderChoice writes it
 * (`actorLabel OR actorPath`, `at least one of labelPrefix/tag`).
 */
export function paramsClause(spec: HandlerSpec): string {
  if (spec.params.length === 0) return "Params: none";
  const items = clauseItems(spec).map((item) => {
    if (item.kind === "choice") return renderChoice(item.choice, spec.params);
    const p = item.param;
    const aliases = p.aliases?.length ? ` (${p.aliases.map((a) => `or ${a}`).join(", ")})` : "";
    return `${p.name}${p.required ? "" : "?"}${aliases}`;
  });
  return `Params: ${items.join(", ")}`;
}

/**
 * Build the action declaration for a spec'd bridge method: the summary a person
 * wrote, then the generated `Params:` clause. There is no mapParams, because the
 * spec names are the bridge names and renames are the registry's aliases. The
 * action carries the spec itself, so describe_action reports it verbatim.
 */
export function makeSpecBp(clauses: Readonly<Record<string, string>>, specs: HandlerSpecs = {}) {
  return (effect: ActionEffect, summary: string, bridge: string): BridgeActionSpec => {
    const clause = clauses[bridge];
    if (clause === undefined) {
      throw new Error(
        `No recorded parameter spec for bridge method '${bridge}'. Register it with a spec in C++, `
        + "then run npm run specs:record and npm run specs:generate.",
      );
    }
    // The literal `bp` builds. Importing `bp` would load category-tool.ts,
    // which reaches this module again through the call pipeline.
    const action: BridgeActionSpec = {
      kind: "bridge",
      effect,
      description: `${summary} ${clause}`,
      bridge,
      mapParams: undefined,
    };
    const spec = specs[bridge];
    if (!spec) return action;
    return spec.choices?.length
      ? { ...action, paramSpec: spec.params, paramChoices: spec.choices }
      : { ...action, paramSpec: spec.params };
  };
}

/**
 * Named keys of another category's generated schema, for an action that
 * dispatches to that category's handler. Throws on a key the spec lacks.
 */
export function borrowSchema(
  schema: Readonly<Record<string, z.ZodType>>,
  keys: readonly string[],
): Record<string, z.ZodType> {
  const out: Record<string, z.ZodType> = {};
  for (const key of keys) {
    const entry = schema[key];
    if (!entry) throw new Error(`borrowSchema: the generated schema declares no '${key}'`);
    out[key] = entry;
  }
  return out;
}

const ZOD_BASE: Record<ParamType, () => z.ZodTypeAny> = {
  string: () => z.string(),
  number: () => z.number(),
  integer: () => z.number().int(),
  boolean: () => z.boolean(),
  object: () => z.record(z.string(), z.unknown()),
  array: () => z.array(z.unknown()),
  vec3: () => z.object({ x: z.number(), y: z.number(), z: z.number() }),
  rotator: () => z.object({ pitch: z.number(), yaw: z.number(), roll: z.number() }),
  color: () => z.object({ r: z.number(), g: z.number(), b: z.number(), a: z.number().optional() }),
  any: () => z.unknown(),
};

// The value forms, as fresh instances every time: a reused instance makes the
// JSON Schema converter emit `$ref` pointers, which a client can fail to resolve.
// Every member is concrete, never z.unknown(), which converts to `{}` (#811).
const argScalar = () => z.union([z.string(), z.number(), z.boolean(), z.null()]);
// `additionalProperties: true`, since zod 4 writes `{}`, the empty schema #811 is about.
const argStruct = () => z.object({}).passthrough().meta({ additionalProperties: true });
const argValue = () => z.union([argScalar(), argStruct(), z.array(z.union([argScalar(), argStruct(), z.array(argScalar())]))]);

const FORM_ZOD: Record<ValueForm, () => z.ZodTypeAny> = {
  argMap: () => z.record(z.string(), argValue()),
  argEntryList: () => z.array(z.object({ name: z.string(), value: argValue().optional() })),
  scalarMap: () => z.record(z.string(), argScalar()),
  stringList: () => z.array(z.string()),
  string: () => z.string(),
};

/** How a refusal names each form, in the order the value declares them. */
export const FORM_PHRASE: Record<ValueForm, string> = {
  argMap: 'an object mapping parameter name to value (e.g. {"bEnabled": true})',
  argEntryList: 'an entry list ([{"name": "bEnabled", "value": true}])',
  scalarMap: 'an object mapping a name to a string, number, boolean or null (e.g. {"Radius": 400})',
  stringList: "an array of positional strings",
  string: "a string",
};

/** The message a value that fits none of its forms is refused with. */
export function formsMessage(name: string, forms: readonly ValueForm[]): string {
  const phrases = forms.map((f) => FORM_PHRASE[f]);
  const listed = phrases.length === 1 ? phrases[0] : `${phrases.slice(0, -1).join(", ")}, or ${phrases[phrases.length - 1]}`;
  return `${name} must be ${listed}`;
}

function formsZod(name: string, forms: readonly ValueForm[]): z.ZodTypeAny {
  if (forms.length === 1) return FORM_ZOD[forms[0]]();
  const message = formsMessage(name, forms);
  return z.union(forms.map((f) => FORM_ZOD[f]()) as [z.ZodTypeAny, z.ZodTypeAny, ...z.ZodTypeAny[]], {
    error: message,
  });
}

/** A scalar of one type with its enum or range applied. */
function ruledZod(type: ParamType, rules: { enum?: string[]; minimum?: number; maximum?: number }): z.ZodTypeAny {
  if (type === "string" && rules.enum?.length) return z.enum(rules.enum as [string, ...string[]]);
  if ((type === "number" || type === "integer") && (rules.minimum !== undefined || rules.maximum !== undefined)) {
    let n = type === "integer" ? z.number().int() : z.number();
    if (rules.minimum !== undefined) n = n.min(rules.minimum);
    if (rules.maximum !== undefined) n = n.max(rules.maximum);
    return n;
  }
  return ZOD_BASE[type]();
}

/** `strict` makes every declared object refuse keys it does not declare, as a strict contract's top level does. */
interface ZodOptions { strict?: boolean }

function fieldZod(f: ParamField, opts: ZodOptions): z.ZodTypeAny {
  if (f.forms?.length) return formsZod(f.name, f.forms);
  if (f.fields) return f.type === "array" ? z.array(fieldsZod(f.fields, opts)) : fieldsZod(f.fields, opts);
  return f.type === "array" ? z.array(ruledZod(f.items ?? "any", f)) : ruledZod(f.type, f);
}

function fieldEntries(fields: readonly ParamField[], opts: ZodOptions): Record<string, z.ZodTypeAny> {
  return Object.fromEntries(fields.map((f) => {
    const base = fieldZod(f, opts);
    return [f.name, (f.required ? base : base.optional()).describe(f.description)];
  }));
}

function fieldsZod(fields: readonly ParamField[], opts: ZodOptions): z.ZodTypeAny {
  const object = z.object(fieldEntries(fields, opts));
  return opts.strict ? object.strict() : object;
}

/**
 * A tagged union: one strict object per variant, its tag a literal. Strict,
 * because a variant is exactly its declared fields; a key that belongs to
 * another variant is a mistake to refuse, not one to strip silently.
 */
function oneOfZod(oneOf: ParamOneOf, opts: ZodOptions): z.ZodTypeAny {
  const variants = oneOf.variants.map((v) =>
    z.object({ [oneOf.key]: z.literal(v.tag), ...fieldEntries(v.fields, opts) }).strict().describe(v.description),
  );
  return z.discriminatedUnion(
    oneOf.key,
    variants as unknown as [z.ZodObject, ...z.ZodObject[]],
  );
}

/**
 * The zod schema one declared parameter accepts, without optionality or its
 * description. The runtime twin of the expression scripts/lib/handler-spec-gen.mjs
 * writes into a generated module; tests/unit/handler-specs.test.ts holds the two
 * to one signature.
 */
export function paramZod(param: ParamSpec, opts: ZodOptions = {}): z.ZodTypeAny {
  let base: z.ZodTypeAny;
  const element = (): z.ZodTypeAny =>
    param.oneOf ? oneOfZod(param.oneOf, opts) : param.fields ? fieldsZod(param.fields, opts) : ruledZod(param.items ?? "any", param);
  if (param.literal !== undefined) base = z.literal(param.literal);
  else if (param.forms?.length) base = formsZod(param.name, param.forms);
  else if (param.type === "array") base = z.array(element());
  else if (param.type === "object" && (param.fields || param.oneOf)) base = element();
  else base = ruledZod(param.type, param);
  if (param.orTypes?.length) {
    base = z.union([base, ...param.orTypes.map((t) => ZOD_BASE[t]())] as [z.ZodTypeAny, z.ZodTypeAny, ...z.ZodTypeAny[]]);
  }
  return param.nullable ? base.nullable() : base;
}

/**
 * A type signature for a zod schema that ignores descriptions, so two
 * declarations of one key can be compared for what they accept. With
 * `rules`, enums and numeric ranges count too; without, an enum reads as
 * string and a range is dropped, which is what a type comparison against a
 * hand-written key wants.
 */
export function zodSignature(schema: z.ZodTypeAny, opts: { rules?: boolean } = {}): string {
  const sig = (inner: z.ZodTypeAny): string => zodSignature(inner, opts);
  const def = schema._zod.def as { type: string; discriminator?: string };
  switch (def.type) {
    case "optional":
      return `${sig((schema as z.ZodOptional<z.ZodTypeAny>).unwrap())}?`;
    case "nullable":
      return `${sig((schema as z.ZodNullable<z.ZodTypeAny>).unwrap())}|null`;
    case "pipe":
      return "ZodEffects";
    case "string":
      return "string";
    case "number": {
      const range = ((schema as z.ZodNumber)._zod.def.checks ?? [])
        .map((c) => c._zod.def as { check: string; value?: number })
        .filter((c) => c.check === "greater_than" || c.check === "less_than")
        .map((c) => `${c.check === "greater_than" ? "min" : "max"}=${c.value}`);
      const kind = (schema as z.ZodNumber).isInt ? "integer" : "number";
      return opts.rules && range.length ? `${kind}[${range.join(",")}]` : kind;
    }
    case "enum":
      return opts.rules ? `enum<${(schema as z.ZodEnum).options.join("|")}>` : "string";
    case "boolean":
      return "boolean";
    case "unknown":
    case "any":
      return "any";
    case "array":
      return `array<${sig((schema as z.ZodArray<z.ZodTypeAny>).element)}>`;
    case "record":
      return `record<${sig((schema as z.ZodRecord).valueType as z.ZodTypeAny)}>`;
    case "object": {
      const shape = (schema as z.ZodObject).shape;
      return `{${Object.keys(shape).sort().map((k) => `${k}:${sig(shape[k] as z.ZodTypeAny)}`).join(",")}}`;
    }
    case "union": {
      const options = (schema as z.ZodUnion).options as readonly z.ZodTypeAny[];
      if (def.discriminator !== undefined) return `oneOf<${def.discriminator}>(${options.map((o) => sig(o)).join("|")})`;
      return options.map(sig).join("|");
    }
    case "null":
      return "null";
    case "literal":
      return `literal<${JSON.stringify((schema as z.ZodLiteral).value)}>`;
    default:
      return def.type ?? "unknown";
  }
}

/** What a connected editor's specs say about the recording this server was built from. */
export interface HandlerSpecDrift {
  /** False when the editor published no specs, so nothing was compared. */
  checked: boolean;
  /** Methods whose live contract differs from the recording, or that only one side has. */
  drifted: string[];
}

function canonicalField(f: ParamField): unknown[] {
  return [
    f.name, f.type, f.required, f.description, f.items ?? null, [...(f.forms ?? [])],
    [...(f.enum ?? [])], f.minimum ?? null, f.maximum ?? null, (f.fields ?? []).map(canonicalField),
  ];
}

function canonical(spec: HandlerSpec | undefined): string {
  if (!spec) return "";
  return JSON.stringify([
    spec.params.map((p) => [
      p.name, p.type, p.required, p.description, [...(p.aliases ?? [])], p.items ?? null,
      p.nullable ?? false, [...(p.orTypes ?? [])], p.literal ?? null,
      (p.fields ?? []).map(canonicalField),
      [...(p.forms ?? [])],
      p.oneOf ? [p.oneOf.key, p.oneOf.variants.map((v) => [v.tag, v.description, v.fields.map(canonicalField)])] : null,
      [...(p.enum ?? [])], p.minimum ?? null, p.maximum ?? null,
    ]),
    (spec.choices ?? []).map((c) => [c.mode, c.branches]),
    spec.contractExempt ?? null,
  ]);
}

/**
 * Compare a live `handlerSpecs` answer against the recording. A difference
 * means the surface this server advertises for those actions was generated
 * from a contract the running plugin no longer has.
 */
export function compareHandlerSpecs(recorded: HandlerSpecs, live: unknown): HandlerSpecDrift {
  if (!live || typeof live !== "object" || Array.isArray(live)) return { checked: false, drifted: [] };
  const liveSpecs = live as HandlerSpecs;
  const methods = new Set([...Object.keys(recorded), ...Object.keys(liveSpecs)]);
  const drifted = [...methods].filter((m) => canonical(recorded[m]) !== canonical(liveSpecs[m])).sort();
  return { checked: true, drifted };
}

/** What one action's call is checked against before it is sent (#1282). */
export interface ParamContract {
  params: readonly ParamSpec[];
  choices?: readonly ParamChoice[];
  /**
   * Also refuse a missing required name and any key the spec does not declare.
   * Set for plugin handlers, which have no read tracking; a core handler
   * reports both itself (RequireString, paramsNotRead).
   */
  strict?: boolean;
}

/**
 * Why a call's parameters break its action's contract, or undefined when they
 * keep it: a value its declared type, enum, range or fields refuse, an unmet
 * choice, and under `strict` a required name absent under every spelling or an
 * undeclared key. The category's flat shape is shared by every action, so only
 * this per-action check can hold a key to the one action's declaration.
 */
export function contractViolation(contract: ParamContract, supplied: Readonly<Record<string, unknown>>): string | undefined {
  const present = (key: string): boolean => Object.prototype.hasOwnProperty.call(supplied, key) && supplied[key] !== undefined;
  const declared = new Set<string>();
  for (const param of contract.params) {
    const names = [param.name, ...(param.aliases ?? [])];
    names.forEach((n) => declared.add(n));
    const given = names.filter(present);
    // The bridge keeps the name and leaves the alias unread, and a plugin handler reports no unread keys.
    if (contract.strict && given.length > 1) return `got ${given.join(" and ")}, which are one parameter; pass only ${param.name}`;
    if (given.length === 0) {
      if (param.required && contract.strict) return `needs ${clauseName(param, param.name)}, and it was not given`;
      continue;
    }
    for (const key of given) {
      const parsed = paramZod(param, { strict: contract.strict }).safeParse(supplied[key]);
      if (!parsed.success) {
        const issue = parsed.error.issues[0];
        const at = [key, ...issue.path].join(".");
        return `got ${JSON.stringify(supplied[key])} for ${at}: ${issue.message}`;
      }
    }
  }
  if (contract.strict) {
    const unknown = Object.keys(supplied).filter((k) => present(k) && !declared.has(k));
    if (unknown.length > 0) {
      const names = contract.params.map((p) => p.name);
      return `does not take ${unknown.join(", ")}; it takes ${names.length ? names.join(", ") : "no parameters"}`;
    }
  }
  return choiceViolation({ params: contract.params, choices: contract.choices }, supplied);
}

/**
 * The flat category shape for a set of spec'd handlers that share one tool:
 * every declared name and alias, optional. A key whose declarations differ
 * across handlers accepts any of them here; the per-action contract holds each
 * call to its own action's declaration.
 */
export function flatContractShape(handlers: ReadonlyArray<readonly [string, HandlerSpec]>): Record<string, z.ZodType> {
  const byKey = new Map<string, { schemas: Map<string, z.ZodTypeAny>; descriptions: Map<string, string[]> }>();
  for (const [method, spec] of handlers) {
    for (const param of spec.params) {
      const schema = paramZod(param, { strict: true });
      const signature = zodSignature(schema, { rules: true });
      for (const key of [param.name, ...(param.aliases ?? [])]) {
        const entry = byKey.get(key) ?? { schemas: new Map(), descriptions: new Map() };
        if (!entry.schemas.has(signature)) entry.schemas.set(signature, schema);
        const text = key === param.name ? param.description : `Alias for ${param.name}`;
        entry.descriptions.set(text, [...(entry.descriptions.get(text) ?? []), method]);
        byKey.set(key, entry);
      }
    }
  }
  const out: Record<string, z.ZodType> = {};
  for (const [key, { schemas, descriptions }] of [...byKey.entries()].sort(([a], [b]) => a.localeCompare(b))) {
    const members = [...schemas.values()];
    const schema = members.length === 1 ? members[0] : z.union(members as [z.ZodTypeAny, z.ZodTypeAny, ...z.ZodTypeAny[]]);
    const described = [...descriptions.entries()];
    const description = described.length === 1
      ? described[0][0]
      : described.map(([text, owners]) => `${text} (${owners.join(", ")})`).join(". ");
    out[key] = schema.optional().describe(description);
  }
  return out;
}

/** A plugin's recorded handler specs file, as `ue-mcp plugin record-specs` writes it (#1282). */
export interface RecordedHandlerSpecs {
  handlerCount: number;
  handlers: HandlerSpecs;
}

/** Why a recorded specs file cannot be used, one line per problem; empty when it can. */
export function recordedSpecsProblems(raw: unknown): string[] {
  if (!raw || typeof raw !== "object" || Array.isArray(raw)) return ["not a JSON object"];
  const handlers = (raw as { handlers?: unknown }).handlers;
  if (!handlers || typeof handlers !== "object" || Array.isArray(handlers)) return ["handlers is not an object"];
  return specProblems(handlers as HandlerSpecs);
}

/**
 * The contract a plugin handler's call is held to: the spec the running
 * module registered when it differs from the recording and is well formed,
 * else the recording (#1290). The bridge enforces the live spec anyway, so
 * refusing a parameter it accepts only blocks the call until a reconnect.
 */
export function liveContract(
  recorded: ParamContract,
  method: string,
  livePluginSpecs: Record<string, unknown> | undefined,
): { contract: ParamContract; adopted: boolean; problem?: string } {
  const live = livePluginSpecs?.[method];
  if (live === undefined) return { contract: recorded, adopted: false };
  const problems = specProblems({ [method]: live } as HandlerSpecs);
  if (problems.length > 0) return { contract: recorded, adopted: false, problem: problems[0] };
  const spec = live as HandlerSpec;
  const same = (s: { params: readonly ParamSpec[]; choices?: readonly ParamChoice[] }) =>
    canonical({ params: s.params, choices: s.choices } as HandlerSpec);
  if (same(recorded) === same(spec)) {
    return { contract: recorded, adopted: false };
  }
  return { contract: { params: spec.params, choices: spec.choices, strict: recorded.strict }, adopted: true };
}
