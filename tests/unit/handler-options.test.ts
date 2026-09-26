/**
 * An in-process action's declared options are the source of its describe
 * schema, its signature and its task's option schema. Its description keeps a
 * `Params:` clause, recorded by the golden baseline, and these tests hold the
 * two to each other: the clause names every declared parameter, marks exactly
 * the optional ones with `?`, and says `none` only when there are none.
 *
 * The reverse direction, a declared key written in the clause but missing from
 * the declaration, is held by action-schema-clause-coverage.test.ts.
 */
import { describe, expect, it } from "vitest";
import { TaskRegistry, type FlowDefinition, type TaskDefinition, FlowRunner } from "@db-lyon/flowkit";
import { ALL_TOOLS } from "../../src/tools.js";
import { createFlowTool } from "../../src/flow/flow-tool.js";
import { buildFlowRegistry } from "../../src/flow/registry.js";
import { buildDefaults } from "../../src/flow/loader.js";
import { categoryTool } from "../../src/surface/category-tool.js";
import { mergeInjectionsIntoTool } from "../../src/extensions/injection.js";
import type { HandlerActionSpec, ToolDef } from "../../src/core/types.js";
import type { FlowContext } from "../../src/flow/context.js";
import { z } from "zod";

/**
 * Handlers that declare nothing yet. Each is being rebuilt as a composite with
 * its own option schema; the entry goes when that lands, and a name here that
 * is no longer an undeclared handler fails the test.
 */
const UNDECLARED = new Set(["blueprint.author", "niagara.batch", "asset.search"]);

const flowTool = createFlowTool(new TaskRegistry(), () => ({ tasks: {}, flows: {} }) as never);

function handlers(): Array<{ key: string; spec: HandlerActionSpec }> {
  const out: Array<{ key: string; spec: HandlerActionSpec }> = [];
  for (const tool of [...ALL_TOOLS, flowTool]) {
    for (const [action, spec] of Object.entries(tool.actions)) {
      if (spec.kind === "handler") out.push({ key: `${tool.name}.${action}`, spec });
    }
  }
  return out;
}

/** The clause as the description writes it, up to where the prose resumes. */
function clauseOf(description: string): string | undefined {
  const at = description.search(/\bParams:/);
  if (at < 0) return undefined;
  const text = description.slice(at + "Params:".length);
  let depth = 0;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if ("([{".includes(c)) depth++;
    else if (")]}".includes(c)) depth = Math.max(0, depth - 1);
    else if (depth === 0 && /^(\.\s|Returns\b)/.test(text.slice(i))) return text.slice(0, i);
  }
  return text;
}

/** Where `name` stands in the clause as a parameter, and whether it is marked optional there. */
function marking(clause: string, name: string): "absent" | "required" | "optional" {
  const re = new RegExp(String.raw`(?<![\w.])${name}(\[\])?(\s*\?)?(?![\w])`, "g");
  let seen = false;
  for (const m of clause.matchAll(re)) {
    seen = true;
    if (m[2]) return "optional";
  }
  return seen ? "required" : "absent";
}

describe("declared handler options", () => {
  it("are declared by every in-process action", () => {
    const missing = handlers().filter(({ key, spec }) => !spec.options && !UNDECLARED.has(key)).map(({ key }) => key);
    expect(missing, "Declare options: { params: [...] } on these actions").toEqual([]);
    const stale = [...UNDECLARED].filter((key) => !handlers().some((h) => h.key === key && !h.spec.options));
    expect(stale, "No longer an undeclared handler; drop it from UNDECLARED").toEqual([]);
  });

  it("agree with the Params clause the description carries", () => {
    const offenders: string[] = [];
    for (const { key, spec } of handlers()) {
      if (!spec.options) continue;
      const clause = clauseOf(spec.description ?? "");
      if (clause === undefined) {
        offenders.push(`${key}: no Params clause`);
        continue;
      }
      const choiceMembers = new Set((spec.options.choices ?? []).flatMap((c) => c.branches.flat()));
      if (spec.options.params.length === 0 && !/^\s*none\b/.test(clause)) {
        offenders.push(`${key}: declares no parameters, clause is not 'none'`);
      }
      for (const raw of spec.options.params) {
        const optional = raw.endsWith("?");
        const name = optional ? raw.slice(0, -1) : raw;
        const seen = marking(clause, name);
        if (seen === "absent") offenders.push(`${key}: declares ${name}, clause does not name it`);
        else if (!choiceMembers.has(name) && (seen === "optional") !== optional) {
          offenders.push(`${key}: ${name} is ${optional ? "optional" : "required"}, clause marks it ${seen}`);
        }
      }
    }
    expect(offenders).toEqual([]);
  });
});

function probeTool(handler: HandlerActionSpec["handler"]): ToolDef {
  return categoryTool("probe", "Probe.", {
    read_thing: {
      kind: "handler",
      effect: "read",
      options: { params: ["name", "count?"] },
      description: "Reads a thing. Params: name, count?",
      handler,
    },
  }, {
    name: z.string().optional().describe("What to read"),
    count: z.number().optional().describe("How many"),
  });
}

function runnerFor(tool: ToolDef, steps: Record<number, unknown>): FlowRunner {
  const defaults = buildDefaults([tool]);
  return new FlowRunner({
    tasks: defaults.tasks as Record<string, TaskDefinition>,
    flows: { probe_flow: { steps } } as unknown as Record<string, FlowDefinition>,
    registry: buildFlowRegistry([tool]),
    context: { bridge: { isConnected: false }, project: {} } as unknown as FlowContext,
  });
}

describe("a declared handler's task", () => {
  it("is refused by the runner when an option has the wrong type", async () => {
    let ran = false;
    const tool = probeTool(async () => { ran = true; return { ok: true }; });
    const result = await runnerFor(tool, { 1: { task: "probe.read_thing", options: { name: "x", count: "three" } } })
      .run({ flowName: "probe_flow" });
    expect(result.success).toBe(false);
    expect(ran).toBe(false);
    expect(result.steps[0].result?.error?.name).toBe("TaskOptionsError");
    expect(result.steps[0].result?.error?.message).toMatch(/count/);
  });

  it("leaves a missing required option to the handler", async () => {
    const seen: Array<Record<string, unknown>> = [];
    const tool = probeTool(async (_ctx, p) => { seen.push(p); return { ok: true }; });
    const result = await runnerFor(tool, { 1: { task: "probe.read_thing", options: { count: 2 } } })
      .run({ flowName: "probe_flow" });
    expect(result.success).toBe(true);
    expect(seen).toEqual([{ count: 2 }]);
  });
});

describe("registry describe", () => {
  const registry = buildFlowRegistry(ALL_TOOLS);
  const defs = buildDefaults(ALL_TOOLS).tasks as Record<string, TaskDefinition>;

  it("reports a handler's declared options, required flags included", async () => {
    const d = await registry.describe("project.read_config", defs);
    expect(d.options_schema?.configName).toMatchObject({ type: "string", required: true });
  });

  it("reports a bridge action's options from its recorded C++ spec", async () => {
    const d = await registry.describe("asset.set_property", defs);
    expect(d.options_schema?.assetPath).toMatchObject({ type: "string", required: true });
    expect(d.options_schema?.propertyName).toMatchObject({ required: true });
  });

  it("reports an Epic action's options from its engine schema", async () => {
    const tool = ALL_TOOLS.find((t) => Object.values(t.actions).some((a) => a.kind === "bridge" && a.epicSchema))!;
    const [action, spec] = Object.entries(tool.actions).find(([, a]) => a.kind === "bridge" && a.epicSchema)!;
    const epic = spec.kind === "bridge" ? spec.epicSchema! : undefined;
    const d = await registry.describe(`${tool.name}.${action}`, defs);
    const names = Object.keys(epic?.properties ?? {}).filter((n) => n !== "action");
    expect(Object.keys(d.options_schema ?? {})).toEqual(expect.arrayContaining(names));
  });

  it("reports a plugin action's options from its manifest", async () => {
    const base = categoryTool("level", "Level.", {
      get_thing: { kind: "handler", effect: "read", options: { params: [] }, description: "Params: none", handler: async () => ({}) },
    });
    const { tool } = mergeInjectionsIntoTool(base, [{
      category: "level",
      prefix: "vpp",
      pluginName: "vpp",
      actions: { scatter: { task: "vpp.scatter", schema: { density: { type: "number", required: true, description: "Per square metre" } } } },
    }]);
    const pluginRegistry = buildFlowRegistry([tool]);
    class Scatter { async run() { return { success: true }; } }
    pluginRegistry.register("level.vpp_scatter", Scatter as never);
    const d = await pluginRegistry.describe("level.vpp_scatter", buildDefaults([tool]).tasks as Record<string, TaskDefinition>);
    expect(d.options_schema).toEqual({ density: { type: "number", required: true, description: "Per square metre" } });
  });
});
