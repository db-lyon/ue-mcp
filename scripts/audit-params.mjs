#!/usr/bin/env node
// Compare params listed in docs/tool-reference.md vs the schema description in src/tools/*.ts.
// For each action: parse "Params: foo, bar?, baz (...)" out of both sides, intersect, and report drift.
import fs from "node:fs";
import path from "node:path";

const ROOT = path.resolve(import.meta.dirname, "..");
const DOC = fs.readFileSync(path.join(ROOT, "docs/tool-reference.md"), "utf8");

function extractActionDescriptions(file) {
  const src = fs.readFileSync(file, "utf8");
  const out = {};
  // Match: <action>: bp("DESCRIPTION", ...) - description can span lines
  const re = /^    ([a-z_][a-z0-9_]*)\s*:\s*bp\(\s*"((?:[^"\\]|\\.)*)"/gm;
  for (const m of src.matchAll(re)) {
    out[m[1]] = m[2].replace(/\\"/g, '"').replace(/\\n/g, ' ');
  }
  // Also: <action>: { description: "..." }
  const re2 = /^    ([a-z_][a-z0-9_]*)\s*:\s*\{\s*description:\s*"((?:[^"\\]|\\.)*)"/gm;
  for (const m of src.matchAll(re2)) {
    out[m[1]] = m[2].replace(/\\"/g, '"').replace(/\\n/g, ' ');
  }
  return out;
}

function extractDocRow(section, action) {
  const start = DOC.indexOf(`\n## ${section}\n`);
  if (start === -1) return null;
  const after = DOC.slice(start + 1);
  const endIdx = after.indexOf("\n## ");
  const chunk = endIdx === -1 ? after : after.slice(0, endIdx);
  const re = new RegExp(`^\\|\\s+\`${action}\`\\s+\\|\\s+(.+?)\\s+\\|`, "m");
  const m = chunk.match(re);
  return m ? m[1] : null;
}

function paramTokens(text) {
  if (!text) return [];
  const idx = text.search(/Params?:\s*/i);
  if (idx === -1) return [];
  let rest = text.slice(idx).replace(/^Params?:\s*/i, "");
  // Strip parenthesized clarifications - they often hold defaults/values, not param names
  let depth = 0;
  let out = "";
  for (const ch of rest) {
    if (ch === "(") depth++;
    else if (ch === ")") depth = Math.max(0, depth - 1);
    else if (depth === 0) out += ch;
  }
  // Stop at first sentence-ending dot or "Returns" or "#NNN"
  out = out.split(/\.\s|Returns|#\d+/)[0];
  // Split on commas and "OR" (for assetPath OR assetPaths)
  const parts = out.split(/[,]/).map(s => s.trim()).filter(Boolean);
  const names = [];
  for (const p of parts) {
    // strip trailing ?, trailing "= default", surrounding backticks, leading "and"
    let n = p.replace(/^and\s+/i, "").replace(/^or\s+/i, "").trim();
    n = n.replace(/[`*]/g, "");
    n = n.split(/\s+(OR|or|\|\|)\s+/)[0];
    n = n.split(/\s+/)[0];
    n = n.replace(/\?$/, "").replace(/:.*/, "");
    if (/^[a-zA-Z_][a-zA-Z0-9_]*$/.test(n)) names.push(n);
  }
  return [...new Set(names)];
}

const cats = ["project","asset","blueprint","level","material","animation","niagara","landscape","pcg","foliage","audio","widget","gameplay","statetree","gas","networking","editor","reflection","demo","feedback"];

let issues = 0;
for (const c of cats) {
  const descs = extractActionDescriptions(path.join(ROOT, "src/tools", c + ".ts"));
  for (const [action, srcDesc] of Object.entries(descs)) {
    const docDesc = extractDocRow(c, action);
    if (docDesc === null) continue; // missing rows already audited separately
    const srcParams = paramTokens(srcDesc);
    const docParams = paramTokens(docDesc);
    const missing = srcParams.filter(p => !docParams.includes(p));
    const extra = docParams.filter(p => !srcParams.includes(p));
    if (missing.length || extra.length) {
      console.log(`${c}.${action}`);
      if (missing.length) console.log(`  doc missing: ${missing.join(", ")}`);
      if (extra.length)   console.log(`  doc extra:   ${extra.join(", ")}`);
      console.log(`  src: ${srcDesc.slice(0, 200)}`);
      console.log(`  doc: ${docDesc.slice(0, 200)}`);
      console.log();
      issues++;
    }
  }
}
console.log(`Total potential param drifts: ${issues}`);
