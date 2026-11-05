/**
 * The parameters a run of a flow may pass, derived from its steps (#1508).
 *
 * `flow(run)` merges run `params` into every step's options, so a flow's inputs
 * are the parameters its step tasks read that the steps leave unset. Each
 * task's parameters come from its action's schema, overlaid by the task's
 * flowkit option specs (a class's static `optionsSchema` refined by the
 * definition's `options_schema`). A task with neither falls back to the
 * `Params:` clause of its configured description.
 */
import { mergeOptionSpecs, taskClassMetadata } from "@db-lyon/flowkit";
import type { FlowDefinition, OptionSpec, OptionSpecs, TaskDefinition, TaskRegistry } from "@db-lyon/flowkit";
import type { ToolDef } from "../core/types.js";
import { actionSchema, parseParams, ROUTING_PARAMS } from "../surface/action-schema.js";

/** One step that reads an input, addressed as an expanded plan addresses it. */
export interface FlowInputStep {
  /** `3`, `2/1` inside step 2's nested flow, or `on_start/1` for a hook step. */
  path: string;
  task: string;
  /** The flow the step is declared in. */
  flow: string;
}

export interface FlowInput {
  name: string;
  /** One type, or every distinct type when the steps disagree (`conflict`). */
  type: string | string[];
  conflict?: true;
  description?: string;
  enum?: unknown[];
  default?: unknown;
  /** A step reading it requires it and nothing supplies a default. */
  required: boolean;
  steps: FlowInputStep[];
}

/** What one step's task declares about one parameter. */
interface StepParam {
  type: string;
  description?: string;
  enum?: unknown[];
  default?: unknown;
  required: boolean;
  /** Other names the action accepts for it. */
  aliases?: string[];
  /** Every name in the choice it belongs to, itself included. */
  choice?: string[];
}

export interface FlowInputSources {
  flows: Record<string, FlowDefinition>;
  tasks: Record<string, TaskDefinition>;
  registry: TaskRegistry;
  tools: readonly ToolDef[];
}

const HOOK_PHASES = ["on_start", "on_success", "on_failure", "finally"] as const;

type StepDef = { task?: string; flow?: string; options?: Record<string, unknown> };

export async function deriveFlowInputs(flowName: string, sources: FlowInputSources): Promise<FlowInput[]> {
  const byName = new Map<string, { params: StepParam[]; steps: FlowInputStep[] }>();
  const order: string[] = [];
  const toolByName = new Map(sources.tools.map((t) => [t.name, t]));

  const visitTask = async (task: string, path: string, flow: string, set: ReadonlySet<string>): Promise<void> => {
    for (const [name, param] of await taskParams(task, sources, toolByName)) {
      // An alias sets the parameter, and a step that picked one side of a choice
      // (`actorLabel OR actorPath`) has no use for the other.
      const taken = (n: string) => set.has(n);
      if (taken(name) || param.aliases?.some(taken) || param.choice?.some(taken) || ROUTING_PARAMS.has(name)) continue;
      let entry = byName.get(name);
      if (!entry) {
        entry = { params: [], steps: [] };
        byName.set(name, entry);
        order.push(name);
      }
      entry.params.push(param);
      entry.steps.push({ path, task, flow });
    }
  };

  const visitFlow = async (
    name: string,
    prefix: string,
    parentOptions: Record<string, Record<string, unknown>>,
    ancestors: ReadonlySet<string>,
  ): Promise<void> => {
    const def = sources.flows[name];
    if (!def || ancestors.has(name)) return;
    const inside = new Set(ancestors).add(name);
    const at = (p: string) => (prefix ? `${prefix}/${p}` : p);
    const rows: Array<{ step: StepDef; path: string }> = Object.keys(def.steps ?? {})
      .map(Number)
      .sort((a, b) => a - b)
      .map((key) => ({ step: (def.steps as Record<string, StepDef>)[String(key)], path: at(String(key)) }));
    for (const phase of HOOK_PHASES) {
      const hooks = (def as Record<string, unknown>)[phase] as StepDef[] | undefined;
      (hooks ?? []).forEach((step, i) => rows.push({ step, path: at(`${phase}/${i + 1}`) }));
    }
    for (const { step, path } of rows) {
      if (step.task === "None" || step.flow === "None") continue;
      if (step.flow) {
        // A nested flow step's options are a per-task override map for the child.
        await visitFlow(step.flow, path, mergeParentOptions(parentOptions, step.options), inside);
        continue;
      }
      if (!step.task) continue;
      const set = new Set([
        ...Object.keys(sources.tasks[step.task]?.options ?? {}),
        ...Object.keys(parentOptions[step.task] ?? {}),
        ...Object.keys(step.options ?? {}),
      ]);
      await visitTask(step.task, path, name, set);
    }
  };

  await visitFlow(flowName, "", {}, new Set());

  const inputs = order.map((name) => mergeInput(name, byName.get(name)!));
  // Required first, then in the order the run meets them.
  return [...inputs.filter((i) => i.required), ...inputs.filter((i) => !i.required)];
}

function mergeParentOptions(
  base: Record<string, Record<string, unknown>>,
  overrides: Record<string, unknown> | undefined,
): Record<string, Record<string, unknown>> {
  if (!overrides) return base;
  const out = { ...base };
  for (const [task, opts] of Object.entries(overrides)) {
    if (opts && typeof opts === "object" && !Array.isArray(opts)) {
      out[task] = { ...(base[task] ?? {}), ...(opts as Record<string, unknown>) };
    }
  }
  return out;
}

/** Every parameter one task reads, with its declared shape. */
async function taskParams(
  task: string,
  sources: FlowInputSources,
  toolByName: ReadonlyMap<string, ToolDef>,
): Promise<Map<string, StepParam>> {
  const def = sources.tasks[task];
  const classPath = def?.class_path ?? task;
  const out = new Map<string, StepParam>();

  const schema = actionSchemaOf(classPath, toolByName) ?? (classPath === task ? undefined : actionSchemaOf(task, toolByName));
  if (schema) {
    for (const p of schema.params) {
      out.set(p.name, {
        type: normaliseType(p.type),
        description: p.description,
        enum: p.enumValues,
        default: p.default,
        required: p.required,
        aliases: p.aliases,
        choice: p.alternativeGroup === undefined ? undefined : schema.alternatives?.[p.alternativeGroup]?.branches.flat(),
      });
    }
  }

  let meta: OptionSpecs | undefined;
  try {
    meta = taskClassMetadata(await sources.registry.resolve(classPath)).optionsSchema;
  } catch {
    // A class that cannot load has no static metadata; it fails where it would anyway, at run time.
  }
  const specs = mergeOptionSpecs(meta, def?.options_schema);

  if (!schema) {
    for (const p of parseParams(def?.description ?? "").params) {
      out.set(p.name, { type: "any", required: p.group === undefined && !p.optional });
    }
  }

  for (const [name, spec] of Object.entries(specs ?? {})) {
    const base = out.get(name);
    out.set(name, overlay(base, spec));
  }
  return out;
}

function actionSchemaOf(ref: string, toolByName: ReadonlyMap<string, ToolDef>) {
  const dot = ref.indexOf(".");
  if (dot <= 0) return undefined;
  const tool = toolByName.get(ref.slice(0, dot));
  const action = ref.slice(dot + 1);
  if (!tool || !Object.hasOwn(tool.actions, action)) return undefined;
  return actionSchema(tool, action);
}

/** An option spec over the action's own declaration, field by field. */
function overlay(base: StepParam | undefined, spec: OptionSpec): StepParam {
  return {
    type: spec.type !== undefined ? specType(spec) : base?.type ?? "any",
    description: spec.description ?? base?.description,
    enum: spec.enum ?? base?.enum,
    default: spec.default !== undefined ? spec.default : base?.default,
    required: spec.required ?? base?.required ?? false,
    aliases: base?.aliases,
    choice: base?.choice,
  };
}

function specType(spec: OptionSpec): string {
  const types = Array.isArray(spec.type) ? spec.type : [spec.type!];
  const item = spec.items && typeof spec.items.type === "string" ? spec.items.type : undefined;
  const names = types.map((t) => (t === "array" && item ? `${item}[]` : t));
  if (spec.nullable && !names.includes("null")) names.push("null");
  return names.join("|");
}

/** The action schema's `enum` is a string enum; say so, so it agrees with an option spec's type. */
function normaliseType(type: string): string {
  return type === "enum" || type === "literal" ? "string" : type;
}

function mergeInput(name: string, entry: { params: StepParam[]; steps: FlowInputStep[] }): FlowInput {
  const types = [...new Set(entry.params.map((p) => p.type).filter((t) => t !== "any"))];
  const enums = entry.params.map((p) => p.enum).filter((e): e is unknown[] => Array.isArray(e));
  const enumValues = enums.length === 0
    ? undefined
    : enums.reduce((acc, e) => acc.filter((v) => e.some((w) => sameValue(v, w))));
  const description = entry.params.find((p) => p.description)?.description;
  const dflt = entry.params.find((p) => p.default !== undefined)?.default;
  const conflict = types.length > 1 || (enumValues !== undefined && enumValues.length === 0);

  const input: FlowInput = {
    name,
    type: types.length === 0 ? "any" : types.length === 1 ? types[0] : types,
    required: dflt === undefined && entry.params.some((p) => p.required),
    steps: entry.steps,
  };
  if (conflict) input.conflict = true;
  if (description) input.description = description;
  if (enumValues !== undefined) input.enum = enumValues.length > 0 ? enumValues : unionOf(enums);
  if (dflt !== undefined) input.default = dflt;
  return input;
}

function unionOf(lists: unknown[][]): unknown[] {
  const out: unknown[] = [];
  for (const list of lists) for (const v of list) if (!out.some((w) => sameValue(v, w))) out.push(v);
  return out;
}

function sameValue(a: unknown, b: unknown): boolean {
  return a === b || JSON.stringify(a) === JSON.stringify(b);
}
