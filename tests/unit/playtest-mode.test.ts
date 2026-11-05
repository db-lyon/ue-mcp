/**
 * The playtest mode: stored per user or per project like the dialog mode, and
 * the refusal manual gives pie(script_run) and pie(script_replay).
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { getPlaytestModeScopes, setPlaytestMode } from "../../src/config/user-state.js";
import { PLAYTEST_GATED_TASKS, playtestRefusal, resolvePlaytestMode } from "../../src/editor/playtest-mode.js";

let tmp: string;
let project: string;
let saved: string | undefined;

beforeEach(() => {
  tmp = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-playtest-mode-"));
  project = path.join(tmp, "Game");
  fs.mkdirSync(project);
  saved = process.env.UE_MCP_USER_STATE;
  process.env.UE_MCP_USER_STATE = path.join(tmp, "state.json");
});
afterEach(() => {
  if (saved === undefined) delete process.env.UE_MCP_USER_STATE;
  else process.env.UE_MCP_USER_STATE = saved;
  fs.rmSync(tmp, { recursive: true, force: true });
});

const state = () => JSON.parse(fs.readFileSync(path.join(tmp, "state.json"), "utf8"));

describe("the stored playtest mode", () => {
  it("is unset until set, and the project's own wins over the user's", () => {
    expect(resolvePlaytestMode(project)).toEqual({ mode: null, project: false });
    setPlaytestMode("automatic");
    expect(resolvePlaytestMode(project)).toEqual({ mode: "automatic", project: false });
    setPlaytestMode("manual", project);
    expect(resolvePlaytestMode(project)).toEqual({ mode: "manual", project: true });
    expect(getPlaytestModeScopes(project)).toEqual({ project: "manual", user: "automatic" });
    expect(state().preferences.playtest).toEqual({ mode: "automatic" });
  });

  it("clears back to the user preference, then to nothing, and leaves no empty containers", () => {
    setPlaytestMode("manual", project);
    setPlaytestMode(undefined, project);
    expect(resolvePlaytestMode(project)).toEqual({ mode: null, project: false });
    expect(fs.existsSync(path.join(tmp, "state.json"))).toBe(false);
  });

  it("reads a stored spelling case-folded", () => {
    fs.writeFileSync(path.join(tmp, "state.json"), JSON.stringify({ preferences: { playtest: { mode: " Manual " } } }));
    expect(resolvePlaytestMode(null).mode).toBe("manual");
    fs.writeFileSync(path.join(tmp, "state.json"), JSON.stringify({ preferences: { playtest: { mode: "sometimes" } } }));
    expect(resolvePlaytestMode(null).mode).toBeNull();
  });
});

describe("playtestRefusal", () => {
  it("gates exactly script_run and script_replay", () => {
    expect([...PLAYTEST_GATED_TASKS].sort()).toEqual(["pie.script_replay", "pie.script_run"]);
  });

  it("lets everything through when the mode is unset or automatic", () => {
    expect(playtestRefusal("pie.script_run", project)).toBeNull();
    setPlaytestMode("automatic", project);
    expect(playtestRefusal("pie.script_replay", project)).toBeNull();
  });

  it("refuses under manual, naming the setting and how to change it", () => {
    setPlaytestMode("manual");
    expect(playtestRefusal("pie.script_status", project)).toBeNull();
    const user = playtestRefusal("pie.script_run", project)!;
    expect(user).toMatchObject({ success: false, refusedMethod: "pie.script_run", playtestMode: "manual" });
    expect(user.reason).toContain("set to manual for this user");
    expect(user.reason).toContain("`ue-mcp playtest mode automatic`");

    setPlaytestMode("manual", project);
    const scoped = playtestRefusal("pie.script_replay", project)!;
    expect(scoped.refusedMethod).toBe("pie.script_replay");
    expect(scoped.reason).toContain("set to manual for this project");
    expect(scoped.reason).toContain("ue-mcp playtest mode automatic --editor");
  });
});
