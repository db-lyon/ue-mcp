/**
 * The live tests (#817, plan items 1.10 and 7.3).
 *
 *     npm run test:live
 *
 * Everything under tests/live needs an Unreal editor that is already running,
 * and it only ever drives this repository's own test project. This script is
 * the preflight for both of those facts: it finds the bridge, proves the
 * editor has tests/ue_mcp open, prints what it found, and only then hands over
 * to vitest. A suite that discovered the editor inside the tests would report
 * "no editor" as a wall of failed assertions; this reports it as one message
 * with the ports it tried and the lockfile it read.
 *
 * It never starts or stops an editor. The suite attaches to one somebody else
 * owns and leaves it as it found it.
 *
 * Flags:
 *   --record-golden   Re-record tests/golden/editor-connected.json instead of
 *                     asserting against it. Reviews the diff yourself.
 *   --only <file>     Run one file under tests/live.
 */
import { spawnSync } from "node:child_process";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import {
  askOnce,
  assertLiveTestProjectDir,
  assertLoopbackHost,
  bridgePortCandidates,
  describeMissingBridge,
  extractReportedProjectDir,
  liveTestProjectDirs,
  PROJECT_IDENTITY_PYTHON,
} from "./bridge-target.mjs";

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

const argv = process.argv.slice(2);
const RECORD_GOLDEN = argv.includes("--record-golden");
const onlyIndex = argv.indexOf("--only");
const ONLY = onlyIndex !== -1 ? argv[onlyIndex + 1] : null;

const HOST = process.env.UE_MCP_LIVE_HOST ?? "127.0.0.1";
const CONNECT_TIMEOUT_MS = 5000;
const CALL_TIMEOUT_MS = 60_000;

async function preflight() {
  assertLoopbackHost(HOST);
  const allowed = liveTestProjectDirs();
  if (allowed.length === 0) {
    throw new Error("No tests/ue_mcp project in this checkout. The live tests drives that project and nothing else.");
  }

  let lastError = null;
  let lastCandidates = [];
  let lastLockfile = null;

  for (const projectDir of allowed) {
    const { candidates, lockfile } = bridgePortCandidates({ projectDir });
    lastCandidates = candidates;
    lastLockfile = lockfile;

    for (const candidate of candidates) {
      const url = `ws://${HOST}:${candidate.port}`;
      let capabilities;
      try {
        capabilities = await askOnce(url, "get_bridge_capabilities", {}, CONNECT_TIMEOUT_MS);
      } catch (err) {
        lastError = err instanceof Error ? err.message : String(err);
        continue;
      }

      // Identity before anything else: an editor with another project open is
      // an abort, not the next candidate to try.
      const result = await askOnce(url, "execute_python", { code: PROJECT_IDENTITY_PYTHON }, CALL_TIMEOUT_MS);
      const reported = extractReportedProjectDir(result);
      assertLiveTestProjectDir(reported, allowed);

      return { projectDir: reported, port: candidate.port, source: candidate.source, capabilities };
    }
  }

  throw new Error(
    `${describeMissingBridge({ host: HOST, candidates: lastCandidates, lockfile: lastLockfile, lastError })}\n\n` +
      `Project directories searched:\n  ${allowed.join("\n  ")}\n\n` +
      "The live tests attaches to an editor that is already running and never starts one itself.",
  );
}

let target;
try {
  target = await preflight();
} catch (err) {
  console.error(`\n[live] ${err instanceof Error ? err.message : err}\n`);
  process.exit(1);
}

const capabilities = target.capabilities ?? {};
console.error("[live] target editor");
console.error(`[live]   project : ${target.projectDir}`);
console.error(`[live]   bridge  : ws://${HOST}:${target.port}  (${target.source})`);
console.error(`[live]   engine  : ${capabilities.engineVersion ?? "unknown"}, pid ${capabilities.pid ?? "?"}`);
console.error(
  `[live]   plugin  : protocol ${capabilities.protocolVersion ?? "?"}, ` +
    `handler api ${capabilities.handlerApiVersion ?? "?"}, built ${capabilities.builtAt ?? "unknown"}, ` +
    `${capabilities.actionCount ?? "?"} actions`,
);
if (capabilities.paramEcho !== true) {
  console.error(
    "[live]   note    : the parameter echo is off, so the leak assertions will be skipped. " +
      "Relaunch the editor with UE_MCP_PARAM_ECHO=1 in its environment to include them.",
  );
}
console.error("[live] nothing below starts or stops an editor.\n");

const targets = ONLY ? [ONLY] : ["tests/live"];
const result = spawnSync(
  process.execPath,
  [path.join(REPO_ROOT, "node_modules", "vitest", "vitest.mjs"), "run", ...targets],
  {
    cwd: REPO_ROOT,
    stdio: "inherit",
    env: {
      ...process.env,
      UE_MCP_LIVE_HOST: HOST,
      UE_MCP_LIVE_PORT: String(target.port),
      UE_MCP_LIVE_PROJECT_DIR: target.projectDir,
      ...(RECORD_GOLDEN ? { UE_MCP_RECORD_GOLDEN: "1" } : {}),
    },
  },
);

if (result.error) {
  console.error(`[live] failed to run vitest: ${result.error.message}`);
  process.exit(1);
}
process.exit(result.status ?? 1);
