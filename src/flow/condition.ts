/**
 * The `when:` expression language and the host reference namespaces.
 *
 * A small interpreter, never `eval`: literals, names under a known namespace,
 * `${ns.path}` references, comparisons, `!`/`not`, `&&`/`and`, `||`/`or` and
 * parentheses. A string that is not an expression (plain `${}` truthiness,
 * prose) keeps the runner's original meaning: references resolved, then the
 * result tested for truthiness.
 */
import { resolveReferences } from "@db-lyon/flowkit";
import type { ConditionContext, FlowRunnerConfig, FlowStepResult } from "@db-lyon/flowkit";
import type { ToolContext } from "../core/types.js";

type ConditionEvaluator = NonNullable<FlowRunnerConfig["conditionEvaluator"]>;

/**
 * `${project.*}`, `${editor.*}` and `${session.*}`, read when referenced so a
 * long run sees the editor as it is at each step.
 */
export function hostNamespaces(ctx: ToolContext): Record<string, unknown> {
  const project = ctx.project;
  return {
    project: {
      get name() { return project.projectName ?? undefined; },
      get path() { return project.projectPath ?? undefined; },
      get dir() { return project.projectDir ?? undefined; },
      get contentDir() { return project.contentDir ?? undefined; },
      get engine() { return project.engineAssociation ?? undefined; },
    },
    editor: {
      get connected() { return ctx.bridge.isConnected === true; },
      get name() { return ctx.session?.name; },
    },
    session: {
      get name() { return ctx.session?.name; },
      get count() { return ctx.sessions?.size ?? 1; },
    },
  };
}

type Token =
  | { kind: "op"; value: string }
  | { kind: "lit"; value: unknown }
  | { kind: "ref"; ns: string; path: string }
  | { kind: "name"; ns: string; path: string };

class NotAnExpression extends Error {}

const OPERATORS = ["==", "!=", "<=", ">=", "&&", "||", "<", ">", "!", "(", ")"];
const WORD_OPERATORS: Record<string, string> = { and: "&&", or: "||", not: "!" };
const KEYWORDS: Record<string, unknown> = { true: true, false: false, null: null };

/** Characters that only an expression would contain. */
const LOOKS_LIKE_EXPRESSION = /==|!=|<|>|&&|\|\||^\s*!|(^|\s)(and|or|not)\s|=/;

function tokenize(src: string, namespaces: ReadonlySet<string>): Token[] {
  const tokens: Token[] = [];
  let i = 0;
  while (i < src.length) {
    const c = src[i];
    if (/\s/.test(c)) { i++; continue; }
    if (src.startsWith("${", i)) {
      const end = src.indexOf("}", i);
      const m = end < 0 ? null : /^(\w+)\.(.+)$/.exec(src.slice(i + 2, end));
      if (!m) throw new NotAnExpression();
      tokens.push({ kind: "ref", ns: m[1], path: m[2] });
      i = end + 1;
      continue;
    }
    const op = OPERATORS.find((o) => src.startsWith(o, i));
    if (op) { tokens.push({ kind: "op", value: op }); i += op.length; continue; }
    if (c === "'" || c === "\"") {
      const end = src.indexOf(c, i + 1);
      if (end < 0) throw new NotAnExpression();
      tokens.push({ kind: "lit", value: src.slice(i + 1, end) });
      i = end + 1;
      continue;
    }
    const num = /^-?\d+(\.\d+)?/.exec(src.slice(i));
    if (num) { tokens.push({ kind: "lit", value: Number(num[0]) }); i += num[0].length; continue; }
    const word = /^[A-Za-z_][\w]*(\.[\w-]+)*/.exec(src.slice(i));
    if (!word) throw new NotAnExpression();
    const text = word[0];
    i += text.length;
    if (text in WORD_OPERATORS) { tokens.push({ kind: "op", value: WORD_OPERATORS[text] }); continue; }
    if (text in KEYWORDS) { tokens.push({ kind: "lit", value: KEYWORDS[text] }); continue; }
    const dot = text.indexOf(".");
    const ns = dot < 0 ? text : text.slice(0, dot);
    if (!namespaces.has(ns)) throw new NotAnExpression();
    tokens.push({ kind: "name", ns, path: dot < 0 ? "" : text.slice(dot + 1) });
  }
  if (tokens.length === 0) throw new NotAnExpression();
  return tokens;
}

/** The runner's truthiness, so `"false"` and `"0"` read as false. */
export function truthy(value: unknown): boolean {
  if (typeof value === "boolean") return value;
  if (value == null) return false;
  if (typeof value === "number") return value !== 0;
  const s = String(value).trim().toLowerCase();
  return !(s === "" || s === "false" || s === "0" || s === "null" || s === "undefined");
}

function asNumber(v: unknown): number | undefined {
  if (typeof v === "number") return v;
  if (typeof v === "string" && v.trim() !== "" && Number.isFinite(Number(v))) return Number(v);
  return undefined;
}

function equal(a: unknown, b: unknown): boolean {
  if (a == null || b == null) return a == null && b == null;
  if (typeof a === typeof b) return typeof a === "object" ? JSON.stringify(a) === JSON.stringify(b) : a === b;
  const na = asNumber(a);
  const nb = asNumber(b);
  if (na !== undefined && nb !== undefined) return na === nb;
  return String(a) === String(b);
}

function compare(op: string, a: unknown, b: unknown): boolean {
  if (op === "==") return equal(a, b);
  if (op === "!=") return !equal(a, b);
  const na = asNumber(a);
  const nb = asNumber(b);
  const [x, y] = na !== undefined && nb !== undefined ? [na, nb] : [String(a ?? ""), String(b ?? "")];
  if (op === "<") return x < y;
  if (op === "<=") return x <= y;
  if (op === ">") return x > y;
  return x >= y;
}

function getPath(obj: unknown, path: string): unknown {
  let cur = obj;
  for (const seg of path === "" ? [] : path.split(".")) {
    if (cur == null || typeof cur !== "object") return undefined;
    cur = (cur as Record<string, unknown>)[seg];
  }
  return cur;
}

interface Scope {
  steps: FlowStepResult[];
  params?: Record<string, unknown>;
  error?: Record<string, unknown>;
  namespaces: Record<string, unknown>;
}

function lookup(ns: string, path: string, scope: Scope): unknown {
  if (ns === "steps") {
    // Step lookup by number or name is the runner's own; it throws on a step
    // that has not run, as a reference in an option does.
    return resolveReferences(`\${steps.${path}}`, { steps: scope.steps });
  }
  if (ns === "error") {
    if (!scope.error) throw new Error(`error.${path} referenced outside on_failure/finally`);
    return getPath(scope.error, path);
  }
  if (ns === "params") return getPath(scope.params ?? {}, path);
  return getPath(scope.namespaces[ns], path);
}

/** Recursive descent over the token list. */
function evaluate(tokens: Token[], scope: Scope): unknown {
  let pos = 0;
  const peek = () => tokens[pos];
  const isOp = (v: string) => peek()?.kind === "op" && (peek() as { value: string }).value === v;

  const primary = (): unknown => {
    const t = tokens[pos++];
    if (!t) throw new NotAnExpression();
    if (t.kind === "lit") return t.value;
    if (t.kind === "ref" || t.kind === "name") return lookup(t.ns, t.path, scope);
    if (t.value === "(") {
      const v = or();
      if (!isOp(")")) throw new NotAnExpression();
      pos++;
      return v;
    }
    throw new NotAnExpression();
  };
  const cmp = (): unknown => {
    const left = primary();
    const t = peek();
    if (t?.kind === "op" && ["==", "!=", "<", "<=", ">", ">="].includes(t.value)) {
      pos++;
      return compare(t.value, left, primary());
    }
    return left;
  };
  const not = (): unknown => {
    if (isOp("!")) { pos++; return !truthy(not()); }
    return cmp();
  };
  const and = (): unknown => {
    let v = not();
    while (isOp("&&")) { pos++; const r = not(); v = truthy(v) && truthy(r); }
    return v;
  };
  const or = (): unknown => {
    let v = and();
    while (isOp("||")) { pos++; const r = and(); v = truthy(v) || truthy(r); }
    return v;
  };

  const value = or();
  if (pos !== tokens.length) throw new NotAnExpression();
  return value;
}

/**
 * Namespaces that depend on the step being gated (`step`, `gate`), read only
 * when an expression names one of them.
 */
export interface StepScope {
  names: readonly string[];
  read(ctx: ConditionContext): Promise<Record<string, unknown>>;
}

/**
 * The flow runner's `conditionEvaluator`. `namespaces` are the host namespaces
 * the option references use, so `when:` and `options:` read the same names.
 */
export function makeConditionEvaluator(namespaces: Record<string, unknown>, stepScope?: StepScope): ConditionEvaluator {
  const base = evaluatorOver(namespaces, stepScope?.names ?? []);
  if (!stepScope) return (expression, ctx) => base(expression, ctx, {});
  const mentions = new RegExp(`(^|[^\\w.])(${stepScope.names.join("|")})\\.`);
  return (expression, ctx) => mentions.test(expression)
    ? stepScope.read(ctx).then((extra) => base(expression, ctx, extra))
    : base(expression, ctx, {});
}

/** One expression over plain namespaces, outside any flow run. */
export function evaluateCondition(expression: string, namespaces: Record<string, unknown>): boolean {
  return evaluatorOver(namespaces, [])(expression, { steps: [] } as unknown as ConditionContext, {});
}

function evaluatorOver(
  namespaces: Record<string, unknown>,
  stepNames: readonly string[],
): (expression: string, ctx: ConditionContext, extra: Record<string, unknown>) => boolean {
  const known = new Set(["steps", "params", "error", ...Object.keys(namespaces), ...stepNames]);
  return (expression, ctx, extra) => {
    const scope: Scope = {
      steps: ctx.steps,
      params: ctx.params,
      error: ctx.error as Record<string, unknown> | undefined,
      namespaces: { ...namespaces, ...extra },
    };
    let tokens: Token[];
    try {
      tokens = tokenize(expression, known);
      return truthy(evaluate(tokens, scope));
    } catch (e) {
      if (!(e instanceof NotAnExpression)) throw e;
      if (LOOKS_LIKE_EXPRESSION.test(expression.replace(/\$\{[^}]*\}/g, "x"))) {
        throw new Error(
          `when: '${expression}' is not a valid expression. It takes literals, names under `
          + `${[...known].join(", ")}, \${ns.path} references, == != < <= > >=, !/not, &&/and, ||/or and parentheses.`,
        );
      }
    }
    // Not an expression: the runner's original meaning.
    return truthy(resolveReferences(expression, { steps: ctx.steps, namespaces: scope.namespaces, error: ctx.error }));
  };
}
