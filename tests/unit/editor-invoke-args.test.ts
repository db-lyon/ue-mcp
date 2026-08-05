import { describe, it, expect } from "vitest";
import { normalizeObjectSchema } from "@modelcontextprotocol/sdk/server/zod-compat.js";
import { toJsonSchemaCompat } from "@modelcontextprotocol/sdk/server/zod-json-schema-compat.js";
import { editorTool } from "../../src/tools/editor.js";
import { applyLeanContext } from "../../src/lean-context.js";
import { normalizeFunctionArgs, normalizePythonArgs } from "../../src/function-args.js";
import type { ToolDef } from "../../src/types.js";

/**
 * #811: `args` was advertised as a union whose object branch carried an empty
 * value schema, and a client that reads `{}` as "nothing validates" rejected
 * every populated args object before the call left the client. These tests
 * assert on the JSON Schema that is actually published over MCP, not on the
 * zod source, and they run it through a validator - reading the zod is exactly
 * what missed the defect the first time.
 */

type JsonSchema = Record<string, any>;

/** Reproduce the conversion McpServer performs when it answers tools/list. */
function advertisedSchema(tool: ToolDef): JsonSchema {
  const shape: Record<string, unknown> = {};
  for (const [key, schema] of Object.entries(tool.schema)) shape[key] = schema;
  const object = normalizeObjectSchema(shape as never);
  return toJsonSchemaCompat(object as never, { strictUnions: true, pipeStrategy: "input" } as never) as JsonSchema;
}

/** Minimal JSON Schema validator covering the keywords this schema uses. */
function validate(schema: unknown, value: unknown): boolean {
  if (schema === true || schema === undefined) return true;
  if (schema === false) return false;
  const s = schema as JsonSchema;

  if (Array.isArray(s.anyOf)) return s.anyOf.some((branch: unknown) => validate(branch, value));
  if (Array.isArray(s.enum) && !s.enum.includes(value as never)) return false;

  if (s.type !== undefined) {
    const types: string[] = Array.isArray(s.type) ? s.type : [s.type];
    const actual =
      value === null ? "null"
        : Array.isArray(value) ? "array"
          : typeof value === "number" ? (Number.isInteger(value) ? "integer" : "number")
            : typeof value;
    const matches = types.some((t) => t === actual || (t === "number" && actual === "integer"));
    if (!matches) return false;
  }

  if (Array.isArray(value)) {
    if (s.items !== undefined && !value.every((entry) => validate(s.items, entry))) return false;
    return true;
  }

  if (value !== null && typeof value === "object") {
    const record = value as Record<string, unknown>;
    for (const key of (s.required ?? []) as string[]) {
      if (!(key in record)) return false;
    }
    for (const [key, entry] of Object.entries(record)) {
      const propertySchema = s.properties?.[key];
      if (propertySchema !== undefined) {
        if (!validate(propertySchema, entry)) return false;
      } else if (s.additionalProperties !== undefined) {
        if (!validate(s.additionalProperties, entry)) return false;
      }
    }
  }

  return true;
}

/** Every position a subschema can sit in, so an empty one cannot hide. */
function subschemas(schema: unknown): unknown[] {
  if (typeof schema !== "object" || schema === null) return [];
  const s = schema as JsonSchema;
  const found: unknown[] = [];
  for (const key of ["items", "additionalProperties", "not"]) {
    if (typeof s[key] === "object" && s[key] !== null) found.push(s[key]);
  }
  for (const key of ["anyOf", "oneOf", "allOf"]) {
    if (Array.isArray(s[key])) found.push(...s[key]);
  }
  if (typeof s.properties === "object" && s.properties !== null) found.push(...Object.values(s.properties));
  return [...found, ...found.flatMap(subschemas)];
}

/** The shapes the tool description promises an agent can send. */
const acceptedShapes: Array<[string, unknown]> = [
  ["bool value", { bEnabled: true }],
  ["string value", { bEnabled: "true" }],
  ["number value", { bEnabled: 1 }],
  ["null value", { Target: null }],
  ["struct value", { Location: { x: 1, y: 2, z: 3 } }],
  ["nested struct value", { Spec: { Transform: { Translation: { x: 1 } } } }],
  ["array of scalars", { Rows: [1, 2, 3] }],
  ["array of structs", { Rows: [{ a: 1 }, { a: 2 }] }],
  ["array of arrays", { Grid: [[1, 2], [3, 4]] }],
  ["empty object", {}],
  ["positional python args", ["one", "two"]],
  ["entry list", [{ name: "bEnabled", value: 1 }]],
  ["json encoded object", '{"bEnabled": true}'],
];

const leanEditorTool = applyLeanContext([editorTool]).find((t) => t.name === "editor")!;

describe("editor args schema (#811)", () => {
  for (const [mode, tool] of [["full", editorTool], ["lean", leanEditorTool]] as Array<[string, ToolDef]>) {
    describe(`${mode} mode`, () => {
      const args = advertisedSchema(tool).properties.args as JsonSchema;

      it("is advertised at all", () => {
        expect(args).toBeDefined();
        expect(args.description).toContain("parameter name to value");
      });

      it("carries no empty subschema a client can read as unsatisfiable", () => {
        const empties = subschemas(args).filter(
          (sub) => typeof sub === "object" && sub !== null && !Array.isArray(sub) && Object.keys(sub).length === 0,
        );
        expect(empties).toEqual([]);
      });

      it("carries no $ref a client has to resolve", () => {
        expect(JSON.stringify(args)).not.toContain("$ref");
      });

      for (const [label, shape] of acceptedShapes) {
        it(`accepts ${label}`, () => {
          expect(validate(args, shape)).toBe(true);
          expect(tool.schema.args.safeParse(shape).success).toBe(true);
        });
      }

      it("rejects a value that is neither map, list nor string, and says what to send", () => {
        const result = tool.schema.args.safeParse(42);
        expect(result.success).toBe(false);
        if (!result.success) {
          expect(result.error.issues[0].message).toContain("parameter name to value");
        }
      });
    });
  }
});

describe("normalizeFunctionArgs", () => {
  it("passes the canonical object through", () => {
    expect(normalizeFunctionArgs({ bEnabled: true })).toEqual({ bEnabled: true });
  });

  it("folds an entry list into the parameter map", () => {
    expect(normalizeFunctionArgs([{ name: "bEnabled", value: 1 }, { name: "Count", value: 2 }]))
      .toEqual({ bEnabled: 1, Count: 2 });
  });

  it("decodes a JSON string of either form", () => {
    expect(normalizeFunctionArgs('{"bEnabled": true}')).toEqual({ bEnabled: true });
    expect(normalizeFunctionArgs('[{"name": "bEnabled", "value": true}]')).toEqual({ bEnabled: true });
  });

  it("drops nothing-to-send values", () => {
    expect(normalizeFunctionArgs(undefined)).toBeUndefined();
    expect(normalizeFunctionArgs(null)).toBeUndefined();
    expect(normalizeFunctionArgs("  ")).toBeUndefined();
  });

  it("explains what to send instead of failing silently", () => {
    expect(() => normalizeFunctionArgs(["bEnabled"])).toThrow(/parameter name to value/);
    expect(() => normalizeFunctionArgs("not json")).toThrow(/not valid JSON/);
    expect(() => normalizeFunctionArgs([{ value: 1 }])).toThrow(/name/);
  });
});

describe("normalizePythonArgs", () => {
  it("keeps positional strings", () => {
    expect(normalizePythonArgs(["one", "two"])).toEqual(["one", "two"]);
  });

  it("accepts a lone string and a JSON array string", () => {
    expect(normalizePythonArgs("one")).toEqual(["one"]);
    expect(normalizePythonArgs('["one", "two"]')).toEqual(["one", "two"]);
  });

  it("refuses a parameter map, which is a function-call shape", () => {
    expect(() => normalizePythonArgs({ bEnabled: true })).toThrow(/positional strings/);
  });
});

describe("editor actions normalize args before the bridge sees them", () => {
  const entryList = [{ name: "bEnabled", value: true }];

  it("invoke_function", () => {
    expect(editorTool.actions.invoke_function.mapParams?.({ actorLabel: "A", functionName: "F", args: entryList }))
      .toMatchObject({ args: { bEnabled: true } });
  });

  it("invoke_object_function", () => {
    expect(editorTool.actions.invoke_object_function.mapParams?.({ target: "playerpawn", functionName: "F", args: '{"bEnabled": true}' }))
      .toMatchObject({ args: { bEnabled: true } });
  });

  it("invoke_static_function", () => {
    expect(editorTool.actions.invoke_static_function.mapParams?.({ className: "C", functionName: "F", args: entryList }))
      .toMatchObject({ args: { bEnabled: true } });
  });

  it("run_python_file keeps positional args a list", () => {
    expect(editorTool.actions.run_python_file.mapParams?.({ filePath: "/tmp/x.py", args: ["one"] }))
      .toMatchObject({ args: ["one"] });
  });
});
