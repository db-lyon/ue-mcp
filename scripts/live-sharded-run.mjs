/**
 * The sharded live run (#1436): one copy of tests/ue_mcp per shard, one editor
 * per copy, each launched with the flags its kind needs, the files split by
 * measured time, and one merged report with the time gates applied.
 *
 * Memory decides how many editors run at once. The headless shard count is
 * picked at launch from the memory free then, and every editor waits for room
 * beside the headroom before it starts, so a busy machine gets fewer editors
 * rather than an out-of-memory kill. An editor that dies mid-run is reported
 * as such, and the files it never reached run again on a fresh editor.
 */
import { spawn, execFileSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { askOnce, extractReportedProjectDir, normalizeProjectRoot, PROJECT_IDENTITY_PYTHON, LIVE_SHARD_ROOT } from "./bridge-target.mjs";
import { resolveTestEngine } from "./build-utils.js";
import { TEST_EDITOR_ARGS } from "./test-editor-args.mjs";
import {
  KIND_FLAGS, EDITOR_PEAK_MB, KEEP_IN_SHARD, liveFiles, planShards, mirror, buildOutputFilter, timingFromReports, overBudget, slowest,
  headlessShardCount, headroomMB, editorFits, splitAtDeath, undeployedSources,
} from "./live-shards.mjs";

const READY_TIMEOUT_S = 300;
const MB = 1048576;

function readJson(file, fallback) {
  try {
    return JSON.parse(fs.readFileSync(file, "utf8"));
  } catch {
    return fallback;
  }
}

const say = (msg) => console.error(`[live] ${msg}`);
const rel = (repoRoot, file) => path.relative(repoRoot, file).split(path.sep).join("/");
const freeMB = () => Math.round(os.freemem() / MB);
const totalMB = () => Math.round(os.totalmem() / MB);

/** Editors whose command line opens this project, any flags. Windows only. */
function editorPidsFor(projectDir) {
  if (process.platform !== "win32") return [];
  const out = execFileSync("powershell", ["-NoProfile", "-Command",
    "Get-CimInstance Win32_Process -Filter \"Name like 'UnrealEditor%'\" | ForEach-Object { \"$($_.ProcessId)|$($_.CommandLine)\" }"],
  { encoding: "utf8" });
  const needle = normalizeProjectRoot(path.join(projectDir, "ue_mcp.uproject"));
  return out.split(/\r?\n/).filter(Boolean).flatMap((line) => {
    const [pid, ...rest] = line.split("|");
    return normalizeProjectRoot(rest.join("|").replace(/"/g, "")).includes(needle) ? [Number(pid)] : [];
  });
}

/** Working set and private bytes of a process, in MB. */
export function processMemoryMB(pid) {
  if (process.platform !== "win32") return null;
  try {
    const out = execFileSync("powershell", ["-NoProfile", "-Command",
      `$p = Get-Process -Id ${pid}; "$($p.WorkingSet64) $($p.PrivateMemorySize64) $($p.PeakWorkingSet64)"`], { encoding: "utf8" }).trim();
    const [ws, priv, peak] = out.split(" ").map((n) => Math.round(Number(n) / MB));
    return { workingSetMB: ws, privateMB: priv, peakWorkingSetMB: peak };
  } catch {
    return null;
  }
}

function isAlive(pid) {
  if (!pid) return false;
  try {
    process.kill(pid, 0);
    return true;
  } catch {
    return false;
  }
}

/**
 * End an editor this runner launched on a shard copy, after asking it to close
 * failed. Only ever called with a pid from editorPidsFor(<shard copy>) or one
 * the runner spawned; never on an editor somebody else started.
 */
function killTree(pid) {
  try {
    if (process.platform === "win32") execFileSync("taskkill", ["/PID", String(pid), "/T", "/F"], { stdio: "ignore" }); // lint-prose-allow: no-kill
    else process.kill(pid, "SIGKILL"); // lint-prose-allow: no-kill
  } catch {
    // already gone
  }
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

/**
 * Make the shard a throwaway git repository with a bare remote, and point the
 * editor's source control at it, for the cases that submit and sync.
 */
function prepareScmShard(shardDir) {
  const remote = path.join(path.dirname(shardDir), "remote.git");
  for (const dir of [path.join(shardDir, ".git"), remote]) fs.rmSync(dir, { recursive: true, force: true });
  const git = (cwd, ...args) => execFileSync("git", args, { cwd, stdio: "pipe" }).toString().trim();
  git(path.dirname(shardDir), "init", "-q", "--bare", remote);
  git(shardDir, "init", "-q", "-b", "main");
  const excluded = ["Saved/", "Binaries/", "Intermediate/", "DerivedDataCache/", "Plugins/*/Binaries/", "Plugins/*/Intermediate/"];
  fs.writeFileSync(path.join(shardDir, ".git", "info", "exclude"), `${excluded.join("\n")}\n`);
  git(shardDir, "config", "user.name", "ue-mcp-live");
  git(shardDir, "config", "user.email", "live@ue-mcp.invalid");
  git(shardDir, "config", "core.autocrlf", "false");
  git(shardDir, "add", "-A");
  git(shardDir, "commit", "-q", "-m", "shard baseline");
  git(shardDir, "remote", "add", "origin", remote);
  git(shardDir, "push", "-q", "-u", "origin", "main");
  const gitExe = execFileSync(process.platform === "win32" ? "where" : "which", ["git"], { encoding: "utf8" }).split(/\r?\n/)[0].trim();
  const ini = path.join(shardDir, "Saved", "Config", process.platform === "win32" ? "WindowsEditor" : "LinuxEditor", "SourceControlSettings.ini");
  fs.mkdirSync(path.dirname(ini), { recursive: true });
  fs.writeFileSync(ini, [
    "[SourceControl.SourceControlSettings]", "Provider=Git", "UseGlobalSettings=False", "",
    "[GitSourceControl.GitSourceControlSettings]", `BinaryPath=${gitExe.split(path.sep).join("/")}`, "UsingGitLfsLocking=False", "",
  ].join("\r\n"));
}

/** A killed editor leaves restore data, and the next launch stops at its prompt. */
function clearRestoreData(projectDir) {
  fs.rmSync(path.join(projectDir, "Saved", "Autosaves", "PackageRestoreData.json"), { force: true });
}

/**
 * The tracked tests/ue_mcp, as git has it: what every shard starts from, so a
 * run never inherits content an earlier run or a developer left behind.
 */
function stageTrackedProject(repoRoot) {
  const stage = path.join(LIVE_SHARD_ROOT, "_tracked");
  fs.rmSync(stage, { recursive: true, force: true });
  fs.mkdirSync(stage, { recursive: true });
  const files = execFileSync("git", ["ls-files", "-z", "tests/ue_mcp"], { cwd: repoRoot });
  execFileSync("git", ["checkout-index", "--stdin", "-z", "-f", `--prefix=${stage}${path.sep}`], { cwd: repoRoot, input: files });
  return path.join(stage, "tests", "ue_mcp");
}

/** Copy the project into the shard (the base shard is tests/ue_mcp itself) and clear what would stall a launch. */
function prepareShard(job, baseDir, trackedDir) {
  if (job.kind === "base") {
    clearRestoreData(baseDir);
    return 0;
  }
  for (const pid of editorPidsFor(job.dir)) killTree(pid);
  // Each run starts from the tracked project plus this checkout's build output, with no saved state.
  fs.rmSync(path.join(job.dir, "Saved"), { recursive: true, force: true });
  let copied = mirror(trackedDir, job.dir, undefined, KEEP_IN_SHARD);
  for (const dir of ["Binaries", "Plugins"]) {
    copied += mirror(path.join(baseDir, dir), path.join(job.dir, dir), buildOutputFilter(path.join(baseDir, dir)));
  }
  if (job.kind === "scm") prepareScmShard(job.dir);
  return copied;
}

/** The bridge already answering for this project and proven to have it open, or null. */
async function answeringBridge(projectDir) {
  const lock = readJson(path.join(projectDir, "Saved", "UE_MCP_Bridge", "port.json"), {});
  if (!Number.isInteger(lock.port)) return null;
  const url = `ws://127.0.0.1:${lock.port}`;
  try {
    const reported = extractReportedProjectDir(await askOnce(url, "execute_python", { code: PROJECT_IDENTITY_PYTHON }, 60_000));
    if (normalizeProjectRoot(reported ?? "") !== normalizeProjectRoot(projectDir)) {
      throw new Error(`the editor on port ${lock.port} has ${reported} open, not ${projectDir}. Nothing was sent.`);
    }
    const caps = await askOnce(url, "get_bridge_capabilities", {}, 15_000);
    return { port: lock.port, pid: caps.pid ?? lock.pid ?? null };
  } catch (err) {
    if (/Nothing was sent/.test(String(err?.message))) throw err;
    return null;
  }
}

async function launch(job, editorExe, tsImport) {
  // The base shard attaches to an editor already running on tests/ue_mcp and leaves it running.
  if (job.kind === "base") {
    const running = await answeringBridge(job.dir);
    if (running) {
      Object.assign(job, running, { launched: false, launchS: 0, memoryAtReady: processMemoryMB(running.pid) });
      return;
    }
  }
  const uproject = path.join(job.dir, "ue_mcp.uproject");
  const { waitForEditorReady } = await tsImport("../src/editor/editor-control.ts", import.meta.url);
  const launchedAtMs = Date.now();
  const child = spawn(editorExe, [uproject, ...KIND_FLAGS[job.kind], ...TEST_EDITOR_ARGS], {
    stdio: "ignore",
    detached: true,
    windowsHide: job.kind !== "interactive",
    env: { ...process.env, UE_MCP_PARAM_ECHO: "1", UE_MCP_DIALOG_POLICY: "Restore=no" },
  });
  child.unref();
  job.pid = child.pid;
  job.launched = true;
  const ready = await waitForEditorReady(uproject, job.dir, READY_TIMEOUT_S, { launchedAtMs });
  job.launchS = (Date.now() - launchedAtMs) / 1000;
  if (!ready.ready) {
    throw new Error(`editor not ready after ${job.launchS.toFixed(0)}s: ${ready.reason ?? "no reason given"}. Check for a modal dialog in its window or log.`);
  }
  const running = await answeringBridge(job.dir);
  if (!running) throw new Error("the editor reported ready but its bridge does not answer");
  Object.assign(job, running);
  job.memoryAtReady = processMemoryMB(job.pid);
}

function runVitest(job, repoRoot) {
  const dir = path.join(LIVE_SHARD_ROOT, job.name);
  fs.mkdirSync(dir, { recursive: true });
  const report = path.join(dir, "report.json");
  const fileTiming = path.join(dir, "file-timing.json");
  const logFile = path.join(dir, "vitest.log");
  for (const f of [report, fileTiming]) fs.rmSync(f, { force: true });
  const log = fs.createWriteStream(logFile);
  return new Promise((resolve) => {
    const child = spawn(process.execPath, [
      path.join(repoRoot, "node_modules", "vitest", "vitest.mjs"), "run", ...job.files,
      "--reporter=verbose", "--reporter=json", `--outputFile.json=${report}`,
      "--reporter=./scripts/live-file-timing-reporter.mjs",
    ], {
      cwd: repoRoot,
      env: {
        ...process.env,
        ...(job.kind === "base" ? {} : { UE_MCP_LIVE_SHARD_DIR: job.dir }),
        UE_MCP_LIVE_HOST: "127.0.0.1",
        UE_MCP_LIVE_PORT: String(job.port),
        UE_MCP_LIVE_PROJECT_DIR: job.dir,
        UE_MCP_LIVE_EDITOR_KIND: job.kind,
        UE_MCP_LIVE_FILE_TIMING: fileTiming,
        ...(job.kind === "scm" ? { UE_MCP_LIVE_SCM: "1" } : {}),
        FORCE_COLOR: "0",
        NO_COLOR: "1",
      },
      stdio: ["ignore", "pipe", "pipe"],
    });
    const relay = (data) => {
      log.write(data);
      for (const line of String(data).split(/\r?\n/)) {
        if (/^\s*(×|✗|FAIL)\s/.test(line)) process.stderr.write(`[${job.name}] ${line.trim()}\n`);
      }
    };
    child.stdout.on("data", relay);
    child.stderr.on("data", (d) => log.write(d));
    child.on("close", (code) => {
      log.end();
      resolve({ code, report: readJson(report, null), fileTimes: readJson(fileTiming, {}) });
    });
  });
}

async function stop(job, tsImport) {
  if (!job.launched) return;
  const { stopEditor } = await tsImport("../src/editor/editor-control.ts", import.meta.url);
  try {
    await stopEditor(job.dir);
  } catch {
    // fall through to the kill below
  }
  for (const pid of editorPidsFor(job.dir)) killTree(pid);
  clearRestoreData(job.dir);
}

/** UTC log stamp, as the engine writes it: [2026.10.23-19.25.32:762]. */
function parseLogStamp(stamp) {
  const m = /(\d{4})\.(\d{2})\.(\d{2})-(\d{2})\.(\d{2})\.(\d{2}):(\d{3})/.exec(stamp);
  return m ? Date.UTC(+m[1], +m[2] - 1, +m[3], +m[4], +m[5], +m[6], +m[7]) : null;
}

/** When and why an editor died, read from its own log. */
function deathOf(projectDir) {
  const logFile = path.join(projectDir, "Saved", "Logs", "ue_mcp.log");
  if (!fs.existsSync(logFile)) return { atMs: Date.now(), reason: "it exited without a log" };
  const text = fs.readFileSync(logFile, "utf8");
  const crash = /^\[([^\]]+)\]\[[^\]]*\]LogWindows: Error: (?:appError called: )?(.*(?:Assertion failed|Fatal error|Unhandled Exception|Ran out of memory|Out of memory|EXCEPTION_).*)$/m.exec(text);
  if (crash) return { atMs: parseLogStamp(crash[1]) ?? fs.statSync(logFile).mtimeMs, reason: `it crashed: ${crash[2].trim().slice(0, 240)}` };
  const exit = /^\[([^\]]+)\]\[[^\]]*\]LogCore: Engine exit requested \(reason: ([^)]*)\)/m.exec(text);
  if (exit) return { atMs: parseLogStamp(exit[1]) ?? fs.statSync(logFile).mtimeMs, reason: `it quit: its log records an exit request (${exit[2]})` };
  return {
    atMs: fs.statSync(logFile).mtimeMs,
    reason: "it exited with no crash in its log, which is what an out-of-memory kill looks like",
  };
}

/** Memory the running editors have yet to take: all of a launching one's peak, the rest of a ready one's. */
function pendingMB(running) {
  let sum = 0;
  for (const job of running) {
    const peak = EDITOR_PEAK_MB[job.kind] ?? EDITOR_PEAK_MB.headless;
    sum += job.memoryAtReady ? Math.max(0, peak - (job.memoryAtReady.privateMB ?? peak)) : peak;
  }
  return sum;
}

/**
 * Hold a launch until one more editor of this kind fits beside the headroom
 * and what the editors already starting will still take. The first editor
 * always starts, so a small machine still runs the suite.
 */
async function waitForRoom(kind, running) {
  const started = Date.now();
  let told = false;
  while (running.size > 0 && !editorFits(kind, freeMB() - pendingMB(running), totalMB())) {
    if (!told && Date.now() - started > 5000) {
      say(`  waiting for memory: ${freeMB()} MB free, a ${kind} editor needs ${EDITOR_PEAK_MB[kind]} MB beside ${headroomMB(totalMB())} MB headroom`);
      told = true;
    }
    await sleep(2000);
  }
}

/**
 * @param {{ repoRoot: string, headless?: number|null, render: number, interactive: number, scm: number,
 *   files?: string[]|null, onlyShard?: string|null, recordTiming: boolean, exeKind: "editor"|"cmd" }} opts
 */
export async function runSharded(opts) {
  const started = Date.now();
  const { repoRoot } = opts;
  const { tsImport } = await import("tsx/esm/api");
  const baseDir = path.join(repoRoot, "tests", "ue_mcp");
  const binary = path.join(baseDir, "Plugins", "UE_MCP_Bridge", "Binaries", "Win64", "UnrealEditor-UE_MCP_Bridge.dll");
  if (process.platform === "win32" && !fs.existsSync(binary)) {
    throw new Error(`No built bridge at ${binary}. Run node scripts/deploy.mjs and npm run build first.`);
  }
  // A bridge built from older source than the deployed copy makes the run test
  // stale code, and the Live Coding test then compiles the difference, which
  // reads as a slow action against its budget.
  const undeployed = undeployedSources(path.join(repoRoot, "plugin", "ue_mcp_bridge", "Source"), path.join(baseDir, "Plugins", "UE_MCP_Bridge", "Source"));
  if (undeployed.length > 0) {
    throw new Error(`tests/ue_mcp has an older bridge than plugin/ (${undeployed.slice(0, 3).join(", ")}${undeployed.length > 3 ? ` and ${undeployed.length - 3} more` : ""}). Run node scripts/deploy.mjs and npm run build first.`);
  }
  const { checkPluginFreshness } = await tsImport("../src/editor/bridge-freshness.ts", import.meta.url);
  const freshness = checkPluginFreshness(path.join(baseDir, "ue_mcp.uproject"));
  if (freshness.stale) {
    throw new Error(`The tests/ue_mcp bridge binary is older than its source (${path.basename(freshness.newestSourcePath ?? "")} changed after the build). Run npm run build first.`);
  }
  const engine = resolveTestEngine();
  const editorExe = opts.exeKind === "cmd"
    ? path.join(path.dirname(engine.editorExecutable), path.basename(engine.editorExecutable).replace(/\.exe$/i, "-Cmd.exe"))
    : engine.editorExecutable;

  const timingFile = path.join(repoRoot, "tests", "live", "timing-baseline.json");
  const budgetsFile = path.join(repoRoot, "tests", "live", "budgets.json");
  const baseline = readJson(timingFile, { files: {}, tests: {} });
  const budgets = readJson(budgetsFile, { wallClockS: 600, defaultTestMs: 30_000, tests: {} });
  let entries = liveFiles(repoRoot);
  if (opts.files?.length) entries = entries.filter((e) => opts.files.some((f) => e.file.includes(f)));

  const otherKinds = [...new Set(entries.map((e) => e.kind))].filter((t) => ["base", "render", "interactive", "scm"].includes(t));
  const memoryAtStart = { freeMB: freeMB(), totalMB: totalMB() };
  const fitted = headlessShardCount({ ...memoryAtStart, otherKinds });
  const headless = opts.headless ? Math.min(opts.headless, fitted) : fitted;
  let plan = planShards(entries, baseline, { ...opts, headless });
  if (opts.onlyShard) {
    plan = plan.filter((s) => s.name === opts.onlyShard);
    if (plan.length === 0) throw new Error(`no shard named ${opts.onlyShard}`);
  }

  say(`sharded run: ${memoryAtStart.freeMB} MB free of ${memoryAtStart.totalMB} MB, ${headroomMB(memoryAtStart.totalMB)} MB kept free for the rest of the machine`);
  say(`  ${headless} headless shard(s)${opts.headless && opts.headless > fitted ? ` (asked for ${opts.headless}; ${fitted} fit)` : ""}, ${editorExe}`);
  for (const s of plan) say(`  ${s.name.padEnd(4)} ${s.kind.padEnd(11)} ${String(s.files.length).padStart(3)} files, ~${Math.round(s.weightMs / 1000)}s measured`);

  const trackedDir = stageTrackedProject(repoRoot);
  const queue = plan.map((s) => ({ ...s, dir: s.kind === "base" ? baseDir : path.join(LIVE_SHARD_ROOT, s.name, "ue_mcp") }));
  const jobs = [];
  const results = new Map();
  const fileTimes = {};
  const deaths = [];
  const launchFailures = [];
  const running = new Set();
  let gate = Promise.resolve();

  const runJob = async (job) => {
    // Launches take turns at the memory check; each reserves its peak until it is ready.
    const turn = gate.then(async () => {
      await waitForRoom(job.kind, running);
      job.memoryAtReady = null;
      running.add(job);
      job.queuedS = (Date.now() - started) / 1000;
    });
    gate = turn;
    try {
      await turn;
      job.copied = prepareShard(job, baseDir, trackedDir);
      await launch(job, editorExe, tsImport);
    } catch (err) {
      launchFailures.push(`${job.name}: ${err?.message ?? err}`);
      running.delete(job);
      await stop({ ...job, launched: true }, tsImport);
      return;
    }
    jobs.push(job);
    say(`  ${job.name} ${job.launched ? `ready in ${job.launchS.toFixed(1)}s` : "attached to the running editor"} on port ${job.port}, ${job.memoryAtReady?.privateMB ?? "?"} MB, ${freeMB()} MB free`);
    const t = Date.now();
    const { code, report, fileTimes: times } = await runVitest(job, repoRoot);
    Object.assign(fileTimes, times);
    job.testsS = (Date.now() - t) / 1000;
    job.memoryAtEnd = processMemoryMB(job.pid);
    const died = !isAlive(job.pid);
    for (const r of report?.testResults ?? []) results.set(rel(repoRoot, r.name), { ...r, shard: job.name });
    if (!report) job.error = `vitest exited ${code} without a report`;
    if (died) {
      const death = deathOf(job.dir);
      const { culprit, unrun } = splitAtDeath(job.files, results, death.atMs);
      for (const f of unrun) results.delete(f);
      deaths.push({ shard: job.name, reason: death.reason, culprit, unrun: unrun.length });
      say(`  ${job.name}: the editor died, ${death.reason}${culprit ? ` (while running ${culprit})` : ""}. ${unrun.length} file(s) not run; running them on a fresh editor.`);
      running.delete(job);
      clearRestoreData(job.dir);
      if (unrun.length) await runJob({ ...job, name: `${job.name}.${deaths.length}`, files: unrun, launched: false, pid: null, port: null });
      return;
    }
    await stop(job, tsImport);
    running.delete(job);
  };
  await Promise.all(queue.map((job) => runJob(job)));
  const wallS = (Date.now() - started) / 1000;

  const merged = { testResults: [...results.values()] };
  const timing = timingFromReports([merged], repoRoot, fileTimes);
  const all = merged.testResults.flatMap((f) => (f.assertionResults ?? []).map((a) => ({ file: rel(repoRoot, f.name), a })));
  const totals = {
    passed: all.filter((x) => x.a.status === "passed").length,
    failed: all.filter((x) => x.a.status === "failed").length,
    skipped: all.filter((x) => ["skipped", "pending", "todo"].includes(x.a.status)).length,
  };
  const failed = all.filter((x) => x.a.status === "failed").map((x) => `${x.file} > ${x.a.fullName}`);
  const fileErrors = merged.testResults
    .filter((f) => f.status === "failed" && (f.assertionResults ?? []).every((a) => a.status !== "failed"))
    .map((f) => `${rel(repoRoot, f.name)}: ${String(f.message ?? "failed outside a test").split("\n")[0]}`);
  const plannedFiles = queue.flatMap((s) => s.files);
  const neverRan = plannedFiles.filter((f) => !results.has(f));
  const over = overBudget(timing.tests, budgets);
  const wallOver = !opts.onlyShard && !opts.files?.length && wallS > budgets.wallClockS;

  const record = {
    recordedAt: new Date().toISOString(),
    machine: { cpus: os.cpus().length, memoryAtStart },
    wallS,
    shards: jobs.map((j) => ({
      name: j.name, kind: j.kind, files: j.files.length, plannedMs: j.weightMs, queuedS: j.queuedS,
      launchS: j.launchS, testsS: j.testsS, memoryAtReady: j.memoryAtReady, memoryAtEnd: j.memoryAtEnd,
    })),
    deaths, totals, ...timing,
  };
  fs.mkdirSync(LIVE_SHARD_ROOT, { recursive: true });
  fs.writeFileSync(path.join(LIVE_SHARD_ROOT, "last-run.json"), `${JSON.stringify(record, null, 2)}\n`);
  if (opts.recordTiming && !opts.onlyShard && !opts.files?.length) fs.writeFileSync(timingFile, `${JSON.stringify(record, null, 2)}\n`);

  console.error("");
  say(`wall clock ${wallS.toFixed(0)}s across ${jobs.length} editor run(s)`);
  for (const j of jobs) {
    say(`  ${j.name.padEnd(6)} ${j.kind.padEnd(11)} start ${j.queuedS.toFixed(0)}s, launch ${j.launchS.toFixed(0)}s, tests ${j.testsS?.toFixed(0) ?? "?"}s, peak ${j.memoryAtEnd?.peakWorkingSetMB ?? "?"} MB`);
  }
  say(`tests: ${totals.passed} passed, ${totals.failed} failed, ${totals.skipped} skipped`);
  say("slowest tests:");
  for (const t of slowest(timing.tests, 15)) say(`  ${(t.ms / 1000).toFixed(1).padStart(6)}s  ${t.test}`);
  const problems = [
    ...launchFailures.map((f) => `launch: ${f}`),
    ...deaths.map((d) => `editor died: shard ${d.shard}, ${d.reason}${d.culprit ? `, while running ${d.culprit}` : ""}`),
    ...jobs.filter((j) => j.error).map((j) => `${j.name}: ${j.error}`),
    ...neverRan.map((f) => `not run: ${f}`),
    ...fileErrors.map((f) => `file: ${f}`),
    ...failed.map((f) => `failed: ${f}`),
    ...(totals.skipped ? [`skipped: ${totals.skipped} test(s) did not run; the suite runs every test`] : []),
    ...over.map((o) => `over budget: ${o.test} took ${(o.ms / 1000).toFixed(1)}s, budget ${(o.limit / 1000).toFixed(0)}s`),
    ...(wallOver ? [`over budget: the run took ${wallS.toFixed(0)}s, budget ${budgets.wallClockS}s`] : []),
  ];
  if (problems.length) {
    say(`${problems.length} problem(s):`);
    for (const p of problems) say(`  ${p}`);
    say(`per-shard output: ${path.join(LIVE_SHARD_ROOT, "<shard>", "vitest.log")}`);
    return 1;
  }
  say("every shard green, within budget.");
  return 0;
}
