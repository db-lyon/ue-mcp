/**
 * #1057: a parameter the category accepts but the action's mapParams never
 * sends is dropped while the call reports success. The runtime half reports it
 * on the result; the surface half below stops a new action from documenting a
 * parameter its own mapper ignores.
 */
import { describe, it, expect, afterEach } from "vitest";
import { z } from "zod";
import { ALL_TOOLS } from "../../src/tools.js";
import { bp, categoryTool } from "../../src/types.js";
import { parseParams, ROUTING_PARAMS } from "../../src/action-schema.js";
import { keysRead, mapTracked } from "../../src/param-forwarding.js";
import { STRICT_PARAMS_ENV } from "../../src/call-pipeline.js";


function fakeCtx(seen: Array<Record<string, unknown>>) {
  return {
    bridge: {
      isConnected: true,
      call: async (_m: string, params: Record<string, unknown>) => { seen.push(params); return { success: true }; },
      getTarget: () => ({ projectPath: null, port: 0, portSource: "default", verified: true }),
      connect: async () => {},
      retargetProject: () => ({}),
    },
    project: {},
  } as never;
}

describe("mapTracked", () => {
  it("reports supplied keys the mapper never read", () => {
    const r = mapTracked((p) => ({ path: p.assetPath }), { assetPath: "/Game/A", nodeId: "G" });
    expect(r.params).toEqual({ path: "/Game/A" });
    expect(r.unforwarded).toEqual(["nodeId"]);
  });

  it("counts a spread, an `in` check and a returned bag as forwarded", () => {
    expect(mapTracked((p) => ({ ...p }), { a: 1, b: 2 }).unforwarded).toEqual([]);
    expect(mapTracked((p) => ("a" in p ? { x: 1 } : {}), { a: 1 }).unforwarded).toEqual([]);
    const bag = { a: 1 };
    const r = mapTracked((p) => p, bag);
    expect(r.params).toBe(bag);
    expect(r.unforwarded).toEqual([]);
  });

  it("counts a string mirrored under another name as forwarded, but not a coincident boolean", () => {
    expect(mapTracked((p) => ({ path: p.path }), { assetPath: "/Game/A", path: "/Game/A" }).unforwarded).toEqual([]);
    expect(mapTracked((p) => ({ compile: p.compile }), { compile: true, save: true }).unforwarded).toEqual(["save"]);
  });

  it("only blames keys the caller supplied", () => {
    const r = mapTracked((p) => ({ a: p.a }), { a: 1, added: 2 }, ["a"]);
    expect(r.unforwarded).toEqual([]);
  });
});

describe("unforwarded parameters at dispatch", () => {
  const probe = () => categoryTool("probe", "Probe.", {
    by_name: bp("mutate", "Delete. Params: nodeName", "probe_delete", (p) => ({ nodeName: p.nodeName })),
    by_id: bp("mutate", "Delete. Params: nodeId", "probe_delete"),
  }, undefined, { nodeName: z.string().optional(), nodeId: z.string().optional() });

  afterEach(() => { delete process.env[STRICT_PARAMS_ENV]; });

  it("reports a parameter the mapper dropped, and still sends the call", async () => {
    const seen: Array<Record<string, unknown>> = [];
    const result = await probe().handler(fakeCtx(seen), { action: "by_name", nodeName: "N", nodeId: "G" }) as Record<string, unknown>;
    expect(seen).toEqual([{ nodeName: "N" }]);
    expect((result.paramsNotForwarded as { params: string[] }).params).toEqual(["nodeId"]);
  });

  it("says nothing when every parameter was forwarded", async () => {
    const result = await probe().handler(fakeCtx([]), { action: "by_name", nodeName: "N", timeoutMs: 5000 }) as Record<string, unknown>;
    expect(result).not.toHaveProperty("paramsNotForwarded");
  });

  it("refuses before dispatch under the strict flag", async () => {
    process.env[STRICT_PARAMS_ENV] = "1";
    const seen: Array<Record<string, unknown>> = [];
    await expect(probe().handler(fakeCtx(seen), { action: "by_name", nodeName: "N", nodeId: "G" }))
      .rejects.toThrow(/does not take nodeId/);
    expect(seen).toEqual([]);
  });
});

describe("parameters the editor never read", () => {
  const probe = () => categoryTool("probe", "Probe.", {
    add: bp("mutate", "Add. Params: blendDuration", "probe_add"),
  }, undefined, { blendDuration: z.number().optional() });

  const answering = (answer: Record<string, unknown>) => ({
    bridge: {
      isConnected: true,
      call: async () => answer,
      getTarget: () => ({ projectPath: null, port: 0, portSource: "default", verified: true }),
      connect: async () => {},
      retargetProject: () => ({}),
    },
    project: {},
  } as never);

  it("reports the editor's paramsNotRead with a note", async () => {
    const result = await probe().handler(
      answering({ success: true, value: 1, paramsNotRead: ["blendDuration"] }),
      { action: "add", blendDuration: 0.2 },
    ) as Record<string, unknown>;
    const report = result.paramsNotRead as { params: string[]; note: string };
    expect(report.params).toEqual(["blendDuration"]);
    expect(report.note).toMatch(/never read/);
    expect(result.value).toBe(1);
  });

  it("keeps the report when select narrows the result", async () => {
    const result = await probe().handler(
      answering({ success: true, value: 1, paramsNotRead: ["blendDuration"] }),
      { action: "add", blendDuration: 0.2, select: ["value"] },
    ) as Record<string, unknown>;
    expect((result.paramsNotRead as { params: string[] }).params).toEqual(["blendDuration"]);
    expect(result).not.toHaveProperty("success");
  });

  it("says nothing when the editor read everything", async () => {
    const result = await probe().handler(answering({ success: true }), { action: "add", blendDuration: 0.2 }) as Record<string, unknown>;
    expect(result).not.toHaveProperty("paramsNotRead");
  });

  it("leaves a field of that name alone when it is not a list", async () => {
    const result = await probe().handler(
      answering({ success: true, paramsNotRead: "unrelated" }),
      { action: "add" },
    ) as Record<string, unknown>;
    expect(result.paramsNotRead).toBe("unrelated");
  });
});

/* ── the surface ─────────────────────────────────────────────────────── */

interface ZodDef { typeName?: string; innerType?: z.ZodTypeAny; schema?: z.ZodTypeAny; values?: unknown[]; options?: z.ZodTypeAny[]; value?: unknown }

/** A value of the declared type, so a mapper that coerces or maps over it does not throw. */
function sample(schema: z.ZodTypeAny | undefined): unknown {
  const def = (schema as unknown as { _def?: ZodDef })?._def ?? {};
  switch (def.typeName) {
    case "ZodOptional": case "ZodNullable": case "ZodDefault": case "ZodEffects": case "ZodBranded": case "ZodReadonly":
      return sample(def.innerType ?? def.schema);
    case "ZodNumber": return 1;
    case "ZodBoolean": return true;
    case "ZodArray": return [];
    case "ZodObject": case "ZodRecord": return {};
    case "ZodEnum": return def.values?.[0];
    case "ZodLiteral": return def.value;
    case "ZodUnion": return sample(def.options?.[0]);
    default: return "/Game/Probe/Probe";
  }
}

/** Documented parameters of one action that its mapper never reads, alone or alongside the rest. */
function ignoredDocumentedParams(schema: Record<string, z.ZodTypeAny>, description: string, mapParams: (p: Record<string, unknown>) => Record<string, unknown>): string[] {
  const declared = new Set(Object.keys(schema));
  const names = parseParams(description, declared).params
    .map((p) => p.name)
    .filter((n) => declared.has(n) && !ROUTING_PARAMS.has(n));
  const full = Object.fromEntries(names.map((n) => [n, sample(schema[n])]));
  const readInFull = keysRead(mapParams, full);
  return names.filter((n) => !readInFull.has(n) && !keysRead(mapParams, { [n]: full[n] }).has(n));
}

function surfaceOffenders(): Record<string, string[]> {
  const out: Record<string, string[]> = {};
  for (const tool of ALL_TOOLS) {
    for (const [action, spec] of Object.entries(tool.actions)) {
      if (spec.kind !== "bridge" || !spec.mapParams) continue;
      const ignored = ignoredDocumentedParams(tool.schema as Record<string, z.ZodTypeAny>, spec.description ?? "", spec.mapParams);
      if (ignored.length > 0) out[`${tool.name}.${action}`] = ignored;
    }
  }
  return out;
}

describe("documented parameters reach the bridge", () => {
  it("finds a documented parameter the mapper ignores", () => {
    const schema = { assetPath: z.string().optional(), nodeName: z.string().optional(), nodeId: z.string().optional() };
    const map = (p: Record<string, unknown>) => ({ path: p.assetPath, nodeName: p.nodeName });
    expect(ignoredDocumentedParams(schema, "Delete. Params: assetPath, nodeId OR nodeName", map)).toEqual(["nodeId"]);
  });

  it("has no action whose mapParams ignores a parameter it documents", () => {
    const offenders = Object.entries(surfaceOffenders()).map(([key, params]) => `${key}: ${params.join(", ")}`);
    expect(
      offenders,
      "These actions document a parameter their mapParams never reads, so the\n"
        + "category accepts it and the call drops it (#1057). Forward it in\n"
        + "mapParams, or stop documenting it:\n  " + offenders.join("\n  "),
    ).toEqual([]);
  });
});
