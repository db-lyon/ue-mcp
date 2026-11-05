/**
 * Source scanning shared by the coverage inventories: the behavioural one over
 * bridge actions (scripts/behavioural-coverage.ts) and the integration one over
 * every other user-facing surface (scripts/integration-coverage.ts). Both
 * credit a test body only when it asserts more than that a call answered.
 */
import * as fs from "node:fs";
import * as path from "node:path";

export function walk(dir: string, accept: (name: string) => boolean): string[] {
  const out: string[] = [];
  if (!fs.existsSync(dir)) return out;
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) out.push(...walk(full, accept));
    else if (accept(entry.name)) out.push(full);
  }
  return out.sort();
}

/** Index of the bracket that closes the one at `open`, in masked text. */
export function matching(masked: string, open: number): number {
  const pairs: Record<string, string> = { "(": ")", "{": "}", "[": "]" };
  const want = pairs[masked[open]];
  if (!want) throw new Error(`no bracket at ${open}`);
  let depth = 0;
  for (let i = open; i < masked.length; i++) {
    const ch = masked[i];
    if (ch === "(" || ch === "{" || ch === "[") depth++;
    else if (ch === ")" || ch === "}" || ch === "]") {
      depth--;
      if (depth === 0) return i;
    }
  }
  return -1;
}

export interface Span {
  start: number;
  end: number;
}

/** Named functions declared in a file, with their bodies, for one level of helper expansion. */
export interface FileHelpers {
  spans: Map<string, Span>;
}

const KEYWORDS = new Set([
  "if", "for", "while", "switch", "catch", "return", "sizeof", "decltype", "static_assert",
  "TEXT", "it", "test", "describe", "expect", "beforeAll", "afterAll", "beforeEach", "afterEach",
]);

export function helperSpans(masked: string, kind: "ts" | "cpp"): FileHelpers {
  const spans = new Map<string, Span>();
  const add = (name: string, open: number) => {
    if (KEYWORDS.has(name) || spans.has(name)) return;
    const close = matching(masked, open);
    if (close > open) spans.set(name, { start: open, end: close });
  };
  if (kind === "ts") {
    for (const m of masked.matchAll(/\bfunction\s+(\w+)\s*(?:<[^>]*>)?\s*\(/g)) {
      const parenClose = matching(masked, m.index! + m[0].length - 1);
      const brace = masked.indexOf("{", parenClose);
      if (brace !== -1) add(m[1], brace);
    }
    for (const m of masked.matchAll(/\b(?:const|let)\s+(\w+)\s*(?::[^=]+)?=\s*(?:async\s*)?(?:\([^)]*\)|\w+)\s*(?::[^=]+)?=>\s*/g)) {
      const at = m.index! + m[0].length;
      if (masked[at] === "{") {
        add(m[1], at);
        continue;
      }
      // An expression body runs to the `;` that ends the declaration.
      let depth = 0;
      let end = at;
      for (; end < masked.length; end++) {
        const ch = masked[end];
        if (ch === "(" || ch === "{" || ch === "[") depth++;
        else if (ch === ")" || ch === "}" || ch === "]") {
          if (--depth < 0) break;
        } else if (ch === ";" && depth === 0) break;
      }
      if (!KEYWORDS.has(m[1]) && !spans.has(m[1])) spans.set(m[1], { start: at, end });
    }
  } else {
    for (const m of masked.matchAll(/\b(\w+)\s*\(([^;{}()]|\([^()]*\))*\)\s*(?:const\s*)?(?:->\s*[\w:<>]+\s*)?\{/g)) {
      add(m[1], m.index! + m[0].length - 1);
    }
  }
  return { spans };
}

/** Names of helpers called inside a span, transitively, never the span's own name. */
export function calledHelpers(masked: string, span: Span, helpers: FileHelpers, self?: string): Span[] {
  const out: Span[] = [];
  const seen = new Set<string>(self ? [self] : []);
  const queue: Span[] = [span];
  while (queue.length > 0) {
    const s = queue.shift()!;
    const text = masked.slice(s.start, s.end);
    for (const m of text.matchAll(/\b(\w+)\s*\(/g)) {
      const name = m[1];
      if (seen.has(name)) continue;
      const body = helpers.spans.get(name);
      if (!body) continue;
      seen.add(name);
      out.push(body);
      queue.push(body);
    }
  }
  return out;
}

const TRIVIAL_EXPECT = /^\s*[\w.?!\[\]"']+\.(ok|isConnected)\s*(,[\s\S]*)?$/;

/** Whether a TS span asserts something beyond "the call answered". */
export function tsAsserts(source: string, masked: string, spans: Span[]): boolean {
  for (const span of spans) {
    const text = masked.slice(span.start, span.end);
    for (const m of text.matchAll(/\bexpect(?:\.soft)?\s*\(/g)) {
      const open = span.start + m.index! + m[0].length - 1;
      const close = matching(masked, open);
      if (close === -1) continue;
      const arg = source.slice(open + 1, close);
      if (!TRIVIAL_EXPECT.test(arg)) return true;
    }
    if (/\bexpect\.fail\s*\(|\bassert\w*\s*\(/.test(text)) return true;
  }
  return false;
}

/**
 * Every titled block a callee opens (`it`, `test`, `describe`): its title and its argument span. A block marked
 * `.skip` or `.todo` never runs, so it is left out and credits nothing.
 */
export function tsTitledBlocks(source: string, masked: string, callee: RegExp): Array<{ title: string; span: Span }> {
  const out: Array<{ title: string; span: Span }> = [];
  for (const m of masked.matchAll(callee)) {
    let i = m.index! + m[0].length;
    let neverRuns = false;
    // Modifiers: .only, .skip, .todo, .skipIf(cond), .runIf(cond), .concurrent.
    for (;;) {
      while (/\s/.test(masked[i] ?? "")) i++;
      if (masked[i] === ".") {
        const word = /^\.\s*(\w+)/.exec(masked.slice(i));
        if (!word) break;
        if (word[1] === "skip" || word[1] === "todo") neverRuns = true;
        i += word[0].length;
        continue;
      }
      if (masked[i] === "(") {
        let j = i + 1;
        while (/\s/.test(masked[j] ?? "")) j++;
        if (/["'`]/.test(source[j] ?? "")) break;
        const close = matching(masked, i);
        if (close === -1) break;
        i = close + 1;
        continue;
      }
      break;
    }
    if (neverRuns || masked[i] !== "(") continue;
    let j = i + 1;
    while (/\s/.test(masked[j] ?? "")) j++;
    const quote = source[j];
    if (!quote || !/["'`]/.test(quote)) continue;
    const titleEnd = source.indexOf(quote, j + 1);
    if (titleEnd === -1) continue;
    const close = matching(masked, i);
    if (close === -1) continue;
    out.push({ title: source.slice(j + 1, titleEnd), span: { start: i, end: close } });
  }
  return out;
}
