/**
 * The advertised call shape of a category tool: `action` plus `args` (#1172).
 *
 * A category used to advertise one flat JSON Schema holding every parameter of
 * every one of its actions, about 2,600 properties across the surface, and the
 * MCP layer validated calls against it. The client now sees only
 *
 *     <category>(action: <enum>, args?: { ...that action's params })
 *
 * with the parameters named by each action's signature line. The flat schema
 * did not go away: it is still `ToolDef.schema`, still what describe_action and
 * the audits read, and every call is still validated against it, here, in the
 * server, with the same zod object the SDK used to build from it. A call that
 * failed validation before fails with the same message now.
 *
 * Backward compatibility: a caller that still sends flat parameters is served.
 * The advertised object passes unknown keys through, and a call is read as an
 * envelope only when everything beside `args` is a routing parameter; any other
 * top-level key makes it a flat call. That keeps `editor(invoke_function,
 * functionName, args={...})`, whose own parameter is named `args`, meaning
 * exactly what it always meant.
 */
import { z } from "zod";
import { getParseErrorMessage, normalizeObjectSchema, safeParse as sdkSafeParse } from "@modelcontextprotocol/sdk/server/zod-compat.js";
import { McpError as SdkMcpError, ErrorCode as SdkErrorCode } from "@modelcontextprotocol/sdk/types.js";
import { EDITOR_TARGET_PARAM, MIGRATE_TARGET_PARAM, ROUTING_PARAM_NAMES } from "./routing-params.js";
import type { ToolDef } from "./types.js";
import { McpError, ErrorCode } from "./errors.js";

/** The key a category call nests its action's parameters under. */
export const ARGS_KEY = "args";

const ROUTING = new Set(ROUTING_PARAM_NAMES);

/**
 * Whether a tool is advertised as `action` + `args`. A tool that already
 * nests its parameters (the micro gateway) or opts out (the lean `catalog`)
 * keeps its declared shape.
 */
export function usesArgsEnvelope(tool: ToolDef): boolean {
  return !tool.options?.nestedParamsKey && !tool.options?.flatSurface && "action" in tool.schema;
}

const ARGS_SCHEMA = z
  .record(z.unknown())
  .optional()
  .describe("The action's parameters, named as its signature names them.");

/** The advertised raw shape: action, args, and the editor targets while they are injected. */
export function envelopeShape(tool: ToolDef): Record<string, z.ZodType> {
  const shape: Record<string, z.ZodType> = { action: tool.schema.action, [ARGS_KEY]: ARGS_SCHEMA };
  if (tool.injectedEditorParam && tool.schema[EDITOR_TARGET_PARAM]) shape[EDITOR_TARGET_PARAM] = tool.schema[EDITOR_TARGET_PARAM];
  if (tool.injectedMigrateParam && tool.schema[MIGRATE_TARGET_PARAM]) shape[MIGRATE_TARGET_PARAM] = tool.schema[MIGRATE_TARGET_PARAM];
  return shape;
}

/**
 * The advertised input schema. Unknown keys pass through, which is what lets
 * a flat call reach the server-side validation below instead of having its
 * parameters stripped by the MCP layer before the handler sees them.
 */
export function envelopeInputSchema(tool: ToolDef): z.ZodTypeAny {
  return z.object(envelopeShape(tool)).passthrough();
}

/**
 * Flatten one call into the category's own parameter bag.
 *
 * Envelope: `{action, args: {...}}`, optionally with routing keys beside
 * `args`. Those win over the same keys inside it, as they do for the micro
 * gateway. Anything else is a flat call and is returned unchanged.
 */
export function unwrapArgsEnvelope(tool: ToolDef, raw: Record<string, unknown>): Record<string, unknown> {
  if (!(ARGS_KEY in raw) || raw[ARGS_KEY] === undefined) return raw;
  const { [ARGS_KEY]: args, ...rest } = raw;
  const flatKeys = Object.keys(rest).filter((k) => !ROUTING.has(k));
  const declaresArgs = ARGS_KEY in tool.schema;
  // A flat call to a category whose own parameter is called `args`.
  if (declaresArgs && (flatKeys.length > 0 || !isPlainObject(args))) return raw;
  if (!isPlainObject(args)) {
    throw new McpError(ErrorCode.INVALID_PARAMS, `${tool.name}: args must be an object holding the action's parameters.`);
  }
  // Parameters split between args and the top level are merged, unless the
  // two places disagree about one of them.
  for (const key of flatKeys) {
    if (key in args && JSON.stringify(args[key]) !== JSON.stringify(rest[key])) {
      throw new McpError(
        ErrorCode.INVALID_PARAMS,
        `${tool.name}: '${key}' is given both inside args and beside it, with different values. Pass it once, inside args.`,
      );
    }
  }
  return { ...args, ...rest };
}

function isPlainObject(value: unknown): value is Record<string, unknown> {
  return !!value && typeof value === "object" && !Array.isArray(value);
}

const objects = new WeakMap<Record<string, z.ZodType>, unknown>();

/** The object the SDK builds from a flat shape, built by the SDK's own helper so parsing and error text match it exactly. */
function flatObject(shape: Record<string, z.ZodType>): unknown {
  let obj = objects.get(shape);
  if (!obj) {
    obj = normalizeObjectSchema(shape as never);
    objects.set(shape, obj);
  }
  return obj;
}

/** Why a flat bag fails a category's shape, worded as the MCP SDK words it, or null. */
export function flatValidationMessage(tool: ToolDef, flat: Record<string, unknown>): { data?: Record<string, unknown>; message?: string } {
  const parsed = sdkSafeParse(flatObject(tool.schema) as never, flat) as { success: boolean; data?: unknown; error?: unknown };
  if (parsed.success) return { data: parsed.data as Record<string, unknown> };
  return { message: `Input validation error: Invalid arguments for tool ${tool.name}: ${getParseErrorMessage(parsed.error)}` };
}

/**
 * Validate a flat bag against the category's declared shape, exactly as the
 * MCP layer did when that shape was the advertised one: same zod object, same
 * unknown-key stripping, and the same error text, which the SDK prefixes with
 * its InvalidParams code. Returns the parsed bag.
 */
export function validateCategoryParams(tool: ToolDef, flat: Record<string, unknown>): Record<string, unknown> {
  const result = flatValidationMessage(tool, flat);
  if (result.message !== undefined) throw new SdkMcpError(SdkErrorCode.InvalidParams, result.message);
  return result.data!;
}
