#!/usr/bin/env node
/**
 * What is left to read in `src/tools/epic/effects.ts`.
 *
 *     npm run epic:unreviewed [--effect mutate] [--limit 40]
 *
 * An entry the seeder wrote carries a `// SEEDED` comment. Removing it is what
 * says a person has read that tool's description and agrees with the verdict.
 * This prints what still carries one, so the review has a worklist and being
 * finished is a number rather than a feeling.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const EFFECTS = path.join(ROOT, "src", "tools", "epic", "effects.ts");

const argv = process.argv.slice(2);
const arg = (name, fallback) => {
  const i = argv.indexOf(`--${name}`);
  return i !== -1 && argv[i + 1] ? argv[i + 1] : fallback;
};
const wantEffect = arg("effect", null);
const limit = Number(arg("limit", "0")) || 0;

const src = fs.readFileSync(EFFECTS, "utf8");
const lines = src.split(/\r?\n/);

const rows = [];
let total = 0;
let seeded = 0;
for (let i = 0; i < lines.length; i++) {
  const m = lines[i].match(/^\s*"([^"]+)":\s*"(read|mutate|unknown)",(\s*\/\/ SEEDED)?/);
  if (!m) continue;
  total++;
  if (!m[3]) continue;
  seeded++;
  if (wantEffect && m[2] !== wantEffect) continue;
  const comment = (lines[i - 1] ?? "").trim().replace(/^\/\/\s?/, "");
  rows.push({ tool: m[1], effect: m[2], comment });
}

const shown = limit > 0 ? rows.slice(0, limit) : rows;
for (const r of shown) {
  console.log(`${r.effect.padEnd(7)} ${r.tool}`);
  if (r.comment) console.log(`        ${r.comment}`);
}
console.log(
  `\n${seeded} of ${total} still seeded${wantEffect ? ` (${rows.length} of them ${wantEffect})` : ""}`
  + `${limit > 0 && rows.length > shown.length ? `, showing ${shown.length}` : ""}.`,
);
if (seeded === 0) console.log("Every effect has been read.");
