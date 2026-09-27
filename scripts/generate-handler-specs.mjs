#!/usr/bin/env node
/**
 * Generate the spec'd part of each category's surface (#1057).
 *
 *     npm run specs:generate
 *
 * Reads tests/golden/handler-specs.json, the recording of the parameter specs
 * the C++ handlers register with (npm run specs:record), and writes
 * src/tools/specs/<category>.generated.ts plus its index: the recorded specs
 * only. The zod entries and each action's `Params:` clause are derived from the
 * specs at load (categorySchema and makeSpecBp in src/surface/handler-spec.ts). A category
 * module declares an action for a spec'd method with `specBp(effect, summary,
 * method)`, and never writes that method's parameters by hand.
 *
 * `--check` writes nothing and exits 1 when a checked-in file differs from what
 * the recording renders to.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { renderAll } from "./lib/handler-spec-gen.mjs";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const SNAPSHOT = path.join(ROOT, "tests", "golden", "handler-specs.json");
const check = process.argv.includes("--check");

const snapshot = JSON.parse(fs.readFileSync(SNAPSHOT, "utf8"));
const files = renderAll(snapshot);

const specsDir = path.join(ROOT, "src", "tools", "specs");
const stale = fs.existsSync(specsDir)
  ? fs.readdirSync(specsDir)
      .map((f) => `src/tools/specs/${f}`)
      .filter((rel) => !files.has(rel))
  : [];

let differs = 0;
for (const [rel, contents] of files) {
  const abs = path.join(ROOT, rel);
  const current = fs.existsSync(abs) ? fs.readFileSync(abs, "utf8").replace(/\r\n/g, "\n") : null;
  if (current === contents) continue;
  differs++;
  if (check) console.error(`[specs] ${rel} differs from the recording`);
  else {
    fs.mkdirSync(path.dirname(abs), { recursive: true });
    fs.writeFileSync(abs, contents);
    console.log(`[specs] wrote ${rel}`);
  }
}
for (const rel of stale) {
  differs++;
  if (check) console.error(`[specs] ${rel} is no longer generated`);
  else {
    fs.rmSync(path.join(ROOT, rel));
    console.log(`[specs] removed ${rel}`);
  }
}

const handlers = Object.keys(snapshot.handlers).length;
if (check && differs > 0) process.exit(1);
console.log(`[specs] ${handlers} spec'd handlers, ${files.size - 1} categories${differs === 0 ? ", up to date" : ""}`);
