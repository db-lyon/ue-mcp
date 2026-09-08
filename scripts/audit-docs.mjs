//
// Does docs/tool-reference.md list every action the server actually advertises?
//
// The answer is only worth having if the reader of the source can be trusted,
// and a regex over TypeScript cannot be. Two ways this audit used to lie:
//
//   * A category whose description contained an escaped quote
//     (foliage's `\"LandscapeGrassOutput\"`) ended the description matcher
//     early, the whole `categoryTool(...)` match failed, and the category
//     reported src=0. Fifteen documented actions read as EXTRA and the exit
//     code stayed 0.
//   * The category list was hard-coded and had stopped at twenty. chooser,
//     plugins, epic and fab were never looked at.
//
// So: the categories come from the source tree, the source is masked rather
// than pattern-matched (see scripts/lib/tool-source.mjs), and a category the
// parser cannot read is a failure in its own right. Reporting nothing is not
// the same as finding nothing.

import fs from "node:fs";
import path from "node:path";
import { readCategories } from "./lib/tool-source.mjs";

const ROOT = path.resolve(import.meta.dirname, "..");
const DOC_PATH = path.join(ROOT, "docs/tool-reference.md");
const DOC = fs.readFileSync(DOC_PATH, "utf8");

/** The action rows of one `## <category>` section of the reference. */
function docActions(section) {
  const start = DOC.indexOf(`\n## ${section}\n`);
  if (start === -1) return null;
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

export function auditDocs() {
  const { categories, blind } = readCategories();
  const rows = [];
  for (const category of categories) {
    const doc = docActions(category.name);
    if (doc === null) {
      blind.push({
        file: path.relative(ROOT, category.file),
        reason: `docs/tool-reference.md has no "## ${category.name}" section`,
      });
      continue;
    }
    const src = category.actions.map((a) => a.name);
    rows.push({
      category: category.name,
      src,
      doc,
      missing: src.filter((a) => !doc.includes(a)),
      extra: doc.filter((a) => !src.includes(a)),
    });
  }
  return { rows, blind };
}

if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(import.meta.filename)) {
  const { rows, blind } = auditDocs();
  let totalMissing = 0;
  let totalExtra = 0;
  for (const r of rows) {
    if (r.missing.length || r.extra.length) {
      console.log(`--- ${r.category} (src=${r.src.length}, doc=${r.doc.length}) ---`);
      if (r.missing.length) {
        console.log(`  MISSING from docs: ${r.missing.join(", ")}`);
        totalMissing += r.missing.length;
      }
      if (r.extra.length) {
        console.log(`  EXTRA in docs:     ${r.extra.join(", ")}`);
        totalExtra += r.extra.length;
      }
    } else {
      console.log(`${r.category}: OK (${r.src.length})`);
    }
  }
  if (blind.length) {
    console.log("");
    for (const b of blind) console.log(`BLIND: ${b.file} - ${b.reason}`);
  }
  console.log(
    `\nCategories read: ${rows.length}${blind.length ? `, unreadable: ${blind.length}` : ""}. `
      + `Total: ${totalMissing} missing, ${totalExtra} extra`
  );
  process.exit(blind.length || totalMissing || totalExtra ? 1 : 0);
}
