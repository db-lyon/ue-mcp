/**
 * Planning and bookkeeping for the sharded live run (#1436).
 *
 * Every file under tests/live declares the editor it needs on a header line:
 *
 *     // @live-editor headless | render | interactive | none
 *
 *   headless     -nullrhi. The default for anything that does not draw.
 *   base         headless, on tests/ue_mcp itself rather than a copy, for the
 *                cases that need its build intermediates (Live Coding). Only
 *                those files run there.
 *   render       -RenderOffScreen: renders and compiles shaders, no window.
 *   interactive  a full editor, for dialogs, tabs and viewport clients.
 *   scm          headless, in an editor whose project is a throwaway git
 *                repository with the Git provider on.
 *   none         needs no editor at all (the matrix and coverage checks).
 *   lifecycle    stops and starts its editor; runs only under --lifecycle.
 *
 * The runner gives each kind its own editors, splits a kind's files across
 * them by measured time, and merges what they report.
 */
import { execFileSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";

export const EDITOR_KINDS = ["headless", "base", "render", "interactive", "scm", "none", "lifecycle"];

/** Editor command line per kind. */
export const KIND_FLAGS = {
  headless: ["-nullrhi", "-unattended", "-nosplash", "-nosound"],
  render: ["-RenderOffScreen", "-unattended", "-nosplash", "-nosound"],
  interactive: ["-RenderOffScreen", "-nosplash", "-nosound"],
  scm: ["-nullrhi", "-unattended", "-nosplash", "-nosound"],
  base: ["-nullrhi", "-unattended", "-nosplash", "-nosound"],
};

const KIND_LINE = /^\s*\/\/\s*@live-editor\s+(\S+)\s*$/m;

/** The kind a test file declares, or null when it declares none. */
export function readKind(source) {
  const head = source.split(/\r?\n/).slice(0, 40).join("\n");
  const m = KIND_LINE.exec(head);
  return m ? m[1] : null;
}

/** Every live test file with its kind. Throws on a missing or unknown kind. */
export function liveFiles(repoRoot) {
  const dir = path.join(repoRoot, "tests", "live");
  const files = fs.readdirSync(dir).filter((n) => n.endsWith(".test.ts")).sort();
  const bad = [];
  const out = files.map((name) => {
    const rel = `tests/live/${name}`;
    const kind = readKind(fs.readFileSync(path.join(dir, name), "utf8"));
    if (!EDITOR_KINDS.includes(kind ?? "")) bad.push(`${rel}: ${kind ?? "no // @live-editor line"}`);
    return { file: rel, kind };
  });
  if (bad.length) throw new Error(`Live test files without a valid kind (${EDITOR_KINDS.join(", ")}):\n  ${bad.join("\n  ")}`);
  return out;
}

/** Per-file weights in ms from a timing record; files it lacks get the median. */
export function fileWeights(files, timing) {
  const known = new Map(Object.entries(timing?.files ?? {}).map(([f, t]) => [f, Number(t.durationMs) || 0]));
  const values = [...known.values()].sort((a, b) => a - b);
  const median = values.length ? values[Math.floor(values.length / 2)] : 10_000;
  return new Map(files.map((f) => [f, known.has(f) ? known.get(f) : median]));
}

/**
 * Split files across `count` shards, longest first onto the lightest shard.
 * Deterministic: ties break on file name.
 */
export function balance(files, weights, count, seeds = []) {
  const shards = Array.from({ length: Math.max(1, count) }, (_, i) => ({
    files: [...(seeds[i] ?? [])],
    weightMs: (seeds[i] ?? []).reduce((sum, f) => sum + (weights.get(f) ?? 0), 0),
  }));
  const order = [...files].sort((a, b) => (weights.get(b) ?? 0) - (weights.get(a) ?? 0) || a.localeCompare(b));
  for (const f of order) {
    const lightest = shards.reduce((min, s) => (s.weightMs < min.weightMs ? s : min), shards[0]);
    lightest.files.push(f);
    lightest.weightMs += weights.get(f) ?? 0;
  }
  return shards;
}

/**
 * The whole plan: shards per kind, named h1.., b1, r1.., i1.., s1. The base
 * shard is tests/ue_mcp itself and holds only the files that need its build
 * intermediates, since it carries whatever state the checkout has. `none`
 * files run with the headless ones, since they need no editor.
 */
export function planShards(entries, timing, counts) {
  const weights = fileWeights(entries.map((e) => e.file), timing);
  const byKind = (t) => entries.filter((e) => e.kind === t).map((e) => e.file);
  const plan = [];
  const add = (name, kind, shard) => {
    if (shard.files.length) plan.push({ name, kind, files: [...shard.files].sort(), weightMs: shard.weightMs });
  };
  const pool = [...byKind("headless"), ...byKind("none")];
  if (pool.length) {
    balance(pool, weights, Math.min(Math.max(1, counts.headless ?? 1), pool.length)).forEach((shard, i) => add(`h${i + 1}`, "headless", shard));
  }
  const prefix = { base: "b", render: "r", interactive: "i", scm: "s" };
  for (const kind of ["base", "render", "interactive", "scm"]) {
    const files = byKind(kind);
    if (files.length === 0) continue;
    const count = kind === "base" ? 1 : Math.min(counts[kind] ?? 1, files.length);
    balance(files, weights, count).forEach((shard, i) => add(`${prefix[kind]}${i + 1}`, kind, shard));
  }
  return plan;
}

/**
 * The headless and `none` files, longest first: the queue the headless editors
 * pull from. A fixed split by measured time left one editor 120 s behind the
 * others when its files ran slower than recorded (1.4.11-beta.9), and could
 * not use an editor started late, once memory freed (#1592).
 */
export function poolQueue(entries, timing) {
  const files = entries.filter((e) => e.kind === "headless" || e.kind === "none").map((e) => e.file);
  const weights = fileWeights(files, timing);
  return { files: [...files].sort((a, b) => weights.get(b) - weights.get(a) || a.localeCompare(b)), weights };
}

/** The other kinds keep their own editors, split by measured time as before. */
export function planFixedShards(entries, timing, counts) {
  return planShards(entries.filter((e) => e.kind !== "headless" && e.kind !== "none"), timing, counts);
}

/**
 * Measured work one headless editor runs before the pool replaces it with a fresh one. A delete
 * scans every loaded object for referencers, and an editor that has run many files has many: one
 * that ran all 125 headless files took 1534 s for 614 s measured, its deletes grown from 8 ms to
 * 6 s each, most of its time (#1592). A launch costs about 30 s.
 */
export const POOL_EDITOR_WORK_MS = 150_000;

/** Work an editor launch has to be worth, in measured ms: a launch and a stop cost about 30 s. */
export const POOL_MIN_SHARE_MS = 60_000;

/**
 * Whether the pool should start another headless editor: files are queued, the
 * cap allows it, and each editor would still get a share worth its launch.
 */
export function poolWantsEditor({ remainingMs, queued, editors, max }) {
  if (queued === 0 || editors >= max) return false;
  return editors === 0 || remainingMs / (editors + 1) >= POOL_MIN_SHARE_MS;
}

/**
 * The next files for one headless editor, off the front of the queue. Batches
 * shrink as the queue drains, so the editors finish together: a share of what
 * is left, between 20 s and 120 s of measured time, and at least one file.
 */
export function nextBatch(queue, weights, editors, capMs = 120_000) {
  const remaining = queue.reduce((sum, f) => sum + (weights.get(f) ?? 0), 0);
  const target = Math.min(capMs, Math.max(20_000, remaining / (2 * Math.max(1, editors))));
  const batch = [];
  let ms = 0;
  while (queue.length > 0 && (batch.length === 0 || ms < target)) {
    const f = queue.shift();
    batch.push(f);
    ms += weights.get(f) ?? 0;
  }
  return { files: batch, weightMs: ms };
}

/** One test's identity in timing records and budgets. */
export function testKey(file, fullName) {
  return `${file} > ${fullName}`;
}

/**
 * Fold vitest JSON reports into one timing record.
 * @returns {{ files: Record<string, { durationMs: number }>, tests: Record<string, number> }}
 */
export function timingFromReports(reports, repoRoot, fileTimes = {}) {
  /** @type {Record<string, { durationMs: number }>} */
  const files = {};
  /** @type {Record<string, number>} */
  const tests = {};
  for (const report of reports) {
    for (const r of report.testResults ?? []) {
      const file = path.relative(repoRoot, r.name).split(path.sep).join("/");
      // The file reporter's time covers hooks and setup too; the JSON report's covers tests only.
      files[file] = { durationMs: fileTimes[file] ?? Math.max(0, (r.endTime ?? 0) - (r.startTime ?? 0)) };
      for (const a of r.assertionResults ?? []) {
        if (a.status === "skipped" || a.status === "pending" || a.status === "todo") continue;
        tests[testKey(file, a.fullName ?? [...(a.ancestorTitles ?? []), a.title].join(" > "))] = Math.round(a.duration ?? 0);
      }
    }
  }
  return { files, tests };
}

/** Tests over their budget: the per-test override, else the default. */
export function overBudget(tests, budgets) {
  const out = [];
  for (const [key, ms] of Object.entries(tests)) {
    const limit = budgets.tests?.[key] ?? budgets.defaultTestMs;
    if (ms > limit) out.push({ test: key, ms, limit });
  }
  return out.sort((a, b) => b.ms - a.ms);
}

/** The slowest tests, longest first. */
export function slowest(tests, n = 15) {
  return Object.entries(tests).sort((a, b) => b[1] - a[1]).slice(0, n).map(([test, ms]) => ({ test, ms }));
}

/** Build output copied into a shard beside the tracked project: binaries and the deployed plugin, no intermediates. */
export function buildOutputFilter(srcRoot) {
  return (src) => !path.relative(srcRoot, src).split(path.sep).includes("Intermediate");
}

/**
 * What the tracked-tree mirror leaves at a shard's root: the build output,
 * which is mirrored in from the checkout's own tests/ue_mcp separately.
 */
export const KEEP_IN_SHARD = new Set(["Binaries", "Plugins"]);

/**
 * Mirror `src` into `dst`: copy what is new or changed, delete what `src` no
 * longer has, except the `keep` names at the root. Size and mtime decide
 * "changed", so a second run copies little.
 */
export function mirror(src, dst, filter = () => true, keep = new Set()) {
  let copied = 0;
  const walk = (s, d) => {
    if (!filter(s)) return;
    const st = fs.statSync(s);
    if (st.isDirectory()) {
      fs.mkdirSync(d, { recursive: true });
      const want = new Set(fs.readdirSync(s).filter((n) => filter(path.join(s, n))));
      for (const n of fs.readdirSync(d)) {
        if (!want.has(n) && !(d === dst && keep.has(n))) fs.rmSync(path.join(d, n), { recursive: true, force: true });
      }
      for (const n of want) walk(path.join(s, n), path.join(d, n));
      return;
    }
    const have = fs.existsSync(d) ? fs.statSync(d) : null;
    if (have && have.size === st.size && Math.abs(have.mtimeMs - st.mtimeMs) < 2000) return;
    fs.copyFileSync(s, d);
    fs.utimesSync(d, st.atime, st.mtime);
    copied++;
  };
  walk(src, dst);
  return copied;
}

/**
 * Peak resident memory per editor, by kind, in MB, rounded up from the peak
 * working sets the runner records in tests/.shards/last-run.json (render 4.48 GB,
 * interactive 4.15 GB, base and scm 3.21 GB). The runner reads free memory at
 * launch and never starts an editor that would not fit beside the headroom.
 */
export const EDITOR_PEAK_MB = { headless: 3600, base: 3300, scm: 3300, render: 4600, interactive: 4300 };

/**
 * The order editors take turns at the memory check: the longest shard first,
 * so the one that ends the run is not the last to find room.
 */
export function launchOrder(plan) {
  return [...plan].sort((a, b) => b.weightMs - a.weightMs || a.name.localeCompare(b.name));
}

/** Memory left for everything else on the machine: at least 4 GB, or 12% of it. */
export function headroomMB(totalMB) {
  return Math.max(4096, Math.round(totalMB * 0.12));
}

/** Whether one more editor of this kind fits in the memory free now. */
export function editorFits(kind, freeMB, totalMB) {
  return freeMB - (EDITOR_PEAK_MB[kind] ?? EDITOR_PEAK_MB.headless) >= headroomMB(totalMB);
}

/** A failure that is only the editor being gone: the file never got to run against it. */
export const TRANSPORT_FAILURE = /Could not reach the UE MCP bridge|connection lost|ECONNREFUSED|not connected|CONNECTION_LOST|NOT_CONNECTED/i;

/** Whether a vitest file result failed only because no editor answered. */
export function failedOnTransport(result) {
  if (!result || result.status !== "failed") return false;
  const messages = [result.message, ...(result.assertionResults ?? []).flatMap((a) => a.failureMessages ?? [])].filter(Boolean);
  return messages.length > 0 && messages.every((m) => TRANSPORT_FAILURE.test(String(m)));
}

/**
 * After an editor died mid-run: the file it died in, and the files that never
 * ran against it. A file never ran when it has no result, failed only on the
 * missing editor, or started after the death.
 */
export function splitAtDeath(files, results, diedAtMs) {
  const unrun = [];
  let culprit = null;
  let culpritStart = -Infinity;
  for (const file of files) {
    const r = results.get(file);
    if (!r || failedOnTransport(r) || r.startTime >= diedAtMs) {
      unrun.push(file);
      continue;
    }
    if (r.status === "failed" && r.startTime < diedAtMs && r.startTime > culpritStart) {
      culprit = file;
      culpritStart = r.startTime;
    }
  }
  return { culprit, unrun };
}

/** Source files under `src` whose deployed copy under `dst` is missing or differs, relative to `src`. */
export function undeployedSources(src, dst) {
  const out = [];
  const walk = (dir) => {
    let entries;
    try {
      entries = fs.readdirSync(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const e of entries) {
      const full = path.join(dir, e.name);
      if (e.isDirectory()) {
        walk(full);
        continue;
      }
      const rel = path.relative(src, full);
      const deployed = path.join(dst, rel);
      if (!fs.existsSync(deployed) || !fs.readFileSync(full).equals(fs.readFileSync(deployed))) out.push(rel.split(path.sep).join("/"));
    }
  };
  walk(src);
  return out;
}

/**
 * The folder one run stages the tracked tests/ue_mcp into. Each runner process
 * gets its own, because the release starts the live and live-base lanes at the
 * same moment and a shared folder had one lane deleting what the other wrote (#1603).
 */
export function trackedStageDir(shardRoot, pid = process.pid) {
  return path.join(shardRoot, `_tracked-${pid}`);
}

/** Remove staging folders left by runners that are no longer alive, and the old shared one. */
export function sweepStaleTrackedStages(shardRoot, alive = pidAlive) {
  let entries = [];
  try {
    entries = fs.readdirSync(shardRoot, { withFileTypes: true });
  } catch {
    return [];
  }
  const removed = [];
  for (const e of entries) {
    if (!e.isDirectory()) continue;
    const m = /^_tracked(?:-(\d+))?$/.exec(e.name);
    if (!m || (m[1] && alive(Number(m[1])))) continue;
    fs.rmSync(path.join(shardRoot, e.name), { recursive: true, force: true });
    removed.push(e.name);
  }
  return removed;
}

function pidAlive(pid) {
  try {
    process.kill(pid, 0);
    return true;
  } catch (err) {
    return err?.code === "EPERM";
  }
}

/**
 * The tracked tests/ue_mcp, as git has it, written into `stage`: what every
 * shard starts from, so a run never inherits content an earlier run or a
 * developer left behind.
 */
export function stageTrackedProject(repoRoot, stage) {
  fs.rmSync(stage, { recursive: true, force: true });
  fs.mkdirSync(stage, { recursive: true });
  const files = execFileSync("git", ["ls-files", "-z", "tests/ue_mcp"], { cwd: repoRoot });
  execFileSync("git", ["checkout-index", "--stdin", "-z", "-f", `--prefix=${stage}${path.sep}`], { cwd: repoRoot, input: files });
  return path.join(stage, "tests", "ue_mcp");
}
