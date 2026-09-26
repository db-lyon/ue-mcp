/**
 * What a parameter spec can say beyond a flat list of names (#1057): required
 * choices, value shapes (a colour, a union, null, a literal flag, an object or
 * array element with declared fields), and a contract exemption. Each is held
 * here from the recording's format, through the generated zod and Params
 * clause, to describe_action and the check every dispatch route runs.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { z } from "zod";
import { paramsClause, renderAll, zodExpression } from "../../scripts/lib/handler-spec-gen.mjs";
import {
  choiceViolation,
  compareHandlerSpecs,
  makeSpecBp,
  paramZod,
  renderChoice,
  specProblems,
  zodSignature,
  type HandlerSpec,
  type HandlerSpecs,
  type ParamSpec,
} from "../../src/handler-spec.js";
import { actionSchema, parseParams } from "../../src/action-schema.js";
import { categoryTool } from "../../src/category-tool.js";
import { prepareCall } from "../../src/dispatch/call-pipeline.js";
import { bridgeTaskClass } from "../../src/flow/task-factory.js";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const SNAPSHOT = JSON.parse(fs.readFileSync(path.join(ROOT, "tests", "golden", "handler-specs.json"), "utf8")) as {
  handlers: HandlerSpecs;
};

const p = (name: string, type: ParamSpec["type"], extra: Partial<ParamSpec> = {}): ParamSpec =>
  ({ name, type, required: false, description: `${name} probe`, ...extra });

/** A pcg-shaped spec: one required name, then a choice between an object and a pair. */
const SETTINGS: HandlerSpec = {
  category: "pcg",
  params: [
    p("assetPath", "string", { required: true, aliases: ["path"] }),
    p("nodeName", "string", { required: true }),
    p("settings", "object"),
    p("propertyName", "string"),
    p("propertyValue", "string"),
  ],
  choices: [{ mode: "exactlyOne", branches: [["settings"], ["propertyName", "propertyValue"]] }],
};

/** A level-shaped spec: an exactly-one actor selector and a nullable reference. */
const SELECTOR: HandlerSpec = {
  category: "level",
  params: [p("actorLabel", "string"), p("actorPath", "string"), p("skeletalMesh", "string", { nullable: true })],
  choices: [{ mode: "exactlyOne", branches: [["actorLabel"], ["actorPath"]] }],
};

/** An at-least-one selector group behind a required parameter. */
const ANY_OF: HandlerSpec = {
  category: "level",
  params: [
    p("hlodLayer", "string", { required: true, nullable: true }),
    p("actorLabels", "array", { items: "string" }),
    p("labelPrefix", "string"),
    p("tag", "string"),
    p("dryRun", "boolean"),
  ],
  choices: [{ mode: "atLeastOne", branches: [["actorLabels"], ["labelPrefix"], ["tag"]] }],
  contractExempt: "Selector-driven batch write",
};

/** Evaluate a generated zod expression the way the generated module will. */
function evalZod(expr: string): z.ZodTypeAny {
  return new Function("z", `return ${expr};`)(z) as z.ZodTypeAny;
}

describe("value shapes", () => {
  const shapes: ParamSpec[] = [
    p("tint", "color"),
    p("value", "number", { orTypes: ["color"] }),
    p("scale", "number", { orTypes: ["vec3"] }),
    p("rotation", "rotator", { nullable: true }),
    p("channels", "array", { items: "string", orTypes: ["string"] }),
    p("layer", "string", { nullable: true }),
    p("reduceKeys", "boolean", { literal: false }),
    p("version", "integer", { literal: 1 }),
    p("entries", "array", {
      items: "object",
      fields: [
        { name: "mesh", type: "string", required: true, description: "Mesh path" },
        { name: "weight", type: "number", required: false, description: "Weight" },
        { name: "tags", type: "array", items: "string", required: false, description: "Tags" },
      ],
    }),
    p("bounds", "object", {
      fields: [
        { name: "min", type: "vec3", required: true, description: "Min corner" },
        { name: "max", type: "vec3", required: true, description: "Max corner" },
      ],
    }),
  ];

  it("are well formed", () => {
    expect(specProblems({ probe: { category: "level", params: shapes } })).toEqual([]);
  });

  it("render to the zod the runtime builds, for every shape and every recorded parameter", () => {
    const recorded = Object.values(SNAPSHOT.handlers).flatMap((s) => s.params);
    for (const param of [...shapes, ...recorded]) {
      expect(zodSignature(evalZod(zodExpression(param))), param.name).toBe(zodSignature(paramZod(param)));
    }
  });

  it("accept exactly what they declare", () => {
    const zodOf = (name: string) => paramZod(shapes.find((s) => s.name === name)!);
    expect(zodOf("tint").safeParse({ r: 1, g: 0.5, b: 0 }).success).toBe(true);
    expect(zodOf("tint").safeParse({ r: 1, g: 0.5 }).success).toBe(false);
    expect(zodOf("value").safeParse(0.5).success).toBe(true);
    expect(zodOf("value").safeParse({ r: 1, g: 1, b: 1, a: 1 }).success).toBe(true);
    expect(zodOf("value").safeParse("red").success).toBe(false);
    expect(zodOf("scale").safeParse({ x: 1, y: 1, z: 1 }).success).toBe(true);
    expect(zodOf("layer").safeParse(null).success).toBe(true);
    expect(zodOf("rotation").safeParse(null).success).toBe(true);
    expect(paramZod(p("plain", "string")).safeParse(null).success).toBe(false);
    expect(zodOf("reduceKeys").safeParse(false).success).toBe(true);
    expect(zodOf("reduceKeys").safeParse(true).success).toBe(false);
    expect(zodOf("channels").safeParse("A,B").success).toBe(true);
    expect(zodOf("channels").safeParse(["A"]).success).toBe(true);
    expect(zodOf("entries").safeParse([{ mesh: "/Game/M", weight: 2 }]).success).toBe(true);
    expect(zodOf("entries").safeParse([{ weight: 2 }]).success).toBe(false);
    expect(zodOf("bounds").safeParse({ min: { x: 0, y: 0, z: 0 }, max: { x: 1, y: 1, z: 1 } }).success).toBe(true);
  });

  it("are refused when they do not fit their type", () => {
    const refused = (param: ParamSpec) => specProblems({ probe: { params: [param] } }).join("\n");
    expect(refused(p("a", "number", { orTypes: ["number"] }))).toContain("its own type");
    expect(refused(p("a", "number", { orTypes: ["any"] }))).toContain("union with any");
    expect(refused(p("a", "string", { orTypes: ["array"] }))).toContain("array as an alternative");
    expect(refused(p("a", "number", { orTypes: ["color", "color"] }))).toContain("listed twice");
    expect(refused(p("a", "boolean", { literal: "yes" }))).toContain("literal");
    expect(refused(p("a", "boolean", { literal: true, orTypes: ["string"] }))).toContain("both a literal and a union");
    expect(refused(p("a", "string", { fields: [] }))).toContain("neither an object nor an array of objects");
    expect(refused(p("a", "array", { items: "string", fields: [] }))).toContain("neither an object");
    expect(refused(p("a", "object", {
      fields: [
        { name: "x", type: "number", required: true, description: "" },
        { name: "x", type: "number", required: true, description: "" },
      ],
    }))).toContain("declared twice");
    expect(refused(p("a", "string", { nullable: false }))).toContain("only as true");
  });

  it("describe as their types, with a union's alternatives and null", () => {
    const tool = categoryTool("probe", "Probe.", {
      set: makeSpecBp({ probe_set: paramsClause({ params: shapes }) }, { probe_set: { category: "probe", params: shapes } })(
        "mutate", "Set it.", "probe_set",
      ),
    }, Object.fromEntries(shapes.map((s) => [s.name, paramZod(s).optional()])));
    const schema = actionSchema(tool, "set");
    const typeOf = (name: string) => schema.params.find((x) => x.name === name)?.type;
    expect(typeOf("tint")).toBe("object");
    expect(typeOf("value")).toBe("number|object");
    expect(typeOf("layer")).toBe("string|null");
    expect(typeOf("entries")).toBe("object[]");
    expect(schema.params.find((x) => x.name === "reduceKeys")?.enumValues).toEqual(["false"]);
  });
});

describe("choices", () => {
  it("are well formed when they name declared, optional parameters once", () => {
    expect(specProblems({ settings: SETTINGS, selector: SELECTOR, anyOf: ANY_OF })).toEqual([]);
  });

  it("are refused when they cannot be satisfied or say nothing", () => {
    const refused = (choices: HandlerSpec["choices"]) =>
      specProblems({ probe: { ...SETTINGS, choices } }).join("\n");
    expect(refused([{ mode: "exactlyOne", branches: [["settings"]] }])).toContain("fewer than two branches");
    expect(refused([{ mode: "exactlyOne", branches: [["settings"], []] }])).toContain("empty branch");
    expect(refused([{ mode: "exactlyOne", branches: [["settings"], ["nothing"]] }])).toContain("not a declared parameter");
    expect(refused([{ mode: "exactlyOne", branches: [["settings"], ["assetPath"]] }])).toContain("required and also a side");
    expect(refused([{ mode: "exactlyOne", branches: [["settings"], ["settings", "propertyName"]] }])).toContain("more than one branch");
    expect(refused([{ mode: "oneOrTwo" as never, branches: [["settings"], ["propertyName"]] }])).toContain("unknown mode");
  });

  it("refuse a required parameter right behind an at-least-one group, which the clause would absorb", () => {
    const behind: HandlerSpec = {
      params: [p("labelPrefix", "string"), p("tag", "string"), p("hlodLayer", "string", { required: true })],
      choices: [{ mode: "atLeastOne", branches: [["labelPrefix"], ["tag"]] }],
    };
    expect(specProblems({ probe: behind }).join("\n")).toContain("follows an at-least-one choice");
  });

  it("render in the clause where their first member is declared, as the hand-written clauses read", () => {
    expect(paramsClause(SETTINGS)).toBe("Params: assetPath (or path), nodeName, settings OR propertyName + propertyValue");
    expect(paramsClause(SELECTOR)).toBe("Params: actorLabel OR actorPath, skeletalMesh?");
    expect(paramsClause(ANY_OF)).toBe("Params: hlodLayer, at least one of actorLabels/labelPrefix/tag, dryRun?");
    expect(renderChoice({ mode: "atLeastOne", branches: [["a"], ["b", "c"]] }, [p("a", "string"), p("b", "string"), p("c", "string")]))
      .toBe("at least one of a OR b + c");
  });

  it("parse back out of the clause as the same choice groups", () => {
    for (const spec of [SETTINGS, SELECTOR, ANY_OF]) {
      const known = new Set(spec.params.flatMap((x) => [x.name, ...(x.aliases ?? [])]));
      const parsed = parseParams(`Do it. ${paramsClause(spec)}`, known);
      // `assetPath (or path)` parses as a choice between spellings; the spec
      // calls that an alias, and describe_action reports it as one.
      const spellings = new Map(spec.params.map((x) => [x.name, new Set([x.name, ...(x.aliases ?? [])])]));
      const isAlias = (branches: string[][]) =>
        [...spellings.values()].some((names) => branches.flat().every((n) => names.has(n)));
      expect(parsed.alternatives.filter((a) => !isAlias(a.branches))).toEqual(spec.choices!.map((c) => ({ branches: c.branches, required: true })));
      expect(parsed.params.map((x) => x.name).sort()).toEqual(
        spec.params.flatMap((x) => [x.name, ...(x.aliases ?? [])]).sort(),
      );
    }
  });

  it("are satisfied by exactly one branch, or at least one, in full, under any alias", () => {
    const aliasChoice: HandlerSpec = {
      params: [p("actorLabel", "string", { aliases: ["label"] }), p("actorPath", "string")],
      choices: [{ mode: "exactlyOne", branches: [["actorLabel"], ["actorPath"]] }],
    };
    expect(choiceViolation(SETTINGS, { assetPath: "/Game/G", nodeName: "N", settings: {} })).toBeUndefined();
    expect(choiceViolation(SETTINGS, { propertyName: "Seed", propertyValue: "4" })).toBeUndefined();
    expect(choiceViolation(SETTINGS, { assetPath: "/Game/G" })).toMatch(/needs settings OR propertyName \+ propertyValue, and none was given/);
    expect(choiceViolation(SETTINGS, { settings: {}, propertyName: "Seed", propertyValue: "4" })).toMatch(/takes one side/);
    expect(choiceViolation(SETTINGS, { propertyName: "Seed" })).toMatch(/without propertyValue/);
    expect(choiceViolation(SETTINGS, { settings: undefined })).toMatch(/none was given/);
    expect(choiceViolation(aliasChoice, { label: "Cube" })).toBeUndefined();
    expect(choiceViolation(aliasChoice, { label: "Cube", actorPath: "/Game/M.M:Cube" })).toMatch(/takes one side/);
    expect(choiceViolation(ANY_OF, { hlodLayer: null, tag: "A", labelPrefix: "B" })).toBeUndefined();
    expect(choiceViolation(ANY_OF, { hlodLayer: null })).toMatch(/at least one of actorLabels\/labelPrefix\/tag/);
    expect(choiceViolation({ params: [] }, {})).toBeUndefined();
  });

  it("travel on the action specBp declares, and describe_action reports them as choice groups", () => {
    const specBp = makeSpecBp({ probe_settings: paramsClause(SETTINGS) }, { probe_settings: SETTINGS });
    const action = specBp("mutate", "Set node settings.", "probe_settings");
    expect(action.kind === "bridge" && action.paramChoices).toEqual(SETTINGS.choices);
    const tool = categoryTool("probe", "Probe.", { set_node_settings: action },
      Object.fromEntries(SETTINGS.params.flatMap((x) => [x.name, ...(x.aliases ?? [])]).map((n) => [n, z.unknown().optional()])));
    const schema = actionSchema(tool, "set_node_settings");
    expect(schema.alternatives).toEqual([{ branches: [["settings"], ["propertyName", "propertyValue"]], required: true }]);
    const byName = new Map(schema.params.map((x) => [x.name, x]));
    expect(byName.get("settings")?.alternativeGroup).toBe(0);
    expect(byName.get("propertyValue")?.alternativeGroup).toBe(0);
    expect(byName.get("settings")?.required).toBe(false);
    expect(byName.get("nodeName")?.alternativeGroup).toBeUndefined();
    expect(byName.get("nodeName")?.required).toBe(true);
  });
});

describe("the choice check at the TS boundary", () => {
  const specBp = makeSpecBp({ probe_settings: paramsClause(SETTINGS) }, { probe_settings: SETTINGS });
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
    };
  }
  const tool = () => categoryTool("probe", "Probe.", { set_node_settings: specBp("mutate", "Set.", "probe_settings") }, {
    assetPath: z.string().optional(), nodeName: z.string().optional(), settings: z.record(z.unknown()).optional(),
    propertyName: z.string().optional(), propertyValue: z.string().optional(),
  });

  it("refuses a call that satisfies no branch, before anything is sent", async () => {
    const seen: Array<Record<string, unknown>> = [];
    await expect(tool().handler(fakeCtx(seen) as never, { action: "set_node_settings", assetPath: "/Game/G", nodeName: "N" }))
      .rejects.toThrow(/set_node_settings needs settings OR propertyName \+ propertyValue, and none was given, so the call was not sent/);
    expect(seen).toEqual([]);
  });

  it("sends a call that satisfies one branch, untouched", async () => {
    const seen: Array<Record<string, unknown>> = [];
    await tool().handler(fakeCtx(seen) as never, { action: "set_node_settings", assetPath: "/Game/G", nodeName: "N", propertyName: "Seed", propertyValue: "4" });
    expect(seen).toEqual([{ assetPath: "/Game/G", nodeName: "N", propertyName: "Seed", propertyValue: "4" }]);
  });

  it("is the same check on the flow route", async () => {
    const seen: Array<Record<string, unknown>> = [];
    const Task = bridgeTaskClass("probe.set_node_settings", "probe_settings", undefined, undefined, {
      action: "set_node_settings",
      paramChoices: { params: SETTINGS.params, choices: SETTINGS.choices! },
    });
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    await expect(new (Task as any)(fakeCtx(seen), { settings: {}, propertyName: "Seed", propertyValue: "4" }).execute())
      .rejects.toThrow(/takes one side/);
    expect(seen).toEqual([]);
  });

  it("runs after the category's folding, so a folded spelling counts", () => {
    const pipeline = prepareCall({ path: "/Game/G", nodeName: "N", props: { A: 1 } }, {
      action: "set_node_settings",
      normalizeParams: (bag) => {
        const { props, ...rest } = bag;
        return props === undefined ? bag : { ...rest, settings: props };
      },
      paramChoices: { params: SETTINGS.params, choices: SETTINGS.choices! },
    });
    expect(pipeline.params.settings).toEqual({ A: 1 });
  });
});

describe("the recording format", () => {
  it("adds nothing to a spec that uses none of it", () => {
    const plain: HandlerSpecs = {
      probe: { category: "animation", params: [{ name: "assetPath", type: "string", required: true, description: "d" }] },
    };
    const files = renderAll({ handlers: plain });
    const module = files.get("src/tools/specs/animation.generated.ts")!;
    expect(module).not.toMatch(/nullable|orTypes|literal|fields|choices|contractExempt/);
  });

  it("carries a contract exemption with a reason, and generates the surface like any other spec", () => {
    expect(specProblems({ probe: { ...ANY_OF, contractExempt: " " } }).join("\n")).toContain("needs a reason");
    const files = renderAll({ handlers: { probe_exempt: ANY_OF } });
    const module = files.get("src/tools/specs/level.generated.ts")!;
    expect(module).toContain('"contractExempt": "Selector-driven batch write"');
    expect(module).toContain('probe_exempt: "Params: hlodLayer, at least one of actorLabels/labelPrefix/tag, dryRun?"');
    expect(module).toContain("hlodLayer: z.string().nullable().optional()");
  });

  it("reports drift in a choice, a shape or an exemption", () => {
    const recorded: HandlerSpecs = { settings: SETTINGS };
    expect(compareHandlerSpecs(recorded, { settings: SETTINGS }).drifted).toEqual([]);
    expect(compareHandlerSpecs(recorded, { settings: { ...SETTINGS, choices: undefined } }).drifted).toEqual(["settings"]);
    expect(compareHandlerSpecs(recorded, { settings: { ...SETTINGS, contractExempt: "writes" } }).drifted).toEqual(["settings"]);
    const nullable = { ...SETTINGS, params: SETTINGS.params.map((x) => (x.name === "nodeName" ? { ...x, nullable: true } : x)) };
    expect(compareHandlerSpecs(recorded, { settings: nullable }).drifted).toEqual(["settings"]);
  });
});
