// Direct parameter read audit (#1057).
//
// No shebang: vitest re-imports this file. Run as
// `node scripts/audit-direct-param-reads.mjs` (the `audit:param-reads` shape),
// with `--verbose` to list every read.
//
// A handler of a reporting category answers with `paramsNotRead` for keys that
// arrived and were never read. Only the helpers in HandlerUtils.h note a read,
// so a direct `Params->TryGet*Field` call is invisible and its key would be
// reported as unread. This lists those calls per category, so a category can be
// converted to the helpers before it joins the allowlist in HandlerRegistry.cpp.
//
// Report-only. The unit test holds the reporting categories at zero.
// It sees the handler's own `Params` only: a helper that takes the object under
// another name is not followed.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(__dirname, '..');
const moduleRoot = path.join(repoRoot, 'plugin', 'ue_mcp_bridge', 'Source', 'UE_MCP_Bridge');
export const HANDLERS_DIR = path.join(moduleRoot, 'Private', 'Handlers');
export const REGISTRY_CPP = path.join(moduleRoot, 'Private', 'HandlerRegistry.cpp');

const DIRECT_READ = /(?<![\w.>])Params->(TryGet\w*Field|Get\w*Field|HasField|HasTypedField|TryGetField|Values)\b/g;

/** Blank out comments, keeping line numbers. String contents are kept for the report. */
export function stripComments(source) {
  let out = '';
  let i = 0;
  let inString = false;
  while (i < source.length) {
    const ch = source[i];
    const next = source[i + 1];
    if (inString) {
      out += ch;
      if (ch === '\\') { out += next ?? ''; i += 2; continue; }
      if (ch === '"' || ch === '\n') inString = false;
      i++;
      continue;
    }
    if (ch === '"') { inString = true; out += ch; i++; continue; }
    if (ch === '/' && next === '/') {
      while (i < source.length && source[i] !== '\n') i++;
      continue;
    }
    if (ch === '/' && next === '*') {
      i += 2;
      while (i < source.length && !(source[i] === '*' && source[i + 1] === '/')) {
        if (source[i] === '\n') out += '\n';
        i++;
      }
      i += 2;
      continue;
    }
    out += ch;
    i++;
  }
  return out;
}

/** `AnimationHandlers_Pose.cpp` -> `animation`. Null for a file that is not a category's. */
export function categoryOfFile(fileName) {
  const m = fileName.match(/^([A-Za-z]+?)Handlers(?:_\w+)?\.cpp$/);
  return m ? m[1].toLowerCase() : null;
}

/** True when `index` falls inside a string literal on `line`. */
function insideString(line, index) {
  let open = false;
  for (let i = 0; i < index; i++) {
    if (line[i] === '\\') { i++; continue; }
    if (line[i] === '"') open = !open;
  }
  return open;
}

/** Every direct read of `Params` in one source text. */
export function findDirectReads(source) {
  const reads = [];
  const lines = stripComments(source).split('\n');
  lines.forEach((line, index) => {
    for (const m of line.matchAll(DIRECT_READ)) {
      if (insideString(line, m.index)) continue;
      const key = line.slice(m.index + m[0].length).match(/^\s*\(\s*(?:TEXT\(\s*"([^"]*)"\s*\)|(\w+))/);
      reads.push({ line: index + 1, method: m[1], key: key ? (key[1] ?? key[2]) : null });
    }
  });
  return reads;
}

/** The categories HandlerRegistry.cpp reports unread parameters for. */
export function reportingCategories(registrySource = fs.readFileSync(REGISTRY_CPP, 'utf8')) {
  const block = registrySource.match(/Reporting\[\]\s*=\s*\{([^}]*)\}/);
  if (!block) return [];
  return [...block[1].matchAll(/TEXT\(\s*"([^"]+)"\s*\)/g)].map((m) => m[1]);
}

/** Direct reads grouped by category, largest first. */
export function auditDirectParamReads(dir = HANDLERS_DIR) {
  const byCategory = new Map();
  for (const fileName of fs.readdirSync(dir).sort()) {
    const category = categoryOfFile(fileName);
    if (!category) continue;
    const reads = findDirectReads(fs.readFileSync(path.join(dir, fileName), 'utf8'));
    const entry = byCategory.get(category) ?? { category, count: 0, files: [] };
    if (reads.length > 0) entry.files.push({ file: fileName, reads });
    entry.count += reads.length;
    byCategory.set(category, entry);
  }
  return [...byCategory.values()].sort((a, b) => b.count - a.count || a.category.localeCompare(b.category));
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const verbose = process.argv.includes('--verbose');
  const reporting = new Set(reportingCategories());
  const audit = auditDirectParamReads();
  const total = audit.reduce((sum, c) => sum + c.count, 0);
  console.log(`audit:param-reads - ${total} direct Params read(s) the #1057 tracking cannot see`);
  for (const c of audit) {
    const tag = reporting.has(c.category) ? ' (reporting)' : '';
    console.log(`  ${String(c.count).padStart(5)}  ${c.category}${tag}`);
    if (!verbose) continue;
    for (const f of c.files) {
      for (const r of f.reads) {
        console.log(`           ${f.file}:${r.line}  Params->${r.method}${r.key ? `("${r.key}")` : ''}`);
      }
    }
  }
  const unconverted = audit.filter((c) => reporting.has(c.category) && c.count > 0);
  for (const c of unconverted) {
    console.log(`  warning: ${c.category} reports unread parameters but still reads ${c.count} directly`);
  }
  process.exit(0);
}
