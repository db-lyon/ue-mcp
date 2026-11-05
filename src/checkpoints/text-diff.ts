/**
 * Unified line diffs for the text files a checkpoint records (config, source).
 * Myers' O(ND) algorithm, capped so a rewritten file reports that it changed
 * instead of holding the server while it works out how.
 */

const MAX_EDITS = 1000;
const MAX_LINES = 20_000;

type Op = { op: " " | "-" | "+"; line: string };

function splitLines(text: string): string[] {
  if (text === "") return [];
  const lines = text.split(/\r?\n/);
  if (lines[lines.length - 1] === "") lines.pop();
  return lines;
}

/** The edit script from a to b, or null when it needs more than MAX_EDITS edits. */
function diffLines(a: string[], b: string[]): Op[] | null {
  const n = a.length;
  const m = b.length;
  const max = n + m;
  const off = max + 1;
  const v = new Int32Array(2 * max + 3);
  // trace[d] holds v for k in [-d, d] as it stood before step d.
  const trace: Int32Array[] = [];
  for (let d = 0; d <= Math.min(max, MAX_EDITS); d++) {
    trace.push(v.slice(off - d, off + d + 1));
    for (let k = -d; k <= d; k += 2) {
      let x = k === -d || (k !== d && v[off + k - 1] < v[off + k + 1]) ? v[off + k + 1] : v[off + k - 1] + 1;
      let y = x - k;
      while (x < n && y < m && a[x] === b[y]) {
        x++;
        y++;
      }
      v[off + k] = x;
      if (x >= n && y >= m) return backtrack(trace, a, b);
    }
  }
  return null;
}

function backtrack(trace: Int32Array[], a: string[], b: string[]): Op[] {
  const out: Op[] = [];
  let x = a.length;
  let y = b.length;
  for (let d = trace.length - 1; d > 0; d--) {
    const at = (k: number) => trace[d][k + d];
    const k = x - y;
    const prevK = k === -d || (k !== d && at(k - 1) < at(k + 1)) ? k + 1 : k - 1;
    const prevX = at(prevK);
    const prevY = prevX - prevK;
    while (x > prevX && y > prevY) {
      out.push({ op: " ", line: a[x - 1] });
      x--;
      y--;
    }
    if (x === prevX) out.push({ op: "+", line: b[y - 1] });
    else out.push({ op: "-", line: a[x - 1] });
    x = prevX;
    y = prevY;
  }
  while (x > 0 && y > 0) {
    out.push({ op: " ", line: a[x - 1] });
    x--;
    y--;
  }
  return out.reverse();
}

/**
 * A unified diff of two texts, headed by the path. Null when they are equal,
 * and a one-line note when either side is too large to diff.
 */
export function unifiedDiff(before: string, after: string, filePath: string, context = 3): string | null {
  if (before === after) return null;
  const a = splitLines(before);
  const b = splitLines(after);
  const head = `--- a/${filePath}\n+++ b/${filePath}\n`;
  if (a.length + b.length > MAX_LINES) return `${head}(diff omitted: ${a.length} lines before, ${b.length} after)\n`;
  const ops = diffLines(a, b);
  if (!ops) return `${head}(diff omitted: more than ${MAX_EDITS} lines changed)\n`;

  let out = head;
  let i = 0;
  // Line numbers before each op, 1-based.
  const aLine: number[] = [];
  const bLine: number[] = [];
  let la = 1;
  let lb = 1;
  for (const o of ops) {
    aLine.push(la);
    bLine.push(lb);
    if (o.op !== "+") la++;
    if (o.op !== "-") lb++;
  }
  while (i < ops.length) {
    if (ops[i].op === " ") {
      i++;
      continue;
    }
    const start = Math.max(0, i - context);
    let end = i;
    // Extend through changes separated by at most 2 * context equal lines.
    while (end < ops.length) {
      if (ops[end].op !== " ") {
        end++;
        continue;
      }
      let run = 0;
      while (end + run < ops.length && ops[end + run].op === " ") run++;
      if (end + run >= ops.length || run > 2 * context) {
        end = Math.min(ops.length, end + context);
        break;
      }
      end += run;
    }
    const hunk = ops.slice(start, end);
    const aCount = hunk.filter((o) => o.op !== "+").length;
    const bCount = hunk.filter((o) => o.op !== "-").length;
    const aStart = aCount === 0 ? aLine[start] - 1 : aLine[start];
    const bStart = bCount === 0 ? bLine[start] - 1 : bLine[start];
    out += `@@ -${aStart},${aCount} +${bStart},${bCount} @@\n`;
    for (const o of hunk) out += `${o.op}${o.line}\n`;
    i = end;
  }
  return out;
}
