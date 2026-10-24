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
 * Peak private memory per editor, by kind, in MB: measured on the reference
 * machine under the suite's load (#1436). The runner reads free memory at
 * launch and never starts an editor that would not fit beside the headroom.
 */
export const EDITOR_PEAK_MB = { headless: 3600, base: 3600, scm: 3600, render: 5600, interactive: 5600 };

/** Memory left for everything else on the machine: at least 4 GB, or 12% of it. */
export function headroomMB(totalMB) {
  return Math.max(4096, Math.round(totalMB * 0.12));
}

/**
 * How many headless shards fit in the memory free now. The headless editors
 * run for most of the suite; the base, render, interactive and scm editors are
 * short and take turns, so room is kept for the largest of them rather than
 * for all at once. At least one, at most `max`.
 */
/** @param {{ freeMB: number, totalMB: number, otherKinds?: string[], max?: number }} opts */
export function headlessShardCount({ freeMB, totalMB, otherKinds = [], max = 8 }) {
  const turn = Math.max(0, ...otherKinds.map((k) => EDITOR_PEAK_MB[k] ?? EDITOR_PEAK_MB.headless));
  const fits = Math.floor((freeMB - headroomMB(totalMB) - turn) / EDITOR_PEAK_MB.headless);
  return Math.max(1, Math.min(max, fits));
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
