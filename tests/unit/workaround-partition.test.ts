/**
 * The workaround log is partitioned per editor (#817, plan item 6.4).
 *
 * Two properties are load-bearing, and both are privacy properties rather than
 * correctness ones: a submit from one editor must not carry another editor's
 * Python source into a public issue, and a submit from one editor must not
 * truncate another editor's log on its way out. Both were broken while the
 * stack was a single module-level array.
 */
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import * as path from "node:path";
import type { ToolContext, ElicitFn } from "../../src/core/types.js";
import {
  pushWorkaround,
  getWorkarounds,
  clearWorkarounds,
  workaroundCount,
  resetAllWorkarounds,
} from "../../src/workaround-tracker.js";
import { SessionRegistry } from "../../src/sessions/session.js";
import { ProjectFixture } from "../helpers/project-fixture.js";

const mockSubmitFeedback = vi.fn();
vi.mock("../../src/feedback/github-app.js", () => ({
  submitFeedback: (...args: unknown[]) => mockSubmitFeedback(...args),
}));

const mockReadUserAuth = vi.fn();
vi.mock("../../src/feedback/github-auth.js", () => ({
  readUserAuth: () => mockReadUserAuth(),
}));

const { feedbackTool } = await import("../../src/tools/feedback.js");

let root: string;
let fixture: ProjectFixture;

beforeEach(() => {
  fixture = new ProjectFixture("ue-mcp-workaround-");
  root = fixture.root;
  resetAllWorkarounds();
  process.env.UE_MCP_FEEDBACK_ROUTING = "off";
  // Redirect ~/.ue-mcp/state.json into the same temp root. resolveFeedbackMode
  // falls back to the user-scoped preference when no env override is set, so
  // without this the submit path reads the developer's real feedback mode and
  // a machine set to auto-approve bypasses the elicitation this asserts on.
  process.env.UE_MCP_USER_STATE = path.join(root, "state.json");
  mockSubmitFeedback.mockReset();
  mockReadUserAuth.mockReset();
  mockReadUserAuth.mockResolvedValue({
    token: "ghu_abc",
    login: "tester",
    authorized_at: "2026-05-20T00:00:00Z",
  });
});

afterEach(() => {
  fixture.cleanup();
  resetAllWorkarounds();
  delete process.env.UE_MCP_FEEDBACK_ROUTING;
  delete process.env.UE_MCP_USER_STATE;
});

describe("workaround tracker partitioning", () => {
  it("keeps each session's entries to itself", () => {
    const a = { session: { key: "c:/proj/alpha" } };
    const b = { session: { key: "c:/proj/beta" } };

    pushWorkaround({ code: "alpha_only()", timestamp: "t1" }, a);
    pushWorkaround({ code: "beta_only()", timestamp: "t2" }, b);
    pushWorkaround({ code: "beta_again()", timestamp: "t3" }, b);

    expect(workaroundCount(a)).toBe(1);
    expect(workaroundCount(b)).toBe(2);
    expect(getWorkarounds(a).map((w) => w.code)).toEqual(["alpha_only()"]);
  });

  it("clears only the session that submitted", () => {
    const a = { session: { key: "c:/proj/alpha" } };
    const b = { session: { key: "c:/proj/beta" } };
    pushWorkaround({ code: "alpha_only()", timestamp: "t1" }, a);
    pushWorkaround({ code: "beta_only()", timestamp: "t2" }, b);

    clearWorkarounds(a);

    expect(workaroundCount(a)).toBe(0);
    expect(workaroundCount(b)).toBe(1);
  });

  it("routes a context with no session to the same partition as a bare call", () => {
    pushWorkaround({ code: "bare()", timestamp: "t1" });
    expect(workaroundCount({})).toBe(1);
    expect(getWorkarounds().map((w) => w.code)).toEqual(["bare()"]);
  });
});

describe("feedback(submit) payload isolation", () => {
  it("bundles only the submitting editor's workarounds and scrubs the other's identifiers", async () => {
    const alpha = fixture.makeProject("Alpha");
    const beta = fixture.makeProject("BetaProjectName");
    const sessions = new SessionRegistry();
    const sa = sessions.register({ projectPath: alpha });
    const sb = sessions.register({ projectPath: beta });

    pushWorkaround({ code: "unreal.alpha_secret_call()", timestamp: "t1" }, { session: sa });
    pushWorkaround({ code: "unreal.beta_secret_call()", timestamp: "t2" }, { session: sb });

    let prompted = "";
    const elicit: ElicitFn = async (p) => {
      prompted = p.message;
      return { action: "decline" };
    };

    const ctx: ToolContext = {
      bridge: {} as never,
      project: sa.project,
      session: sa,
      sessions,
      elicit,
    };

    await feedbackTool.actions.submit.handler!(ctx, {
      action: "submit",
      title: "blueprint.set_class_default does not save the asset it edits",
      summary:
        "blueprint.set_class_default marks the asset dirty but never saves it, so every call needs an execute_python flush afterwards to persist.",
      pythonWorkaround: "import unreal\nunreal.do_thing()",
    });

    expect(prompted).toContain("unreal.alpha_secret_call()");
    // The other editor's Python never reaches the body that gets posted.
    expect(prompted).not.toContain("unreal.beta_secret_call()");
    // Nor does the other editor's project name or root.
    expect(prompted).not.toContain("BetaProjectName");
    expect(prompted).not.toContain(path.dirname(beta));
  });
});
