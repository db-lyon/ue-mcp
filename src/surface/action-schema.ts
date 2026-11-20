// @session independent: Derives one action's parameter schema from whichever tool graph it is handed; holds nothing of its own.
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
 * This module answers the question directly, from the action's structured
 * declaration and the category's zod shape:
 *
 *   documented  the names the action declares: its C++ spec (paramSpec), its
 *               TypeScript `params`, or a wrapped engine tool's properties
 *   declared    the category's zod shape, which is what the wire accepts
 *
 * `declared` is what is load-bearing at runtime, so a documented name absent
 * from it is a silent drop rather than a documentation nit. `drift` reports
 * exactly those, and a unit test gates the whole surface on it.
 */
import { z } from "zod";
import { ROUTING_PARAM_NAMES } from "./routing-params.js";
import type { ActionEffectSource, ActionSpec, ParamDecl, ToolDef } from "./tool-types.js";
import { paramZod, type ParamSpec, type ValueForm } from "./handler-spec.js";
import type { ActionClass } from "./action-class.js";
import { EPIC_ASSET_KEYS, EPIC_ENVELOPE_KEYS, epicBridgeKeys, epicForwardedParams, epicPropZod } from "./epic-input.js";

/** A readable schema summary, not a substitute for runtime validation. */
export interface ValueSchema {
  /** Wire type, unwrapped through optional/default/nullable. */
  type: string;
  required: boolean;
  description?: string;
  /** Allowed values, when the parameter is an enum or a union of literals. */
  enumValues?: string[];
  /** Inclusive bounds of a number. */
  minimum?: number;
  maximum?: number;
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
  sources: Array<"documented" | "declared">;
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
   * Names the action declares that the category's shape does not. Passing one
   * of these has no effect.
   */
  drift: string[];
}

/* ── zod introspection ─────────────────────────────────────────────── */

interface ZodDef {
  type?: string;
  innerType?: z.ZodTypeAny;
  in?: z.ZodTypeAny;
  out?: z.ZodTypeAny;
  element?: z.ZodTypeAny;
  entries?: Record<string, unknown>;
  values?: unknown[];
  options?: z.ZodTypeAny[];
  discriminator?: string;
  defaultValue?: unknown;
  valueType?: z.ZodTypeAny;
}

function defOf(schema: z.ZodTypeAny): ZodDef {
  return (schema as unknown as { _zod?: { def?: ZodDef } })._zod?.def ?? {};
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
    const name = def.type;
    let next: z.ZodTypeAny | undefined;
    if (name === "optional" || name === "nullable") {
      optional = true;
      next = def.innerType;
    } else if (name === "default" || name === "prefault") {
      optional = true;
      if (dflt === undefined) dflt = def.defaultValue;
      next = def.innerType;
    } else if (name === "readonly") {
      next = def.innerType;
    } else if (name === "pipe") {
      next = def.in && defOf(def.in).type === "transform" ? def.out : def.in;
    } else {
      break;
    }
    if (!next) break;
    cur = next;
    description = description ?? cur.description;
  }
  return { inner: cur, optional, description, default: dflt };
}

/** A short, agent-readable name for a zod type. */
function typeName(schema: z.ZodTypeAny): string {
  const def = defOf(schema);
  switch (def.type) {
    case "string": return "string";
    case "number": return (schema as z.ZodNumber).isInt ? "integer" : "number";
    case "boolean": return "boolean";
    case "unknown": case "any": return "any";
    case "enum": return "enum";
    case "literal": return "literal";
    case "record": return "object";
    case "object": return "object";
    case "array": {
      const el = def.element;
      return el ? `${typeName(unwrap(el).inner)}[]` : "array";
    }
    case "union": {
      // Every option of a tagged union is an object; `discriminator` says which field picks it.
      if (def.discriminator) return "object";
      const opts = def.options ?? [];
      const names = [...new Set(opts.map((o) => typeName(unwrap(o).inner)))];
      return names.join("|") || "union";
    }
    default: return def.type ?? "unknown";
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
  scalarMap: "object",
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
  if (def.type === "enum" && def.entries) {
    return Object.values(def.entries).map((v) => String(v));
  }
  if (def.type === "union" && !def.discriminator && Array.isArray(def.options)) {
    const literals: string[] = [];
    for (const opt of def.options) {
      const od = defOf(unwrap(opt).inner);
      if (od.type !== "literal" || od.values?.length !== 1) return undefined;
      literals.push(String(od.values[0]));
    }
    return literals.length > 0 ? literals : undefined;
  }
  return undefined;
}

function numberBounds(schema: z.ZodTypeAny): { minimum?: number; maximum?: number } {
  if (!(schema instanceof z.ZodNumber)) return {};
  const out: { minimum?: number; maximum?: number } = {};
  for (const check of schema._zod.def.checks ?? []) {
    const cd = check._zod.def as { check: string; value?: number; inclusive?: boolean };
    if (cd.check === "greater_than") out.minimum = cd.value;
    if (cd.check === "less_than") out.maximum = cd.value;
  }
  return out;
}

/** Reuse the declared shape so nested argument names never become another catalog. */
function valueSchema(schema: z.ZodTypeAny, depth = 0): ValueSchema {
  const { inner, description, default: dflt } = unwrap(schema);
  const result: ValueSchema = {
    type: typeName(inner), required: !schema.isOptional(), description,
    enumValues: enumValues(inner), default: dflt, ...numberBounds(inner),
  };
  // Bound the recursion so one deeply nested parameter cannot dominate a
  // discovery response. Deeper shapes are still validated at call time.
  if (depth >= 6) return { ...result, truncated: true };
  if (inner instanceof z.ZodObject) {
    result.properties = Object.fromEntries(Object.entries(inner.shape).map(([name, child]) =>
      [name, valueSchema(child as z.ZodTypeAny, depth + 1)],
    ));
  } else if (inner instanceof z.ZodArray) {
    result.items = valueSchema(inner.element as z.ZodTypeAny, depth + 1);
  } else if (inner instanceof z.ZodRecord) {
    result.items = valueSchema(inner.valueType as z.ZodTypeAny, depth + 1);
  } else if (inner instanceof z.ZodUnion) {
    const def = defOf(inner);
    if (def.discriminator) result.discriminator = def.discriminator;
    result.variants = (inner.options as z.ZodTypeAny[]).map((option) => valueSchema(option, depth + 1));
  }
  return result;
}

/**
 * A required choice an action declares.
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

/** The keys one action accepts, and the names its refusal lists. */
export interface ActionAccepts {
  keys: ReadonlySet<string>;
  takes: readonly string[];
}

/** The refusal for keys an action does not read. */
export function undeclaredMessage(action: string, unknown: readonly string[], accepts: ActionAccepts): string {
  return `${action} does not take ${unknown.join(", ")}; it takes `
    + `${accepts.takes.length ? accepts.takes.join(", ") : "no parameters"}, so the call was not sent. `
    + `project(action="describe_action") lists what it takes.`;
}

const acceptsCache = new WeakMap<ActionSpec, ActionAccepts | null>();

/**
 * The keys an action reads, when they can be known, so an undeclared one is
 * refused instead of dropped (#1336): a spec'd action's names and aliases, a
 * wrapped engine tool's properties and envelope, or the names a TypeScript
 * action declares in `params`. An action that declares none of them (a plugin
 * handler with no recorded spec) accepts whatever its category shape does.
 */
export function actionAccepts(spec: ActionSpec): ActionAccepts | undefined {
  if (acceptsCache.has(spec)) return acceptsCache.get(spec) ?? undefined;
  const found = computeAccepts(spec);
  acceptsCache.set(spec, found ?? null);
  return found;
}

function computeAccepts(spec: ActionSpec): ActionAccepts | undefined {
  if (spec.paramSpec) {
    const takes = spec.paramSpec.map((p) => p.name);
    return { keys: new Set([...takes, ...spec.paramSpec.flatMap((p) => p.aliases ?? [])]), takes };
  }
  if (spec.kind === "bridge" && spec.epicTool) {
    const takes = [...epicForwardedParams(spec.epicSchema), ...epicBridgeKeys(spec.epicTool)];
    return { keys: new Set([...takes, ...EPIC_ENVELOPE_KEYS, ...EPIC_ASSET_KEYS]), takes };
  }
  if (!spec.params) return undefined;
  const takes = declaredNames(spec.params);
  return { keys: new Set(takes), takes };
}

/** Every name a TypeScript declaration takes, required first. */
export function declaredNames(decl: ParamDecl): string[] {
  return [...new Set([...(decl.required ?? []), ...(decl.optional ?? [])])];
}

/* ── the schema itself ─────────────────────────────────────────────── */

/** Routing instructions the dispatcher consumes and strips before a handler
 *  ever sees them. They are real wire parameters, so they are reported, but
 *  they are never counted as drift against an action's own documentation.
 *
 *  Also the set a generated surface must never declare, so
 *  scripts/generate-epic-actions.mjs imports it rather than keeping a copy. */
export const ROUTING_PARAMS: ReadonlySet<string> = new Set(ROUTING_PARAM_NAMES);

/** The names an action declares: its C++ spec's (aliases included), its TypeScript `params`, or a wrapped engine tool's properties. */
function documentedNames(spec: ActionSpec): Set<string> {
  if (spec.paramSpec) return new Set(spec.paramSpec.flatMap((p) => [p.name, ...(p.aliases ?? [])]));
  if (spec.params) return new Set(declaredNames(spec.params));
  if (spec.kind === "bridge" && spec.epicTool) {
    return new Set([...epicForwardedParams(spec.epicSchema), ...epicBridgeKeys(spec.epicTool)]);
  }
  return new Set();
}

/** Build the full schema for one action of one tool. */
export function actionSchema(tool: ToolDef, action: string): ActionSchema {
  const spec = tool.actions[action];
  if (!spec) {
    throw new Error(
      `Unknown action '${action}' on tool '${tool.name}'. Available: ${Object.keys(tool.actions).join(", ")}`,
    );
  }
  const recorded = spec.paramSpec;
  const decl = spec.params;
  const epic = spec.kind === "bridge" ? spec.epicSchema : undefined;
  const named = documentedNames(spec);
  const recordedByName = new Map((recorded ?? []).map((p) => [p.name, p]));
  const recordedAliases = new Set((recorded ?? []).flatMap((p) => p.aliases ?? []));
  const required = new Set(decl?.required ?? epic?.required ?? []);

  // A spec's choices are published as declared. A TypeScript action's choice
  // only survives while more than one of its branches is reachable:
  // `asset.migrate` takes `toEditor OR destinationContentDir`, and with one
  // editor registered `toEditor` is not in the shape at all, which leaves an
  // ordinary required parameter rather than a choice.
  const alternatives: AlternativeGroup[] = [];
  const groupOf = new Map<string, number>();
  for (const choice of (recorded ? spec.paramChoices : decl?.choices) ?? []) {
    const branches = recorded
      ? choice.branches.map((b) => [...b])
      : choice.branches.map((b) => b.filter((n) => n in tool.schema)).filter((b) => b.length > 0);
    if (branches.length === 1) for (const name of branches[0]) required.add(name);
    if (branches.length < 2) continue;
    const index = alternatives.length;
    alternatives.push({ branches, required: true });
    for (const branch of branches) for (const name of branch) groupOf.set(name, index);
  }

  const params: ParamSchema[] = [];
  const covered = new Set<string>();

  for (const [name, schema] of Object.entries(tool.schema)) {
    if (name === "action") continue;
    // A category's shape is one flat bag shared by all its actions, so most of
    // its keys belong to some other action. Report the ones this action takes,
    // plus the routing parameters, which every action accepts.
    const documented = named.has(name);
    if (!documented && !ROUTING_PARAMS.has(name)) continue;
    covered.add(name);
    // An alias is reported on the parameter it names, not as one of its own.
    if (recordedAliases.has(name)) continue;
    const sources: ParamSchema["sources"] = documented ? ["documented", "declared"] : ["declared"];
    const { inner, optional, description: paramDoc, default: dflt } = unwrap(schema);
    const declared = recordedByName.get(name);
    if (declared) {
      // The action's own declaration, not the category key, which other actions share (#1282).
      const own = valueSchema(paramZod(declared));
      params.push({
        ...own,
        name,
        type: specParamTypeName(declared),
        required: declared.required,
        description: declared.description || paramDoc,
        enumValues: declared.literal !== undefined ? [String(declared.literal)] : own.enumValues,
        default: dflt,
        sources,
        alternativeGroup: groupOf.get(name),
        aliases: declared.aliases?.length ? [...declared.aliases] : undefined,
      });
      continue;
    }
    // A wrapped engine tool's recorded property carries its own item fields and
    // enums, which the category's shared key cannot (#1281).
    const epicProp = epic?.properties?.[name];
    const own = epicProp ? valueSchema(epicPropZod(epicProp)) : undefined;
    const group = groupOf.get(name);
    params.push({
      ...(own ?? valueSchema(schema)),
      name,
      type: own ? own.type : typeName(inner),
      // The declaration wins over the category shape, which has to declare
      // nearly everything optional to let its other actions through.
      required: documented && (decl || epic) ? group === undefined && required.has(name) : !optional,
      description: decl?.notes?.[name] ?? paramDoc,
      enumValues: own ? own.enumValues : enumValues(inner),
      default: dflt,
      sources,
      alternativeGroup: group,
    });
  }

  // Routing parameters are consumed by the dispatcher, and two of them are
  // injected into the shape only while this server drives more than one
  // editor, so their absence from a single-editor graph is not drift.
  const drift = [...named].filter((name) => !ROUTING_PARAMS.has(name) && !covered.has(name) && !(name in tool.schema));

  params.sort((a, b) => {
    if (a.required !== b.required) return a.required ? -1 : 1;
    return a.name.localeCompare(b.name);
  });

  return {
    tool: tool.name,
    action,
    description: spec.description ?? "",
    bridge: spec.bridge,
    local: !spec.bridge,
    timeoutMs: spec.timeoutMs,
    class: spec.effect,
    classSource: spec.effectSource ?? "declared",
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

