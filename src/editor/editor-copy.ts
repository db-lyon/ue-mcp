// @session independent: Pure helpers over files and processes it is handed; it holds no state.
/**
 * A hidden editor on a copy of a project: what the sharded live run
 * (scripts/live-shards.mjs, which re-exports these) and the Playtest Hub's
 * headless runner (src/daemon/hub-headless.ts) both need.
 *
 * - mirror: copy a project into its copy, only what changed since last time.
 * - the memory gate: how much an editor takes at its peak, and whether one
 *   more fits beside the headroom the rest of the machine keeps.
 * - deathOf and awaitEditorStartup: a launch watched until it reports ready,
 *   with the cause from its own log when it dies first.
 *
 * Imports only node builtins and uses only erasable syntax, so the .mjs
 * scripts load it as it is.
 */
import type { ChildProcess } from "node:child_process";
import fs from "node:fs";
import path from "node:path";

/** Editor command line per kind of hidden editor. */
export const KIND_FLAGS: Readonly<Record<string, readonly string[]>> = {
  headless: ["-nullrhi", "-unattended", "-nosplash", "-nosound"],
  render: ["-RenderOffScreen", "-unattended", "-nosplash", "-nosound"],
  interactive: ["-RenderOffScreen", "-nosplash", "-nosound"],
  scm: ["-nullrhi", "-unattended", "-nosplash", "-nosound"],
  base: ["-nullrhi", "-unattended", "-nosplash", "-nosound"],
};

/** A copy leaves out build intermediates wherever they are. */
export function buildOutputFilter(srcRoot: string): (src: string) => boolean {
  return (src) => !path.relative(srcRoot, src).split(path.sep).includes("Intermediate");
}

/** What a project copy leaves out at the project's root: per-machine state, caches and source control. */
export const PROJECT_COPY_SKIP: ReadonlySet<string> = new Set(["Saved", "Intermediate", "DerivedDataCache", ".git", ".vs", ".idea", ".svn", ".p4config"]);

/** A filter for mirror that copies a project to run an editor on it: everything but PROJECT_COPY_SKIP at the root and intermediates anywhere. */
export function projectCopyFilter(projectDir: string): (src: string) => boolean {
  const inner = buildOutputFilter(projectDir);
  return (src) => {
    const rel = path.relative(projectDir, src);
    const top = rel.split(path.sep)[0] ?? "";
    return !PROJECT_COPY_SKIP.has(top) && inner(src);
  };
}

/**
 * Mirror `src` into `dst`: copy what is new or changed, delete what `src` no
 * longer has, except the `keep` names at the root. Size and mtime decide
 * "changed", so a second run copies little. Returns how many files it copied.
 */
export function mirror(src: string, dst: string, filter: (p: string) => boolean = () => true, keep: ReadonlySet<string> = new Set()): number {
  let copied = 0;
  const walk = (s: string, d: string): void => {
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
 * working sets the live runner records (render 4.48 GB, interactive 4.15 GB,
 * base and scm 3.21 GB). Nothing starts an editor that would not fit beside
 * the headroom.
 */
export const EDITOR_PEAK_MB: Readonly<Record<string, number>> = { headless: 3600, base: 3300, scm: 3300, render: 4600, interactive: 4300 };

/** Memory left for everything else on the machine: at least 4 GB, or 12% of it. */
export function headroomMB(totalMB: number): number {
  return Math.max(4096, Math.round(totalMB * 0.12));
}

/** Whether one more editor of this kind fits in the memory free now. */
export function editorFits(kind: string, freeMB: number, totalMB: number): boolean {
  return freeMB - (EDITOR_PEAK_MB[kind] ?? EDITOR_PEAK_MB.headless!) >= headroomMB(totalMB);
}

export function pidAlive(pid: number | undefined): boolean {
  if (!pid) return false;
  try {
    process.kill(pid, 0);
    return true;
  } catch (err) {
    return (err as NodeJS.ErrnoException)?.code === "EPERM";
  }
}

/** UTC log stamp, as the engine writes it: [2026.10.23-19.25.32:762]. */
export function parseLogStamp(stamp: string): number | null {
  const m = /(\d{4})\.(\d{2})\.(\d{2})-(\d{2})\.(\d{2})\.(\d{2}):(\d{3})/.exec(stamp);
  return m ? Date.UTC(+m[1]!, +m[2]! - 1, +m[3]!, +m[4]!, +m[5]!, +m[6]!, +m[7]!) : null;
}

/** The editor's own log for a project: Saved/Logs/<project name>.log. */
function logFileOf(projectDir: string): string {
  const logs = path.join(projectDir, "Saved", "Logs");
  const uproject = fs.existsSync(projectDir) ? fs.readdirSync(projectDir).find((n) => n.endsWith(".uproject")) : undefined;
  return path.join(logs, `${uproject ? uproject.slice(0, -".uproject".length) : "ue_mcp"}.log`);
}

/** When and why an editor died, read from its own log. A log older than `sinceMs` is a previous session's. */
export function deathOf(projectDir: string, sinceMs = 0): { atMs: number; reason: string } {
  const logFile = logFileOf(projectDir);
  if (!fs.existsSync(logFile)) return { atMs: Date.now(), reason: "it exited without a log" };
  const mtimeMs = fs.statSync(logFile).mtimeMs;
  if (mtimeMs < sinceMs - 1000) return { atMs: Date.now(), reason: "it exited before writing a log" };
  const text = fs.readFileSync(logFile, "utf8");
  const crash = /^\[([^\]]+)\]\[[^\]]*\]LogWindows: Error: (?:appError called: )?(.*(?:Assertion failed|Fatal error|Unhandled Exception|Ran out of memory|Out of memory|EXCEPTION_).*)$/m.exec(text);
  if (crash) return { atMs: parseLogStamp(crash[1]!) ?? mtimeMs, reason: `it crashed: ${crash[2]!.trim().slice(0, 240)}` };
  const exit = /^\[([^\]]+)\]\[[^\]]*\]LogCore: Engine exit requested \(reason: ([^)]*)\)/m.exec(text);
  if (exit) return { atMs: parseLogStamp(exit[1]!) ?? mtimeMs, reason: `it quit: its log records an exit request (${exit[2]})` };
  return { atMs: mtimeMs, reason: "it exited with no crash in its log, which is what an out-of-memory kill looks like" };
}

/** The readiness wait's own verdicts that mean the process is gone or going. */
const STARTUP_DEATH_REASON = /crashed|exited during startup|process is gone/i;

export interface StartupWait {
  child: Pick<ChildProcess, "pid" | "once" | "off">;
  wait: (signal: AbortSignal) => Promise<{ ready: boolean; reason?: string }>;
  readyTimeoutS: number;
  graceMs?: number;
  projectDir: string;
  launchedAtMs: number;
  pollMs?: number;
}

/**
 * Wait for a launched editor to report ready, watching its process the whole
 * time (#1609). Resolves with the ready result. Rejects with `startupDeath` and
 * the cause from its log when the process exits first, and with `startupTimeout`
 * when it is not ready within the budget, even if the wait itself never returns.
 */
export async function awaitEditorStartup({ child, wait, readyTimeoutS, graceMs = 30_000, projectDir, launchedAtMs, pollMs = 1000 }: StartupWait): Promise<{ ready: boolean; reason?: string }> {
  const abort = new AbortController();
  const timers: Array<ReturnType<typeof setTimeout>> = [];
  let onExit = (): void => {};
  type Outcome = { ready?: { ready: boolean; reason?: string }; exited?: boolean; timedOut?: boolean };
  const exited = new Promise<Outcome>((resolve) => {
    onExit = () => resolve({ exited: true });
    child.once("exit", onExit);
    child.once("error", onExit);
    timers.push(setInterval(() => {
      if (!pidAlive(child.pid)) onExit();
    }, pollMs));
  });
  const deadline = new Promise<Outcome>((resolve) => {
    timers.push(setTimeout(() => resolve({ timedOut: true }), readyTimeoutS * 1000 + graceMs));
  });
  const died = (): Error => {
    const death = deathOf(projectDir, launchedAtMs);
    return Object.assign(new Error(`the editor died during startup, ${death.reason}`), { startupDeath: true, death });
  };
  try {
    const outcome = await Promise.race<Outcome>([wait(abort.signal).then((ready) => ({ ready })), exited, deadline]);
    if (outcome.ready?.ready) return outcome.ready;
    if (outcome.exited || !pidAlive(child.pid) || STARTUP_DEATH_REASON.test(outcome.ready?.reason ?? "")) throw died();
    const reason = outcome.ready?.reason && !/still not ready after/.test(outcome.ready.reason) ? `: ${outcome.ready.reason}` : "";
    throw Object.assign(new Error(`the editor did not report ready within ${readyTimeoutS}s${reason}. Check for a modal dialog in its window or log.`), { startupTimeout: true });
  } finally {
    abort.abort();
    for (const t of timers) clearTimeout(t);
    child.off("exit", onExit);
    child.off("error", onExit);
  }
}
