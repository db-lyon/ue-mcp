//
// Do the parameters an action's description names match the ones its row in
// docs/tool-reference.md names?
//
// This is the audit standing behind CLAUDE.md's rule that param names must
// match exactly between the TS schema and the C++ handler: drift there is how
// a silent failure starts, and the description is where a caller reads the
// names from.
//
// It was blind to that whole class. It captured only the FIRST string literal
// of a concatenated `description:`, so every action whose description spans
// more than one literal lost its `Params:` clause - which sits at the END of
// the description, and is therefore exactly the part that a first-literal read
// never reaches. With no source params to compare, every parameter the docs
// listed read as one the docs invented, and fifteen actions were reported as
// drifting when none of them were. It exited 0 the whole time.
//
// The description is now read whole, through scripts/lib/tool-source.mjs, and
// the categories come from the tree rather than a list that had stopped at
// twenty.

import fs from "node:fs";
import path from "node:path";
import { readCategories } from "./lib/tool-source.mjs";

const ROOT = path.resolve(import.meta.dirname, "..");
const DOC = fs.readFileSync(path.join(ROOT, "docs/tool-reference.md"), "utf8");

// The names paged() appends to a paged action's Params clause. Mirrors
// PAGINATION_PARAM_NAMES in src/pagination.ts, which this plain-node script
// cannot import; the parity is asserted in tests/unit/audit-params.test.ts.
const PAGINATION_PARAMS = ["cursor", "limit"];

function docSection(section) {
  const start = DOC.indexOf(`\n## ${section}\n`);
  if (start === -1) return null;
  const after = DOC.slice(start + 1);
  const endIdx = after.indexOf("\n## ");
  return endIdx === -1 ? after : after.slice(0, endIdx);
}

function extractDocRow(chunk, action) {
  if (!chunk) return null;
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
  // Stop at first sentence-ending dot or "Returns" or "#NNN". The dot must
  // also terminate at end-of-string: "Params: assetPath." otherwise kept the
  // trailing period on the token, which then failed the identifier test below
  // and silently dropped the last parameter of every description that ends in
  // a full stop - reported as the doc having an extra param it shares.
  out = out.split(/\.\s|\.$|Returns|#\d+/)[0];
  // Split on commas and "OR" (for assetPath OR assetPaths)
  const parts = out.split(/[,]/).map((s) => s.trim()).filter(Boolean);
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

export function auditParams() {
  const { categories, blind } = readCategories();
  const drifts = [];
  let compared = 0;
  for (const category of categories) {
    const chunk = docSection(category.name);
    if (chunk === null) {
      blind.push({
        file: path.relative(ROOT, category.file),
        reason: `docs/tool-reference.md has no "## ${category.name}" section`,
      });
      continue;
    }
    for (const { name, description, paged } of category.actions) {
      const docDesc = extractDocRow(chunk, name);
      if (docDesc === null) continue; // missing rows are audit-docs's report
      compared++;
      const srcParams = paramTokens(description);
      // A paged action's runtime description carries cursor and limit, added by
      // paged() after the literal in the source ends. They belong to the source
      // side of the comparison even though no literal spells them.
      if (paged) for (const p of PAGINATION_PARAMS) if (!srcParams.includes(p)) srcParams.push(p);
      const docParams = paramTokens(docDesc);
      const missing = srcParams.filter((p) => !docParams.includes(p));
      const extra = docParams.filter((p) => !srcParams.includes(p));
      if (missing.length || extra.length) {
        drifts.push({ category: category.name, action: name, missing, extra, srcDesc: description, docDesc });
      }
    }
  }
  return { drifts, blind, compared };
}

if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(import.meta.filename)) {
  const { drifts, blind, compared } = auditParams();
  for (const d of drifts) {
    console.log(`${d.category}.${d.action}`);
    if (d.missing.length) console.log(`  doc missing: ${d.missing.join(", ")}`);
    if (d.extra.length) console.log(`  doc extra:   ${d.extra.join(", ")}`);
    console.log(`  src: ${d.srcDesc.slice(0, 200)}`);
    console.log(`  doc: ${d.docDesc.slice(0, 200)}`);
    console.log();
  }
  if (blind.length) {
    for (const b of blind) console.log(`BLIND: ${b.file} - ${b.reason}`);
    console.log();
  }
  console.log(`Actions compared: ${compared}. Param drifts: ${drifts.length}.`);
  process.exit(blind.length || drifts.length ? 1 : 0);
}
