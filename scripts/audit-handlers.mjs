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
const TS_TOOLS = path.join(ROOT, "src/tools");
const CPP_HANDLERS = path.join(ROOT, "plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers");

// ── TS side ───────────────────────────────────────────────────────────────────
// `bp("desc", "method_name", ...)` and `bridge: "method_name"`. Action keys
// without a bridge (pure local handlers) are excluded.

function tsBridgeMethods() {
  const methods = new Map(); // method -> [{file, action}]
  for (const f of fs.readdirSync(TS_TOOLS)) {
    if (!f.endsWith(".ts")) continue;
    const file = path.join(TS_TOOLS, f);
    const src = fs.readFileSync(file, "utf8");

    // bp("...", "method_name", ...)
    for (const m of src.matchAll(/^\s*([a-z_][a-z0-9_]*)\s*:\s*bp\(\s*"[^"]*"\s*,\s*"([a-z_][a-z0-9_]*)"/gm)) {
      const action = m[1], method = m[2];
      if (!methods.has(method)) methods.set(method, []);
      methods.get(method).push({ file: f, action });
    }
    // { ..., bridge: "method_name", ... }
    for (const m of src.matchAll(/^\s*([a-z_][a-z0-9_]*)\s*:\s*\{[\s\S]*?bridge:\s*"([a-z_][a-z0-9_]*)"/gm)) {
      const action = m[1], method = m[2];
      if (!methods.has(method)) methods.set(method, []);
      methods.get(method).push({ file: f, action });
    }
  }
  return methods;
}

// ── C++ side ──────────────────────────────────────────────────────────────────

function cppRegistrations() {
  const methods = new Map(); // method -> [{file}]
  function walk(dir) {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) { walk(full); continue; }
      if (!entry.name.endsWith(".cpp")) continue;
      const src = fs.readFileSync(full, "utf8");
      for (const m of src.matchAll(/Registry\.RegisterHandler(?:WithTimeout)?\(\s*TEXT\("([a-z_][a-z0-9_]*)"\)/g)) {
        const method = m[1];
        if (!methods.has(method)) methods.set(method, []);
        methods.get(method).push({ file: entry.name });
      }
    }
  }
  walk(CPP_HANDLERS);
  return methods;
}

const ts = tsBridgeMethods();
const cpp = cppRegistrations();

const tsOnly = [];   // bridge methods declared in TS but not registered in C++ -> "Unknown method" at runtime
const cppOnly = [];  // C++ handlers with no TS exposure -> unreachable handler

for (const [method, sites] of ts) {
  if (!cpp.has(method)) tsOnly.push({ method, sites });
}
for (const [method, sites] of cpp) {
  if (!ts.has(method)) cppOnly.push({ method, sites });
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

if (!errored && !cppOnly.length) {
  console.log(`OK — ${ts.size} TS bridge methods all map to ${cpp.size} C++ handler registrations.`);
}

process.exit(errored ? 1 : 0);
