#!/usr/bin/env node
/**
 * Cut a release in one command (#1465).
 *
 *   node scripts/release-run.mjs --version X.Y.Z[-pre.N] --notes <notes.md> [--dry-run]
 *   node scripts/release-run.mjs --rehearsal [--since <tag>]
 *
 * 1. Checks the notes (headline frontmatter, prose rules) before anything moves.
 * 2. Bumps the version on a bump/<version> branch from origin/main and stamps the metadata.
 * 3. Selects the tests from the dependency graph (scripts/select-tests.ts, #1509):
 *    the diff since the previous release tag, walked back to the actions it reaches,
 *    then every test that sends one. A stable release runs the full suite.
 * 4. In parallel (#1592):
 *      ci        creates the draft release, pushes the branch, opens the pull request
 *                and waits for its checks
 *      binaries  once tests/ue_mcp is built: its engine packaged from that build, every other
 *                engine compiled incrementally; uploaded to the draft once it exists
 *      editor    tests/ue_mcp built and staged for the shards, then one editor on it for
 *                the lifecycle cases, smoke over the actions no behavioural test dispatches
 *                and the selected UE.MCP.* automation tests (when plugin/ changed), and the
 *                base live files; closed at the end
 *      live      every other selected live file, sharded, once the binaries compile is done
 * 5. Merges the pull request once CI and every lane above are green. The merge to
 *    main publishes.
 *
 * The binaries compile takes all the memory the machine has, so the live shards wait
 * for it, and the tests/ue_mcp editor runs beside it instead: lifecycle after live cost
 * 1.4.11-beta.9 220 s at the end of the run.
 *
 * --rehearsal runs the lanes on the checkout as it stands and publishes nothing: no bump,
 * draft, push, pull request, CI wait, upload or merge. The binaries go to
 * release-logs/rehearsal-binaries, and the plugin files changed since --since (the
 * previous release tag by default) are marked changed in the binaries host project, so
 * the compile does the work that release would. Use it to measure the run.
 *
 * Unit, integration and the release gates are not run here: the pull request's
 * CI runs them on the same commit. Each lane logs to release-logs/<lane>.log, the
 * selection goes to release-logs/selection.json, free memory to release-logs/memory.json,
 * and the timing to release-logs/release-timing.json. A run over the 600 s budget says so
 * in its summary.
 */
import { spawn, execFileSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const argv = process.argv.slice(2);
const flag = (name) => {
  const i = argv.indexOf(name);
  return i === -1 ? null : argv[i + 1];
};
const rehearsal = argv.includes("--rehearsal");
const pkgPath = path.join(ROOT, "package.json");
const version = rehearsal ? JSON.parse(fs.readFileSync(pkgPath, "utf-8")).version : flag("--version");
const notes = flag("--notes") && path.resolve(flag("--notes"));
const dryRun = argv.includes("--dry-run");
if (!version || (!notes && !rehearsal)) {
  console.error("usage: node scripts/release-run.mjs --version X.Y.Z[-pre.N] --notes <notes.md> [--dry-run]\n       node scripts/release-run.mjs --rehearsal [--since <tag>]");
  process.exit(2);
}
const logDir = path.join(ROOT, "release-logs");
fs.mkdirSync(logDir, { recursive: true });
const npm = process.platform === "win32" ? "npm.cmd" : "npm";
const node = process.execPath;
const t0 = Date.now();
const stamp = () => `${((Date.now() - t0) / 1000).toFixed(0).padStart(5)}s`;
const sh = (cmd, args, opts = {}) => (execFileSync(cmd, args, { cwd: ROOT, encoding: "utf-8", stdio: ["ignore", "pipe", "inherit"], ...opts }) ?? "").trim();
const git = (...args) => sh("git", args);

// Free memory over the run, which decides how many editors the live lane gets.
const memory = [];
const sampleMemory = () => memory.push({ s: Math.round((Date.now() - t0) / 1000), freeMB: Math.round(os.freemem() / 1048576) });
sampleMemory();
const sampler = setInterval(sampleMemory, 5000);

// What changed since the last release decides which tests run.
const head = rehearsal ? "HEAD" : "origin/main";
const previousTag = flag("--since") ?? git("describe", "--tags", "--abbrev=0", "--match", "v*", head);
const changed = git("diff", "--name-only", `${previousTag}...${head}`).split("\n").filter(Boolean);
const pluginChanged = changed.some((f) => f.startsWith("plugin/"));
const stable = !version.includes("-");
const BUDGET_S = 600;
console.log(`since ${previousTag}: ${changed.length} files; plugin ${pluginChanged ? "changed" : "unchanged"}${stable ? "; stable release" : ""}${rehearsal ? "; rehearsal, nothing is published" : ""}`);

const branch = `bump/${version}`;
if (!rehearsal) {
  // 1. Notes first: a bad headline or banned character found after the push costs a CI round.
  sh(node, ["scripts/release-headline.mjs", "--body-file", notes], { stdio: ["ignore", "ignore", "inherit"] });
  sh(node, ["scripts/lint-prose.mjs", "--release-body", notes], { stdio: ["ignore", "ignore", "inherit"] });

  // 2. The bump, on its own branch from origin/main.
  git("checkout", "-B", branch, "origin/main");
  const pkg = JSON.parse(fs.readFileSync(pkgPath, "utf-8"));
  pkg.version = version;
  fs.writeFileSync(pkgPath, `${JSON.stringify(pkg, null, 2)}\n`);
  sh(npm, ["install", "--package-lock-only", "--ignore-scripts", "--no-audit", "--no-fund"], { shell: process.platform === "win32" });
  sh(npm, ["run", "generate:metadata"], { shell: process.platform === "win32", stdio: ["ignore", "ignore", "inherit"] });
  git("add", "-A", "package.json", "package-lock.json", "plugin/", "src/", "docs/", "CLAUDE.md", "README.md");
  git("commit", "-m", `chore(release): ${version}`);
}

// 3. The selection, from the graph of the tree being released. A dry run prints it too.
const selectionFile = path.join(logDir, "selection.json");
execFileSync(node, ["--import", "tsx", "scripts/select-tests.ts", "--base", previousTag, "--head", head, "--out", selectionFile, ...(stable ? ["--stable"] : [])], { cwd: ROOT, stdio: ["ignore", "inherit", "inherit"] });
const selection = JSON.parse(fs.readFileSync(selectionFile, "utf-8"));
const runLive = selection.live.length > 0;
const runLifecycle = selection.lifecycle;
const runAutomation = selection.automation === "all" || selection.automation.length > 0;
if (dryRun) {
  console.log("dry run: stopping before the push");
  clearInterval(sampler);
  process.exit(0);
}

// 4. Lanes.
const steps = [];
function run(name, cmd, args, env = {}) {
  const logFile = path.join(logDir, `${name}.log`);
  const out = fs.openSync(logFile, "w");
  const started = Date.now();
  console.log(`${stamp()} start  ${name}`);
  return new Promise((resolve) => {
    const child = spawn(cmd, args, { cwd: ROOT, stdio: ["ignore", out, out], shell: process.platform === "win32", env: { ...process.env, ...env } });
    child.on("close", (code) => {
      fs.closeSync(out);
      const step = { name, code, startS: (started - t0) / 1000, durationS: (Date.now() - started) / 1000 };
      steps.push(step);
      console.log(`${stamp()} ${code === 0 ? "pass " : "FAIL "} ${name} (${step.durationS.toFixed(0)}s)${code === 0 ? "" : ` see ${logFile}`}`);
      resolve(code === 0);
    });
  });
}
async function lane(list) {
  for (const s of list) if (!(await run(...s))) return false;
  return true;
}
/** A step that is not a process: timed and logged like one. */
async function task(name, fn) {
  const started = Date.now();
  console.log(`${stamp()} start  ${name}`);
  let ok = true;
  let value;
  try {
    value = await fn();
  } catch (err) {
    ok = false;
    fs.writeFileSync(path.join(logDir, `${name}.log`), `${err?.stack ?? err}\n`);
  }
  steps.push({ name, code: ok ? 0 : 1, startS: (started - t0) / 1000, durationS: (Date.now() - started) / 1000 });
  console.log(`${stamp()} ${ok ? "pass " : "FAIL "} ${name} (${((Date.now() - started) / 1000).toFixed(0)}s)${ok ? "" : ` see ${path.join(logDir, `${name}.log`)}`}`);
  return ok ? value ?? true : null;
}
/** execFile without blocking the lanes already running. */
function shAsync(cmd, args) {
  return new Promise((resolve, reject) => {
    const child = spawn(cmd, args, { cwd: ROOT, stdio: ["ignore", "pipe", "pipe"] });
    let out = "";
    let err = "";
    child.stdout.on("data", (d) => { out += d; });
    child.stderr.on("data", (d) => { err += d; });
    child.on("close", (code) => (code === 0 ? resolve(out.trim()) : reject(new Error(`${cmd} ${args.join(" ")} exited ${code}: ${err.trim()}`))));
  });
}
const tryAsync = (cmd, args) => shAsync(cmd, args).catch(() => "");

// The draft before the push, so the merge always finds it; a rerun reuses the draft and the pull
// request. Off the critical path: the lanes below do not need GitHub until the upload and the merge.
const published = rehearsal ? Promise.resolve({ pr: null }) : task("publish-prep", async () => {
  if ((await tryAsync("gh", ["release", "view", `v${version}`, "--json", "isDraft", "--jq", ".isDraft"])) === "true") {
    await shAsync("gh", ["release", "edit", `v${version}`, "--notes-file", notes]);
  } else {
    await shAsync("gh", ["release", "create", `v${version}`, "--draft", "--notes-file", notes, "--title", `v${version}`]);
  }
  await shAsync("git", ["push", "-u", "origin", branch, "--force"]);
  const pr = (await tryAsync("gh", ["pr", "view", branch, "--json", "url,state", "--jq", "select(.state == \"OPEN\") | .url"]))
    || (await shAsync("gh", ["pr", "create", "--base", "main", "--head", branch, "--title", `chore(release): ${version}`, "--body", `Release ${version}.`]));
  console.log(`${stamp()} pull request ${pr}`);
  return { pr };
});

// gh pr checks fails while GitHub has not registered any check yet, so wait for the first one.
const ciGreen = async () => {
  if (rehearsal) return true;
  const prep = await published;
  if (!prep) return false;
  for (let i = 0; i < 60 && !(await tryAsync("gh", ["pr", "checks", prep.pr, "--json", "name", "--jq", ".[0].name"])); i++) {
    await new Promise((r) => setTimeout(r, 3000));
  }
  return run("ci", "gh", ["pr", "checks", prep.pr, "--watch", "--fail-fast"]);
};

/** Rehearsal only: mark the plugin files this release changed as changed in the binaries host projects. */
function replayPluginChanges() {
  const stageRoot = path.join(os.tmpdir(), "uemcpb");
  const pluginFiles = changed.filter((f) => f.startsWith("plugin/ue_mcp_bridge/"));
  const now = new Date();
  let touched = 0;
  for (const engine of fs.existsSync(stageRoot) ? fs.readdirSync(stageRoot) : []) {
    for (const f of pluginFiles) {
      const hosted = path.join(stageRoot, engine, "Host", "Plugins", "UE_MCP_Bridge", f.slice("plugin/ue_mcp_bridge/".length));
      if (fs.existsSync(hosted)) {
        fs.utimesSync(hosted, now, now);
        touched++;
      }
    }
  }
  console.log(`${stamp()} rehearsal: ${touched} host project file(s) marked changed for the binaries compile`);
}

const selected = selection.full ? [] : ["--files-from", selectionFile];
const needsBuild = pluginChanged || runLive || runLifecycle;
// The shards copy their build output from a snapshot taken here, because the lifecycle cases
// relink tests/ue_mcp and hold its DLL locked while the shards are starting.
const built = needsBuild
  ? lane([
    ["deploy", node, ["scripts/deploy.mjs"]],
    ["build", npm, ["run", "build"]],
    ...(runLive ? [["stage-build", node, ["scripts/live-tests.mjs", "--stage-build"]]] : []),
  ])
  : Promise.resolve(true);

// The engine tests/ue_mcp builds against is packaged from that build rather than compiled again:
// the duplicate compile held that engine's build tool, and every editor of that engine waits for
// it at launch (UBT -Mode=ValidatePlatforms), which stalled the tests/ue_mcp editor 236 s.
const binariesOut = ["--", "--from-test-build", ...(rehearsal ? ["--out", path.join(logDir, "rehearsal-binaries")] : [])];
if (rehearsal && pluginChanged) replayPluginChanges();
const binariesBuilt = (async () => (await built) && run("binaries", npm, ["run", "binaries:build", ...binariesOut]))();
const binaries = (async () => {
  if (!(await binariesBuilt)) return false;
  if (rehearsal) return true;
  if (!(await published)) return false;
  return run("binaries-upload", npm, ["run", "binaries:upload"]);
})();

// One editor on tests/ue_mcp, beside the binaries compile, closed at the end so the run never leaves
// an editor behind.
// Lifecycle first: it builds and restarts the editor four times, which beside the live shards took
// 375 s and lost its rebuild case to the contention; before them it shares the machine only with the
// binaries compile. The editor it leaves running serves the rest.
const editorSteps = [
  ...(runLifecycle ? [["lifecycle", node, ["scripts/live-tests.mjs", "--lifecycle"]]] : []),
  ...(pluginChanged ? [["smoke", node, ["scripts/smoke-test.js", "--uncovered"]]] : []),
  ...(pluginChanged && runAutomation ? [["automation", node, ["scripts/automation-tests.mjs", ...(selection.full ? [] : ["--filters-from", selectionFile])]]] : []),
  ...(runLive ? [["live-base", node, ["scripts/live-tests.mjs", "--kinds", "base", "--staged-build", ...selected]]] : []),
];
const editor = (async () => {
  if (!(await built)) return false;
  if (editorSteps.length === 0) return true;
  if (!(await run("up", npm, ["run", "up"], { UE_MCP_PARAM_ECHO: "1" }))) {
    await run("down", node, ["scripts/stop-editor.mjs"]);
    return false;
  }
  const ok = await lane(editorSteps);
  return (await run("down", node, ["scripts/stop-editor.mjs"])) && ok;
})();

// The live shards size themselves to the memory free, and the binaries compile takes it all while
// it runs: one beside it cost 1.4.11-beta.4 its second headless editor (884 s on one). The pool
// starts more headless editors as the tests/ue_mcp editor's memory comes back.
const live = (async () => {
  if (!runLive) return true;
  if (!(await built)) return false;
  await binariesBuilt;
  return run("live", node, ["scripts/live-tests.mjs", "--kinds", "headless,none,render,interactive,scm", "--staged-build", ...selected]);
})();

const results = await Promise.all([binaries, editor, live, ciGreen()]);

// Each exit records the timing; an overrun fails the summary, not the release, so it shows every time.
function finish(outcome, code) {
  clearInterval(sampler);
  sampleMemory();
  const totalS = (Date.now() - t0) / 1000;
  fs.writeFileSync(path.join(logDir, "memory.json"), `${JSON.stringify(memory)}\n`);
  fs.writeFileSync(path.join(logDir, "release-timing.json"), `${JSON.stringify({
    recordedAt: new Date().toISOString(),
    version,
    rehearsal,
    outcome,
    machine: {
      cpus: os.cpus().length,
      memoryGB: Math.round(os.totalmem() / 1073741824),
      freeGBAtStart: Math.round(memory[0].freeMB / 1024),
      freeGBMin: Math.round(Math.min(...memory.map((m) => m.freeMB)) / 1024),
      freeGB: Math.round(os.freemem() / 1073741824),
    },
    since: previousTag,
    pluginChanged,
    selection: {
      full: selection.full,
      fullReason: selection.fullReason,
      liveFiles: selection.live.length,
      liveEstimateS: selection.liveEstimateS,
      lifecycle: runLifecycle,
      automation: selection.automation === "all" ? "all" : selection.automation.length,
    },
    totalS,
    budgetS: BUDGET_S,
    overBudget: totalS > BUDGET_S,
    steps: steps.sort((a, b) => a.startS - b.startS),
  }, null, 2)}\n`);
  console.log(`\n${stamp()} ${outcome}`);
  if (totalS > BUDGET_S) {
    console.log(`WARNING: the ${rehearsal ? "rehearsal" : "release"} took ${totalS.toFixed(0)}s, over the ${BUDGET_S}s budget. The steps are in release-logs/release-timing.json.`);
  }
  process.exit(code);
}

if (results.some((ok) => !ok)) {
  finish(rehearsal ? "rehearsal: a lane failed" : "not merged: a lane failed. The draft and the pull request are left as they are.", 1);
}
if (rehearsal) finish("rehearsal: every lane green, nothing published", 0);

// 5. Merge; the push to main publishes.
const { pr } = await published;
sh("gh", ["pr", "merge", pr, "--squash", "--delete-branch"]);
const sha = sh("gh", ["pr", "view", pr, "--json", "mergeCommit", "--jq", ".mergeCommit.oid"]);
console.log(`${stamp()} merged as ${sha.slice(0, 8)}; CI publishes ${version} from main`);
let runId = "";
for (let i = 0; i < 30 && !runId; i++) {
  runId = sh("gh", ["run", "list", "--commit", sha, "--workflow", "CI", "--json", "databaseId", "--jq", ".[0].databaseId // empty"]);
  if (!runId) await new Promise((r) => setTimeout(r, 2000));
}
const publishedOk = runId ? await run("publish", "gh", ["run", "watch", runId, "--exit-status"]) : false;
finish(publishedOk ? `released ${version}` : "publish failed or never started, see release-logs/publish.log", publishedOk ? 0 : 1);
