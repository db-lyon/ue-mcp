#!/usr/bin/env node
/**
 * Record the handler parameter specs the running plugin publishes (#1057).
 *
 *     npm run specs:record
 *
 * The specs are authored in C++, at each handler's RegisterHandler call, and
 * the bridge publishes them in get_bridge_capabilities.handlerSpecs, with each
 * action's facts (timeout, modal safety, batch refusal, server-internal caller)
 * in actionFacts. This writes both to tests/golden/handler-specs.json, which is what
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
 *
 * It also writes tests/golden/handler-specs.source.json, a fingerprint of the
 * C++ spec source (scripts/lib/spec-source.mjs), so CI, which has no Unreal
 * install, can tell when the source changed and the recording did not.
 * `--fingerprint-only` writes just that file, for when the recording is known
 * current: tests/live/handler-specs.test.ts passed against an editor built from
 * this source.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { connectTestBridge } from "./bridge-target.mjs";
import { SPEC_SOURCE_FILE, serializeSpecSource, specSourceFingerprint } from "./lib/spec-source.mjs";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const OUT = path.join(ROOT, "tests", "golden", "handler-specs.json");

function writeSourceFingerprint() {
  fs.writeFileSync(SPEC_SOURCE_FILE, serializeSpecSource(specSourceFingerprint()));
  console.log(`wrote ${path.relative(ROOT, SPEC_SOURCE_FILE)}`);
}

async function main() {
  if (process.argv.includes("--fingerprint-only")) {
    writeSourceFingerprint();
    return;
  }
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

  const facts = answered?.actionFacts;
  if (!facts || typeof facts !== "object") {
    throw new Error(
      "The connected plugin published no actionFacts, so it predates the action declaration; recording it would "
      + "drop every timeout and flag. Rebuild the plugin from this checkout and restart the editor.",
    );
  }

  // Sorted on the way in, so a registration order change is not a diff.
  const sorted = (map) => Object.fromEntries(Object.keys(map).sort().map((m) => [m, map[m]]));
  const handlers = sorted(specs);
  const snapshot = {
    handlerCount: Object.keys(handlers).length,
    handlers,
    // Every action whose facts differ from the default, spec'd or not: its timeout,
    // modal safety, batch refusal, server-internal caller. FMCPActionFacts in C++.
    actionFacts: sorted(facts),
    ...(answered.timeouts ? { timeouts: answered.timeouts } : {}),
  };

  fs.writeFileSync(OUT, `${JSON.stringify(snapshot, null, 2)}\n`);
  console.log(`recorded ${snapshot.handlerCount} spec'd handlers`);
  writeSourceFingerprint();
  console.log(`wrote ${path.relative(ROOT, OUT)}; now run npm run generate`);
  bridge.close();
}

main().catch((err) => {
  console.error(`[specs] ${err.message}`);
  process.exit(1);
});
