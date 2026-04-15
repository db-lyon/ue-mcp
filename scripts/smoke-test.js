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

function rpcCall(ws, method, id) {
  return new Promise((resolve) => {
    const payload = JSON.stringify({ method, params: {}, id });

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

  // Run calls sequentially to avoid flooding the bridge
  const results = [];
  let nextId = 1;

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
    // Clear line, reprint
    process.stdout.write(`\r  [${id}/${handlers.length}] ${method} ${tag}\n`);
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
