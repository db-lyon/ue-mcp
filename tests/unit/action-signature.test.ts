/**
 * Compact action signatures (#1172): the unit every seed and every discovery
 * answer is built from. Each source is covered: a recorded C++ spec, an Epic
 * input schema, and the declared shape an in-process action falls back to.
 */
import { describe, expect, it } from "vitest";
import { z } from "zod";
import { actionSignature, SIGNATURE_LEGEND } from "../../src/action-signature.js";
import type { ActionSpec, ToolDef } from "../../src/types.js";
import { bp, categoryTool } from "../../src/category-tool.js";
import type { ParamChoice, ParamSpec } from "../../src/handler-spec.js";
import { ALL_TOOLS } from "../../src/tools.js";

function specTool(params: ParamSpec[], choices?: ParamChoice[]): ToolDef {
  const action: ActionSpec = { ...bp("mutate", "Do it.", "do_it"), paramSpec: params, ...(choices ? { paramChoices: choices } : {}) };
  return categoryTool("demo", "Demo.", { do_it: action });
}

const p = (name: string, type: ParamSpec["type"], required: boolean, extra: Partial<ParamSpec> = {}): ParamSpec =>
  ({ name, type, required, description: "", ...extra });

describe("signatures from a recorded C++ spec", () => {
  it("writes names, optionality, aliases and type codes, string implicit", () => {
    const tool = specTool([
      p("assetPath", "string", true, { aliases: ["path"] }),
      p("propertyName", "string", true),
      p("value", "any", true),
      p("save", "boolean", false),
      p("count", "integer", false),
      p("scale", "number", false),
      p("location", "vec3", false),
      p("rotation", "rotator", false),
      p("tint", "color", false),
      p("settings", "object", false),
    ]);
    expect(actionSignature(tool, "do_it")).toBe(
      "do_it(assetPath|path, propertyName, value:*, save?:b, count?:i, scale?:n, location?:v, rotation?:r, tint?:c, settings?:o)",
    );
  });

  it("writes arrays with their item type, unions and literals", () => {
    const tool = specTool([
      p("names", "array", true, { items: "string" }),
      p("rows", "array", false, { fields: [{ name: "a", type: "string", required: true, description: "" }] }),
      p("anything", "array", false),
      p("target", "string", false, { orTypes: ["object"] }),
      p("mode", "string", false, { literal: "fast" }),
    ]);
    expect(actionSignature(tool, "do_it")).toBe("do_it(names:[s], rows?:[o], anything?:[*], target?:s/o, mode?:=fast)");
  });

  it("writes a value's forms as the codes each form already has, and a tagged union by its tag field", () => {
    const variants = [
      { tag: "set", description: "", fields: [{ name: "frame", type: "integer" as const, required: true, description: "" }] },
      { tag: "clear", description: "", fields: [] },
    ];
    const tool = specTool([
      p("args", "any", false, { forms: ["argMap", "stringList", "argEntryList", "string"] }),
      p("operations", "array", true, { items: "object", oneOf: { key: "op", variants } }),
      p("operation", "object", false, { oneOf: { key: "op", variants } }),
    ]);
    expect(actionSignature(tool, "do_it")).toBe("do_it(args?:o/[s]/[o]/s, operations:[o<op>], operation?:o<op>)");
  });

  it("writes a required choice where its first member is declared", () => {
    const tool = specTool(
      [p("actorLabel", "string", false), p("functionName", "string", true), p("actorPath", "string", false)],
      [{ mode: "exactlyOne", branches: [["actorLabel"], ["actorPath"]] }],
    );
    expect(actionSignature(tool, "do_it")).toBe("do_it(one(actorLabel; actorPath), functionName)");
  });

  it("writes an at-least-one choice and a multi-name branch", () => {
    const tool = specTool(
      [p("settings", "object", false), p("propertyName", "string", false), p("propertyValue", "any", false)],
      [{ mode: "atLeastOne", branches: [["settings"], ["propertyName", "propertyValue"]] }],
    );
    expect(actionSignature(tool, "do_it")).toBe("do_it(any(settings:o; propertyName+propertyValue:*))");
  });

  it("drops optional parameters from the end past maxLength, and counts them", () => {
    const tool = specTool([
      p("required", "string", true),
      p("alpha", "string", false),
      p("beta", "string", false),
      p("gamma", "string", false),
    ]);
    expect(actionSignature(tool, "do_it", { maxLength: 30 })).toBe("do_it(required, alpha?, +2)");
    // Required parameters are never dropped, however tight the cap.
    expect(actionSignature(tool, "do_it", { maxLength: 5 })).toBe("do_it(required, +3)");
  });
});

describe("signatures from an Epic input schema", () => {
  it("reads the generated schema, with object references as ref", () => {
    const action: ActionSpec = {
      ...bp("mutate", "[Epic X] Adds an event. Params: blueprint, event_name, position?", "epic_call_tool"),
      epicSchema: {
        properties: {
          blueprint: { type: "object", properties: { refPath: {} } },
          event_name: { type: "string" },
          position: { type: "object" },
          nodes: { type: "array" },
          untyped: {},
        },
        required: ["blueprint", "event_name"],
      },
    };
    const tool = categoryTool("blueprint", "BP.", { epic_add_event: action });
    expect(actionSignature(tool, "epic_add_event")).toBe(
      "epic_add_event(blueprint:ref, event_name, position?:o, nodes?:[], untyped?:*)",
    );
  });
});

describe("signatures from an Epic input schema, continued", () => {
  it("sends an argument named like a dispatcher key through input, never at the top level", () => {
    const action: ActionSpec = {
      ...bp("read", "[Epic X] Windows. Params: index?, input?", "epic_call_tool"),
      epicSchema: { properties: { action: { type: "string" }, index: { type: "integer" } } },
    };
    const tool = categoryTool("widget", "W.", { epic_windows: action });
    expect(actionSignature(tool, "epic_windows")).toBe("epic_windows(index?:i, input?:o)");
  });
});

describe("signatures from the declared shape", () => {
  it("covers an in-process action with no spec from describe_action's reading", () => {
    const tool = categoryTool("demo", "Demo.", {
      run: { kind: "handler", effect: "read", description: "Run. Params: name, limit?", handler: async () => ({}) },
    }, { name: z.string().optional(), limit: z.number().optional() });
    expect(actionSignature(tool, "run")).toBe("run(name, limit?:n)");
  });
});

describe("the shipped surface", () => {
  it("gives every action a signature named after it, from structured data", () => {
    for (const tool of ALL_TOOLS) {
      for (const action of Object.keys(tool.actions)) {
        const sig = actionSignature(tool, action);
        expect(sig.startsWith(`${action}(`) && sig.endsWith(")"), `${tool.name}.${sig}`).toBe(true);
        // Routing parameters are the legend's business, never a signature's.
        expect(sig, `${tool.name}.${action}`).not.toMatch(/[(, ](timeoutMs|select|omit)\??[,:)]/);
      }
    }
  });

  it("renders a spec'd action from its spec: asset.set_property", () => {
    const asset = ALL_TOOLS.find((t) => t.name === "asset")!;
    expect(actionSignature(asset, "set_property")).toMatch(/^set_property\(assetPath\|path, propertyName, value:\*/);
  });

  it("gives every wrapped engine tool its generated schema", () => {
    const missing = ALL_TOOLS.flatMap((t) => Object.entries(t.actions)
      .filter(([name, spec]) => name.startsWith("epic_") && spec.kind === "bridge" && spec.bridge === "epic_call_tool" && !spec.epicSchema)
      .map(([name]) => `${t.name}.${name}`));
    expect(missing).toEqual([]);
  });

  it("explains every code a signature can use", () => {
    for (const code of ["?", "|", "one(", "any(", "s ", "n ", "i ", "b ", "o ", "v ", "r ", "c ", "ref", "*", "[t]", "t/u", "=x", "o<k>", "+N"]) {
      expect(SIGNATURE_LEGEND).toContain(code);
    }
  });
});
