#!/usr/bin/env node
/**
 * Record the handler parameter specs the running plugin publishes (#1057).
 *
 *     npm run specs:record
 *
 * The specs are authored in C++, at each handler's RegisterHandler call, and
 * the bridge publishes them in get_bridge_capabilities.handlerSpecs. This writes
 * that answer to tests/golden/handler-specs.json, which is what
 * `npm run specs:generate` renders the TS surface from. Run the generator
 * afterwards and review both diffs.
 *
 * A recording rather than a read at startup, for the reason the Epic catalog is
 * one: the advertised surface is decided before any editor is contacted, so it
 * is the same with or without one. `tests/live/handler-specs.test.ts` compares
 * a live editor against this file and fails when they differ.
 *
 * Talks to the bridge directly, and only to an editor that has tests/ue_mcp
 * open, the same guard every other script in this repo applies.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { connectTestBridge } from "./bridge-target.mjs";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const OUT = path.join(ROOT, "tests", "golden", "handler-specs.json");

async function main() {
  const bridge = await connectTestBridge({ log: console.log });
  console.log(`target confirmed: ${bridge.projectDir}`);

  const answered = await bridge.call("get_bridge_capabilities");
  const specs = answered?.handlerSpecs;
  if (!specs || typeof specs !== "object" || Object.keys(specs).length === 0) {
    throw new Error(
      "The connected plugin published no handler specs. Recording that would delete every generated "
      + "action, so nothing was written. Rebuild the plugin from this checkout and restart the editor.",
    );
  }

  // Sorted on the way in, so a registration order change is not a diff.
  const handlers = Object.fromEntries(Object.keys(specs).sort().map((m) => [m, specs[m]]));
  const snapshot = { handlerCount: Object.keys(handlers).length, handlers };

  fs.writeFileSync(OUT, `${JSON.stringify(snapshot, null, 2)}\n`);
  console.log(`recorded ${snapshot.handlerCount} spec'd handlers`);
  console.log(`wrote ${path.relative(ROOT, OUT)}; now run npm run specs:generate`);
  bridge.close();
}

main().catch((err) => {
  console.error(`[specs] ${err.message}`);
  process.exit(1);
});
