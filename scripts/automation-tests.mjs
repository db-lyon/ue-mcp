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
import { connectTestBridge, TEST_PROJECT_UPROJECT } from "./bridge-target.mjs";

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

async function main() {
  console.log(`\n${BOLD}UE MCP Bridge - C++ automation suite${RESET}`);
  console.log(`${DIM}Project : ${TEST_PROJECT_UPROJECT}${RESET}`);
  console.log(`${DIM}Filter  : ${FILTER}${RESET}\n`);

  // The same guard the smoke harness applies. These tests write into temp
  // mounts, but run_automation_tests dispatches every EditorContext test in
  // the process against whatever project is attached.
  let bridge;
  try {
    bridge = await connectTestBridge({
      host: HOST,
      explicitPort: Number.isInteger(EXPLICIT_PORT) ? EXPLICIT_PORT : null,
      log: (m) => console.log(`${GREEN}${m}${RESET}`),
    });
    console.log(`${DIM}  target confirmed: ${bridge.projectDir}${RESET}
`);
  } catch (err) {
    console.error(`${RED}${BOLD}${err.message}${RESET}
`);
    process.exit(1);
  }

  let result;
  try {
    result = await bridge.call("run_automation_tests", { filter: FILTER, maxTests: MAX_TESTS }, TIMEOUT_MS);
  } catch (err) {
    console.error(`${RED}${BOLD}The run did not complete: ${err.message}${RESET}`);
    console.error(`${DIM}A test that takes the editor down looks exactly like this. Check the editor log.${RESET}\n`);
    bridge.close();
    process.exit(1);
  }
  bridge.close();

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
