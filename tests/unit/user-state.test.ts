/**
 * ~/.ue-mcp/state.json: how it is keyed, and how it is written.
 *
 * D2: every other project key in this server folds case and forward-slashes
 * (port.ts derives the bridge port from that shape, session.ts keys sessions by
 * it). This file resolved only, so it was the one place that did not.
 * `npx ue-mcp dialog mode defer --editor C:/Projects/X` keyed it `C:\Projects\X`
 * and printed success; the server, launched from .mcp.json with
 * `c:/projects/x/X.uproject`, looked up `c:/projects/x`, found nothing, and fell
 * back to interactive. The user asked for "press nothing in my editor" and the
 * server pressed a dialog button. The same mechanism hit `feedback mode`, which
 * gates posting to a public issue tracker, and `installedHooks`, which is what
 * `uninstall-hooks` reads.
 *
 * D4: every setter was read, mutate, direct writeFileSync. Two writers lost one
 * of the two writes, and a crash or a full disk mid-write left a truncated file
 * that readState treats exactly like a missing one - every project's
 * installedHooks record discarded with nothing said.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import {
  getDialogMode,
  setDialogMode,
  getFeedbackMode,
  setFeedbackMode,
  getInstalledHooks,
  setInstalledHooks,
  getUserStatePath,
} from "../../src/user-state.js";

/**
 * Two spellings of ONE project root, derived from the running platform.
 *
 * These were hardcoded Windows drive paths, which are not ABSOLUTE on POSIX,
 * so path.resolve prefixed the runner working directory and the two spellings
 * stopped naming one root at all. The property under test is real on both
 * platforms, because normalizeProjectRoot folds separators and case
 * unconditionally; only the way a root is spelled differs.
 *
 * PROJECT is what a caller passes. LEGACY_KEY is what an older version wrote
 * into state.json, before the folding existed: path.resolve and nothing more.
 * CANON_KEY is what it folds to now.
 */
const WIN = path.sep === "\\";
const PROJECT = WIN ? "C:/Projects/X" : "/Projects/X";
const OTHER_PROJECT = WIN ? "D:/Other" : "/Other";
/**
 * The same root spelled with the other separator, and absolute on both
 * platforms. A leading backslash is not absolute on POSIX, so the POSIX form
 * keeps its leading slash and varies only the inner separator, which is
 * exactly what the fold collapses.
 */
const SEP_SPELLING = WIN ? "C:\\Projects\\X" : "/Projects\\X";
/** A trailing separator, which the fold strips. */
const TRAILING_SPELLING = PROJECT + "/";
/** A root with nothing of its own, used to prove the user-wide fallback. */
const UNTOUCHED_PROJECT = WIN ? "D:/Untouched" : "/Untouched";
const LEGACY_KEY = path.resolve(PROJECT);
const CANON_KEY = LEGACY_KEY.replace(/\\/g, "/").replace(/\/+$/, "").toLowerCase();


let stateFile: string;
let saved: string | undefined;
const roots: string[] = [];

beforeEach(() => {
  saved = process.env.UE_MCP_USER_STATE;
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-state-"));
  roots.push(root);
  stateFile = path.join(root, "state.json");
  process.env.UE_MCP_USER_STATE = stateFile;
});

afterEach(() => {
  if (saved === undefined) delete process.env.UE_MCP_USER_STATE;
  else process.env.UE_MCP_USER_STATE = saved;
  while (roots.length > 0) fs.rmSync(roots.pop()!, { recursive: true, force: true });
});

function writeRaw(state: unknown): void {
  fs.writeFileSync(stateFile, typeof state === "string" ? state : JSON.stringify(state, null, 2));
}

function readRaw(): { projects?: Record<string, Record<string, unknown>> } {
  return JSON.parse(fs.readFileSync(stateFile, "utf-8")) as {
    projects?: Record<string, Record<string, unknown>>;
  };
}

describe("project keys fold the way every other project key folds (D2)", () => {
  it("finds a dialog mode written through one spelling when read through another", () => {
    setDialogMode("defer", PROJECT);
    expect(getDialogMode(SEP_SPELLING)).toBe("defer");
    expect(getDialogMode(TRAILING_SPELLING)).toBe("defer");
  });

  it("does the same for the feedback mode, which gates posting to a public tracker", () => {
    setFeedbackMode("defer", PROJECT);
    expect(getFeedbackMode(CANON_KEY)).toBe("defer");
  });

  it("does the same for installedHooks, which is what uninstall-hooks reads", () => {
    setInstalledHooks(PROJECT, ["C:/Users/me/.claude/settings.json"]);
    expect(getInstalledHooks(SEP_SPELLING)).toEqual(["C:/Users/me/.claude/settings.json"]);
  });

  it("stores one entry, not two, for two spellings of one root", () => {
    setDialogMode("defer", PROJECT);
    setFeedbackMode("auto-approve", SEP_SPELLING);
    expect(Object.keys(readRaw().projects ?? {})).toHaveLength(1);
    expect(getDialogMode(PROJECT)).toBe("defer");
    expect(getFeedbackMode(PROJECT)).toBe("auto-approve");
  });
});

describe("keys already on disk in the old shape keep applying (D2 migration)", () => {
  it("reads a legacy key written before the normalization existed", () => {
    writeRaw({
      projects: {
        [LEGACY_KEY]: { dialog: { mode: "defer" }, feedback: { mode: "defer" } },
      },
    });
    expect(getDialogMode(CANON_KEY)).toBe("defer");
    expect(getFeedbackMode(CANON_KEY)).toBe("defer");
  });

  it("keeps the legacy value when a later write touches a different key", () => {
    writeRaw({ projects: { [LEGACY_KEY]: { dialog: { mode: "defer" } } } });
    setFeedbackMode("auto-approve", OTHER_PROJECT);
    expect(getDialogMode(PROJECT)).toBe("defer");
  });

  // The auditor's target: prove the migration loses an existing setting.
  it("loses nothing when two spellings of one root are both on disk", () => {
    writeRaw({
      projects: {
        [LEGACY_KEY]: {
          dialog: { mode: "defer" },
          installedHooks: ["C:/a/settings.json"],
        },
        [CANON_KEY]: {
          feedback: { mode: "auto-approve" },
          installedHooks: ["C:/b/settings.json"],
        },
      },
    });

    // Neither key's value is dropped.
    expect(getDialogMode(PROJECT)).toBe("defer");
    expect(getFeedbackMode(PROJECT)).toBe("auto-approve");
    // Hooks are UNIONED: a path this file forgets is a hook left in somebody's
    // settings with no record of where it is.
    expect(getInstalledHooks(PROJECT).sort()).toEqual([
      "C:/a/settings.json",
      "C:/b/settings.json",
    ]);
  });

  it("prefers the already-canonical spelling when both name a mode", () => {
    writeRaw({
      projects: {
        [LEGACY_KEY]: { dialog: { mode: "auto" } },
        [CANON_KEY]: { dialog: { mode: "defer" } },
      },
    });
    expect(getDialogMode(PROJECT)).toBe("defer");
  });

  it("persists the folded form on the next write, without dropping the merged values", () => {
    writeRaw({
      projects: {
        [LEGACY_KEY]: { dialog: { mode: "defer" }, installedHooks: ["C:/a/settings.json"] },
      },
    });
    setFeedbackMode("defer", PROJECT);

    const keys = Object.keys(readRaw().projects ?? {});
    expect(keys).toEqual([CANON_KEY]);
    expect(getDialogMode(PROJECT)).toBe("defer");
    expect(getInstalledHooks(PROJECT)).toEqual(["C:/a/settings.json"]);
  });

  it("leaves the user-wide preferences alone while folding project keys", () => {
    writeRaw({
      preferences: { dialog: { mode: "auto" } },
      projects: { [LEGACY_KEY]: { dialog: { mode: "defer" } } },
    });
    expect(getDialogMode()).toBe("auto");
    expect(getDialogMode(CANON_KEY)).toBe("defer");
    // A project with nothing of its own still falls through to the user's.
    expect(getDialogMode(UNTOUCHED_PROJECT)).toBe("auto");
  });
});

describe("the file is written atomically (D4)", () => {
  it("leaves no temp file behind", () => {
    setDialogMode("defer", PROJECT);
    const dir = path.dirname(stateFile);
    expect(fs.readdirSync(dir).filter((n) => n.endsWith(".tmp"))).toEqual([]);
  });

  it("releases its lock", () => {
    setDialogMode("defer", PROJECT);
    expect(fs.existsSync(`${stateFile}.lock`)).toBe(false);
  });

  it("does not block forever on a lock whose holder died", () => {
    const lock = `${stateFile}.lock`;
    fs.mkdirSync(path.dirname(lock), { recursive: true });
    fs.mkdirSync(lock);
    const old = new Date(Date.now() - 60_000);
    fs.utimesSync(lock, old, old);

    const started = Date.now();
    setDialogMode("defer", PROJECT);
    expect(Date.now() - started).toBeLessThan(2_000);
    expect(getDialogMode(PROJECT)).toBe("defer");
  });

  // The lock is what stops a `npx ue-mcp feedback mode` run landing in the
  // middle of the server's setInstalledHooks and throwing one of the two writes
  // away. A single-threaded test cannot stage that race, so this proves the
  // mechanism instead: a lock held by somebody else is waited for, and the
  // command still lands rather than being refused.
  it("waits for a lock another writer is holding, and still writes", () => {
    const lock = `${stateFile}.lock`;
    fs.mkdirSync(path.dirname(lock), { recursive: true });
    fs.mkdirSync(lock);

    const started = Date.now();
    setDialogMode("defer", PROJECT);
    const waited = Date.now() - started;

    expect(waited).toBeGreaterThan(2_000);
    expect(getDialogMode(PROJECT)).toBe("defer");
    fs.rmdirSync(lock);
  });

  it("preserves an unreadable state.json instead of replacing it silently", () => {
    // What a crash or a full disk mid-write leaves: a truncated file. readState
    // treats it exactly like a missing one, which is what discards every
    // project's installedHooks record with nothing said.
    writeRaw('{"projects": {"c:/projects/x": {"installedHooks": ["C:/a/set');
    const warned = vi.spyOn(console, "error").mockImplementation(() => {});
    try {
      expect(getInstalledHooks(PROJECT)).toEqual([]);
      expect(warned.mock.calls.flat().join(" ")).toContain("could not be parsed");

      setDialogMode("defer", PROJECT);
    } finally {
      warned.mockRestore();
    }

    // The bytes that named the hook files are still on disk.
    expect(fs.existsSync(`${stateFile}.corrupt`)).toBe(true);
    expect(fs.readFileSync(`${stateFile}.corrupt`, "utf-8")).toContain("installedHooks");
    // And the new file is valid.
    expect(getDialogMode(PROJECT)).toBe("defer");
  });

  it("keeps the file owner-readable only", () => {
    setDialogMode("defer", PROJECT);
    expect(fs.existsSync(getUserStatePath())).toBe(true);
    if (process.platform !== "win32") {
      expect(fs.statSync(stateFile).mode & 0o077).toBe(0);
    }
  });

  it("still deletes the file when the last setting is cleared", () => {
    setDialogMode("defer", PROJECT);
    setDialogMode(undefined, PROJECT);
    expect(fs.existsSync(stateFile)).toBe(false);
  });
});
