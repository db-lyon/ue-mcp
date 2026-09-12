#!/usr/bin/env node
/**
 * The C++ automation suite, run against an editor that is already up.
 *
 * The plugin's UE.MCP.* tests only run inside Unreal, so the only way to reach
 * them is the bridge. Without a runner here the choice was the MCP tool, which
 * answers with every result and floods a transcript, or an ad-hoc script that
 * nobody can review. This is the third option, alongside test:smoke and
 * test:live: same target guard, same refusal to touch anything else.
 *
 * Prints one line per failure and a summary. Exit 0 when nothing failed.
 *
 *   node scripts/automation-tests.mjs [--filter UE.MCP.] [--max 500]
 *                                     [--timeout 900000] [--verbose]
 */
import WebSocket from "ws";
import {
  assertLoopbackHost,
  bridgePortCandidates,
  describeMissingBridge,
  assertTestProjectDir,
  extractReportedProjectDir,
  PROJECT_IDENTITY_PYTHON,
  TEST_PROJECT_UPROJECT,
} from "./bridge-target.mjs";

const args = process.argv.slice(2);
function flag(name, fallback) {
  const i = args.indexOf(`--${name}`);
  return i >= 0 && args[i + 1] ? args[i + 1] : fallback;
}
const HOST = flag("host", "127.0.0.1");
const EXPLICIT_PORT = Number.parseInt(flag("port", ""), 10);
const FILTER = flag("filter", "UE.MCP.");
const MAX_TESTS = Number(flag("max", "500"));
// The suite runs every test in one game-thread call, so this is a whole-suite
// budget rather than a per-test one.
const TIMEOUT_MS = Number(flag("timeout", "900000"));
const VERBOSE = args.includes("--verbose");

const RESET = "\x1b[0m", RED = "\x1b[31m", GREEN = "\x1b[32m";
const YELLOW = "\x1b[33m", BOLD = "\x1b[1m", DIM = "\x1b[2m";

function connect(url, timeoutMs) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(url);
    const timer = setTimeout(() => { ws.terminate(); reject(new Error("connect timeout")); }, timeoutMs);
    ws.on("open", () => { clearTimeout(timer); resolve(ws); });
    ws.on("error", (e) => { clearTimeout(timer); reject(e); });
  });
}

let nextId = 1;
function rpc(ws, method, params, timeoutMs) {
  return new Promise((resolve, reject) => {
    const id = nextId++;
    const timer = setTimeout(() => reject(new Error(`timeout waiting for ${method}`)), timeoutMs);
    const onMessage = (data) => {
      let msg;
      try { msg = JSON.parse(data.toString()); } catch { return; }
      if (msg.id !== id) return;
      clearTimeout(timer);
      ws.removeListener("message", onMessage);
      if (msg.error) reject(new Error(msg.error.message ?? String(msg.error)));
      else resolve(msg.result);
    };
    ws.on("message", onMessage);
    ws.send(JSON.stringify({ id, method, params: params ?? {} }));
  });
}

async function connectToTestBridge() {
  assertLoopbackHost(HOST);
  const { candidates, lockfile } = bridgePortCandidates({
    explicitPort: Number.isInteger(EXPLICIT_PORT) ? EXPLICIT_PORT : null,
  });
  let lastError = null;
  for (const c of candidates) {
    const url = `ws://${HOST}:${c.port}`;
    try {
      const ws = await connect(url, 3000);
      console.log(`${GREEN}Connected to ${url}${RESET} ${DIM}(${c.source})${RESET}`);
      return ws;
    } catch (err) {
      lastError = err.message;
    }
  }
  throw new Error(describeMissingBridge({ host: HOST, candidates, lockfile, lastError }));
}

async function main() {
  console.log(`\n${BOLD}UE MCP Bridge - C++ automation suite${RESET}`);
  console.log(`${DIM}Project : ${TEST_PROJECT_UPROJECT}${RESET}`);
  console.log(`${DIM}Filter  : ${FILTER}${RESET}\n`);

  let ws;
  try {
    ws = await connectToTestBridge();
  } catch (err) {
    console.error(`${RED}${BOLD}${err.message}${RESET}\n`);
    process.exit(1);
  }

  // The same guard the smoke harness applies. These tests write into temp
  // mounts, but run_automation_tests dispatches every EditorContext test in
  // the process against whatever project is attached.
  try {
    const identity = await rpc(ws, "execute_python", { code: PROJECT_IDENTITY_PYTHON }, 60000);
    const reported = assertTestProjectDir(extractReportedProjectDir(identity));
    console.log(`${DIM}  target confirmed: ${reported}${RESET}\n`);
  } catch (err) {
    console.error(`\n${RED}${BOLD}Aborting: could not confirm the connected editor's project (${err.message}).${RESET}\n`);
    ws.close();
    process.exit(1);
  }

  let result;
  try {
    result = await rpc(ws, "run_automation_tests", { filter: FILTER, maxTests: MAX_TESTS }, TIMEOUT_MS);
  } catch (err) {
    console.error(`${RED}${BOLD}The run did not complete: ${err.message}${RESET}`);
    console.error(`${DIM}A test that takes the editor down looks exactly like this. Check the editor log.${RESET}\n`);
    ws.close();
    process.exit(1);
  }
  ws.close();

  const results = result.results ?? [];
  const failures = results.filter((r) => !r.passed);
  const abandoned = results.filter((r) => r.abandoned);

  for (const r of failures) {
    console.log(`${RED}FAIL${RESET}  ${r.test}`);
    for (const m of (VERBOSE ? (r.errorMessages ?? []) : (r.errorMessages ?? []).slice(0, 1))) {
      console.log(`${DIM}      ${String(m).split("\n")[0].slice(0, 160)}${RESET}`);
    }
  }

  console.log(`\n${BOLD}Summary${RESET}`);
  console.log(`  Ran            : ${result.ran}`);
  console.log(`  ${GREEN}Passed         : ${result.passed}${RESET}`);
  console.log(`  ${RED}Failed         : ${result.failed}${RESET}`);
  if (abandoned.length) console.log(`  ${YELLOW}Abandoned      : ${abandoned.length}${RESET}`);

  if (result.failed > 0) {
    console.log(`\n${RED}${BOLD}AUTOMATION TESTS FAILED${RESET} - ${result.failed} of ${result.ran}.`);
    console.log(`${DIM}Re-run with --verbose for every error line, or --filter to narrow.${RESET}\n`);
    process.exit(1);
  }
  console.log(`\n${GREEN}${BOLD}AUTOMATION TESTS PASSED${RESET} - all ${result.ran} passed.\n`);
  process.exit(0);
}

main();
