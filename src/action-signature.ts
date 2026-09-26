/**
 * One-line action signatures, the unit the compact context seed is built from
 * (#1172).
 *
 * The advertised surface used to flatten every action's parameters into one
 * JSON Schema per category and repeat the prose Params clause in each tool
 * description, which cost roughly 270k tokens before an agent made a call. A
 * signature carries the same names, optionality, aliases, choices and types in
 * a few dozen characters:
 *
 *     set_property(assetPath|path, propertyName, value:*, save?:b)
 *
 * Every signature is built from structured data, never from prose: a spec'd
 * bridge action from its recorded C++ spec, an `epic_*` action from the input
 * schema it was generated with, and anything else (in-process handlers, plugin
 * actions) from the declared schema `describe_action` reports. The notation is
 * explained once, by SIGNATURE_LEGEND, in the server instructions.
 */
import type { ParamChoice, ParamSpec, ParamType, ValueForm } from "./handler-spec.js";
import { clauseItems } from "./handler-spec.js";
import { actionSchema, ROUTING_PARAMS } from "./action-schema.js";
import type { EpicInputSchema } from "./epic-input.js";
import type { ActionSpec, ToolDef } from "./types.js";

/** The notation, written once in `instructions` rather than in every tool. */
export const SIGNATURE_LEGEND =
  "Signatures read action(param, ...). Suffix ? = optional. a|b = one parameter, b an accepted alias. " +
  "one(a; b+c) = give exactly one group, any(a; b) = at least one. " +
  "Type after a colon, none = string: s string, n number, i integer, b boolean, o object, v {x,y,z}, " +
  "r {pitch,yaw,roll}, c {r,g,b,a?}, ref asset path or {refPath}, * any JSON, [t] array of t, t/u either, =x only x, " +
  "o<k> object whose field k picks its shape (describe_action lists them). " +
  "+N = N more optional params.";

/** One parameter as a signature writes it. */
interface SigParam {
  /** `name` or `name|alias`. */
  label: string;
  /** Empty for a string. */
  type: string;
  required: boolean;
}

/** A signature item: a parameter, or a choice between groups of them. */
type SigItem =
  | { kind: "param"; param: SigParam }
  | { kind: "choice"; mode: ParamChoice["mode"]; branches: SigParam[][] };

const SPEC_TYPE: Record<ParamType, string> = {
  string: "",
  number: "n",
  integer: "i",
  boolean: "b",
  object: "o",
  array: "[]",
  vec3: "v",
  rotator: "r",
  color: "c",
  any: "*",
};

/** A type code for use inside `[...]` or after `/`, where string cannot be implicit. */
function explicit(code: string): string {
  return code === "" ? "s" : code;
}

/** Each value form as the type codes already say it. */
const FORM_TYPE: Record<ValueForm, string> = {
  argMap: "o",
  argEntryList: "[o]",
  stringList: "[s]",
  string: "s",
};

function specType(param: ParamSpec): string {
  if (param.literal !== undefined) return `=${typeof param.literal === "string" ? param.literal : JSON.stringify(param.literal)}`;
  if (param.forms?.length) return param.forms.length === 1 && param.forms[0] === "string" ? "" : param.forms.map((f) => FORM_TYPE[f]).join("/");
  const tagged = param.oneOf ? `o<${param.oneOf.key}>` : undefined;
  let base: string;
  if (param.type === "array") {
    const item = tagged ?? (param.fields ? "o" : param.items ? explicit(SPEC_TYPE[param.items]) : "*");
    base = `[${item}]`;
  } else {
    base = tagged && param.type === "object" ? tagged : SPEC_TYPE[param.type];
  }
  if (param.orTypes?.length) base = [explicit(base), ...param.orTypes.map((t) => explicit(SPEC_TYPE[t]))].join("/");
  return base;
}

function specParam(param: ParamSpec, inChoice: boolean): SigParam {
  const label = [param.name, ...(param.aliases ?? [])].join("|");
  return { label, type: specType(param), required: inChoice || param.required };
}

function specItems(params: readonly ParamSpec[], choices: readonly ParamChoice[] | undefined): SigItem[] {
  const byName = new Map(params.map((p) => [p.name, p]));
  return clauseItems({ params: [...params], choices: choices ? [...choices] : undefined }).map((item) => {
    if (item.kind === "param") return { kind: "param", param: specParam(item.param, false) };
    return {
      kind: "choice",
      mode: item.choice.mode,
      branches: item.choice.branches.map((branch) =>
        branch.map((name) => {
          const p = byName.get(name);
          return p ? specParam(p, true) : { label: name, type: "", required: true };
        }),
      ),
    };
  });
}

interface EpicProp {
  type?: string;
  properties?: Record<string, unknown>;
}

function epicType(prop: EpicProp | undefined): string {
  if (!prop || typeof prop !== "object") return "*";
  if (prop.type === "object" && prop.properties && "refPath" in prop.properties) return "ref";
  switch (prop.type) {
    case "string": return "";
    case "number": return "n";
    case "integer": return "i";
    case "boolean": return "b";
    case "object": return "o";
    case "array": return "[]";
    default: return "*";
  }
}

function epicItems(schema: EpicInputSchema): SigItem[] {
  const required = new Set(schema.required ?? []);
  const items: SigItem[] = [];
  let nested = false;
  for (const [name, prop] of Object.entries(schema.properties ?? {})) {
    // A tool argument named like a dispatcher key (`action`) cannot be sent at
    // the top level; it travels inside `input`, which the category declares.
    if (ROUTING_PARAMS.has(name)) {
      nested = true;
      continue;
    }
    items.push({ kind: "param", param: { label: name, type: epicType(prop as EpicProp), required: required.has(name) } });
  }
  if (nested) items.push({ kind: "param", param: { label: "input", type: "o", required: false } });
  return items;
}

/** describe_action's reading of a declared type, as a signature code. */
function declaredType(type: string): string {
  const one = (t: string): string => {
    if (t.endsWith("[]")) return `[${explicit(one(t.slice(0, -2)))}]`;
    switch (t) {
      case "string": case "enum": case "literal": return "";
      case "number": return "n";
      case "integer": return "i";
      case "boolean": return "b";
      case "object": return "o";
      case "array": return "[]";
      default: return "*";
    }
  };
  const parts = type.split("|").filter((t) => t !== "null");
  if (parts.length <= 1) return one(parts[0] ?? "any");
  return [...new Set(parts.map((t) => explicit(one(t))))].join("/");
}

function declaredItems(tool: ToolDef, action: string): SigItem[] {
  const schema = actionSchema(tool, action);
  const params = schema.params.filter((p) => !ROUTING_PARAMS.has(p.name));
  const byGroup = new Map<number, typeof params>();
  const items: SigItem[] = [];
  const placed = new Set<number>();
  for (const p of params) {
    if (p.alternativeGroup !== undefined) {
      const list = byGroup.get(p.alternativeGroup) ?? [];
      list.push(p);
      byGroup.set(p.alternativeGroup, list);
    }
  }
  for (const p of params) {
    const sig: SigParam = {
      label: [p.name, ...(p.aliases ?? [])].join("|"),
      type: declaredType(p.type),
      required: p.required,
    };
    const group = p.alternativeGroup;
    if (group === undefined) {
      items.push({ kind: "param", param: sig });
      continue;
    }
    if (placed.has(group)) continue;
    placed.add(group);
    const alt = schema.alternatives?.[group];
    const members = new Map((byGroup.get(group) ?? []).map((m) => [m.name, m]));
    if (!alt) continue;
    if (!alt.required) {
      // A choice nobody has to make is a set of optional parameters.
      for (const m of byGroup.get(group) ?? []) {
        items.push({ kind: "param", param: { label: m.name, type: declaredType(m.type), required: false } });
      }
      continue;
    }
    const branches = alt.branches.map((branch) =>
      branch.map((name) => {
        const m = members.get(name);
        return { label: name, type: m ? declaredType(m.type) : "", required: true };
      }),
    );
    items.push({ kind: "choice", mode: alt.required ? "exactlyOne" : "atLeastOne", branches });
  }
  // Required first, the order describe_action already reports.
  return items;
}

/** The structured items of one action's signature, from the best source it has. */
function signatureItems(tool: ToolDef, action: string, spec: ActionSpec): SigItem[] {
  if (spec.kind === "bridge" && spec.paramSpec) return specItems(spec.paramSpec, spec.paramChoices);
  if (spec.kind === "bridge" && spec.epicSchema) return epicItems(spec.epicSchema);
  try {
    return declaredItems(tool, action);
  } catch {
    return [];
  }
}

function renderParam(p: SigParam): string {
  return `${p.label}${p.required ? "" : "?"}${p.type ? `:${p.type}` : ""}`;
}

function renderItem(item: SigItem): string {
  if (item.kind === "param") return renderParam(item.param);
  const branches = item.branches.map((b) => b.map(renderParam).join("+")).join("; ");
  return `${item.mode === "exactlyOne" ? "one" : "any"}(${branches})`;
}

function isOptional(item: SigItem): boolean {
  return item.kind === "param" && !item.param.required;
}

export interface SignatureOptions {
  /**
   * Longest signature to return. Optional parameters are dropped from the end
   * first and counted as `+N`; required ones and choices are always kept.
   */
  maxLength?: number;
}

const cache = new WeakMap<ActionSpec, Map<string, string>>();

/**
 * The signature of one action: `name(param, param?, ...)`.
 *
 * Cached per action object and the name it is registered under, since describe
 * and search render the same actions over and over and one spec may be
 * registered twice. A plugin or enrichment that replaces an action replaces the
 * object, so the cache cannot serve a stale signature.
 */
export function actionSignature(tool: ToolDef, action: string, options: SignatureOptions = {}): string {
  const spec = tool.actions[action];
  if (!spec) return `${action}()`;
  const slot = `${tool.name}.${action}`;
  let full = cache.get(spec)?.get(slot);
  let items: SigItem[] | undefined;
  if (full === undefined) {
    items = signatureItems(tool, action, spec);
    full = `${action}(${items.map(renderItem).join(", ")})`;
    if (!cache.has(spec)) cache.set(spec, new Map());
    cache.get(spec)!.set(slot, full);
  }
  const max = options.maxLength;
  if (max === undefined || full.length <= max) return full;

  items ??= signatureItems(tool, action, spec);
  const kept = [...items];
  let dropped = 0;
  const render = (): string => `${action}(${[...kept.map(renderItem), ...(dropped ? [`+${dropped}`] : [])].join(", ")})`;
  let out = render();
  while (out.length > max) {
    let last = -1;
    for (let i = kept.length - 1; i >= 0; i--) {
      if (isOptional(kept[i])) { last = i; break; }
    }
    if (last < 0) break;
    kept.splice(last, 1);
    dropped++;
    out = render();
  }
  return out;
}
