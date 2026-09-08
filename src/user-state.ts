import * as fs from "node:fs";
import * as path from "node:path";
import * as os from "node:os";
import { normalizeProjectRoot } from "./port.js";
import { warn } from "./log.js";

/**
 * User-scoped, machine-only state. Lives at `~/.ue-mcp/state.json`. Stores
 * things that:
 *
 *   - Vary per machine (absolute filesystem paths)
 *   - Are written by ue-mcp commands, not by hand
 *   - Have no business being committed alongside the project
 *
 * Keyed by absolute project root so a single user can run ue-mcp across many
 * projects without state collision.
 *
 * Currently just `installedHooks` - the list of Claude Code settings files
 * where the feedback PostToolUse hook was installed for a given project.
 * Read on `npx ue-mcp uninstall-hooks` and on re-init to seed the hook
 * checkbox.
 *
 * NOT for project policy. Anything a collaborator should also see goes in
 * `ue-mcp.yml`, not here.
 */

interface ProjectState {
  installedHooks?: string[];
  /** Per-project feedback approval mode (#817). Same preference as the
   *  user-wide one below, scoped to one project root, which is what makes it a
   *  per-session equivalent of UE_MCP_FEEDBACK_MODE without moving it into
   *  tracked project yaml, where it does not belong. */
  feedback?: { mode?: FeedbackMode };
  /** Per-project dialog handling mode. Same preference as the user-wide one
   *  below, scoped to one project root, for the same reason the feedback mode
   *  is: one editor can be a long unattended run while the user sits in front
   *  of another. */
  dialog?: { mode?: DialogMode };
}

export type FeedbackMode = "interactive" | "auto-approve" | "defer";

/**
 * How a modal dialog blocking the editor is handled.
 *
 *   interactive - put the dialog to the user in an MCP elicitation form, with
 *                 its own buttons as the choices, and press only what they pick.
 *   auto        - hand the dialog back in full and let the agent choose and
 *                 answer it. The server presses nothing.
 *   defer       - suspend: press nothing, elicit nothing, and tell the user a
 *                 dialog is blocking the editor and needs answering by hand.
 *
 * Same three-value shape as FeedbackMode, read the same way, defaulted in one
 * place (resolveDialogMode in editor-control.ts).
 */
export type DialogMode = "interactive" | "auto" | "defer";

interface Preferences {
  /** Per-user, per-device feedback approval mode. Set via
   *  `npx ue-mcp feedback mode <value>`. NOT in project yaml because the
   *  preference varies per developer / per machine (am I at the keyboard,
   *  is this a long unattended run, etc.). */
  feedback?: { mode?: FeedbackMode };
  /** Per-user, per-device dialog handling mode. NOT in project yaml for the
   *  same reason: whether a person is at the keyboard to answer a modal is a
   *  property of the machine and the session, not of the project. */
  dialog?: { mode?: DialogMode };
}

interface UserState {
  preferences?: Preferences;
  projects?: Record<string, ProjectState>;
}

function statePath(): string {
  return (
    process.env.UE_MCP_USER_STATE ||
    path.join(os.homedir(), ".ue-mcp", "state.json")
  );
}

/**
 * The lock every read-modify-write of state.json is taken under.
 *
 * Each setter below is read, mutate, write. Two of them running at once - a
 * `npx ue-mcp feedback mode` while the server is mid-setInstalledHooks - each
 * read the same file and the second write threw the first one's change away.
 * A directory create is the one filesystem operation that is atomic and
 * exclusive on every platform this ships to, so it is the mutex.
 *
 * Failing to take it NEVER fails the caller's command: a machine where the
 * lock cannot be created degrades to exactly the unlocked behaviour that came
 * before, which is worse than the lock and better than a refusal.
 */
const LOCK_STALE_MS = 10_000;
const LOCK_WAIT_MS = 3_000;

function sleepSync(ms: number): void {
  try {
    Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, ms);
  } catch {
    const until = Date.now() + ms;
    while (Date.now() < until) {
      /* SharedArrayBuffer unavailable: spin, briefly. */
    }
  }
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
      sleepSync(25);
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

function readState(): UserState {
  const file = statePath();
  if (!fs.existsSync(file)) return {};
  try {
    const parsed: unknown = JSON.parse(fs.readFileSync(file, "utf-8"));
    // A catch on JSON.parse only covers a file that is not JSON at all. `null`,
    // `[]`, `3` and `true` are all valid JSON documents and none of them is a
    // state object, so returning what parsed handed every reader something to
    // dereference: `state.projects` on null throws a TypeError with no
    // explanation, out of a call that had nothing to do with preferences. Every
    // reader below assumes an object, so this is the one place to insist on it.
    // A file that is not one is treated exactly like a file that is not there,
    // and said out loud for the same reason the unparseable case is: silence
    // here is what makes a setting look applied when it was never read.
    if (parsed === null || typeof parsed !== "object" || Array.isArray(parsed)) {
      warn("state", `${file} could not be parsed as a state object; ignoring its contents`);
      return {};
    }
    return foldProjectKeys(parsed as UserState);
  } catch {
    // An unreadable file is treated like a missing one, which is the only
    // thing every reader below can do with it. It is NOT silent: this is the
    // state that makes `uninstall-hooks` report nothing to remove while the
    // hooks are still installed, and the file is preserved before the next
    // write replaces it (see writeState).
    warn(
      "state",
      `${file} could not be parsed, so no stored preference or installed-hook record could be read from it. ` +
        `The file is preserved as ${file}.corrupt the next time state is written.`,
    );
    return {};
  }
}

/** Is the file on disk something readState could not use? */
function stateFileIsCorrupt(file: string): boolean {
  if (!fs.existsSync(file)) return false;
  try {
    const parsed: unknown = JSON.parse(fs.readFileSync(file, "utf-8"));
    return parsed === null || typeof parsed !== "object" || Array.isArray(parsed);
  } catch {
    return true;
  }
}

function writeState(state: UserState): void {
  const file = statePath();
  // Drop empty containers so the file shrinks toward {} as state clears.
  if (state.projects) {
    for (const key of Object.keys(state.projects)) {
      const proj = state.projects[key];
      if (
        proj.installedHooks !== undefined &&
        proj.installedHooks.length === 0
      ) {
        delete proj.installedHooks;
      }
      if (proj.feedback && proj.feedback.mode === undefined) delete proj.feedback;
      if (proj.dialog && proj.dialog.mode === undefined) delete proj.dialog;
      if (Object.keys(proj).length === 0) {
        delete state.projects[key];
      }
    }
    if (Object.keys(state.projects).length === 0) {
      delete state.projects;
    }
  }
  if (state.preferences) {
    const fb = state.preferences.feedback;
    if (fb && fb.mode === undefined) delete state.preferences.feedback;
    const dlg = state.preferences.dialog;
    if (dlg && dlg.mode === undefined) delete state.preferences.dialog;
    if (Object.keys(state.preferences).length === 0) {
      delete state.preferences;
    }
  }

  // No state at all → delete the file rather than leave an empty stub.
  if (Object.keys(state).length === 0) {
    if (fs.existsSync(file)) fs.unlinkSync(file);
    return;
  }

  const dir = path.dirname(file);
  if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });

  // A file nobody could read is about to be replaced by one that does not
  // carry what it held. Keep it: it is the only surviving record of which
  // settings files the hooks went into, and a person can read the JSON back
  // out of it by hand.
  if (stateFileIsCorrupt(file)) {
    try {
      fs.copyFileSync(file, `${file}.corrupt`);
      warn("state", `preserved the unreadable ${file} as ${file}.corrupt before replacing it`);
    } catch {
      /* best effort; the write below is the thing that matters */
    }
  }

  // Temp-and-rename, for the same reason requested-port.ts does it: the reader
  // is a separate process that can arrive at any moment, and a rename is the
  // one step it cannot catch halfway through. A direct write can be seen (and
  // left, on a crash or a full disk) truncated, and a truncated state.json
  // reads exactly like a missing one - every project's installedHooks record
  // discarded with nothing said.
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

/**
 * The key one project root is stored under.
 *
 * Case-folded with forward slashes, which is what port.ts derives the bridge
 * port from and what session.ts keys sessions by. Resolving only, as this used
 * to, made this file the one project key in the server that did NOT fold case:
 * `npx ue-mcp dialog mode defer --editor C:/Projects/X` stored `C:\Projects\X`
 * and the server, launched from .mcp.json with `c:/projects/x/X.uproject`,
 * looked up `c:/projects/x` and found nothing. The setting silently did not
 * apply, on a preference whose whole job is to stop the server pressing a
 * button in somebody's editor.
 */
function projectKey(projectRoot: string): string {
  return normalizeProjectRoot(path.resolve(projectRoot));
}

/**
 * Fold every stored project key into the canonical shape, merging any two
 * spellings of one root.
 *
 * Keys written before this normalization existed are in whatever case and
 * slash style the caller happened to pass, so a straight lookup would miss
 * them and the user's setting would silently stop applying on upgrade. They
 * are folded on read, which is what makes the old key still answer, and the
 * next write persists the folded form.
 *
 * Nothing is dropped in the fold. installedHooks from two spellings are
 * unioned, because a path this file forgets is a hook left in somebody's
 * settings with no record of where it is. For the single-valued modes the
 * already-canonical spelling wins, and a value only the legacy spelling has is
 * kept rather than discarded.
 */
function mergeProjectState(base: ProjectState, extra: ProjectState, extraWins: boolean): ProjectState {
  const merged: ProjectState = {};
  const hooks = [...(base.installedHooks ?? []), ...(extra.installedHooks ?? [])];
  if (hooks.length > 0) merged.installedHooks = [...new Set(hooks)];
  const first = extraWins ? extra : base;
  const second = extraWins ? base : extra;
  const feedback = first.feedback?.mode !== undefined ? first.feedback : second.feedback;
  const dialog = first.dialog?.mode !== undefined ? first.dialog : second.dialog;
  if (feedback) merged.feedback = feedback;
  if (dialog) merged.dialog = dialog;
  return merged;
}

function foldProjectKeys(state: UserState): UserState {
  const projects = state.projects;
  if (!projects || typeof projects !== "object" || Array.isArray(projects)) return state;
  const folded: Record<string, ProjectState> = {};
  for (const [raw, value] of Object.entries(projects)) {
    if (!value || typeof value !== "object" || Array.isArray(value)) continue;
    let key: string;
    try {
      key = projectKey(raw);
    } catch {
      key = raw;
    }
    const prior = folded[key];
    folded[key] = prior
      ? mergeProjectState(prior, value as ProjectState, raw === key)
      : { ...(value as ProjectState) };
  }
  state.projects = folded;
  return state;
}

export function getInstalledHooks(projectRoot: string): string[] {
  const state = readState();
  return state.projects?.[projectKey(projectRoot)]?.installedHooks ?? [];
}

export function setInstalledHooks(projectRoot: string, hooks: string[]): void {
  withStateLock(() => {
    const state = readState();
    const key = projectKey(projectRoot);
    if (!state.projects) state.projects = {};
    if (!state.projects[key]) state.projects[key] = {};
    if (hooks.length > 0) {
      state.projects[key].installedHooks = hooks;
    } else {
      delete state.projects[key].installedHooks;
    }
    writeState(state);
  });
}

/**
 * Read a stored mode the way the env var is read: trimmed and case-folded.
 *
 * The same rule as asDialogMode below, deliberately. These are two adjacent
 * keys in one file, and a user who learns that " AUTO " works for one of them
 * has learned something false about the other.
 *
 * This is kept knowing what it costs: a hand-edited state.json holding
 * "AUTO-APPROVE" used to fall back to interactive, and now resolves to
 * auto-approve, so upgrading removes that user's approval prompt. It is still
 * the right reading. Nothing writes this key but setFeedbackMode, which stores
 * the canonical spelling, so the only way to hold "AUTO-APPROVE" is to have
 * typed it, and somebody who typed the name of a mode named that mode. The old
 * behaviour was not a safety gate: it was a value silently ignored, with the
 * effective mode reported nowhere the person who wrote it would look. Honouring
 * what they wrote is the reading that can be checked against the file.
 */
function asFeedbackMode(mode: unknown): FeedbackMode | undefined {
  const named = typeof mode === "string" ? mode.trim().toLowerCase() : mode;
  return named === "interactive" || named === "auto-approve" || named === "defer" ? named : undefined;
}

/**
 * The feedback approval mode, for one project or for this user.
 *
 * A project's own preference wins over the user-wide one when it has been set
 * (#817): with more than one editor registered, one of them can be a long
 * unattended run while the user sits in front of another, and a single
 * per-device answer cannot describe both. Nothing here reads project yaml -
 * this is a per-user preference either way, and where a collaborator would see
 * it is exactly where it does not belong.
 */
export function getFeedbackMode(projectRoot?: string | null): FeedbackMode | undefined {
  const state = readState();
  if (projectRoot) {
    const scoped = asFeedbackMode(state.projects?.[projectKey(projectRoot)]?.feedback?.mode);
    if (scoped) return scoped;
  }
  return asFeedbackMode(state.preferences?.feedback?.mode);
}

/** Set or clear the feedback mode preference. Pass undefined to clear. */
export function setFeedbackMode(mode: FeedbackMode | undefined, projectRoot?: string | null): void {
  withStateLock(() => {
    const state = readState();
    if (projectRoot) {
      const key = projectKey(projectRoot);
      if (!state.projects) state.projects = {};
      if (!state.projects[key]) state.projects[key] = {};
      if (mode === undefined) {
        delete state.projects[key].feedback;
      } else {
        state.projects[key].feedback = { mode };
      }
      writeState(state);
      return;
    }
    if (!state.preferences) state.preferences = {};
    if (!state.preferences.feedback) state.preferences.feedback = {};
    if (mode === undefined) {
      delete state.preferences.feedback.mode;
    } else {
      state.preferences.feedback.mode = mode;
    }
    writeState(state);
  });
}

/**
 * Read a stored mode the way the env var is read: trimmed and case-folded.
 *
 * The env override lowercases, so "AUTO" there is auto. A stored value that
 * demanded an exact match made the same word in state.json fall through to the
 * default without a word said, which is the kind of difference nobody finds by
 * reading their own config.
 */
function asDialogMode(mode: unknown): DialogMode | undefined {
  const named = typeof mode === "string" ? mode.trim().toLowerCase() : mode;
  return named === "interactive" || named === "auto" || named === "defer" ? named : undefined;
}

/**
 * The dialog handling mode, for one project or for this user.
 *
 * Read exactly like the feedback mode above, and stored in the same file under
 * `dialog.mode`: a project's own preference wins over the user-wide one when it
 * has been set, and nothing here reads project yaml, because whether somebody
 * is at the keyboard to answer a modal is a property of the machine and the
 * session rather than of the project.
 *
 * Returns undefined when nothing is stored. The default is NOT decided here:
 * it depends on whether the connected client advertised elicitation, which this
 * module cannot see. resolveDialogMode in editor-control.ts owns it.
 */
export function getDialogMode(projectRoot?: string | null): DialogMode | undefined {
  const state = readState();
  if (projectRoot) {
    const scoped = asDialogMode(state.projects?.[projectKey(projectRoot)]?.dialog?.mode);
    if (scoped) return scoped;
  }
  return asDialogMode(state.preferences?.dialog?.mode);
}

/**
 * The dialog mode each scope holds, without the fallback.
 *
 * getDialogMode answers "what applies", which is the right question for the
 * server and the wrong one for a display: it falls back to the user preference,
 * so a caller printing its result as the project's own value reports a value
 * the project never set. This answers "what is set where".
 */
export function getDialogModeScopes(projectRoot?: string | null): {
  project?: DialogMode;
  user?: DialogMode;
} {
  const state = readState();
  return {
    project: projectRoot
      ? asDialogMode(state.projects?.[projectKey(projectRoot)]?.dialog?.mode)
      : undefined,
    user: asDialogMode(state.preferences?.dialog?.mode),
  };
}

/** Set or clear the dialog mode preference. Pass undefined to clear. */
export function setDialogMode(mode: DialogMode | undefined, projectRoot?: string | null): void {
  // Normalised on the way in as well as on the way out, so the file never holds
  // a spelling that only one of the two readers accepts.
  const value = mode !== undefined ? asDialogMode(mode) ?? mode : undefined;
  withStateLock(() => {
    const state = readState();
    if (projectRoot) {
      const key = projectKey(projectRoot);
      if (!state.projects) state.projects = {};
      if (!state.projects[key]) state.projects[key] = {};
      if (value === undefined) {
        delete state.projects[key].dialog;
      } else {
        state.projects[key].dialog = { mode: value };
      }
      writeState(state);
      return;
    }
    if (!state.preferences) state.preferences = {};
    if (!state.preferences.dialog) state.preferences.dialog = {};
    if (value === undefined) {
      delete state.preferences.dialog.mode;
    } else {
      state.preferences.dialog.mode = value;
    }
    writeState(state);
  });
}

export function getUserStatePath(): string {
  return statePath();
}
