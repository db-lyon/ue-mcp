/**
 * blueprint(author): a composite over the blueprint actions (#607).
 *
 * The plan is the input: create, one step per component, variable and
 * function, then compile. Best-effort: a failed step is recorded and the rest
 * still run, so the caller gets every step's verdict in one answer.
 */
import { z } from "zod";
import type { ChildOutcome, FlowActionSpec } from "../core/types.js";
import { handlerFailure } from "../flow/handler-outcome.js";

/** Parameters author shares with the rest of the category. */
export const AUTHOR_INPUTS = {
  assetPath: z.string().optional().describe("Blueprint asset path. Read/graph actions also accept a World/umap path (e.g. /Game/Maps/SomeLevel), resolved to that map's level script Blueprint (#942)"),
  parentClass: z.string().optional().describe("create / author / reparent: the parent class. export_batch: only Blueprints deriving from this class (#1166)"),
  components: z.array(z.record(z.unknown())).optional().describe("author: [{componentClass, componentName?, parentComponent?, childActorClass?}] (#607)"),
  variables: z.array(z.record(z.unknown())).optional().describe("author: [{name, varType}] (#607)"),
  functions: z.array(z.record(z.unknown())).optional().describe("author: [{functionName}] (#607)"),
  compile: z.boolean().optional().describe("author: compile after authoring (default true) (#607)"),
};

interface PlannedStep {
  step: string;
  target: string;
  action: string;
  params: Record<string, unknown>;
}

interface StepRecord {
  step: string;
  target: string;
  ok: boolean;
  result?: unknown;
  error?: string;
}

/** Every child author runs for this input, in order. Throws when there is no asset to author. */
function authorPlan(p: Record<string, unknown>): PlannedStep[] {
  const assetPath = p.assetPath as string;
  if (!assetPath) throw new Error("Missing 'assetPath'");
  const plan: PlannedStep[] = [];
  const add = (step: string, target: string, action: string, params: Record<string, unknown>) => {
    // A step names only the parameters the input actually carried.
    const defined = Object.fromEntries(Object.entries(params).filter(([, v]) => v !== undefined));
    plan.push({ step, target, action, params: defined });
  };
  if (p.parentClass) add("create", assetPath, "create", { assetPath, parentClass: p.parentClass });
  for (const c of (p.components as Array<Record<string, unknown>> ?? [])) {
    add("add_component", String(c.componentClass ?? ""), "add_component", { assetPath, componentClass: c.componentClass, componentName: c.componentName ?? c.componentClass, parentComponent: c.parentComponent, childActorClass: c.childActorClass });
  }
  for (const v of (p.variables as Array<Record<string, unknown>> ?? [])) {
    add("add_variable", String(v.name ?? ""), "add_variable", { assetPath, name: v.name, varType: v.varType ?? v.type });
  }
  for (const f of (p.functions as Array<Record<string, unknown>> ?? [])) {
    add("create_function", String(f.functionName ?? ""), "create_function", { assetPath, functionName: f.functionName });
  }
  if ((p.compile ?? true) !== false) add("compile", assetPath, "compile", { assetPath });
  return plan;
}

/** One step's row: a child that answered success:false is failed, with the reason it gave. */
function stepRecord(planned: PlannedStep, r: ChildOutcome): StepRecord {
  return {
    step: planned.step,
    target: planned.target,
    ok: r.success,
    ...(r.data !== undefined ? { result: r.data } : {}),
    ...(r.success ? {} : { error: handlerFailure(r.data) ?? r.error?.message ?? `blueprint.${planned.action} failed` }),
  };
}

export const authorAction: FlowActionSpec<{ assetPath: string; created: boolean; steps: StepRecord[] }> = {
  kind: "flow",
  effect: "mutate",
  description: "Author a whole Blueprint in one call: optionally create it (parentClass), then add components, variables and function stubs, then compile - one agent-facing action instead of a dozen add_* round-trips. Params: assetPath, parentClass? (create/ensure the BP if given), components? [{componentClass, componentName?, parentComponent?, childActorClass?}], variables? [{name, varType}], functions? [{functionName}], compile? (default true). Returns per-step results + created flag. (#607)",
  inputs: AUTHOR_INPUTS,
  expand: (p) => {
    try {
      return authorPlan(p).map((s) => ({ task: `blueprint.${s.action}`, options: s.params }));
    } catch {
      return null;
    }
  },
  compose: async (run, p) => {
    const plan = authorPlan(p);
    const steps: StepRecord[] = [];
    let created = false;
    for (const planned of plan) {
      const r = await run(`blueprint.${planned.action}`, planned.params);
      steps.push(stepRecord(planned, r));
      if (planned.step === "create") created = r.success && r.data?.created !== false;
    }
    return { assetPath: p.assetPath as string, created, steps };
  },
  result: ({ assetPath, created, steps }) => {
    const failed = steps.filter((s) => !s.ok);
    return { assetPath, created, stepCount: steps.length, failedCount: failed.length, ok: failed.length === 0, steps };
  },
};
