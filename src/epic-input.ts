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
import { McpError, ErrorCode } from "./errors.js";
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
}

function asProp(value: unknown): SchemaProp | undefined {
  return value && typeof value === "object" ? (value as SchemaProp) : undefined;
}

/** True when a schema property is the engine's `{ refPath }` object reference. */
function isAssetRefProp(prop: SchemaProp | undefined): boolean {
  return !!prop && prop.type === "object" && !!prop.properties && "refPath" in prop.properties;
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
    const prop = asProp(props[name]);
    let value = parseObjectish(flat);
    if (isAssetRefProp(prop) && typeof value === "string") {
      value = { refPath: value };
    }
    input[name] = value;
  }

  // 2. The category's canonical asset path fills the tool's asset reference,
  //    but only when exactly one is still unresolved, so nothing is guessed.
  const unresolvedRefs = propNames.filter(
    (n) => input[n] === undefined && isAssetRefProp(asProp(props[n])),
  );
  const canonicalAsset = coerceAssetPathValue(params.assetPath) ?? coerceAssetPathValue(params.path);
  if (unresolvedRefs.length === 1 && canonicalAsset) {
    input[unresolvedRefs[0]] = { refPath: canonicalAsset };
  }

  // 3. Refuse rather than dispatch a call the engine will reject anyway.
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
 * The bridge arguments for one wrapped engine tool. Generated action modules
 * call this as their `mapParams`, so the whole of an Epic action's dispatch is
 * a declaration plus this one function.
 */
export function epicToolCall(
  toolset: string,
  tool: string,
  schema: EpicInputSchema | undefined,
  params: Record<string, unknown>,
): Record<string, unknown> {
  return { toolset, tool, ...resolveEpicToolInput(tool, schema, params) };
}
