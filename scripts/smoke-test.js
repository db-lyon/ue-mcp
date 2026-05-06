/**
 * Smoke Test — UE MCP Bridge Handler Verification
 *
 * Connects to the UE MCP bridge via WebSocket and sends a JSON-RPC call
 * to every registered handler. Categorises each response as:
 *   SUCCESS        — handler returned a result
 *   EXPECTED_ERROR — handler returned an error (e.g. missing params) — still alive
 *   FAILURE        — timeout, connection lost, or unknown-method error
 *
 * Exit code 0 when no FAILURES, 1 otherwise.
 *
 * Usage:  node scripts/smoke-test.js [--host 127.0.0.1] [--port 9877] [--timeout 5000]
 */

import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";
import WebSocket from "ws";

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const args = process.argv.slice(2);
function flag(name, fallback) {
  const idx = args.indexOf(`--${name}`);
  return idx !== -1 && args[idx + 1] ? args[idx + 1] : fallback;
}

const HOST = flag("host", "127.0.0.1");
const PORT = flag("port", "9877");
const TIMEOUT_MS = Number(flag("timeout", "5000"));
const WS_URL = `ws://${HOST}:${PORT}`;

// ---------------------------------------------------------------------------
// ANSI helpers
// ---------------------------------------------------------------------------
const RESET = "\x1b[0m";
const RED = "\x1b[31m";
const GREEN = "\x1b[32m";
const YELLOW = "\x1b[33m";
const CYAN = "\x1b[36m";
const BOLD = "\x1b[1m";
const DIM = "\x1b[2m";

// ---------------------------------------------------------------------------
// 1. Discover handler names from C++ sources
// ---------------------------------------------------------------------------
function discoverHandlers() {
  const handlersDir = path.resolve(
    __dirname,
    "..",
    "plugin",
    "ue_mcp_bridge",
    "Source",
    "UE_MCP_Bridge",
    "Private",
    "Handlers"
  );

  if (!fs.existsSync(handlersDir)) {
    console.error(`${RED}Handler directory not found: ${handlersDir}${RESET}`);
    process.exit(1);
  }

  const cppFiles = fs
    .readdirSync(handlersDir)
    .filter((f) => f.endsWith(".cpp"))
    .sort();

  // Matches Registry.RegisterHandler(...) and Registry.RegisterHandlerWithTimeout(...)
  const pattern = /Registry\.RegisterHandler(?:WithTimeout)?\(TEXT\("([^"]+)"\)/g;
  const handlers = []; // { method, file }

  for (const file of cppFiles) {
    const contents = fs.readFileSync(path.join(handlersDir, file), "utf-8");
    let match;
    while ((match = pattern.exec(contents)) !== null) {
      handlers.push({ method: match[1], file });
    }
  }

  return handlers;
}

// ---------------------------------------------------------------------------
// 2. WebSocket helpers
// ---------------------------------------------------------------------------
function connect(url) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(url);
    const timer = setTimeout(() => {
      ws.terminate();
      reject(new Error(`Connection to ${url} timed out after ${TIMEOUT_MS}ms`));
    }, TIMEOUT_MS);

    ws.on("open", () => {
      clearTimeout(timer);
      resolve(ws);
    });
    ws.on("error", (err) => {
      clearTimeout(timer);
      reject(err);
    });
  });
}

// Per-method param overrides for handlers that, called with empty params,
// do something inherently slow (full registry walk, project-wide save, full
// build). Empty-params smoke would time out on these AND clog the editor's
// game-thread queue, cascading every later handler into a false-FAILURE.
//
// The intent of smoke is "did the handler register and respond at all" - so
// we send the cheapest valid params we can to keep work small. Real param
// validation lives in the per-category vitest suites.
const PARAM_OVERRIDES = {
  // Asset registry walks
  list_assets:                 { query: "__smoke_no_match__" },
  search_assets:               { query: "__smoke_no_match__", maxResults: 1 },
  search_assets_fts:           { query: "__smoke_no_match__", maxResults: 1 },
  list_textures:               { directory: "/Game/MCPTest", maxResults: 1 },
  list_anim_assets:            { directory: "/Game/MCPTest", maxResults: 1 },
  list_skeletal_meshes:        { directory: "/Game/MCPTest", maxResults: 1 },
  list_behavior_trees:         { directory: "/Game/MCPTest" },
  list_eqs_queries:            { directory: "/Game/MCPTest" },
  list_state_trees:            { directory: "/Game/MCPTest" },
  list_input_assets:           { directory: "/Game/MCPTest" },
  diagnose_registry:           { directory: "/Game/MCPTest" },
  validate_assets:             { directory: "/Game/MCPTest" },
  reindex_assets_fts:          { directory: "/Game/MCPTest", maxAssets: 1 },
  // Level walks
  get_world_outliner:          { maxActors: 1 },
  list_actors_in_level:        { maxActors: 1 },
  list_streaming_levels:       { maxLevels: 1 },
  list_sublevels:              { maxLevels: 1 },
  // Long-running editor ops
  save_all:                    { dryRun: true },
  build_lighting:              { dryRun: true },
  build_all:                   { dryRun: true },
  build_project:               { dryRun: true },
  read_editor_log:             { tailLines: 1 },
  get_crash_reports:           { maxReports: 1 },
};

// Direct RPC helper for setup/teardown (separate from rpcCall which classifies
// for smoke summary). Returns {result, error} from the JSON-RPC frame; resolves
// even on RPC errors so the caller can sequence cleanly.
function rpcRaw(ws, method, params, id) {
  return new Promise((resolve) => {
    const payload = JSON.stringify({ method, params: params ?? {}, id });
    const timer = setTimeout(() => resolve({ error: { message: "timeout" } }), TIMEOUT_MS);
    const onMessage = (data) => {
      let msg;
      try { msg = JSON.parse(data.toString()); } catch { return; }
      if (msg.id !== id) return;
      clearTimeout(timer);
      ws.removeListener("message", onMessage);
      resolve(msg);
    };
    ws.on("message", onMessage);
    ws.send(payload);
  });
}

const SCRATCH_LEVEL = "/Game/MCP_SmokeScratch";
const HOME_LEVEL = "/Game/MCP_Home";

// Pre-flight: park the editor on a brand-new blank scratch level so every
// spawn handler in the smoke run lands there and not in MCP_Home (the
// editor's anchor) or DemoLevel or whatever the user had open. Idempotent
// across crashed previous runs - we anchor on MCP_Home first to ensure
// we're not currently sitting on the scratch we're about to delete.
async function preFlight(ws, idGen) {
  console.log(`${DIM}  preflight: anchor MCP_Home + create blank ${SCRATCH_LEVEL}${RESET}`);
  await rpcRaw(ws, "demo_go_home", {}, idGen());
  await rpcRaw(ws, "delete_asset", { assetPath: SCRATCH_LEVEL, force: true }, idGen());
  const r = await rpcRaw(ws, "create_new_level", { levelPath: SCRATCH_LEVEL }, idGen());
  if (r.error) {
    console.error(`${YELLOW}  preflight warning: create_new_level failed: ${r.error.message}${RESET}`);
  }
}

// Teardown: wipe every test artifact and leave the editor on a freshly-created
// blank MCP_Home so closing the editor never spawns a save-dialog and the
// next smoke run starts from a clean slate.
async function teardown(ws, idGen) {
  console.log(`${DIM}  teardown: reset MCP_Home + delete ${SCRATCH_LEVEL}${RESET}`);
  // We're currently on SCRATCH (full of smoke spawns). Delete MCP_Home from
  // disk so we can recreate it blank. Safe because we're not on it.
  await rpcRaw(ws, "delete_asset", { assetPath: HOME_LEVEL, force: true }, idGen());
  // Create a fresh blank MCP_Home; this also switches the editor to it,
  // unloading SCRATCH.
  await rpcRaw(ws, "create_new_level", { levelPath: HOME_LEVEL }, idGen());
  await rpcRaw(ws, "save_current_level", {}, idGen());
  // SCRATCH now unloaded; safe to delete.
  await rpcRaw(ws, "delete_asset", { assetPath: SCRATCH_LEVEL, force: true }, idGen());
}

function rpcCall(ws, method, id) {
  return new Promise((resolve) => {
    const params = PARAM_OVERRIDES[method] ?? {};
    const payload = JSON.stringify({ method, params, id });

    const timer = setTimeout(() => {
      resolve({ status: "FAILURE", reason: "timeout" });
    }, TIMEOUT_MS);

    const onMessage = (data) => {
      let msg;
      try {
        msg = JSON.parse(data.toString());
      } catch {
        return; // ignore non-JSON frames
      }
      if (msg.id !== id) return; // not our response

      clearTimeout(timer);
      ws.removeListener("message", onMessage);

      if (msg.error) {
        // "Method not found" style errors are real failures.
        // Everything else (missing params, invalid input, etc.) means
        // the handler exists and responded — that's a pass.
        const code = msg.error.code;
        const message = (msg.error.message || "").toLowerCase();
        const isUnknown =
          code === -32601 || message.includes("method not found") || message.includes("unknown method");
        if (isUnknown) {
          resolve({ status: "FAILURE", reason: "unknown method", detail: msg.error.message });
        } else {
          resolve({ status: "EXPECTED_ERROR", detail: msg.error.message });
        }
      } else {
        resolve({ status: "SUCCESS", detail: null });
      }
    };

    ws.on("message", onMessage);
    ws.send(payload);
  });
}

// ---------------------------------------------------------------------------
// 3. Main
// ---------------------------------------------------------------------------
async function main() {
  // Discover
  const handlers = discoverHandlers();
  if (handlers.length === 0) {
    console.error(`${RED}No handlers discovered — check the C++ source path.${RESET}`);
    process.exit(1);
  }

  console.log(`\n${BOLD}UE MCP Bridge — Smoke Test${RESET}`);
  console.log(`${DIM}Endpoint : ${WS_URL}${RESET}`);
  console.log(`${DIM}Handlers : ${handlers.length}${RESET}`);
  console.log(`${DIM}Timeout  : ${TIMEOUT_MS}ms per call${RESET}\n`);

  // Connect
  let ws;
  try {
    ws = await connect(WS_URL);
  } catch (err) {
    console.error(
      `${RED}${BOLD}Could not connect to the UE MCP bridge at ${WS_URL}${RESET}`
    );
    console.error(`${RED}${err.message}${RESET}`);
    console.error(
      `\n${DIM}Make sure Unreal Editor is running with the UE_MCP_Bridge plugin enabled.${RESET}\n`
    );
    process.exit(1);
  }

  console.log(`${GREEN}Connected to ${WS_URL}${RESET}\n`);

  // Run calls sequentially to avoid flooding the bridge.
  //
  // After any FAILURE (which is almost always a timeout), the editor's game
  // thread is still working through the late request. Sleeping + reconnecting
  // the WebSocket here lets that work drain before we queue the next call -
  // otherwise one slow handler cascades into 50+ false-FAILUREs as everything
  // queues behind it and also hits the per-call timeout.
  const results = [];
  let nextId = 1;
  const idGen = () => nextId++;

  // Pre-flight: anchor every spawn into a throwaway scratch level so the
  // anchor (MCP_Home) and any other map the user was on stays untouched.
  await preFlight(ws, idGen);

  for (const { method, file } of handlers) {
    const id = nextId++;
    process.stdout.write(`${DIM}  [${id}/${handlers.length}] ${method} ...${RESET}`);
    const result = await rpcCall(ws, method, id);
    result.method = method;
    result.file = file;
    results.push(result);

    // Inline status
    const tag =
      result.status === "SUCCESS"
        ? `${GREEN}SUCCESS${RESET}`
        : result.status === "EXPECTED_ERROR"
          ? `${YELLOW}EXPECTED_ERROR${RESET}`
          : `${RED}FAILURE${RESET}`;
    process.stdout.write(`\r  [${id}/${handlers.length}] ${method} ${tag}\n`);

    if (result.status === "FAILURE" && result.reason === "timeout") {
      try { ws.terminate(); } catch {}
      await new Promise((r) => setTimeout(r, 3000));
      try {
        ws = await connect(WS_URL);
      } catch (err) {
        console.error(`${RED}Reconnect after timeout failed: ${err.message}${RESET}`);
        break;
      }
    }
  }

  // Teardown ALWAYS runs - even on partial failures - so the editor never
  // exits with unsaved spawns or a dirty MCP_Home. Errors here are surfaced
  // but don't fail the smoke summary.
  try {
    await teardown(ws, idGen);
  } catch (e) {
    console.error(`${YELLOW}Teardown encountered an error: ${e.message}${RESET}`);
  }

  ws.close();

  // ---------------------------------------------------------------------------
  // 4. Summary
  // ---------------------------------------------------------------------------
  const successes = results.filter((r) => r.status === "SUCCESS");
  const expectedErrors = results.filter((r) => r.status === "EXPECTED_ERROR");
  const failures = results.filter((r) => r.status === "FAILURE");

  const COL_STATUS = 18;
  const COL_METHOD = 45;
  const COL_FILE = 35;

  console.log(`\n${"=".repeat(COL_STATUS + COL_METHOD + COL_FILE + 6)}`);
  console.log(
    `${BOLD}${"STATUS".padEnd(COL_STATUS)}${"METHOD".padEnd(COL_METHOD)}${"FILE".padEnd(COL_FILE)}DETAIL${RESET}`
  );
  console.log(`${"-".repeat(COL_STATUS + COL_METHOD + COL_FILE + 6)}`);

  for (const r of results) {
    const color =
      r.status === "SUCCESS" ? GREEN : r.status === "EXPECTED_ERROR" ? YELLOW : RED;
    const statusStr = `${color}${r.status.padEnd(COL_STATUS)}${RESET}`;
    const methodStr = r.method.padEnd(COL_METHOD);
    const fileStr = r.file.padEnd(COL_FILE);
    const detail = r.detail ? r.detail.slice(0, 60) : "";
    console.log(`${statusStr}${methodStr}${fileStr}${DIM}${detail}${RESET}`);
  }

  console.log(`${"=".repeat(COL_STATUS + COL_METHOD + COL_FILE + 6)}\n`);

  console.log(`${BOLD}Summary${RESET}`);
  console.log(`  Total          : ${handlers.length}`);
  console.log(`  ${GREEN}Success        : ${successes.length}${RESET}`);
  console.log(`  ${YELLOW}Expected Error : ${expectedErrors.length}${RESET}`);
  console.log(`  ${RED}Failure        : ${failures.length}${RESET}`);

  if (failures.length > 0) {
    console.log(`\n${RED}${BOLD}SMOKE TEST FAILED${RESET} — ${failures.length} handler(s) did not respond.\n`);
    process.exit(1);
  } else {
    console.log(`\n${GREEN}${BOLD}SMOKE TEST PASSED${RESET} — all ${handlers.length} handlers responded.\n`);
    process.exit(0);
  }
}

main();
