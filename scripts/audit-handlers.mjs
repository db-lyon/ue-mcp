#!/usr/bin/env node
//
// Compare bridge method names declared on the TS side (`bp(..., "method_name", ...)`)
// against C++ `Registry.RegisterHandler(TEXT("method_name"), ...)` registrations.
// Catches the silent-failure class CLAUDE.md warns about: drift between schema
// and handler names that turns a real action into "Unknown method".
//
// Exits non-zero when drift is found.

import fs from "node:fs";
import path from "node:path";

const ROOT = path.resolve(import.meta.dirname, "..");
const CPP_HANDLERS = path.join(ROOT, "plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers");

// ── TS side ───────────────────────────────────────────────────────────────────
// `bp("desc", "method_name", ...)` and `bridge: "method_name"`. Action keys
// without a bridge (pure local handlers) are excluded.

function tsBridgeMethods() {
  const methods = new Map(); // method -> [{file, action}]

  // Walk both src/tools/ (typed action surface) and the rest of src/ which
  // also makes direct bridge calls (flow runtime, custom tool handlers).
  function walkTs(dir) {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) { walkTs(full); continue; }
      if (!entry.name.endsWith(".ts")) continue;
      if (entry.name.endsWith(".d.ts")) continue;
      const src = fs.readFileSync(full, "utf8");
      const rel = path.relative(path.join(ROOT, "src"), full).replace(/\\/g, "/");

      // bp("desc", "method_name", ...) - the dominant pattern in tools/*.ts.
      // The 2nd string arg is the C++ bridge method. The description must be
      // matched with escape awareness: a naive "[^"]*" stops at the first \"
      // inside the prose, so any action whose description quotes something
      // (parentPath=\"None\") was reported as an unbridged handler.
      for (const m of src.matchAll(/\bbp\(\s*"(?:[^"\\]|\\.)*"\s*,\s*"([a-z_][a-z0-9_]*)"/g)) {
        const method = m[1];
        if (!methods.has(method)) methods.set(method, []);
        methods.get(method).push({ file: rel });
      }
      // { ..., bridge: "method_name", ... } - custom action specs.
      for (const m of src.matchAll(/\bbridge\s*:\s*"([a-z_][a-z0-9_]*)"/g)) {
        const method = m[1];
        if (!methods.has(method)) methods.set(method, []);
        methods.get(method).push({ file: rel });
      }
      // ctx.bridge.call("method_name", ...) and bridge.call("...", ...)
      // are direct calls in custom handlers (asset.list, asset.search, etc.).
      for (const m of src.matchAll(/\bbridge\.call\(\s*"([a-z_][a-z0-9_]*)"/g)) {
        const method = m[1];
        if (!methods.has(method)) methods.set(method, []);
        methods.get(method).push({ file: rel });
      }
    }
  }
  walkTs(path.join(ROOT, "src"));
  return methods;
}

// ── C++ side ──────────────────────────────────────────────────────────────────

function cppRegistrations() {
  const methods = new Map(); // method -> [{file}]
  const byHandlerFn = new Map(); // handler fn -> Set(method)
  function walk(dir) {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) { walk(full); continue; }
      if (!entry.name.endsWith(".cpp")) continue;
      const src = fs.readFileSync(full, "utf8");
      for (const m of src.matchAll(/Registry\.RegisterHandler(?:WithTimeout)?\(\s*TEXT\("([a-z_][a-z0-9_]*)"\)\s*,\s*&([\w:]+)/g)) {
        const [, method, fn] = m;
        if (!methods.has(method)) methods.set(method, []);
        methods.get(method).push({ file: entry.name });
        if (!byHandlerFn.has(fn)) byHandlerFn.set(fn, new Set());
        byHandlerFn.get(fn).add(method);
      }
    }
  }
  walk(CPP_HANDLERS);
  // method -> every other method backed by the same handler function.
  const aliases = new Map();
  for (const names of byHandlerFn.values()) {
    for (const n of names) aliases.set(n, names);
  }
  return { methods, aliases };
}

const ts = tsBridgeMethods();
const { methods: cpp, aliases } = cppRegistrations();

const tsOnly = [];   // bridge methods declared in TS but not registered in C++ -> "Unknown method" at runtime
const cppOnly = [];  // C++ handlers with no TS exposure -> unreachable handler
const aliasOnly = []; // reachable under a different name for the same handler fn

for (const [method, sites] of ts) {
  if (!cpp.has(method)) tsOnly.push({ method, sites });
}
for (const [method, sites] of cpp) {
  if (ts.has(method)) continue;
  // Several handler functions are deliberately registered under more than one
  // name (add_hismc_instances / add_ismc_instances / add_instances). Only the
  // canonical name is bridged, which is fine: the behaviour is reachable.
  // Reporting the spellings as gaps buries the handlers that really are
  // unreachable, so separate the two.
  const siblings = aliases.get(method);
  const reachable = siblings && [...siblings].some((s) => s !== method && ts.has(s));
  if (reachable) {
    aliasOnly.push({ method, canonical: [...siblings].filter((s) => ts.has(s)).join(", ") });
  } else {
    cppOnly.push({ method, sites });
  }
}

let errored = false;

if (tsOnly.length) {
  errored = true;
  console.error(`\n${tsOnly.length} TS bridge call(s) reference a method with NO C++ handler:`);
  for (const { method, sites } of tsOnly.sort((a, b) => a.method.localeCompare(b.method))) {
    const where = sites.map(s => `${s.file}:${s.action}`).join(", ");
    console.error(`  ${method.padEnd(40)} ${where}`);
  }
}

if (cppOnly.length) {
  // Non-fatal: some handlers are intentionally callable only from inside the
  // bridge (e.g. demo internals). Report them so adds are reviewable, but
  // do not fail CI for them.
  console.error(`\n${cppOnly.length} C++ handler(s) have no TS bridge caller:`);
  for (const { method, sites } of cppOnly.sort((a, b) => a.method.localeCompare(b.method))) {
    const where = sites.map(s => s.file).join(", ");
    console.error(`  ${method.padEnd(40)} ${where}`);
  }
}

if (aliasOnly.length) {
  console.error(`\n${aliasOnly.length} C++ handler name(s) are alternate spellings, reachable under their canonical name:`);
  for (const { method, canonical } of aliasOnly.sort((a, b) => a.method.localeCompare(b.method))) {
    console.error(`  ${method.padEnd(40)} -> ${canonical}`);
  }
}

if (!errored && !cppOnly.length) {
  console.log(`OK - ${ts.size} TS bridge methods all map to ${cpp.size} C++ handler registrations.`);
}

process.exit(errored ? 1 : 0);
