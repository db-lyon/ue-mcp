/**
 * Shared target resolution for the test harnesses (scripts/smoke-test.js and
 * everything under tests/).
 *
 * Two jobs:
 *
 *  1. Find the bridge. The editor no longer binds a fixed port: it derives a
 *     per-project port from the project root path and publishes the port it
 *     actually bound to <Project>/Saved/UE_MCP_Bridge/port.json. Harnesses that
 *     hardcode 9877 cannot connect. Discovery order here is lockfile, then the
 *     derived port, then the legacy fixed port, so a running editor is found
 *     with no environment variables and no flags.
 *
 *  2. Refuse anything that is not the dedicated test project. Smoke runs
 *     perform real mutations (create blueprints, delete assets, rewrite the
 *     level), so pointing them at a working project can destroy someone's work.
 *     The project is hardcoded to tests/ue_mcp/ue_mcp.uproject relative to this
 *     file. Host and port remain overridable for odd local setups, so the guard
 *     is enforced after connecting by asking the editor which project it has
 *     open and aborting on any mismatch.
 */

import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));

/** Repository root (the checkout this harness belongs to). */
export const REPO_ROOT = path.resolve(HERE, "..");

/** The one project any test harness in this repo is allowed to touch. */
export const TEST_PROJECT_DIR = path.join(REPO_ROOT, "tests", "ue_mcp");
export const TEST_PROJECT_UPROJECT = path.join(TEST_PROJECT_DIR, "ue_mcp.uproject");

/** Where the running bridge publishes the port it bound for that project. */
export const TEST_PORT_LOCKFILE = path.join(
  TEST_PROJECT_DIR, "Saved", "UE_MCP_Bridge", "port.json",
);

/** Pre-derived-port bridges bound this. Kept as the last candidate. */
export const LEGACY_BRIDGE_PORT = 9877;

// Ephemeral range used by the derived-port scheme. Must match src/port.ts and
// FMCPBridgeServer::DeriveProjectPort; tests/unit/bridge-target.test.ts pins
// this implementation against src/port.ts so the two cannot drift apart.
const EPHEMERAL_BASE = 49152;
const EPHEMERAL_SPAN = 65535 - EPHEMERAL_BASE + 1;

/** Canonical form of a project root for hashing and for identity comparison. */
export function normalizeProjectRoot(dir) {
  return String(dir).replace(/\\/g, "/").replace(/\/+$/, "").toLowerCase();
}

/** Port the bridge would bind for a project root when nothing is in its way. */
export function deriveProjectPort(projectRootDir) {
  const h = crypto.createHash("sha1")
    .update(normalizeProjectRoot(projectRootDir), "utf8")
    .digest();
  const v = ((h[0] << 24) | (h[1] << 16) | (h[2] << 8) | h[3]) >>> 0;
  return EPHEMERAL_BASE + (v % EPHEMERAL_SPAN);
}

/**
 * Read the port lockfile for the test project.
 *
 * Never throws: a missing, empty, or malformed lockfile is an ordinary state
 * (editor not started yet, editor shut down cleanly) and the reason is carried
 * in the returned record so the failure message can quote it.
 */
export function readPortLockfile(lockfilePath = TEST_PORT_LOCKFILE) {
  const record = {
    path: lockfilePath,
    exists: false,
    port: null,
    pid: null,
    startedAt: null,
    error: null,
    pidAlive: null,
  };

  let raw;
  try {
    raw = fs.readFileSync(lockfilePath, "utf8");
    record.exists = true;
  } catch (err) {
    record.error = err.code === "ENOENT" ? "not found" : `unreadable (${err.code ?? err.message})`;
    return record;
  }

  let parsed;
  try {
    parsed = JSON.parse(raw);
  } catch {
    record.error = "present but not valid JSON";
    return record;
  }

  const port = Number(parsed?.port);
  if (!Number.isInteger(port) || port <= 0 || port > 65535) {
    record.error = `present but has no usable "port" field (got ${JSON.stringify(parsed?.port)})`;
    return record;
  }

  record.port = port;
  record.pid = Number.isInteger(Number(parsed?.pid)) ? Number(parsed.pid) : null;
  record.startedAt = typeof parsed?.startedAt === "string" ? parsed.startedAt : null;
  record.pidAlive = record.pid === null ? null : isProcessAlive(record.pid);
  return record;
}

/** Best-effort liveness probe, used only to make failure messages specific. */
export function isProcessAlive(pid) {
  try {
    process.kill(pid, 0);
    return true;
  } catch (err) {
    return err.code === "EPERM";
  }
}

/**
 * Ordered list of ports worth trying for the test project bridge.
 *
 * An explicit port (CLI flag or environment variable) short-circuits discovery
 * so a developer can pin an unusual setup. It does not weaken the project
 * guard, which runs after the connection is up.
 */
export function bridgePortCandidates(options = {}) {
  // `projectDir` names a project other than this repo's test project. It is
  // only ever passed by a harness driving more than one editor (#817); every
  // ordinary caller omits it and gets exactly the behaviour it always had.
  const { explicitPort = null, projectDir = null } = options;
  const targetDir = projectDir ?? TEST_PROJECT_DIR;
  const lockfile = options.lockfile ?? readPortLockfile(
    projectDir
      ? path.join(projectDir, "Saved", "UE_MCP_Bridge", "port.json")
      : TEST_PORT_LOCKFILE,
  );

  const candidates = [];
  const add = (port, source) => {
    if (!Number.isInteger(port) || port <= 0 || port > 65535) return;
    if (candidates.some((c) => c.port === port)) return;
    candidates.push({ port, source });
  };

  if (Number.isInteger(explicitPort) && explicitPort > 0) {
    add(explicitPort, "explicit");
    return { candidates, lockfile };
  }

  add(lockfile.port, "lockfile");
  add(deriveProjectPort(targetDir), "derived from project path");
  add(LEGACY_BRIDGE_PORT, "legacy fixed port");
  return { candidates, lockfile };
}

/** Loopback-only: a smoke run must never reach an editor on another machine. */
export function isLoopbackHost(host) {
  const h = String(host).trim().toLowerCase().replace(/^\[|\]$/g, "");
  return h === "localhost" || h === "127.0.0.1" || h === "::1" || h.startsWith("127.");
}

export function assertLoopbackHost(host) {
  if (isLoopbackHost(host)) return;
  throw new Error(
    `Refusing to run the smoke harness against host "${host}". ` +
    `It is hardcoded to the local test project (${TEST_PROJECT_UPROJECT}) and only loopback hosts can serve it.`,
  );
}

/**
 * The failure message for "no bridge answered". Names every port that was
 * tried, where each came from, and the exact lockfile path that was read, so
 * the next step is obvious without reading harness source.
 */
export function describeMissingBridge(options) {
  const { host, candidates, lockfile, lastError = null } = options;

  const lines = [];
  lines.push("Could not reach the UE MCP bridge for the smoke test project.");
  lines.push(`  Project  : ${TEST_PROJECT_UPROJECT}${fs.existsSync(TEST_PROJECT_UPROJECT) ? "" : "  (MISSING)"}`);

  let lockNote;
  if (lockfile.port !== null) {
    const pidNote = lockfile.pid === null
      ? ""
      : `, pid ${lockfile.pid}${lockfile.pidAlive === false ? " (not running, stale lockfile)" : ""}`;
    lockNote = `port ${lockfile.port}${pidNote}`;
  } else {
    lockNote = lockfile.error ?? "no port recorded";
  }
  lines.push(`  Lockfile : ${lockfile.path}  (${lockNote})`);

  for (const [i, c] of candidates.entries()) {
    const label = i === 0 ? "  Tried    : " : "             ";
    lines.push(`${label}ws://${host}:${c.port}  (${c.source})`);
  }
  if (lastError) lines.push(`  Last error: ${lastError}`);
  lines.push("");
  lines.push("Start the editor on the test project (npm run up), wait for \"Bridge listening\" in the editor log,");
  lines.push(`then retry. The bridge writes its bound port to the lockfile path above.`);
  return lines.join("\n");
}

// ---------------------------------------------------------------------------
// Project identity guard
// ---------------------------------------------------------------------------

/**
 * Python the harness runs on the connected editor to learn which project is
 * open. Printed with a marker so it survives whatever shape the bridge wraps
 * python output in.
 */
export const PROJECT_IDENTITY_PYTHON =
  'import unreal\nprint("MCP_PROJECT_DIR:" + unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))';

/**
 * Pull the reported project directory out of an execute_python result. The
 * result is stringified first, so the marker is found whatever key the python
 * output landed under; JSON escaping is then undone so a Windows path survives
 * the round trip intact.
 */
export function extractReportedProjectDir(result) {
  const text = typeof result === "string" ? result : JSON.stringify(result ?? "");
  const match = /MCP_PROJECT_DIR:((?:\\.|[^"\r\n])*)/.exec(text);
  if (!match) return null;
  const unescaped = match[1].replace(/\\(.)/g, (_, ch) =>
    ch === "n" ? "\n" : ch === "r" ? "\r" : ch === "t" ? "\t" : ch,
  );
  const dir = unescaped.split(/[\r\n]/, 1)[0].trim();
  return dir.length > 0 ? dir : null;
}

/** True when the editor's open project is this checkout's test project. */
export function isTestProjectDir(reportedDir) {
  if (!reportedDir) return false;
  return normalizeProjectRoot(reportedDir) === normalizeProjectRoot(TEST_PROJECT_DIR);
}

/**
 * Hard guard. Throws unless the connected editor has the test project open.
 * Called before the harness issues a single mutating request.
 */
export function assertTestProjectDir(reportedDir) {
  if (isTestProjectDir(reportedDir)) return reportedDir;
  const what = reportedDir
    ? `it reported "${reportedDir}"`
    : "the editor did not report a project directory (is the Python plugin enabled?)";
  throw new Error(
    "Aborting: the connected editor is not the smoke test project.\n" +
    `  Expected : ${TEST_PROJECT_DIR}\n` +
    `  Reported : ${reportedDir ?? "(unknown)"}\n` +
    `Smoke runs perform destructive mutations, so the harness only ever talks to its own test project. ` +
    `Because ${what}, nothing was sent.`,
  );
}

/**
 * Run the guard over any RPC caller. `call` takes (method, params) and resolves
 * to the raw handler result (or anything containing it); the identity marker is
 * matched out of the stringified value.
 */
export async function verifyTestProjectTarget(call) {
  let result;
  try {
    result = await call("execute_python", { code: PROJECT_IDENTITY_PYTHON });
  } catch (err) {
    throw new Error(
      "Aborting: could not confirm which project the connected editor has open " +
      `(execute_python failed: ${err instanceof Error ? err.message : String(err)}).\n` +
      `The smoke harness only runs against ${TEST_PROJECT_UPROJECT}, so it will not send mutations it cannot vouch for.`,
    );
  }
  return assertTestProjectDir(extractReportedProjectDir(result));
}
