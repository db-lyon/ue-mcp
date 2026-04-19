/**
 * Path selector evaluation over a composed view.
 *
 * Grammar (strict subset of kantext HQL - enough to be useful):
 *
 *   selector := '/' segment ( '/' segment )* ( '@' predicate )?
 *   segment  := '*' | '**' | <identifier>
 *   predicate:= field op value
 *   op       := '=' | '!=' | '<' | '<=' | '>' | '>='
 *   value    := number | bare-string | "quoted string"
 *
 * '*'  matches exactly one child segment (any name).
 * '**' matches zero or more child segments (recursive descent).
 *
 * Predicates filter on scalar fields. Signal values compare on their
 * numeric `.value` (so `@classification>=0.5` works), and the `.marker`
 * can be compared as a string (`@classification=destructive`).
 *
 * Returns a flat list of matching points with their resolved path.
 */

import type { KantPoint, KantScalar, KantSignal } from "./types.js";

export interface MatchResult {
  readonly path: string;
  readonly point: KantPoint;
}

interface Predicate {
  readonly field: string;
  readonly op: "=" | "!=" | "<" | "<=" | ">" | ">=";
  readonly value: string | number;
}

interface ParsedSelector {
  readonly segments: readonly string[];
  readonly predicate?: Predicate;
}

export function parseSelector(selector: string): ParsedSelector {
  if (!selector.startsWith("/")) {
    throw new Error(`selector must start with '/': got '${selector}'`);
  }
  const atIdx = selector.indexOf("@");
  const pathPart = atIdx < 0 ? selector : selector.slice(0, atIdx);
  const predPart = atIdx < 0 ? undefined : selector.slice(atIdx + 1);

  const segments = pathPart.split("/").slice(1).filter((s) => s.length > 0);
  for (const seg of segments) {
    if (seg !== "*" && seg !== "**" && !/^[A-Za-z_][A-Za-z0-9_.\-]*$/.test(seg)) {
      throw new Error(`invalid selector segment '${seg}' in '${selector}'`);
    }
  }

  if (predPart === undefined) return { segments };

  const m = predPart.match(/^([A-Za-z_][A-Za-z0-9_]*)(<=|>=|!=|=|<|>)(.+)$/);
  if (!m) throw new Error(`invalid predicate '${predPart}' in '${selector}'`);
  const [, field, op, rawValue] = m;

  let value: string | number;
  const trimmed = rawValue.trim();
  if (trimmed.startsWith('"') && trimmed.endsWith('"')) {
    value = JSON.parse(trimmed);
  } else {
    const num = Number(trimmed);
    value = Number.isNaN(num) ? trimmed : num;
  }

  return { segments, predicate: { field, op: op as Predicate["op"], value } };
}

function isSignal(v: KantScalar | KantSignal): v is KantSignal {
  return typeof v === "object" && v !== null && (v as KantSignal).kind === "signal";
}

function scalarForCompare(v: KantScalar | KantSignal, op: Predicate["op"]): string | number {
  if (isSignal(v)) {
    if (op === "=" || op === "!=") {
      return v.marker ?? v.value;
    }
    return v.value;
  }
  return v;
}

function matchesPredicate(point: KantPoint, pred: Predicate): boolean {
  const fv = point.fields?.[pred.field];
  if (fv === undefined) return false;
  const compared = scalarForCompare(fv, pred.op);
  if (typeof compared === "number" && typeof pred.value === "number") {
    switch (pred.op) {
      case "=": return compared === pred.value;
      case "!=": return compared !== pred.value;
      case "<": return compared < pred.value;
      case "<=": return compared <= pred.value;
      case ">": return compared > pred.value;
      case ">=": return compared >= pred.value;
    }
  }
  if (pred.op === "=") return String(compared) === String(pred.value);
  if (pred.op === "!=") return String(compared) !== String(pred.value);
  return false;
}

export function select(root: KantPoint, selector: string, rootPath: string = "/UE"): MatchResult[] {
  const parsed = parseSelector(selector);
  // The composed root IS the UE point. If the selector starts with "/UE/..."
  // we consume that segment before walking children.
  const rootSegment = rootPath.split("/").filter(Boolean).pop();
  let startIdx = 0;
  if (rootSegment && parsed.segments[0] === rootSegment) {
    startIdx = 1;
  }
  const out: MatchResult[] = [];
  walk(root, rootPath, parsed.segments, startIdx, parsed.predicate, out);
  return out;
}

function walk(
  point: KantPoint,
  currentPath: string,
  segments: readonly string[],
  idx: number,
  pred: Predicate | undefined,
  out: MatchResult[],
): void {
  if (idx >= segments.length) {
    if (!pred || matchesPredicate(point, pred)) {
      out.push({ path: currentPath, point });
    }
    return;
  }
  const seg = segments[idx];
  const children = point.children ?? {};

  if (seg === "**") {
    // Match zero segments (current point satisfies, continue past '**').
    walk(point, currentPath, segments, idx + 1, pred, out);
    // And recursively descend consuming one child per step while '**' stays at idx.
    for (const [name, child] of Object.entries(children)) {
      walk(child, `${currentPath}/${name}`, segments, idx, pred, out);
    }
    return;
  }

  if (seg === "*") {
    for (const [name, child] of Object.entries(children)) {
      walk(child, `${currentPath}/${name}`, segments, idx + 1, pred, out);
    }
    return;
  }

  const child = children[seg];
  if (child) walk(child, `${currentPath}/${seg}`, segments, idx + 1, pred, out);
}
