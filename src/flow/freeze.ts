/**
 * Freeze: a flow resolved into the steps a run would execute, written as a
 * flow definition that is itself valid input.
 *
 * Nested flows are expanded in place, every option is resolved (task defaults,
 * the step's own, a nested flow step's overrides, the run's params, and the
 * `${project.*}`, `${editor.*}`, `${session.*}` and `${params.*}` references),
 * and `${steps.N.x}` references are renumbered to the flat plan. Saved under
 * `flows:` in ue-mcp.yml and run with no params, it runs the same steps.
 *
 * A plan that cannot be flattened without changing what it does is refused
 * with the reason: a nested flow with its own hooks or checks, a flow step
 * that retries or tolerates failure, or a reference to a nested flow's result.
 */
import type { FlowDefinition, StepCheck } from "@db-lyon/flowkit";

export interface FrozenPlan {
  /** The flow this was frozen from. */
  from: string;
  /** A flow definition: put it under `flows:` and run it with no params. */
  definition: Record<string, unknown>;
}

export interface FreezeRefusal {
  from: string;
  refused: string[];
}

export interface FreezeInput {
  config: { tasks: Record<string, unknown>; flows: Record<string, unknown> };
  flowName: string;
  params?: Record<string, unknown>;
  skip?: string[];
  /** Host reference namespaces (`project`, `editor`, `session`), read at freeze time. */
  namespaces: Record<string, unknown>;
}

interface StepDef {
  task?: string;
  flow?: string;
  options?: Record<string, unknown>;
  when?: string | boolean;
  checks?: StepCheck[];
  ignore_failure?: boolean;
  retries?: number;
  retryDelay?: number;
  retryOn?: string;
}

/** A main step of one frame, and the number it has in the frozen plan (null: a nested flow). */
interface FrameEntry {
  old: number;
  name: string;
  frozen: number | null;
}

const HOOK_PHASES = ["on_start", "on_success", "on_failure", "finally"] as const;
const REF = /\$\{(\w+)\.([^}]+)\}/g;
const WHOLE_REF = /^\$\{(\w+)\.([^}]+)\}$/;

function isRecord(v: unknown): v is Record<string, unknown> {
  return v !== null && typeof v === "object" && !Array.isArray(v);
}

function getPath(obj: unknown, path: string[]): unknown {
  let cur = obj;
  for (const seg of path) {
    if (cur == null || typeof cur !== "object") return undefined;
    cur = (cur as Record<string, unknown>)[seg];
  }
  return cur;
}

export function freezeFlow(input: FreezeInput): FrozenPlan | FreezeRefusal {
  const flows = input.config.flows as Record<string, FlowDefinition | undefined>;
  const tasks = input.config.tasks as Record<string, { options?: Record<string, unknown> } | undefined>;
  const params = input.params ?? {};
  const skip = new Set(input.skip ?? []);
  const hostRefs: Record<string, unknown> = { ...input.namespaces, params };
  const refused: string[] = [];
  const steps: Record<string, Record<string, unknown>> = {};
  let count = 0;

  const root = flows[input.flowName];
  if (!root) return { from: input.flowName, refused: [`No flow named '${input.flowName}'.`] };

  /** Host references resolved now; step and error references left for the run. */
  const resolveHost = (value: unknown): unknown => {
    if (typeof value === "string") {
      const whole = WHOLE_REF.exec(value);
      if (whole && whole[1] in hostRefs) {
        const v = getPath(hostRefs[whole[1]], whole[2].split("."));
        return v === undefined ? value : v;
      }
      return value.replace(REF, (m, ns: string, ref: string) => {
        if (!(ns in hostRefs)) return m;
        const v = getPath(hostRefs[ns], ref.split("."));
        if (v === undefined) return m;
        return typeof v === "object" && v !== null ? JSON.stringify(v) : String(v);
      });
    }
    if (Array.isArray(value)) return value.map(resolveHost);
    if (isRecord(value)) return Object.fromEntries(Object.entries(value).map(([k, v]) => [k, resolveHost(v)]));
    return value;
  };

  /** The frozen number a step reference in this frame points at, or null. */
  const target = (frame: FrameEntry[], id: string[], where: string): { num: number; rest: string[] } | null => {
    for (let i = id.length; i >= 1; i--) {
      const key = id.slice(0, i).join(".");
      const entry = /^\d+$/.test(key)
        ? frame.find((e) => String(e.old) === key)
        : [...frame].reverse().find((e) => e.name === key);
      if (!entry) continue;
      if (entry.frozen === null) {
        refused.push(`${where} reads the result of nested flow step ${entry.old} (${entry.name}), which a flat plan does not have.`);
        return null;
      }
      return { num: entry.frozen, rest: id.slice(i) };
    }
    return null;
  };

  /** Renumber `${steps.x}` (and a bare `steps.x` in an expression) to the frozen plan. */
  const renumber = <T>(value: T, frame: FrameEntry[], where: string, bare = false): T => {
    const fix = (ref: string, wrap: (s: string) => string, original: string): string => {
      const t = target(frame, ref.split("."), where);
      return t ? wrap([String(t.num), ...t.rest].join(".")) : original;
    };
    const rewrite = (s: string): string => {
      let out = s.replace(/\$\{steps\.([^}]+)\}/g, (m, ref: string) => fix(ref, (r) => `\${steps.${r}}`, m));
      if (bare) {
        out = out.replace(/(^|[^\w.$])steps\.([\w.-]+)/g, (m, lead: string, ref: string) =>
          fix(ref, (r) => `${lead}steps.${r}`, m));
      }
      return out;
    };
    const walk = (v: unknown): unknown => {
      if (typeof v === "string") return rewrite(v);
      if (Array.isArray(v)) return v.map(walk);
      if (isRecord(v)) return Object.fromEntries(Object.entries(v).map(([k, x]) => [k, walk(x)]));
      return v;
    };
    return walk(value) as T;
  };

  const both = (outer: string | undefined, inner: string | boolean | undefined): string | boolean | undefined => {
    if (inner === false) return false;
    const own = inner === true || inner === undefined ? undefined : inner;
    if (outer === undefined) return own;
    return own === undefined ? outer : `(${outer}) && (${own})`;
  };

  const runtimeFor = (scope: string | undefined, task: string, path: string): Record<string, unknown> => {
    if (scope !== "step") return params;
    return {
      ...(isRecord(params[task]) ? params[task] as Record<string, unknown> : {}),
      ...(isRecord(params[path]) ? params[path] as Record<string, unknown> : {}),
    };
  };

  const taskStep = (
    def: StepDef,
    frame: FrameEntry[],
    where: string,
    inherited: Record<string, Record<string, unknown>>,
    runtime: Record<string, unknown>,
    when: string | boolean | undefined,
  ): Record<string, unknown> => {
    const task = def.task!;
    // The runner's precedence: task default, enclosing override, step, runtime params.
    const options = resolveHost({
      ...(tasks[task]?.options ?? {}),
      ...(inherited[task] ?? {}),
      ...(def.options ?? {}),
      ...runtime,
    }) as Record<string, unknown>;
    const out: Record<string, unknown> = { task };
    if (Object.keys(options).length) out.options = renumber(options, frame, where);
    if (when !== undefined) out.when = when;
    if (def.checks?.length) out.checks = def.checks.map((c) => ({ ...c, when: renumber(resolveHost(c.when), frame, where, true) }));
    for (const key of ["ignore_failure", "retries", "retryDelay", "retryOn"] as const) {
      if (def[key] !== undefined) out[key] = def[key];
    }
    return out;
  };

  const expand = (
    name: string,
    prefix: string,
    inherited: Record<string, Record<string, unknown>>,
    outerWhen: string | undefined,
    scope: string | undefined,
    stack: string[],
  ): FrameEntry[] => {
    const flow = flows[name]!;
    const frame: FrameEntry[] = [];
    const entries = Object.entries(flow.steps ?? {}).sort(([a], [b]) => Number(a) - Number(b));
    for (const [n, raw] of entries) {
      const def = raw as StepDef;
      const path = `${prefix}${n}`;
      const where = `step ${path}`;
      const target_ = def.task ?? def.flow;
      if (!target_ || def.task === "None" || def.flow === "None") continue;
      if (!prefix && (skip.has(n) || skip.has(target_))) continue;
      const own = def.when === undefined || typeof def.when === "boolean"
        ? def.when
        : renumber(resolveHost(def.when) as string, frame, where, true);
      const when = both(outerWhen, own);
      if (when === false) continue;

      if (def.flow) {
        const child = flows[def.flow];
        if (!child) { refused.push(`${where} runs '${def.flow}', which does not exist.`); continue; }
        if (stack.includes(def.flow)) { refused.push(`${where}: '${def.flow}' runs itself.`); continue; }
        if (def.retries || def.ignore_failure) {
          refused.push(`${where} retries or tolerates the failure of the whole flow '${def.flow}', which a flat plan cannot say.`);
        }
        if (HOOK_PHASES.some((p) => (child[p] ?? []).length > 0) || child.checks?.length || def.checks?.length) {
          refused.push(`${where}: '${def.flow}' has hooks or checks of its own, which a flat plan cannot keep.`);
        }
        const overrides = { ...inherited };
        for (const [task, opts] of Object.entries(def.options ?? {})) {
          if (isRecord(opts)) overrides[task] = { ...(overrides[task] ?? {}), ...opts };
        }
        expand(def.flow, `${path}/`, overrides, typeof when === "string" ? when : undefined, scope, [...stack, def.flow]);
        frame.push({ old: Number(n), name: def.flow, frozen: null });
        continue;
      }

      const step = taskStep(def, frame, where, inherited, runtimeFor(scope, def.task!, path), when);
      steps[String(++count)] = step;
      frame.push({ old: Number(n), name: def.task!, frozen: count });
    }
    return frame;
  };

  const rootFrame = expand(input.flowName, "", {}, undefined, root.options_scope, [input.flowName]);

  const definition: Record<string, unknown> = {
    description: `Frozen plan of '${input.flowName}'.${root.description ? ` ${root.description}` : ""}`,
    steps,
  };
  for (const phase of HOOK_PHASES) {
    const hooks = (root[phase] ?? []) as StepDef[];
    if (!hooks.length) continue;
    definition[phase] = hooks.map((def, i) => {
      const where = `${phase}/${i + 1}`;
      if (def.flow) refused.push(`${where} runs the flow '${def.flow}' as a hook, which a flat plan cannot expand.`);
      const when = def.when === undefined || typeof def.when === "boolean"
        ? def.when
        : renumber(resolveHost(def.when) as string, rootFrame, where, true);
      return def.flow ? { ...def } : taskStep(def, rootFrame, where, {}, runtimeFor(root.options_scope, def.task ?? "", where), when);
    });
  }
  if (root.checks?.length) definition.checks = root.checks;
  if (root.rollback_on_failure !== undefined) definition.rollback_on_failure = root.rollback_on_failure;

  if (refused.length) return { from: input.flowName, refused };
  return { from: input.flowName, definition };
}
