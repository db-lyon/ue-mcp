import { z } from "zod";
import { McpError, ErrorCode } from "./errors.js";

/**
 * The `args` bag for the UFUNCTION invocation actions (#811).
 *
 * Two rules drive the shape of this file.
 *
 * 1. Every branch is spelled out. The tool schema does not stay zod: it is
 *    converted to JSON Schema and published in the MCP handshake, and clients
 *    validate a tool call against that published copy before it reaches the
 *    server. `z.unknown()` / `z.any()` convert to the empty schema `{}`, which
 *    a client is free to read as "nothing is valid here" - the reported
 *    symptom was that every populated args object was rejected while
 *    parameterless calls went through. Concrete types survive the round trip.
 *
 * 2. Every shape an agent plausibly reaches for is accepted and normalized
 *    server-side, rather than being bounced with a validation error. The
 *    canonical form is the name -> value object; the entry-list and
 *    JSON-string forms are accepted because agents reach for them when the
 *    canonical one is refused, and refusing those too leaves no way to call a
 *    function with parameters at all.
 *
 * The schemas are built by factory functions on purpose. Reusing one instance
 * makes the JSON Schema converter emit `$ref` pointers into the tool schema,
 * another construct a client can fail to resolve. Fresh instances inline.
 */

/** Leaf value: anything JSON can express without nesting. */
const argScalar = () => z.union([z.string(), z.number(), z.boolean(), z.null()]);

/** A struct argument (FVector, FTransform, a user struct): any object. */
const argStruct = () => z.object({}).passthrough();

/** One argument value: scalar, struct, or an array of either (TArray). */
const argValue = () =>
  z.union([
    argScalar(),
    argStruct(),
    z.array(z.union([argScalar(), argStruct(), z.array(argScalar())])),
  ]);

/** The canonical form: `{ ParamName: value }`. */
export const functionArgsObject = () => z.record(z.string(), argValue());

/** The entry-list form: `[{ name, value }]`, same shape `set_cvars` takes. */
export const functionArgsEntries = () =>
  z.array(z.object({ name: z.string(), value: argValue().optional() }));

/**
 * Everything `args` accepts across the editor category: the name -> value
 * object (function calls), a positional string array (run_python_file), the
 * entry list, and a JSON-encoded string of any of those.
 */
export const FunctionArgs = z.union(
  [functionArgsObject(), z.array(z.string()), functionArgsEntries(), z.string()],
  {
    errorMap: () => ({
      message:
        'args must be an object mapping parameter name to value (e.g. {"bEnabled": true}), ' +
        'an entry list ([{"name": "bEnabled", "value": true}]), or, for run_python_file, ' +
        "an array of positional strings",
    }),
  },
);

function invalid(param: string, detail: string): McpError {
  return new McpError(
    ErrorCode.INVALID_PARAMS,
    `${param} ${detail}. Pass an object mapping parameter name to value, e.g. {"bEnabled": true}.`,
  );
}

/**
 * Reduce any accepted `args` shape to the name -> value object the bridge
 * marshals into the UFUNCTION's parameter struct. Returns undefined when there
 * is nothing to send, so the param is dropped rather than sent as null.
 */
export function normalizeFunctionArgs(raw: unknown, param = "args"): Record<string, unknown> | undefined {
  if (raw === undefined || raw === null) return undefined;

  if (typeof raw === "string") {
    const text = raw.trim();
    if (text === "") return undefined;
    let parsed: unknown;
    try {
      parsed = JSON.parse(text);
    } catch {
      throw invalid(param, "was a string, but it is not valid JSON");
    }
    if (typeof parsed === "string") throw invalid(param, "decoded to a string, not a parameter map");
    return normalizeFunctionArgs(parsed, param);
  }

  if (Array.isArray(raw)) {
    const out: Record<string, unknown> = {};
    for (const entry of raw) {
      if (typeof entry !== "object" || entry === null || Array.isArray(entry)) {
        throw invalid(param, "was an array of values, so no parameter name can be resolved");
      }
      const record = entry as Record<string, unknown>;
      const name = record.name;
      if (typeof name !== "string" || name === "") {
        throw invalid(param, 'was an array whose entries are missing a "name" string');
      }
      out[name] = record.value;
    }
    return out;
  }

  if (typeof raw === "object") return raw as Record<string, unknown>;

  throw invalid(param, `was a ${typeof raw}`);
}

/**
 * `run_python_file` takes positional strings, not a parameter map. Accepts the
 * array it documents, plus a JSON-encoded array, plus a lone string (one
 * positional arg), so the widened `args` schema cannot smuggle an object into
 * a positional list.
 */
export function normalizePythonArgs(raw: unknown, param = "args"): string[] | undefined {
  if (raw === undefined || raw === null) return undefined;

  if (typeof raw === "string") {
    const text = raw.trim();
    if (text === "") return undefined;
    if (text.startsWith("[")) {
      try {
        return normalizePythonArgs(JSON.parse(text), param);
      } catch {
        throw new McpError(
          ErrorCode.INVALID_PARAMS,
          `${param} looked like a JSON array but does not parse. Pass an array of positional strings.`,
        );
      }
    }
    return [text];
  }

  if (Array.isArray(raw)) {
    return raw.map((entry) => (typeof entry === "string" ? entry : JSON.stringify(entry)));
  }

  throw new McpError(
    ErrorCode.INVALID_PARAMS,
    `${param} must be an array of positional strings for run_python_file, not an object.`,
  );
}
