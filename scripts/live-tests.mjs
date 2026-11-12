/**
 * The live tests (#817, plan items 1.10 and 7.3; sharded in #1436).
 *
 *     npm run test:live
 *
 * By default the run is sharded: scripts/live-sharded-run.mjs copies
 * tests/ue_mcp once per shard, launches an editor on each with the flags its
 * files declare (`// @live-editor <kind>`), splits the files by measured
 * time, and merges the results, reporting the run's time goals. It starts and
 * stops those editors itself, and never touches any other project.
 *
 * Attach mode drives the tests/ue_mcp editor that is already running, through
 * the preflight below: it finds the bridge, proves the editor has tests/ue_mcp
 * open, prints what it found, and only then hands over to vitest. It never
 * starts or stops that editor, except in --lifecycle.
 *
 * Flags:
 *   --shards <n>      Most headless editors; fewer start when memory is short.
 *   --shard <name>    Run one shard of the plan (r1, i1, b1, s1), or `headless` for the headless pool.
 *   --kinds a,b       Run only the files of these kinds (headless, none, base, render, interactive, scm).
 *   --stage-build     Snapshot tests/ue_mcp's build output for the shards to copy, and exit.
 *   --staged-build    Copy the snapshot --stage-build took instead of taking one now. The release
 *                     runner stages after its build, because the lifecycle cases it runs beside the
 *                     shards relink tests/ue_mcp and hold its DLL locked.
 *   --files a,b       Run only files whose path contains one of these.
 *   --files-from <f>  Run only the files a selection lists: the JSON scripts/select-tests.ts
 *                     writes (its `live` array), or one path per line. An empty list runs nothing.
 *   --record-timing   Make this full run the committed timing baseline.
 *   --attach          Attach mode for the whole suite.
 *   --record-golden   Re-record tests/golden/editor-connected.json instead of
 *                     asserting against it, in attach mode. Review the diff.
 *   --only <file>     Run one file under tests/live, in attach mode.
 *   --lifecycle       Run only tests/live/editor-lifecycle.test.ts, which stops,
 *                     crashes, builds and restarts the tests/ue_mcp editor and
 *                     leaves one running at the end (#1223). Part of the
 *                     release check, never of the default run.
 */
import { spawnSync } from "node:child_process";
import * as fs from "node:fs";
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
const LIFECYCLE = argv.includes("--lifecycle");
const LIFECYCLE_FILE = "tests/live/editor-lifecycle.test.ts";

const flagValue = (name) => {
  const i = argv.indexOf(name);
  return i !== -1 ? argv[i + 1] : null;
};
/** The live files a --files-from list names, or null without one. */
function filesFrom(file) {
  if (!file) return null;
  const text = fs.readFileSync(file, "utf8");
  if (text.trimStart().startsWith("{")) return JSON.parse(text).live ?? [];
  return text.split(/\r?\n/).map((l) => l.trim()).filter(Boolean);
}
const FILES = flagValue("--files-from") ? filesFrom(flagValue("--files-from")) : flagValue("--files")?.split(",").filter(Boolean) ?? null;
if (FILES && FILES.length === 0 && !LIFECYCLE) {
  console.error("[live] the selection lists no live files, so nothing runs.");
  process.exit(0);
}

// Attach mode drives the tests/ue_mcp editor already running; the default
// launches one editor per shard on its own copy of the project.
const ATTACH = argv.includes("--attach") || LIFECYCLE || RECORD_GOLDEN || ONLY !== null;
// The most headless shards to run; fewer start when free memory says so.
const SHARDS_FLAG = flagValue("--shards") ?? process.env.UE_MCP_LIVE_SHARDS ?? null;
const HEADLESS_SHARDS = SHARDS_FLAG === null ? null : Number(SHARDS_FLAG);

if (argv.includes("--stage-build")) {
  const { stageBuildOutput, STAGED_BUILD_DIR } = await import("./live-sharded-run.mjs");
  const copied = stageBuildOutput(REPO_ROOT);
  console.error(`[live] staged tests/ue_mcp's build output at ${STAGED_BUILD_DIR} (${copied} file(s) copied)`);
  process.exit(0);
}

if (!ATTACH) {
  const { runSharded } = await import("./live-sharded-run.mjs");
  try {
    process.exit(await runSharded({
      repoRoot: REPO_ROOT,
      headless: HEADLESS_SHARDS,
      kinds: flagValue("--kinds")?.split(",").filter(Boolean) ?? null,
      stagedBuild: argv.includes("--staged-build"),
      render: Number(flagValue("--render-shards") ?? 1),
      interactive: Number(flagValue("--interactive-shards") ?? 1),
      scm: 1,
      files: FILES,
      onlyShard: flagValue("--shard"),
      recordTiming: argv.includes("--record-timing"),
      exeKind: flagValue("--editor-exe") === "cmd" ? "cmd" : "editor",
    }));
  } catch (err) {
    console.error(`
[live] ${err instanceof Error ? err.message : err}
`);
    process.exit(1);
  }
}

// The attached editor shares the developer's per-user layout; close what the run opens in it.
const { guardUserLayoutsUntilExit } = await import("./user-layout-guard.mjs");
guardUserLayoutsUntilExit((msg) => console.error(`[live] ${msg}`));

const HOST = process.env.UE_MCP_LIVE_HOST ?? process.env.UE_MCP_TEST_HOST ?? "127.0.0.1";
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
console.error(
  LIFECYCLE
    ? "[live] --lifecycle: the cases below stop, crash, build and restart this editor, and leave it running.\n"
    : "[live] nothing below starts or stops an editor.\n",
);

const targets = LIFECYCLE ? [LIFECYCLE_FILE] : ONLY ? [ONLY] : FILES ?? ["tests/live"];
// Every run records its timing; --record-timing on a full run makes it the baseline.
const attachReport = path.join(REPO_ROOT, "tests", ".shards", "attach-report.json");
fs.mkdirSync(path.dirname(attachReport), { recursive: true });
fs.rmSync(attachReport, { force: true });
const attachFileTiming = path.join(REPO_ROOT, "tests", ".shards", "attach-file-timing.json");
fs.rmSync(attachFileTiming, { force: true });
const attachStarted = Date.now();
const result = spawnSync(
  process.execPath,
  [path.join(REPO_ROOT, "node_modules", "vitest", "vitest.mjs"), "run", ...targets,
    "--reporter=verbose", "--reporter=json", `--outputFile.json=${attachReport}`,
    "--reporter=./scripts/live-file-timing-reporter.mjs"],
  {
    cwd: REPO_ROOT,
    stdio: "inherit",
    env: {
      ...process.env,
      UE_MCP_LIVE_HOST: HOST,
      UE_MCP_LIVE_PORT: String(target.port),
      UE_MCP_LIVE_PROJECT_DIR: target.projectDir,
      UE_MCP_LIVE_FILE_TIMING: attachFileTiming,
      ...(RECORD_GOLDEN ? { UE_MCP_RECORD_GOLDEN: "1" } : {}),
      ...(LIFECYCLE ? { UE_MCP_LIVE_LIFECYCLE: "1" } : {}),
    },
  },
);

if (result.error) {
  console.error(`[live] failed to run vitest: ${result.error.message}`);
  process.exit(1);
}
{
  const { timingFromReports, slowest } = await import("./live-shards.mjs");
  const report = fs.existsSync(attachReport) ? JSON.parse(fs.readFileSync(attachReport, "utf8")) : null;
  if (report) {
    const wallS = (Date.now() - attachStarted) / 1000;
    const record = { recordedAt: new Date().toISOString(), mode: "attach", wallS, ...timingFromReports([report], REPO_ROOT, fs.existsSync(attachFileTiming) ? JSON.parse(fs.readFileSync(attachFileTiming, "utf8")) : {}) };
    fs.writeFileSync(path.join(REPO_ROOT, "tests", ".shards", "last-run.json"), `${JSON.stringify(record, null, 2)}
`);
    if (argv.includes("--record-timing") && targets[0] === "tests/live") {
      fs.writeFileSync(path.join(REPO_ROOT, "tests", "live", "timing-baseline.json"), `${JSON.stringify(record, null, 2)}
`);
    }
    console.error(`[live] wall clock ${wallS.toFixed(0)}s; slowest tests:`);
    for (const t of slowest(record.tests, 10)) console.error(`[live]   ${(t.ms / 1000).toFixed(1).padStart(6)}s  ${t.test}`);
  }
}
process.exit(result.status ?? 1);
