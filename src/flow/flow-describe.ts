/**
 * Flows as describe, search and list_available_actions report them.
 *
 * A flow is not an action and never appears in tools/list; these readers are
 * how an agent finds one and learns what it will run: its resolved steps with
 * nested flows flattened, their conditions and checks, deprecation, and which
 * config layer each flow and step came from.
 */
import { FlowRunner } from "@db-lyon/flowkit";
import type { FlowDefinition, PlanStep, StepCheck, TaskDefinition, TaskRegistry } from "@db-lyon/flowkit";
import type { ToolDef } from "../core/types.js";
import { classifyAvailability, type ActionAvailability } from "../dispatch/offline.js";
import { searchToolGraph } from "../surface/context/tool-search.js";

/** One config layer's parsed document, and the name it is reported under. */
export interface ConfigLayerDoc {
  source: string;
  doc: unknown;
}

/** What describing a session's flows needs: its merged config, its registry, and its layers. */
export interface FlowSource {
  config: { tasks: Record<string, unknown>; flows: Record<string, unknown> };
  registry: TaskRegistry;
  /** The layers the config was merged from, lowest precedence first. */
  layers?: () => ConfigLayerDoc[];
}

export interface FlowStepReport {
  path: string;
  depth: number;
  stepNumber: number;
  type: "task" | "flow";
  name: string;
  /** The config layer this step's definition was last set in. */
  source?: string;
  skipped?: boolean;
  when?: string | boolean;
  ignore_failure?: boolean;
  retries?: number;
  options?: Record<string, unknown>;
  checks?: StepCheck[];
  deprecated?: boolean | string;
  replaced_by?: string;
  /** The task or nested flow does not resolve in this session. */
  unresolved?: true;
}

export interface FlowReport {
  kind: "flow";
  name: string;
  description?: string;
  source?: string;
  deprecated?: boolean | string;
  replaced_by?: string;
  rollback_on_failure?: boolean;
  options_scope?: "flat" | "step";
  checks?: StepCheck[];
  steps: FlowStepReport[];
  hooks?: Record<string, string[]>;
  run: string;
}

const HOOK_PHASES = ["on_start", "on_success", "on_failure", "finally"] as const;

function runnerFor(source: FlowSource): FlowRunner {
  return new FlowRunner({
    tasks: source.config.tasks as Record<string, TaskDefinition>,
    flows: source.config.flows as Record<string, FlowDefinition>,
    registry: source.registry,
    context: {} as never,
  });
}

function at(doc: unknown, keys: string[]): unknown {
  let cur = doc;
  for (const key of keys) {
    if (cur === null || typeof cur !== "object") return undefined;
    cur = (cur as Record<string, unknown>)[key];
  }
  return cur;
}

/** The highest-precedence layer that sets `keys`. */
function sourceOf(layers: ConfigLayerDoc[] | undefined, keys: string[]): string | undefined {
  if (!layers) return undefined;
  for (let i = layers.length - 1; i >= 0; i--) {
    if (at(layers[i].doc, keys) !== undefined) return layers[i].source;
  }
  return undefined;
}

/** A flow's resolved plan, or undefined when this session has no flow of that name. */
export async function describeFlow(source: FlowSource, name: string): Promise<FlowReport | undefined> {
  const flows = source.config.flows as Record<string, FlowDefinition>;
  if (!flows[name]) return undefined;
  const runner = runnerFor(source);
  const layers = source.layers?.();
  const declared = runner.describeFlow(name);

  const steps: FlowStepReport[] = [];
  const walk = async (flowName: string, planSteps: PlanStep[], prefix: string, depth: number, seen: Set<string>) => {
    for (const step of planSteps) {
      const path = prefix ? `${prefix}/${step.stepNumber}` : String(step.stepNumber);
      const row: FlowStepReport = { path, depth, stepNumber: step.stepNumber, type: step.type, name: step.name };
      const where = sourceOf(layers, ["flows", flowName, "steps", String(step.stepNumber)]);
      if (where) row.source = where;
      if (step.skipped) row.skipped = true;
      if (step.when !== undefined) row.when = step.when;
      if (step.ignore_failure) row.ignore_failure = true;
      if (step.retries) row.retries = step.retries;
      if (step.options && Object.keys(step.options).length > 0) row.options = step.options;
      if (step.checks?.length) row.checks = step.checks;
      steps.push(row);
      if (step.skipped) continue;
      if (step.type === "flow") {
        const child = flows[step.name];
        if (!child) { row.unresolved = true; continue; }
        if (child.deprecated) row.deprecated = child.deprecated;
        if (child.replaced_by !== undefined) row.replaced_by = child.replaced_by;
        // A flow that nests itself is listed once, not expanded forever.
        if (!seen.has(step.name)) {
          await walk(step.name, runner.describeFlow(step.name).steps, path, depth + 1, new Set(seen).add(step.name));
        }
        continue;
      }
      try {
        const task = await runner.describeTask(step.name);
        if (task.deprecated) row.deprecated = task.deprecated;
        if (task.replaced_by !== undefined) row.replaced_by = task.replaced_by;
      } catch {
        row.unresolved = true;
      }
    }
  };
  await walk(name, declared.steps, "", 0, new Set([name]));

  const report: FlowReport = { kind: "flow", name, steps, run: `flow(action="run", flowName="${name}")` };
  if (declared.description !== undefined) report.description = declared.description;
  const where = sourceOf(layers, ["flows", name]);
  if (where) report.source = where;
  if (declared.deprecated) report.deprecated = declared.deprecated;
  if (declared.replaced_by !== undefined) report.replaced_by = declared.replaced_by;
  if (declared.rollback_on_failure !== undefined) report.rollback_on_failure = declared.rollback_on_failure;
  if (declared.options_scope !== undefined) report.options_scope = declared.options_scope;
  if (declared.checks?.length) report.checks = declared.checks;
  const flow = flows[name] as unknown as Record<string, unknown>;
  const hooks: Record<string, string[]> = {};
  for (const phase of HOOK_PHASES) {
    const block = flow[phase];
    if (!block || typeof block !== "object") continue;
    hooks[phase] = Object.values(block as Record<string, { task?: string; flow?: string }>)
      .map((s) => s?.task ?? (s?.flow ? `flow:${s.flow}` : undefined))
      .filter((s): s is string => typeof s === "string" && s !== "None");
  }
  if (Object.keys(hooks).length > 0) report.hooks = hooks;
  return report;
}

/** A flow reference as a caller may write it: `name`, `flow.name` or `flow:name`. */
export function flowNameOf(ref: string): string {
  return ref.trim().replace(/^flow\s*[.:]\s*/i, "");
}

export interface FlowHit {
  flow: string;
  description?: string;
  score: number;
  run: string;
}

/** Flows matching a search query, scored the way actions are. */
export function searchFlows(flows: Array<{ name: string; description?: string }>, query: string, limit: number): FlowHit[] {
  const actions = Object.fromEntries(flows.map((f) => [f.name, { description: f.description ?? "" }]));
  return searchToolGraph([{ name: "flow", actions }], query, limit).map((hit) => ({
    flow: hit.action,
    description: hit.description || undefined,
    score: hit.score,
    run: `flow(action="run", flowName="${hit.action}")`,
  }));
}

export interface FlowAvailability {
  flow: string;
  availability: ActionAvailability;
  availableNow: boolean;
  /** The steps that need an editor, or whose needs nothing declares. */
  editorSteps?: string[];
}

/** Whether each flow can run with no editor: a flow needs one when any step it runs does. */
export async function flowAvailability(
  source: FlowSource,
  graph: readonly ToolDef[],
  editorConnected: boolean,
): Promise<FlowAvailability[]> {
  const out: FlowAvailability[] = [];
  for (const name of Object.keys(source.config.flows)) {
    const report = await describeFlow(source, name);
    if (!report) continue;
    const bound = new Set<string>();
    let needsEditor = false;
    for (const step of report.steps) {
      if (step.type !== "task" || step.skipped) continue;
      const verdict = stepAvailability(step.name, graph);
      if (verdict === "always") continue;
      bound.add(step.name);
      if (verdict === "editor") needsEditor = true;
    }
    const availability: ActionAvailability = needsEditor ? "editor" : bound.size > 0 ? "unknown" : "always";
    out.push({
      flow: name,
      availability,
      availableNow: editorConnected || availability === "always",
      editorSteps: bound.size > 0 ? [...bound] : undefined,
    });
  }
  return out;
}

/** One step's needs, from the action it names. A task that is not an action is not known. */
export function stepAvailability(taskName: string, graph: readonly ToolDef[]): ActionAvailability {
  if (taskName === "shell") return "always";
  const dot = taskName.indexOf(".");
  const tool = dot > 0 ? graph.find((t) => t.name === taskName.slice(0, dot)) : undefined;
  const spec = tool?.actions[taskName.slice(dot + 1)];
  return tool && spec ? classifyAvailability(tool.name, taskName.slice(dot + 1), spec).availability : "unknown";
}
