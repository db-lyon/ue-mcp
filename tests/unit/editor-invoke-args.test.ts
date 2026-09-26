import { readFileSync } from "node:fs";
import { describe, it, expect, vi } from "vitest";
import type { z } from "zod";
import { normalizeObjectSchema } from "@modelcontextprotocol/sdk/server/zod-compat.js";
import { toJsonSchemaCompat } from "@modelcontextprotocol/sdk/server/zod-json-schema-compat.js";
import { editorTool } from "../../src/tools/editor.js";
import { applyLeanContext } from "../../src/surface/context/lean-context.js";
import { handlerSpecs } from "../../src/tools/specs/editor.generated.js";
import { paramZod, zodSignature } from "../../src/surface/handler-spec.js";
import type { ToolContext, ToolDef } from "../../src/core/types.js";
import { readHandlerFile } from "../../scripts/lib/cpp-registrations.mjs";

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

const ARG_METHODS = ["run_python_file", "invoke_function", "invoke_object_function", "invoke_static_function"] as const;

describe("args is declared once, in each handler's C++ spec (#1057)", () => {
  it("declares the same four forms on every action that takes args", () => {
    for (const method of ARG_METHODS) {
      const args = handlerSpecs[method].params.find((p) => p.name === "args");
      expect(args, method).toMatchObject({ type: "any", forms: ["argMap", "stringList", "argEntryList", "string"] });
    }
    const calls = handlerSpecs.invoke_object_functions.params.find((p) => p.name === "calls");
    expect(calls?.fields?.find((f) => f.name === "args")).toMatchObject({ type: "any", forms: ["argMap", "stringList", "argEntryList", "string"] });
  });

  it("advertises exactly the schema the spec generates", () => {
    const spec = handlerSpecs.invoke_function.params.find((p) => p.name === "args")!;
    expect(zodSignature(editorTool.schema.args as z.ZodTypeAny)).toBe(zodSignature(paramZod(spec).optional()));
  });

  it("forwards args to the bridge as sent, for the handler to normalize", async () => {
    const call = vi.fn().mockResolvedValue({ success: true });
    const ctx = { bridge: { call } } as unknown as ToolContext;
    for (const action of [...ARG_METHODS, "invoke_object_functions"] as const) {
      expect(editorTool.actions[action].mapParams, action).toBeUndefined();
    }
    const entryList = [{ name: "bEnabled", value: true }];
    await editorTool.handler(ctx, { action: "invoke_function", actorLabel: "A", functionName: "F", args: entryList });
    expect(call).toHaveBeenLastCalledWith("invoke_function", { actorLabel: "A", functionName: "F", args: entryList }, undefined);
    await editorTool.handler(ctx, { action: "invoke_object_function", target: "playerpawn", functionName: "F", args: '{"bEnabled": true}' });
    expect(call).toHaveBeenLastCalledWith("invoke_object_function", { target: "playerpawn", functionName: "F", args: '{"bEnabled": true}' }, undefined);
  });
});

describe("the C++ normalizers", () => {
  const read = (rel: string): string => readFileSync(new URL(`../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/${rel}`, import.meta.url), "utf8");
  const utils = read("Public/HandlerFunctionCall.h");

  it("live in HandlerFunctionCall.h, once, with the refusals the TS normalizers had", () => {
    for (const refusal of [
      "was a string, but it is not valid JSON",
      "decoded to a string, not a parameter map",
      "was an array of values, so no parameter name can be resolved",
      "was an array whose entries are missing a \\\"name\\\" string",
      "Pass an object mapping parameter name to value",
      "looked like a JSON array but does not parse. Pass an array of positional strings.",
      "must be an array of positional strings for run_python_file, not an object.",
    ]) {
      expect(utils, refusal).toContain(refusal);
    }
  });

  it("are how every handler taking args reads it", () => {
    const editor = readHandlerFile("EditorHandlers.cpp");
    const pie = readHandlerFile("EditorHandlers_PIE.cpp");
    const runtime = readHandlerFile("EditorHandlers_PIERuntime.cpp");
    expect(editor).toContain('MCPReadPythonArgs(Params, TEXT("args"), ExtraArgs)');
    expect(pie.match(/MCPReadFunctionArgs\(Params, TEXT\("args"\), ArgsMap\)/g)).toHaveLength(2);
    expect(runtime).toContain('MCPReadFunctionArgs(Params, TEXT("args"), ArgsMap)');
    expect(runtime).toContain("MCPNormalizeFunctionArgs(");
    for (const source of [editor, pie, runtime]) {
      expect(source).not.toMatch(/TryGet(Object|Array)Param\(Params, TEXT\("args"\)/);
    }
  });
});
