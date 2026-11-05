/**
 * `init --feedback-mode` stores the project-scoped feedback approval mode, the
 * same setting `feedback mode <mode> --editor` writes, and the plan reports the
 * mode in effect and which setting decided it.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as path from "node:path";
import { apply, feedbackModeOf, plan, type InitChoices } from "../../src/cli/init-core.js";
import { choicesFromFlags } from "../../src/cli/init.js";
import { getFeedbackMode, getFeedbackModeScopes, setFeedbackMode } from "../../src/config/user-state.js";
import type { ProbeHooks } from "../../src/editor/install-check.js";
import { ProjectFixture } from "../helpers/project-fixture.js";
import { restoreEnv } from "../helpers/env.js";

const WITH_TOOLCHAIN: ProbeHooks = { platform: "linux", env: {}, exists: () => true, run: () => "clang version 18" };

let fixture: ProjectFixture;
let savedEnv: NodeJS.ProcessEnv;

beforeEach(() => {
  fixture = new ProjectFixture("ue-mcp-initmode-");
  savedEnv = { ...process.env };
  for (const k of ["HOME", "USERPROFILE"]) process.env[k] = fixture.root;
  process.env.APPDATA = path.join(fixture.root, "AppData");
  process.env.UE_MCP_USER_STATE = path.join(fixture.root, "state.json");
  delete process.env.UE_MCP_FEEDBACK_MODE;
});

afterEach(() => {
  restoreEnv(savedEnv);
  fixture.cleanup();
});

const choices = (uproject: string, over: Partial<InitChoices> = {}): InitChoices =>
  ({ project: uproject, clients: ["cursor"], install: "source", ...over });

describe("init --feedback-mode flag", () => {
  it("reads each mode and leaves it unset when absent", () => {
    for (const mode of ["interactive", "auto-approve", "defer"] as const) {
      expect(choicesFromFlags(["--feedback-mode", mode], "x.uproject").feedbackMode).toBe(mode);
      expect(choicesFromFlags([`--feedback-mode=${mode}`], "x.uproject").feedbackMode).toBe(mode);
    }
    expect(choicesFromFlags([], "x.uproject").feedbackMode).toBeUndefined();
  });

  it("refuses a mode that does not exist", () => {
    expect(() => choicesFromFlags(["--feedback-mode", "always"], "x.uproject"))
      .toThrow("--feedback-mode must be interactive, auto-approve or defer, not 'always'");
    expect(() => choicesFromFlags(["--feedback-mode", "default"], "x.uproject")).toThrow(/--feedback-mode/);
  });
});

describe("plan feedbackMode", () => {
  it("is interactive by default", () => {
    const dir = path.dirname(fixture.makeProject("Fresh"));
    expect(feedbackModeOf(dir)).toEqual({ mode: "interactive", scope: "default", set: null });
  });

  it("follows the per-user mode, then the project's own, then the env override", () => {
    const dir = path.dirname(fixture.makeProject("Scoped"));
    setFeedbackMode("defer");
    expect(feedbackModeOf(dir)).toEqual({ mode: "defer", scope: "user", set: null });
    setFeedbackMode("auto-approve", dir);
    expect(feedbackModeOf(dir)).toEqual({ mode: "auto-approve", scope: "project", set: null });
    process.env.UE_MCP_FEEDBACK_MODE = "interactive";
    expect(feedbackModeOf(dir)).toEqual({ mode: "interactive", scope: "env", set: null });
  });

  it("reports the mode a choice would store, and writes nothing while planning", () => {
    const uproject = fixture.makeProject("Planned");
    const planned = plan(choices(uproject, { feedbackMode: "defer" }), WITH_TOOLCHAIN);
    expect(planned.feedbackMode).toEqual({ mode: "defer", scope: "project", set: "defer" });
    expect(getFeedbackModeScopes(path.dirname(uproject)).project).toBeUndefined();
  });
});

describe("apply feedbackMode", () => {
  it("stores the project-scoped mode exactly as feedback mode --editor does, and only for that project", async () => {
    const uproject = fixture.makeProject("Applied");
    const other = path.dirname(fixture.makeProject("Other"));
    const dir = path.dirname(uproject);
    const result = await apply(plan(choices(uproject, { feedbackMode: "auto-approve" }), WITH_TOOLCHAIN));
    expect(result.error).toBeUndefined();
    expect(result.steps.find((s) => s.id === "feedback-mode")).toEqual(
      { id: "feedback-mode", status: "changed", target: dir, detail: "feedback mode auto-approve" },
    );
    expect(getFeedbackModeScopes(dir)).toEqual({ project: "auto-approve", user: undefined });
    expect(getFeedbackMode(dir)).toBe("auto-approve");
    expect(getFeedbackMode(other)).toBeUndefined();

    const again = await apply(plan(choices(uproject, { feedbackMode: "auto-approve" }), WITH_TOOLCHAIN));
    expect(again.steps.find((s) => s.id === "feedback-mode")?.status).toBe("unchanged");

    const changed = await apply(plan(choices(uproject, { feedbackMode: "defer" }), WITH_TOOLCHAIN));
    expect(changed.steps.find((s) => s.id === "feedback-mode")?.status).toBe("changed");
    expect(getFeedbackMode(dir)).toBe("defer");
  });

  it("leaves the mode alone when no mode is chosen", async () => {
    const uproject = fixture.makeProject("Untouched");
    const dir = path.dirname(uproject);
    setFeedbackMode("defer", dir);
    const result = await apply(plan(choices(uproject), WITH_TOOLCHAIN));
    expect(result.ok).toBe(true);
    expect(result.plan.feedbackMode).toEqual({ mode: "defer", scope: "project", set: null });
    expect(result.steps.some((s) => s.id === "feedback-mode")).toBe(false);
    expect(getFeedbackMode(dir)).toBe("defer");
  });
});
