/**
 * The argument envelope for a wrapped engine tool.
 *
 * Unreal's toolset registry takes one nested `input` object; a ue-mcp category
 * takes flat, canonical parameters. This is the seam between them, and it is
 * what lets a generated Epic action be called exactly like a native one:
 *
 *     gas(action="epic_list_attributes", className="UMyHealthSet")
 *
 * rather than making a caller hand-assemble `input`. An explicit `input` still
 * wins, and `inputJson` is the raw escape hatch.
 *
 * This used to live inside the runtime enrichment that built Epic actions from
 * the live catalog. It moved out when those actions became declared modules:
 * generated code calls `epicToolCall`, so nothing has to read a catalog at
 * startup to know how to dispatch one.
 */
import { z } from "zod";
import { McpError, ErrorCode } from "../core/errors.js";
import { coerceAssetPathValue } from "./asset-path.js";

/** The shape of one wrapped tool's JSON Schema, as far as this needs it. */
export interface EpicInputSchema {
  readonly properties?: Readonly<Record<string, unknown>>;
  readonly required?: readonly string[];
}

interface SchemaProp {
  type?: string;
  properties?: Record<string, unknown>;
  required?: string[];
  enum?: readonly string[];
  items?: unknown;
}

/**
 * Why a value does not fit its recorded property, naming the field, or
 * undefined when it fits (#1281). Checks enums and array/struct shape only;
 * field names and enum spellings match case-insensitively, as the engine reads them.
 */
export function epicValueProblem(propValue: unknown, value: unknown, at: string): string | undefined {
  const prop = asProp(propValue);
  if (!prop || isAssetRefProp(prop) || value === undefined || value === null) return undefined;
  if (prop.type === "string" && prop.enum?.length && typeof value === "string") {
    const want = value.toLowerCase();
    if (!prop.enum.some((e) => e.toLowerCase() === want)) {
      return `${at} is ${JSON.stringify(value)}, which is not one of ${prop.enum.join(", ")}`;
    }
  }
  if (prop.type === "array") {
    if (!Array.isArray(value)) return `${at} must be an array, got ${typeof value}`;
    for (let i = 0; i < value.length; i++) {
      const problem = epicValueProblem(prop.items, value[i], `${at}[${i}]`);
      if (problem) return problem;
    }
  }
  if (prop.type === "object" && prop.properties) {
    if (typeof value !== "object" || Array.isArray(value)) return `${at} must be an object, got ${Array.isArray(value) ? "array" : typeof value}`;
    const fields = Object.entries(prop.properties);
    for (const [key, field] of Object.entries(value as Record<string, unknown>)) {
      const match = fields.find(([name]) => name.toLowerCase() === key.toLowerCase());
      const problem = match ? epicValueProblem(match[1], field, `${at}.${key}`) : undefined;
      if (problem) return problem;
    }
  }
  return undefined;
}

/** The zod shape of one recorded property, for describe_action's nested fields and enums. */
export function epicPropZod(propValue: unknown, depth = 0): z.ZodTypeAny {
  const prop = asProp(propValue);
  if (!prop) return z.unknown();
  if (isAssetRefProp(prop)) return z.union([z.string(), z.record(z.string(), z.unknown())]);
  switch (prop.type) {
    case "string":
      return prop.enum?.length ? z.enum(prop.enum as [string, ...string[]]) : z.string();
    case "number": return z.number();
    case "integer": return z.number().int();
    case "boolean": return z.boolean();
    case "array": return z.array(prop.items && depth < 4 ? epicPropZod(prop.items, depth + 1) : z.unknown());
    case "object": {
      if (!prop.properties || depth >= 4) return z.record(z.string(), z.unknown());
      const shape = Object.fromEntries(Object.entries(prop.properties)
        .map(([k, v]) => [k, epicPropZod(v, depth + 1).optional()]));
      return z.object(shape).passthrough().meta({ additionalProperties: true });
    }
    default: return z.unknown();
  }
}

function asProp(value: unknown): SchemaProp | undefined {
  return value && typeof value === "object" ? (value as SchemaProp) : undefined;
}

/** True when a schema property is the engine's `{ refPath }` object reference. */
function isAssetRefProp(prop: SchemaProp | undefined): boolean {
  return !!prop && prop.type === "object" && !!prop.properties && "refPath" in prop.properties;
}

/**
 * Expand a bare package path to the object path an engine reference needs:
 * `/Game/X/SG_A` becomes `/Game/X/SG_A.SG_A` (#1289). A path that already names
 * an object, a subobject, or nothing mount-rooted is returned as given.
 */
export function expandRefPath(value: string): string {
  const path = value.trim().replace(/\\/g, "/");
  if (!path.startsWith("/") || path.includes(":")) return value;
  const leaf = path.slice(path.lastIndexOf("/") + 1);
  if (leaf === "" || leaf.includes(".") || path.split("/").filter(Boolean).length < 2) return value;
  return `${path}.${leaf}`;
}

/** A `{ refPath }` value with its path expanded; anything else unchanged. */
function expandRefValue(value: unknown): unknown {
  if (typeof value === "string") return { refPath: expandRefPath(value) };
  if (value && typeof value === "object" && !Array.isArray(value)) {
    const ref = (value as Record<string, unknown>).refPath;
    if (typeof ref === "string") return { ...(value as Record<string, unknown>), refPath: expandRefPath(ref) };
  }
  return value;
}

function parseObjectish(value: unknown): unknown {
  if (typeof value !== "string") return value;
  const trimmed = value.trim();
  if (!trimmed.startsWith("{") && !trimmed.startsWith("[")) return value;
  try {
    return JSON.parse(trimmed);
  } catch {
    return value;
  }
}

/** Render one required property as the JSON a caller should send for it. */
function exampleFor(name: string, prop: SchemaProp | undefined): string {
  if (isAssetRefProp(prop)) return `"${name}": { "refPath": "/Game/UI/WBP_Example" }`;
  if (prop?.type === "number" || prop?.type === "integer") return `"${name}": 0`;
  if (prop?.type === "boolean") return `"${name}": false`;
  if (prop?.type === "array") return `"${name}": []`;
  if (prop?.type === "object") return `"${name}": {}`;
  return `"${name}": "..."`;
}

/**
 * Build the `input` object for one wrapped engine tool call.
 *
 * Throws INVALID_PARAMS naming the exact shape to send when a required
 * argument cannot be assembled, rather than dispatching a call the engine will
 * reject with "input params Json is empty".
 */
export function resolveEpicToolInput(
  toolName: string,
  schema: EpicInputSchema | undefined,
  params: Record<string, unknown>,
): { input?: unknown; inputJson?: unknown } {
  // An explicit raw JSON string is the caller taking full control.
  if (typeof params.inputJson === "string" && params.inputJson.trim() !== "") {
    return { inputJson: params.inputJson };
  }

  const props = schema?.properties;
  if (!props) return { input: params.input, inputJson: params.inputJson };

  const given = parseObjectish(params.input);
  const input: Record<string, unknown> =
    given && typeof given === "object" && !Array.isArray(given)
      ? { ...(given as Record<string, unknown>) }
      : {};

  const propNames = Object.keys(props);
  const required = schema?.required ?? [];

  // 1. Top-level parameters the tool's own schema names. These are declared in
  //    the category's zod shape by the generator, so they now survive the MCP
  //    layer instead of being stripped before dispatch.
  for (const name of propNames) {
    if (input[name] !== undefined) continue;
    const flat = params[name];
    if (flat === undefined || flat === null || flat === "") continue;
    input[name] = parseObjectish(flat);
  }

  // 2. The category's canonical asset path fills the tool's asset reference,
  //    but only when exactly one is still unresolved, so nothing is guessed.
  const unresolvedRefs = propNames.filter(
    (n) => input[n] === undefined && isAssetRefProp(asProp(props[n])),
  );
  const canonicalAsset = coerceAssetPathValue(params.assetPath) ?? coerceAssetPathValue(params.path);
  if (unresolvedRefs.length === 1 && canonicalAsset) {
    input[unresolvedRefs[0]] = canonicalAsset;
  }

  // Every reference, flat or inside an explicit `input`, takes the object path.
  for (const name of propNames) {
    const prop = asProp(props[name]);
    if (input[name] === undefined) continue;
    if (isAssetRefProp(prop)) input[name] = expandRefValue(input[name]);
    // A list of references, once its item schema is recorded (#1281).
    else if (prop?.type === "array" && isAssetRefProp(asProp(prop.items)) && Array.isArray(input[name])) {
      input[name] = (input[name] as unknown[]).map(expandRefValue);
    }
  }

  // 3. Refuse rather than dispatch a call the engine will reject anyway: a
  //    value off its recorded shape, named by field, then a missing argument.
  for (const name of propNames) {
    const problem = epicValueProblem(props[name], input[name], name);
    if (problem) {
      throw new McpError(ErrorCode.INVALID_PARAMS, `${toolName}: ${problem}. project(action="describe_action") lists every field and value.`);
    }
  }
  const missing = required.filter((n) => input[n] === undefined);
  if (missing.length > 0) {
    const known = new Set([...propNames, "action", "input", "inputJson"]);
    const ignored = Object.keys(params).filter(
      (k) => !known.has(k) && params[k] !== undefined && params[k] !== null,
    );
    const shape = missing.map((n) => exampleFor(n, asProp(props[n]))).join(", ");
    throw new McpError(
      ErrorCode.INVALID_PARAMS,
      `${toolName} is missing required argument(s): ${missing.join(", ")}. `
      + `Pass them in 'input', for example {"input": {${shape}}}.`
      + (ignored.length > 0
        ? ` These top-level parameters are not arguments of this tool and were not sent: ${ignored.join(", ")}.`
        : ""),
    );
  }

  return { input };
}

/**
 * The bridge arguments for one wrapped engine tool, so the whole of an Epic
 * action's dispatch is a declaration plus this one function.
 */
export function epicToolCall(
  toolset: string,
  tool: string,
  schema: EpicInputSchema | undefined,
  params: Record<string, unknown>,
): Record<string, unknown> {
  return { toolset, tool, ...resolveEpicToolInput(tool, schema, params) };
}

/**
 * The top-level parameters a wrapped tool forwards: the properties its own
 * input schema names. The `input` escape hatch and the asset-path fallback are
 * the category's spellings, not the tool's, so they are not listed.
 */
export function epicForwardedParams(schema: EpicInputSchema | undefined): string[] {
  return Object.keys(schema?.properties ?? {}).filter((n) => n !== "action");
}

/** The engine tool a generated `epic_*` action wraps, declared on the action. */
export interface EpicToolRef {
  readonly toolset: string;
  /** The qualified tool name, e.g. `UMGToolSet.UMGToolSet.GetWidgets`. */
  readonly name: string;
}

type ParamMapper = (p: Record<string, unknown>) => Record<string, unknown>;

/**
 * How a bridge action turns its parameters into the bridge call's arguments:
 * its own `mapParams`, the envelope for a declared `epicTool`, or none (the bag
 * is sent as given). Every dispatch route asks here.
 */
export function paramMapperOf(spec: {
  mapParams?: ParamMapper;
  epicTool?: EpicToolRef;
  epicSchema?: EpicInputSchema;
}): ParamMapper | undefined {
  if (spec.mapParams) return spec.mapParams;
  const tool = spec.epicTool;
  if (!tool) return undefined;
  return (p) => epicToolCall(tool.toolset, tool.name, spec.epicSchema, p);
}
