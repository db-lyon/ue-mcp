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
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import type { ToolContext, ElicitFn } from "../../src/types.js";
import {
  pushWorkaround,
  getWorkarounds,
  clearWorkarounds,
  workaroundCount,
  resetAllWorkarounds,
} from "../../src/workaround-tracker.js";
import { SessionRegistry } from "../../src/session.js";

const mockSubmitFeedback = vi.fn();
vi.mock("../../src/github-app.js", () => ({
  submitFeedback: (...args: unknown[]) => mockSubmitFeedback(...args),
}));

const mockReadUserAuth = vi.fn();
vi.mock("../../src/auth.js", () => ({
  readUserAuth: () => mockReadUserAuth(),
}));

const { feedbackTool } = await import("../../src/tools/feedback.js");

let root: string;

function makeProject(name: string): string {
  const dir = path.join(root, name);
  fs.mkdirSync(dir, { recursive: true });
  const uproject = path.join(dir, `${name}.uproject`);
  fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, EngineAssociation: "5.6" }), "utf-8");
  return uproject;
}

beforeEach(() => {
  root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-workaround-"));
  resetAllWorkarounds();
  process.env.UE_MCP_FEEDBACK_ROUTING = "off";
  mockSubmitFeedback.mockReset();
  mockReadUserAuth.mockReset();
  mockReadUserAuth.mockResolvedValue({
    token: "ghu_abc",
    login: "tester",
    authorized_at: "2026-05-20T00:00:00Z",
  });
});

afterEach(() => {
  fs.rmSync(root, { recursive: true, force: true });
  resetAllWorkarounds();
  delete process.env.UE_MCP_FEEDBACK_ROUTING;
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
    const alpha = makeProject("Alpha");
    const beta = makeProject("BetaProjectName");
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
