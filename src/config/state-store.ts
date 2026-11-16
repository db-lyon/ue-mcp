// @session per-session: Keyed by absolute project root; each feature module reads and writes its own entries.
import * as fs from "node:fs";
import * as path from "node:path";
import { normalizeProjectRoot } from "../bridge/port.js";
import { warn } from "../core/log.js";
import { userDir } from "../core/user-dir.js";
import { readEnv } from "../core/env.js";

/**
 * User-scoped, machine-only state at `~/.ue-mcp/state.json`: things that vary
 * per machine (absolute paths), are written by ue-mcp commands rather than by
 * hand, and have no business being committed with the project. Anything a
 * collaborator should also see goes in `ue-mcp.yml`, not here.
 *
 * This module owns the file: where it is, its lock, reading, writing and the
 * project key. What goes in it belongs to the feature modules: modes.ts (the
 * feedback, dialog and playtest modes), hook-state.ts, extension-state.ts and
 * project-list-state.ts.
 */

/** One project's entry, keyed by projectKey. Each feature owns its own fields. */
export type ProjectState = Record<string, unknown>;

export interface UserState {
  /** Per-user values, by feature. */
  preferences?: Record<string, unknown>;
  projects?: Record<string, ProjectState>;
  [feature: string]: unknown;
}

function statePath(): string {
  return readEnv("userState") || path.join(userDir(), "state.json");
}

export function getUserStatePath(): string {
  return statePath();
}

/**
 * The lock every read-modify-write of state.json is taken under.
 *
 * Two writers at once (a `ue-mcp feedback mode` while the server records
 * installed hooks) each read the same file, and the second write threw the
 * first one's change away. A directory create is atomic and exclusive on every
 * platform this ships to, so it is the mutex.
 *
 * Failing to take it never fails the caller's command: a machine where the
 * lock cannot be created degrades to the unlocked behaviour.
 */
const LOCK_STALE_MS = 10_000;
const LOCK_WAIT_MS = 3_000;
const LOCK_POLL_MS = 25;

/** Block this thread without burning it: Atomics.wait sleeps, and Node allows it on the main thread. */
function sleepSync(ms: number): void {
  Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, ms);
}

function withStateLock<T>(fn: () => T): T {
  const lock = `${statePath()}.lock`;
  try {
    fs.mkdirSync(path.dirname(lock), { recursive: true });
  } catch {
    return fn();
  }
  const deadline = Date.now() + LOCK_WAIT_MS;
  let held = false;
  for (;;) {
    try {
      fs.mkdirSync(lock);
      held = true;
      break;
    } catch (e) {
      if ((e as NodeJS.ErrnoException).code !== "EEXIST") break;
      // A holder that died mid-write leaves the directory behind. Age it out
      // rather than blocking every later command on a process that is gone.
      let ageMs = 0;
      try {
        ageMs = Date.now() - fs.statSync(lock).mtimeMs;
      } catch {
        continue;
      }
      if (ageMs > LOCK_STALE_MS) {
        try {
          fs.rmdirSync(lock);
        } catch {
          /* somebody else broke it first */
        }
        continue;
      }
      if (Date.now() >= deadline) break;
      sleepSync(LOCK_POLL_MS);
    }
  }
  try {
    return fn();
  } finally {
    if (held) {
      try {
        fs.rmdirSync(lock);
      } catch {
        /* already gone */
      }
    }
  }
}

function isPlainObject(v: unknown): v is Record<string, unknown> {
  return v !== null && typeof v === "object" && !Array.isArray(v);
}

/** The state file, or {} when it is missing or unreadable (said out loud, never silently). */
export function readState(): UserState {
  const file = statePath();
  if (!fs.existsSync(file)) return {};
  try {
    const parsed: unknown = JSON.parse(fs.readFileSync(file, "utf-8"));
    // `null`, `[]`, `3` and `true` are valid JSON and none is a state object.
    // Every reader assumes an object, so this is the one place to insist.
    if (!isPlainObject(parsed)) {
      warn("state", `${file} could not be parsed as a state object; ignoring its contents`);
      return {};
    }
    return foldProjectKeys(parsed as UserState);
  } catch {
    // An unreadable file reads like a missing one, which is the state that
    // makes `uninstall-hooks` report nothing to remove while hooks are still
    // installed, so it is said, and the file is kept (see writeState).
    warn(
      "state",
      `${file} could not be parsed, so no stored preference or installed-hook record could be read from it. ` +
        `The file is preserved as ${file}.corrupt the next time state is written.`,
    );
    return {};
  }
}

function stateFileIsCorrupt(file: string): boolean {
  if (!fs.existsSync(file)) return false;
  try {
    return !isPlainObject(JSON.parse(fs.readFileSync(file, "utf-8")));
  } catch {
    return true;
  }
}

/** Drop undefined values, empty arrays and empty objects, so the file shrinks toward {} as state clears. */
function prune(obj: Record<string, unknown>): void {
  for (const key of Object.keys(obj)) {
    const v = obj[key];
    if (isPlainObject(v)) prune(v);
    if (v === undefined || (Array.isArray(v) && v.length === 0) || (isPlainObject(v) && Object.keys(v).length === 0)) {
      delete obj[key];
    }
  }
}

function writeState(state: UserState): void {
  const file = statePath();
  prune(state);

  // No state at all: delete the file rather than leave an empty stub.
  if (Object.keys(state).length === 0) {
    if (fs.existsSync(file)) fs.unlinkSync(file);
    return;
  }

  const dir = path.dirname(file);
  if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });

  // A file nobody could read is about to be replaced by one that does not
  // carry what it held. Keep it: it is the only record of which settings files
  // the hooks went into, and a person can read the JSON back out by hand.
  if (stateFileIsCorrupt(file)) {
    try {
      fs.copyFileSync(file, `${file}.corrupt`);
      warn("state", `preserved the unreadable ${file} as ${file}.corrupt before replacing it`);
    } catch {
      /* best effort; the write below is the thing that matters */
    }
  }

  // Temp-and-rename: the reader is another process that can arrive at any
  // moment, and a truncated state.json reads exactly like a missing one.
  const tmp = `${file}.${process.pid}.tmp`;
  try {
    fs.writeFileSync(tmp, JSON.stringify(state, null, 2), { mode: 0o600 });
    fs.renameSync(tmp, file);
  } catch (e) {
    try {
      fs.unlinkSync(tmp);
    } catch {
      /* nothing to clean up */
    }
    throw e;
  }
}

/** Read, change and write the state file under its lock. */
export function updateState(mutate: (state: UserState) => void): void {
  withStateLock(() => {
    const state = readState();
    mutate(state);
    writeState(state);
  });
}

/**
 * The key one project root is stored under: case-folded with forward slashes,
 * the shape port.ts derives the bridge port from and session.ts keys sessions
 * by. A key that only resolved made `dialog mode defer --editor C:/Projects/X`
 * store `C:\Projects\X` while the server looked up `c:/projects/x`, and the
 * setting silently did not apply.
 */
export function projectKey(projectRoot: string): string {
  return normalizeProjectRoot(path.resolve(projectRoot));
}

/** This project's entry, or undefined. */
export function projectEntry(state: UserState, projectRoot: string): ProjectState | undefined {
  return state.projects?.[projectKey(projectRoot)];
}

/** This project's entry, created when absent. */
export function ensureProjectEntry(state: UserState, projectRoot: string): ProjectState {
  const projects = (state.projects ??= {});
  return (projects[projectKey(projectRoot)] ??= {});
}

/**
 * Merge two spellings of one root. Arrays are unioned, because a hook path
 * this file forgets is a hook left in somebody's settings with no record of
 * where it is. Objects merge key by key with `first` winning, which keeps a
 * mode only the legacy spelling holds rather than discarding it.
 */
function mergeProjectState(base: ProjectState, extra: ProjectState, extraWins: boolean): ProjectState {
  const first = extraWins ? extra : base;
  const second = extraWins ? base : extra;
  const items = (v: unknown): unknown[] => (Array.isArray(v) ? v : []);
  const merged: ProjectState = {};
  for (const key of new Set([...Object.keys(second), ...Object.keys(first)])) {
    const a = first[key];
    const b = second[key];
    if (Array.isArray(a) || Array.isArray(b)) {
      const union = [...new Set([...items(base[key]), ...items(extra[key])])];
      if (union.length > 0) merged[key] = union;
    } else if (isPlainObject(a) || isPlainObject(b)) {
      merged[key] = { ...(isPlainObject(b) ? b : {}), ...(isPlainObject(a) ? a : {}) };
    } else {
      merged[key] = a ?? b;
    }
  }
  return merged;
}

/**
 * Fold every stored project key into the canonical shape. Keys written before
 * the fold existed are in whatever case and slash style the caller passed;
 * folding on read is what makes them still answer, and the next write
 * persists the folded form. The already-canonical spelling wins a conflict.
 */
function foldProjectKeys(state: UserState): UserState {
  const projects = state.projects;
  if (!isPlainObject(projects)) return state;
  const folded: Record<string, ProjectState> = {};
  for (const [raw, value] of Object.entries(projects)) {
    if (!isPlainObject(value)) continue;
    let key: string;
    try {
      key = projectKey(raw);
    } catch {
      key = raw;
    }
    const prior = folded[key];
    folded[key] = prior ? mergeProjectState(prior, value, raw === key) : { ...value };
  }
  state.projects = folded;
  return state;
}
