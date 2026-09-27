/**
 * Parameter contracts authored in C++ (#1057).
 *
 * A handler registered with a spec declares its parameters once, at its
 * `RegisterHandler` call. The bridge publishes every spec in
 * `get_bridge_capabilities.handlerSpecs`; `npm run specs:record` writes that to
 * `tests/golden/handler-specs.json`, and `npm run specs:generate` turns the
 * recording into `src/tools/specs/<category>.generated.ts`: the specs and the
 * `Params:` clause of every spec'd action. The zod entries are built from the
 * specs at load, by categorySchema.
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
export const VALUE_FORMS = ["argMap", "argEntryList", "stringList", "string"] as const;
export type ValueForm = (typeof VALUE_FORMS)[number];

/**
 * Bounds on a value. Mirrors FMCPValueBounds: min and max bound a number,
 * minLength and maxLength a string, and on an array each applies to every
 * element; minItems and maxItems bound the array itself.
 */
export interface ValueBounds {
  min?: number;
  max?: number;
  minLength?: number;
  maxLength?: number;
  minItems?: number;
  maxItems?: number;
}

/**
 * What a parameter addresses, beyond its wire type. Mirrors EMCPParamRole.
 * Recorded only: the advertised schema never carries a role.
 */
export const PARAM_ROLES = ["editTarget", "nodeRef", "pinRef", "slotIndex", "actorRef", "outputPath"] as const;
export type ParamRole = (typeof PARAM_ROLES)[number];

/** What a call commits after its edit. Mirrors EMCPCommitPolicy; absent means the handler's own behaviour. */
export const COMMIT_POLICIES = ["none", "compile", "save", "both"] as const;
export type CommitPolicy = (typeof COMMIT_POLICIES)[number];

/** One field of an object parameter, or of each element of an array of objects. */
export interface ParamField extends ValueBounds {
  name: string;
  type: ParamType;
  required: boolean;
  description: string;
  /** Element type of an `array` field. */
  items?: ParamType;
  /** The named shapes an `any` field takes. */
  forms?: ValueForm[];
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
export interface ParamSpec extends ValueBounds {
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
  /** What the parameter addresses: `editTarget` is the asset the call writes. */
  role?: ParamRole;
  /** On an array of objects, the element keys that carry the role; the first one present counts. */
  roleKeys?: string[];
  /** The value the handler uses when the parameter is absent. */
  default?: unknown;
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
   * like any other; tests/unit/plugin/handler-spec-exempt.test.ts holds the handler
   * source to it instead.
   */
  contractExempt?: string;
  /** What the call commits after its edit. Absent means the handler's own behaviour. */
  commit?: CommitPolicy;
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
    if (spec.commit !== undefined && !COMMIT_POLICIES.includes(spec.commit)) {
      problems.push(`${method}: unknown commit policy '${String(spec.commit)}'`);
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
  const bounded = BOUND_KEYS.some((k) => param[k] !== undefined);
  if (bounded && param.literal !== undefined) problems.push(`${at}: a bounded literal, which already takes one value`);
  problems.push(...boundProblems(at, param.type, param.items, param));
  problems.push(...roleProblems(at, param));
  return problems;
}

/** Mirrors FMCPHandlerRegistry::ValidateRoleAndDefault. */
function roleProblems(at: string, param: ParamSpec): string[] {
  const problems: string[] = [];
  const holds = (t: ParamType) => param.type === t || (param.type === "array" && param.items === t);
  const objectElements = param.type === "array" && param.items === "object";
  const keyed = objectElements && (param.roleKeys?.length ?? 0) > 0;
  if (param.roleKeys !== undefined) {
    if (!objectElements) problems.push(`${at}: role keys on something that is not an array of objects`);
    if (param.role === undefined) problems.push(`${at}: role keys without a role`);
    if (!Array.isArray(param.roleKeys) || param.roleKeys.length === 0) problems.push(`${at}: roleKeys is not a non-empty array`);
    for (const key of param.roleKeys ?? []) {
      if (typeof key !== "string" || !IDENTIFIER.test(key)) problems.push(`${at}: role key '${String(key)}' is not an identifier`);
      else if (param.fields && !param.fields.some((f) => f.name === key)) problems.push(`${at}: role key '${key}' is not one of its declared fields`);
    }
  }
  if (param.role !== undefined) {
    if (!PARAM_ROLES.includes(param.role)) problems.push(`${at}: unknown role '${String(param.role)}'`);
    else if (param.role === "slotIndex") {
      if (!holds("integer") && !holds("number")) problems.push(`${at}: a slot index that is not a number`);
    } else if (param.role === "nodeRef" || param.role === "pinRef") {
      if (!holds("string") && !holds("integer") && !keyed) problems.push(`${at}: a node or pin reference that is not a string, an integer or an array of them`);
    } else if (!holds("string") && !keyed) {
      problems.push(`${at}: a path role on something that is not a string, an array of strings, or an array of objects with role keys`);
    }
    if (objectElements && !keyed) problems.push(`${at}: a role on an array of objects with no role keys`);
  }
  if (param.default !== undefined) {
    if (param.required) problems.push(`${at}: required and also has a default`);
    if (param.literal !== undefined) problems.push(`${at}: both a literal and a default`);
    const fits = (t: ParamType): boolean => {
      const v = param.default;
      switch (t) {
        case "any": return true;
        case "string": return typeof v === "string";
        case "boolean": return typeof v === "boolean";
        case "number": return typeof v === "number";
        case "integer": return typeof v === "number" && Number.isInteger(v);
        case "array": return Array.isArray(v);
        default: return typeof v === "object" && v !== null && !Array.isArray(v);
      }
    };
    const ok = (param.default === null && param.nullable === true) || [param.type, ...(param.orTypes ?? [])].some(fits);
    if (!ok) problems.push(`${at}: a default that is not a value of its type`);
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
    problems.push(...boundProblems(`${at}.${field.name}`, field.type, field.items, field));
  }
  return problems;
}

/** Mirrors FMCPHandlerRegistry::ValidateBounds. */
function boundProblems(at: string, type: ParamType, items: ParamType | undefined, bounds: ValueBounds): string[] {
  const problems: string[] = [];
  const valueType = type === "array" ? items ?? "any" : type;
  const numeric = valueType === "number" || valueType === "integer";
  const set = (v: number | undefined): v is number => v !== undefined;
  if ((set(bounds.min) || set(bounds.max)) && !numeric) problems.push(`${at}: a minimum or maximum on something that is not a number or an array of numbers`);
  if (valueType === "integer" && [bounds.min, bounds.max].some((v) => set(v) && !Number.isInteger(v))) problems.push(`${at}: an integer with a fractional bound`);
  if ((set(bounds.minLength) || set(bounds.maxLength)) && valueType !== "string") problems.push(`${at}: a length bound on something that is not a string or an array of strings`);
  if ((set(bounds.minItems) || set(bounds.maxItems)) && type !== "array") problems.push(`${at}: an item count bound on something that is not an array`);
  if ([bounds.minLength, bounds.maxLength, bounds.minItems, bounds.maxItems].some((v) => set(v) && (!Number.isInteger(v) || v < 0))) {
    problems.push(`${at}: a length or item count bound that is not a whole number of zero or more`);
  }
  const inverted = (lo: number | undefined, hi: number | undefined) => set(lo) && set(hi) && lo > hi;
  if (inverted(bounds.min, bounds.max) || inverted(bounds.minLength, bounds.maxLength) || inverted(bounds.minItems, bounds.maxItems)) {
    problems.push(`${at}: a lower bound above its upper bound`);
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

export function clauseItems(spec: Pick<HandlerSpec, "params" | "choices">): ClauseItem[] {
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
 * The `Params:` clause for one handler, in the grammar tests/helpers/params-clause.ts
 * reads: required names bare, optional ones with `?`, aliases as `(or alias)`,
 * and a choice where its first member is declared.
 */
export function paramsClause(spec: Pick<HandlerSpec, "params" | "choices">): string {
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
 * wrote, then the `Params:` clause derived from the spec. There is no mapParams,
 * because the spec names are the bridge names and renames are the registry's
 * aliases. The action carries the spec itself, so describe_action reports it verbatim.
 */
export function makeSpecBp(specs: HandlerSpecs) {
  return (effect: ActionEffect, summary: string, bridge: string): BridgeActionSpec => {
    const spec = specs[bridge];
    if (!spec) {
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
      description: `${summary} ${paramsClause(spec)}`,
      bridge,
      mapParams: undefined,
    };
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
  object: () => z.record(z.unknown()),
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
const argStruct = () => z.object({}).passthrough();
const argValue = () => z.union([argScalar(), argStruct(), z.array(z.union([argScalar(), argStruct(), z.array(argScalar())]))]);

const FORM_ZOD: Record<ValueForm, () => z.ZodTypeAny> = {
  argMap: () => z.record(z.string(), argValue()),
  argEntryList: () => z.array(z.object({ name: z.string(), value: argValue().optional() })),
  stringList: () => z.array(z.string()),
  string: () => z.string(),
};

/** How a refusal names each form, in the order the value declares them. */
export const FORM_PHRASE: Record<ValueForm, string> = {
  argMap: 'an object mapping parameter name to value (e.g. {"bEnabled": true})',
  argEntryList: 'an entry list ([{"name": "bEnabled", "value": true}])',
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
    errorMap: () => ({ message }),
  });
}

const BOUND_KEYS = ["min", "max", "minLength", "maxLength", "minItems", "maxItems"] as const;

/** A number or string schema with its min/max or length bounds. Anything else passes through. */
function boundedValue(schema: z.ZodTypeAny, bounds: ValueBounds): z.ZodTypeAny {
  if (schema instanceof z.ZodNumber) {
    let bounded = schema;
    if (bounds.min !== undefined) bounded = bounded.min(bounds.min);
    if (bounds.max !== undefined) bounded = bounded.max(bounds.max);
    return bounded;
  }
  if (schema instanceof z.ZodString) {
    let bounded = schema;
    if (bounds.minLength !== undefined) bounded = bounded.min(bounds.minLength);
    if (bounds.maxLength !== undefined) bounded = bounded.max(bounds.maxLength);
    return bounded;
  }
  return schema;
}

/** An array of element, with its item count bounds. */
function boundedArray(element: z.ZodTypeAny, bounds: ValueBounds): z.ZodTypeAny {
  let array = z.array(boundedValue(element, bounds));
  if (bounds.minItems !== undefined) array = array.min(bounds.minItems);
  if (bounds.maxItems !== undefined) array = array.max(bounds.maxItems);
  return array;
}

function fieldZod(f: ParamField): z.ZodTypeAny {
  if (f.forms?.length) return formsZod(f.name, f.forms);
  return f.type === "array" ? boundedArray(ZOD_BASE[f.items ?? "any"](), f) : boundedValue(ZOD_BASE[f.type](), f);
}

function fieldEntries(fields: readonly ParamField[]): Record<string, z.ZodTypeAny> {
  return Object.fromEntries(fields.map((f) => {
    const base = fieldZod(f);
    return [f.name, (f.required ? base : base.optional()).describe(f.description)];
  }));
}

function fieldsZod(fields: readonly ParamField[]): z.ZodTypeAny {
  return z.object(fieldEntries(fields));
}

/**
 * A tagged union: one strict object per variant, its tag a literal. Strict,
 * because a variant is exactly its declared fields; a key that belongs to
 * another variant is a mistake to refuse, not one to strip silently.
 */
function oneOfZod(oneOf: ParamOneOf): z.ZodTypeAny {
  const variants = oneOf.variants.map((v) =>
    z.object({ [oneOf.key]: z.literal(v.tag), ...fieldEntries(v.fields) }).strict().describe(v.description),
  );
  return z.discriminatedUnion(
    oneOf.key,
    variants as unknown as [z.ZodDiscriminatedUnionOption<string>, ...z.ZodDiscriminatedUnionOption<string>[]],
  );
}

/**
 * The zod schema one declared parameter accepts, without optionality or its
 * description. Every spec'd key's schema is built by this, at load.
 */
export function paramZod(param: ParamSpec): z.ZodTypeAny {
  let base: z.ZodTypeAny;
  const element = (): z.ZodTypeAny =>
    param.oneOf ? oneOfZod(param.oneOf) : param.fields ? fieldsZod(param.fields) : ZOD_BASE[param.items ?? "any"]();
  if (param.literal !== undefined) base = z.literal(param.literal);
  else if (param.forms?.length) base = formsZod(param.name, param.forms);
  else if (param.type === "array") base = boundedArray(element(), param);
  else if (param.type === "object" && (param.fields || param.oneOf)) base = element();
  else base = boundedValue(ZOD_BASE[param.type](), param);
  if (param.orTypes?.length) {
    base = z.union([base, ...param.orTypes.map((t) => ZOD_BASE[t]())] as [z.ZodTypeAny, z.ZodTypeAny, ...z.ZodTypeAny[]]);
  }
  return param.nullable ? base.nullable() : base;
}

/** What one declared parameter accepts, for telling two declarations of a key apart. */
function acceptedShape(param: ParamSpec): string {
  // A role or default says nothing about what the key accepts, so a reader and a writer may share it.
  const { name, description: _description, required: _required, aliases: _aliases, role: _role, roleKeys: _roleKeys, default: _default, ...shape } = param;
  // A forms refusal names the parameter, so the name is part of what it accepts.
  return JSON.stringify(shape.forms?.length ? { ...shape, formsOf: name } : shape);
}

/**
 * One zod entry per key across a category's specs, aliases included, sorted by
 * key. A key several handlers declare must accept the same thing in every one
 * of them, because the category's shape is shared; the descriptions are
 * merged, naming which handlers each belongs to when they differ.
 */
export function categorySchema(specs: HandlerSpecs): Record<string, z.ZodType> {
  const keys = new Map<string, { param: ParamSpec; shape: string; descriptions: Map<string, string[]> }>();
  const claim = (key: string, param: ParamSpec, description: string, method: string): void => {
    const shape = acceptedShape(param);
    const entry = keys.get(key);
    if (!entry) {
      keys.set(key, { param, shape, descriptions: new Map([[description, [method]]]) });
      return;
    }
    if (entry.shape !== shape) {
      throw new Error(`'${key}' is declared as ${entry.shape} and as ${shape} (${method}); one category key has one type`);
    }
    const owners = entry.descriptions.get(description);
    if (owners) owners.push(method);
    else entry.descriptions.set(description, [method]);
  };
  for (const [method, spec] of Object.entries(specs)) {
    for (const param of spec.params) {
      claim(param.name, param, param.description, method);
      for (const alias of param.aliases ?? []) claim(alias, param, `Alias for ${param.name}`, method);
    }
  }
  const out: Record<string, z.ZodType> = {};
  for (const [key, entry] of [...keys.entries()].sort(([a], [b]) => a.localeCompare(b))) {
    const described = [...entry.descriptions.entries()];
    const description = described.length === 1
      ? described[0][0]
      : described.map(([text, owners]) => `${text} (${owners.join(", ")})`).join(". ");
    out[key] = paramZod(entry.param).optional().describe(description);
  }
  return out;
}

/**
 * A type signature for a zod schema that ignores descriptions, so two
 * declarations of one key can be compared for what they accept.
 */
export function zodSignature(schema: z.ZodTypeAny): string {
  const def = schema._def as { typeName?: string; checks?: Array<{ kind: string }> };
  switch (def.typeName) {
    case "ZodOptional":
      return `${zodSignature((schema as z.ZodOptional<z.ZodTypeAny>).unwrap())}?`;
    case "ZodNullable":
      return `${zodSignature((schema as z.ZodNullable<z.ZodTypeAny>).unwrap())}|null`;
    case "ZodString":
      return "string";
    case "ZodNumber":
      return def.checks?.some((c) => c.kind === "int") ? "integer" : "number";
    case "ZodBoolean":
      return "boolean";
    case "ZodUnknown":
    case "ZodAny":
      return "any";
    case "ZodArray":
      return `array<${zodSignature((schema as z.ZodArray<z.ZodTypeAny>).element)}>`;
    case "ZodRecord":
      return `record<${zodSignature((schema as z.ZodRecord).valueSchema)}>`;
    case "ZodObject": {
      const shape = (schema as z.ZodObject<z.ZodRawShape>).shape;
      return `{${Object.keys(shape).sort().map((k) => `${k}:${zodSignature(shape[k])}`).join(",")}}`;
    }
    case "ZodUnion":
      return (schema as z.ZodUnion<[z.ZodTypeAny, ...z.ZodTypeAny[]]>).options.map(zodSignature).join("|");
    case "ZodDiscriminatedUnion": {
      const tagged = schema as z.ZodDiscriminatedUnion<string, z.ZodDiscriminatedUnionOption<string>[]>;
      return `oneOf<${tagged.discriminator}>(${tagged.options.map((o) => zodSignature(o)).join("|")})`;
    }
    case "ZodNull":
      return "null";
    case "ZodLiteral":
      return `literal<${JSON.stringify((schema as z.ZodLiteral<unknown>).value)}>`;
    default:
      return def.typeName ?? "unknown";
  }
}

/** What a connected editor's specs say about the recording this server was built from. */
export interface HandlerSpecDrift {
  /** False when the editor published no specs, so nothing was compared. */
  checked: boolean;
  /** Methods whose live contract differs from the recording, or that only one side has. */
  drifted: string[];
}

function canonicalBounds(b: ValueBounds): unknown[] {
  return BOUND_KEYS.map((k) => b[k] ?? null);
}

function canonicalField(f: ParamField): unknown[] {
  return [f.name, f.type, f.required, f.description, f.items ?? null, [...(f.forms ?? [])], canonicalBounds(f)];
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
      canonicalBounds(p),
      p.role ?? null, [...(p.roleKeys ?? [])], p.default === undefined ? null : JSON.stringify(p.default),
    ]),
    (spec.choices ?? []).map((c) => [c.mode, c.branches]),
    spec.contractExempt ?? null,
    spec.commit ?? null,
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
