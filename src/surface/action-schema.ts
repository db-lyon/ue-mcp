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
 *   documented  what the action declares: its `options`, its recorded C++
 *               spec, its Epic input schema or its plugin manifest schema
 *   forwards    the keys the action's `mapParams` closure actually reads
 *   declared    the category's zod shape, which is what the wire accepts
 *
 * `declared` is the only one of the three that is load-bearing at runtime, so
 * a name present in either of the other two and absent from it is a silent
 * drop rather than a documentation nit. `schemaDrift()` reports exactly those,
 * and a unit test gates the whole surface on it.
 */
import { z } from "zod";
import { ROUTING_PARAM_NAMES } from "./routing-params.js";
import type { ActionEffectSource, ActionSpec, HandlerOptions, ToolDef } from "../core/types.js";
import type { ParamSpec, ValueForm } from "./handler-spec.js";
import type { ActionClass } from "./action-class.js";
import { epicForwardedParams } from "./epic-input.js";

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
  /** For a tagged union: the field whose literal value picks one of `variants`. */
  discriminator?: string;
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
  /** Other names the bridge accepts for this parameter, from a spec'd action's C++ spec. */
  aliases?: string[];
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
   * Whether this observes the editor or changes it, as the action DECLARES it.
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
   *
   * Read straight off the ActionSpec. It used to be recomputed here from the
   * action's name, which meant this field could disagree with the gate that
   * actually stops the call.
   */
  class: ActionClass;
  /**
   * Where that answer came from. `declared` is a person's, written at the
   * declaration. `inferred` is the verb lexicon's, and appears only on the
   * actions this package does not declare: Epic's wrapped engine tools and a
   * plugin action whose manifest did not say. A caller building its own
   * approval policy should treat `inferred` reads with more suspicion than
   * declared ones.
   */
  classSource: ActionEffectSource;
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
  /** Set when the action declares no parameters anywhere, so `params` cannot list them. */
  paramsNote?: string;
}

/** What describe says about a plugin action whose manifest has no `schema:`. */
export const UNDECLARED_PLUGIN_PARAMS =
  "This plugin action's manifest declares no schema, so its parameters are not listed here. "
  + "Read them from the description, and ask the plugin's author to add a schema: block.";

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
    // Every option of a tagged union is an object; `discriminator` says which field picks it.
    case "ZodDiscriminatedUnion": return "object";
    default: return def.typeName ? def.typeName.replace(/^Zod/, "").toLowerCase() : "unknown";
  }
}

/** A spec's wire type in typeName's vocabulary; vec3, rotator and color are objects. */
function specTypeName(type: string, items?: string): string {
  if (type === "array") return `${specTypeName(items ?? "any")}[]`;
  return type === "vec3" || type === "rotator" || type === "color" ? "object" : type;
}

/** Each value form in typeName's vocabulary. */
const FORM_TYPE_NAME: Record<ValueForm, string> = {
  argMap: "object",
  argEntryList: "object[]",
  stringList: "string[]",
  string: "string",
};

/** One declared parameter's type: a union's alternatives joined with `|`, and null when it takes one. */
function specParamTypeName(param: ParamSpec): string {
  if (param.forms?.length) return [...new Set(param.forms.map((f) => FORM_TYPE_NAME[f]))].join("|");
  const names = [specTypeName(param.type, (param.fields || param.oneOf) && param.type === "array" ? "object" : param.items)];
  for (const orType of param.orTypes ?? []) {
    const name = specTypeName(orType);
    if (!names.includes(name)) names.push(name);
  }
  if (param.nullable) names.push("null");
  return names.join("|");
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
  // Bound the recursion so one deeply nested parameter cannot dominate a
  // discovery response. Deeper shapes are still validated at call time.
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
  } else if (inner instanceof z.ZodDiscriminatedUnion) {
    result.discriminator = inner.discriminator;
    result.variants = (inner.options as z.ZodTypeAny[]).map((option) => valueSchema(option, depth + 1));
  }
  return result;
}

/* ── declared parameters ───────────────────────────────────────────── */

/**
 * A parameter an action declares.
 *
 * `group` is set when it is one of a choice (`actorLabel OR actorPath`). A
 * member of a choice is never individually required, because the handler
 * takes either.
 */
export interface DocumentedParam {
  name: string;
  optional: boolean;
  /** Index into `alternatives`, when this name is one of a choice. */
  group?: number;
}

/**
 * A choice the action offers.
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

/** An in-process action's declared parameters, in the shape the schema builder reads. */
export function declaredOptions(options: HandlerOptions): DocumentedParams {
  const alternatives: AlternativeGroup[] = (options.choices ?? []).map((choice) => ({
    branches: choice.branches.map((branch) => [...branch]),
    required: choice.required,
  }));
  const params = options.params.map((raw): DocumentedParam => {
    const name = raw.endsWith("?") ? raw.slice(0, -1) : raw;
    const param: DocumentedParam = { name, optional: raw.endsWith("?") };
    const group = alternatives.findIndex((alt) => alt.branches.some((branch) => branch.includes(name)));
    if (group >= 0) param.group = group;
    return param;
  });
  return { params, alternatives };
}

/**
 * The parameters an action declares in structured form: a handler's options,
 * a bridge method's recorded C++ spec, an Epic tool's input schema, a plugin
 * manifest's schema. Undefined for an action that declares none of these.
 */
function structuredParams(spec: ActionSpec): DocumentedParams | undefined {
  if (spec.kind === "handler") return spec.options ? declaredOptions(spec.options) : undefined;
  if (spec.kind === "registry") {
    if (!spec.optionsSchema) return undefined;
    const params = Object.entries(spec.optionsSchema).map(([name, field]) => ({ name, optional: field.required !== true }));
    return { params, alternatives: [] };
  }
  if (spec.kind === "flow" && !spec.paramSpec) return spec.options ? declaredOptions(spec.options) : undefined;
  if (spec.kind !== "bridge" && spec.kind !== "flow") return undefined;
  if (spec.paramSpec) {
    // Aliases are named too: the category declares them, and describe folds them onto their parameter.
    const params = spec.paramSpec.flatMap((p) =>
      [p.name, ...(p.aliases ?? [])].map((name) => ({ name, optional: !p.required })));
    return { params, alternatives: [] };
  }
  if (spec.kind === "bridge" && spec.epicSchema) {
    const required = new Set(spec.epicSchema.required ?? []);
    const names = Object.keys(spec.epicSchema.properties ?? {});
    const params: DocumentedParam[] = names
      .filter((name) => !ROUTING_PARAMS.has(name))
      .map((name) => ({ name, optional: !required.has(name) }));
    // A tool argument named like a dispatcher key travels inside `input`.
    if (names.some((name) => ROUTING_PARAMS.has(name))) params.push({ name: "input", optional: true });
    return { params, alternatives: [] };
  }
  return undefined;
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
  // A wrapped engine tool declares its mapping instead of writing a closure:
  // it forwards the properties its own input schema names.
  if (spec.kind === "bridge" && !spec.mapParams && spec.epicTool) {
    return epicForwardedParams(spec.epicSchema);
  }
  // A flow-backed action declares what it reads.
  if (spec.kind === "flow") return Object.keys(spec.inputs);
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
 *  they are never counted as drift against an action's own documentation.
 *
 *  Also the set a generated surface must never declare, so
 *  scripts/generate-epic-actions.mjs imports it rather than keeping a copy. */
export const ROUTING_PARAMS: ReadonlySet<string> = new Set(ROUTING_PARAM_NAMES);

/** Build the full schema for one action of one tool. */
export function actionSchema(tool: ToolDef, action: string): ActionSchema {
  const spec = tool.actions[action];
  if (!spec) {
    throw new Error(
      `Unknown action '${action}' on tool '${tool.name}'. Available: ${Object.keys(tool.actions).join(", ")}`,
    );
  }
  const description = spec.description ?? "";
  const declaredNames = new Set(Object.keys(tool.schema));
  // Never read from the description: an action that declares nothing documents nothing.
  const structured = structuredParams(spec);
  const parsed = structured ?? { params: [], alternatives: [] };
  // A plugin field's type is the wire's, compiled from the manifest unless a built-in key shadows it.
  const manifest = spec.kind === "registry" ? spec.optionsSchema : undefined;
  const documented = parsed.params;
  const forwards = new Set(forwardedParams(spec));
  const documentedByName = new Map(documented.map((d) => [d.name, d]));

  // A choice only survives while more than one of its branches is reachable.
  // `asset.migrate` documents `toEditor OR destinationContentDir`, and with
  // one editor registered `toEditor` is not in the shape at all, which leaves
  // an ordinary required parameter rather than a choice.
  // A spec'd action's parameters are its C++ spec's (#1057). Its clause is
  // generated, so an `(or x)` there is an alias of one parameter, not a
  // choice between two, and each name is required exactly when the spec says.
  const recorded = spec.kind === "bridge" || spec.kind === "flow" ? spec.paramSpec : undefined;
  const recordedByName = new Map((recorded ?? []).map((p) => [p.name, p]));
  const recordedAliases = new Set((recorded ?? []).flatMap((p) => p.aliases ?? []));

  const alternatives: AlternativeGroup[] = [];
  const groupIndex = new Map<number, number>();
  // A spec's choices are declared, not parsed: each is published with the
  // branches the spec names, and every one of them is required.
  const specGroup = new Map<string, number>();
  if (recorded) (spec.kind === "bridge" || spec.kind === "flow" ? spec.paramChoices ?? [] : []).forEach((choice) => {
    const index = alternatives.length;
    alternatives.push({ branches: choice.branches.map((b) => [...b]), required: true });
    for (const branch of choice.branches) for (const name of branch) specGroup.set(name, index);
  });
  if (!recorded) parsed.alternatives.forEach((group, i) => {
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
    // An alias is reported on the parameter it names, not as one of its own.
    if (recordedAliases.has(name)) continue;
    const declared = recordedByName.get(name);
    if (declared) {
      params.push({
        ...valueSchema(schema),
        name,
        type: specParamTypeName(declared),
        required: declared.required,
        description: declared.description || paramDoc,
        enumValues: declared.literal !== undefined ? [String(declared.literal)] : enumValues(inner),
        default: dflt,
        sources,
        alternativeGroup: specGroup.get(name),
        aliases: declared.aliases?.length ? [...declared.aliases] : undefined,
      });
      continue;
    }
    params.push({
      ...valueSchema(schema),
      name,
      type: typeName(inner),
      // The description's `?` marker wins: it is per-action, whereas the
      // category shape has to declare nearly everything optional to let its
      // other actions through.
      required: doc ? group === undefined && !doc.optional : !optional,
      description: manifest?.[name]?.description ?? paramDoc,
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
    class: spec.effect,
    classSource: spec.effectSource ?? "declared",
    params,
    alternatives: alternatives.length > 0 ? alternatives : undefined,
    drift,
    paramsNote: !structured && spec.kind === "registry" ? UNDECLARED_PLUGIN_PARAMS : undefined,
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

/** How a project adds an action of its own, for a miss that is not a typo (#1105). */
const PROJECT_ACTIONS_HINT =
  "To add a project-side action, register it from a native-module plugin with UEMCP::RegisterExternalHandler"
  + " and list it in a handlers: manifest (docs/plugins-native-modules.md). Edits to the vendored bridge plugin"
  + " are overwritten on the next deploy.";

/**
 * The refusal for an action a category does not have. Leads with the closest
 * spellings; with none, the name is probably not a typo, so it says how a
 * project adds one.
 */
export function unknownActionMessage(action: string, category: string, available: string[]): string {
  const close = nearestActions(action, available);
  return `Unknown action '${action}' on '${category}'.`
    + (close.length ? ` Did you mean: ${close.join(", ")}?` : "")
    + ` ${available.length} actions available - project(action="describe_action", category="${category}")`
    + ` lists them with their parameters, and project(action="search_tools") searches by intent.`
    + (close.length ? "" : ` ${PROJECT_ACTIONS_HINT}`);
}

