#!/usr/bin/env node
import fs from "node:fs";
import path from "node:path";

const ROOT = path.resolve(import.meta.dirname, "..");
const DOC = fs.readFileSync(path.join(ROOT, "docs/tool-reference.md"), "utf8");

function extractActions(file) {
  const src = fs.readFileSync(file, "utf8");
  const m = src.match(/categoryTool\(\s*["'][^"']+["'],\s*["'][^"']*["'],\s*\{([\s\S]*?)\n\s{2}\},\s*(undefined|["'])/);
  if (!m) return [];
  const block = m[1];
  const keys = [];
  for (const line of block.split("\n")) {
    const km = line.match(/^    ([a-z_][a-z0-9_]*)\s*:/);
    if (km) keys.push(km[1]);
  }
  return keys;
}

function docActions(section) {
  const start = DOC.indexOf(`\n## ${section}\n`);
  if (start === -1) return [];
  const after = DOC.slice(start + 1);
  const end = after.indexOf("\n## ");
  const chunk = end === -1 ? after : after.slice(0, end);
  const rows = [];
  for (const line of chunk.split("\n")) {
    const rm = line.match(/^\|\s+`([a-z_][a-z0-9_]*)`/);
    if (rm) rows.push(rm[1]);
  }
  return rows;
}

const cats = ["project","asset","blueprint","level","material","animation","niagara","landscape","pcg","foliage","audio","widget","gameplay","statetree","gas","networking","editor","reflection","demo","feedback"];

let totalMissing = 0;
let totalExtra = 0;
for (const c of cats) {
  const src = extractActions(path.join(ROOT, "src/tools", c + ".ts"));
  const doc = docActions(c);
  const missing = src.filter(a => !doc.includes(a));
  const extra = doc.filter(a => !src.includes(a));
  if (missing.length || extra.length) {
    console.log(`--- ${c} (src=${src.length}, doc=${doc.length}) ---`);
    if (missing.length) { console.log(`  MISSING from docs: ${missing.join(", ")}`); totalMissing += missing.length; }
    if (extra.length)   { console.log(`  EXTRA in docs:     ${extra.join(", ")}`);   totalExtra += extra.length; }
  } else {
    console.log(`${c}: OK (${src.length})`);
  }
}
console.log(`\nTotal: ${totalMissing} missing, ${totalExtra} extra`);
