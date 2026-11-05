#!/usr/bin/env node
/**
 * Refresh tests/timing/vitest-weights.json, the per-file seconds CI shards by (tests/setup/weighted-shards.ts).
 *
 *   npx vitest run tests/unit --reporter=json --outputFile=unit.json
 *   npx vitest run --config vitest.integration.config.ts --reporter=json --outputFile=it.json
 *   node scripts/vitest-weights.mjs unit.json it.json
 *
 * Files in the reports replace their entries; files that no longer exist are dropped.
 * Only the ratios matter, so a run on any machine is good enough to rebalance.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const OUT = path.join(ROOT, "tests", "timing", "vitest-weights.json");
const reports = process.argv.slice(2);
if (reports.length === 0) {
  console.error("usage: node scripts/vitest-weights.mjs <vitest json report>...");
  process.exit(2);
}

const current = fs.existsSync(OUT) ? JSON.parse(fs.readFileSync(OUT, "utf8")) : { files: {} };
const files = { ...current.files };
for (const report of reports) {
  for (const r of JSON.parse(fs.readFileSync(report, "utf8")).testResults ?? []) {
    const rel = path.relative(ROOT, r.name).split(path.sep).join("/");
    files[rel] = Math.round((r.endTime - r.startTime) / 100) / 10;
  }
}
for (const f of Object.keys(files)) if (!fs.existsSync(path.join(ROOT, f))) delete files[f];
const sorted = Object.fromEntries(Object.keys(files).sort().map((k) => [k, files[k]]));
fs.writeFileSync(OUT, `${JSON.stringify({ recordedAt: new Date().toISOString().slice(0, 10), source: "seconds per test file, from vitest JSON reports. Refresh with scripts/vitest-weights.mjs.", files: sorted }, null, 2)}\n`);
console.log(`vitest-weights: ${Object.keys(sorted).length} files written to ${path.relative(ROOT, OUT)}`);
