#!/usr/bin/env node
/**
 * Ask the tests/ue_mcp editor this checkout launched to close, and wait for it to exit.
 *
 *   node scripts/stop-editor.mjs
 *
 * For the release runner, which closes its tests/ue_mcp editor at the end of the editor
 * lane so the run leaves none behind. The editor on this checkout's lockfile port is asked
 * which project it has open first, and only tests/ue_mcp is asked to close. Unsaved test
 * changes are discarded (requireClean: false).
 *
 * An editor that stops answering, or is still exiting 30 s after agreeing to close, is ended
 * (#1592): one held its port for an hour after its exit began, and the next run's lanes
 * attached to it. Only the lockfile's process, and only when its command line opens this
 * checkout's tests/ue_mcp.
 */
import { execFileSync } from "node:child_process";
import { askOnce, extractReportedProjectDir, isProcessAlive, isTestProjectDir, normalizeProjectRoot, PROJECT_IDENTITY_PYTHON, readPortLockfile, TEST_PROJECT_UPROJECT } from "./bridge-target.mjs";

const lock = readPortLockfile();
if (!lock.pidAlive || !lock.port) {
  console.log("no tests/ue_mcp editor running");
  process.exit(0);
}

/** Whether the process opens this checkout's tests/ue_mcp, read from its command line. */
function opensTestProject(pid) {
  if (process.platform !== "win32") return false;
  try {
    const line = execFileSync("powershell", ["-NoProfile", "-Command", `(Get-CimInstance Win32_Process -Filter "ProcessId = ${Number(pid)}").CommandLine`], { encoding: "utf8" });
    return normalizeProjectRoot(line.replace(/"/g, "")).includes(normalizeProjectRoot(TEST_PROJECT_UPROJECT));
  } catch {
    return false;
  }
}

async function waitForExit(seconds) {
  for (let i = 0; i < seconds * 2 && isProcessAlive(lock.pid); i++) await new Promise((r) => setTimeout(r, 500));
  return !isProcessAlive(lock.pid);
}

function end(reason) {
  if (!opensTestProject(lock.pid)) {
    console.error(`editor ${lock.pid} ${reason}, and its command line does not open ${TEST_PROJECT_UPROJECT}; leaving it alone`);
    process.exit(1);
  }
  console.log(`editor ${lock.pid} ${reason}; ending it`);
  try {
    execFileSync("taskkill", ["/PID", String(lock.pid), "/T", "/F"], { stdio: "ignore" }); // lint-prose-allow: no-kill
  } catch {
    // already gone
  }
}

const url = `ws://127.0.0.1:${lock.port}`;
let asked = false;
try {
  const reported = extractReportedProjectDir(await askOnce(url, "execute_python", { code: PROJECT_IDENTITY_PYTHON }, 10000));
  if (!isTestProjectDir(reported)) {
    console.error(`${url} has ${reported ?? "an unknown project"} open, not tests/ue_mcp; leaving it alone`);
    process.exit(1);
  }
  const reply = await askOnce(url, "request_editor_shutdown", { requireClean: false }, 15000);
  if (reply?.success === false) {
    console.error(`the editor declined to close: ${reply.error ?? "no reason given"}`);
    process.exit(1);
  }
  asked = true;
} catch (err) {
  end(`does not answer (${err instanceof Error ? err.message : err})`);
}
// An editor on its way out can spend minutes on the shared DDC's maintenance pass (63 s, and past
// 180 s, in #1592). It has agreed to close with its changes discarded, so nothing is lost by ending
// it, and the run does not wait on it.
if (asked && !(await waitForExit(30))) end("did not exit within 30 s of agreeing to close");
const closed = await waitForExit(30);
console.log(closed ? `editor ${lock.pid} closed` : `editor ${lock.pid} is still running`);
process.exit(closed ? 0 : 1);
