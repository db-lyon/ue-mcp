/**
 * Shared bridge discovery for the test harnesses (scripts/smoke-test.js and
 * everything under tests/).
 *
 * The editor no longer binds a fixed port: it derives a per-project port from
 * the project root path and publishes the port it actually bound to
 * <Project>/Saved/UE_MCP_Bridge/port.json. Harnesses that hardcode 9877 cannot
 * connect. Discovery order here is lockfile, then the derived port, then the
 * legacy fixed port, so a running editor is found with no environment variables
 * and no flags.
 *
 * The project itself is hardcoded to tests/ue_mcp/ue_mcp.uproject relative to
 * this file: smoke runs perform real mutations, so they only ever address the
 * dedicated test project.
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
 * so a developer can pin an unusual setup.
 */
export function bridgePortCandidates(options = {}) {
  const { explicitPort = null, lockfile = readPortLockfile() } = options;

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
  add(deriveProjectPort(TEST_PROJECT_DIR), "derived from project path");
  add(LEGACY_BRIDGE_PORT, "legacy fixed port");
  return { candidates, lockfile };
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

