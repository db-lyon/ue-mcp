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
import { PAGINATION_PARAM_NAMES } from "../src/surface/pagination.js";

const ROOT = path.resolve(import.meta.dirname, "..");
const DOC_PATH = path.join(ROOT, "docs/tool-reference.md");

function docSection(doc, section) {
  const start = doc.indexOf(`\n## ${section}\n`);
  if (start === -1) return null;
  const after = doc.slice(start + 1);
  const endIdx = after.indexOf("\n## ");
  return endIdx === -1 ? after : after.slice(0, endIdx);
}

function extractDocRow(chunk, action) {
  if (!chunk) return null;
  // The first cell is the action's signature, `name(params)` (#1172); the
  // prose Params clause this compares is in the second.
  const re = new RegExp(`^\\|\\s+\`${action}(?:\\(.*?\\))?\`\\s+\\|\\s+(.+?)\\s+\\|\\s*$`, "m");
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

/** Defaults to the real tree and docs; a test passes a fixture of either. */
export function auditParams({ read = readCategories(), doc = fs.readFileSync(DOC_PATH, "utf8") } = {}) {
  const { categories, blind } = read;
  const drifts = [];
  let compared = 0;
  for (const category of categories) {
    const chunk = docSection(doc, category.name);
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
      if (paged) for (const p of PAGINATION_PARAM_NAMES) if (!srcParams.includes(p)) srcParams.push(p);
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
