/**
 * With the feedback category disabled, nothing the agent reads may tell it to
 * call feedback(submit): the call only fails with "Unknown action". The Python
 * workaround directive and the FEEDBACK section of the server instructions
 * point at the issue tracker instead.
 */
import { afterEach, describe, expect, it } from "vitest";
import { editorTool, workaroundDirective } from "../../src/tools/editor.js";
import {
  SERVER_INSTRUCTIONS,
  SERVER_INSTRUCTIONS_LEAN,
  SERVER_INSTRUCTIONS_MICRO,
  composeServerInstructions,
  feedbackAvailable,
} from "../../src/surface/context/instructions.js";
import { CORE_ISSUES_URL } from "../../src/extensions/registry-catalog.js";
import { resetAllWorkarounds } from "../../src/dispatch/workaround-tracker.js";
import { resetRulings } from "../../src/dispatch/python-gate.js";
import { ALL_TOOLS } from "../../src/tools.js";
import type { SessionSurface } from "../../src/sessions/session-surface.js";
import type { IBridge } from "../../src/bridge/bridge.js";
import type { ToolContext } from "../../src/core/types.js";
import type { DirectiveResponse } from "../../src/core/directive.js";

const NAMES_FEEDBACK_CALL = /feedback\(action=|feedback\(submit\)/;

function surface(name: string, disabled: string[]): SessionSurface {
  return {
    session: { name } as SessionSurface["session"],
    tools: ALL_TOOLS,
    disabled: new Set(disabled),
    pluginRecords: [],
    knowledgeByCategory: {},
  };
}

afterEach(() => {
  resetAllWorkarounds();
  resetRulings();
});

describe("workaround directive", () => {
  it("offers feedback(submit) when the category is enabled", () => {
    const d = workaroundDirective(1, { ok: true }, true);
    expect(d.directive).toContain('feedback(action="submit")');
    expect(d.machine?.requiredActions).toContain("on_agreement_call_feedback_submit");
  });

  it("names the issue tracker and never feedback(submit) when it is disabled", () => {
    const d = workaroundDirective(2, { ok: true }, false);
    expect(d.directive).not.toMatch(NAMES_FEEDBACK_CALL);
    expect(d.directive).toContain(CORE_ISSUES_URL);
    expect(d.directive).toContain("disabled");
    expect(JSON.stringify(d.machine)).not.toContain("feedback_submit");
    expect(d.machine?.context?.issueTracker).toBe(CORE_ISSUES_URL);
  });

  it("follows the addressed editor's graph through execute_python", async () => {
    const bridge = {
      isConnected: true,
      call: async () => ({ success: true }),
    } as unknown as IBridge;
    const run = async (graph: typeof ALL_TOOLS): Promise<DirectiveResponse> => {
      const ctx = {
        bridge,
        project: {} as ToolContext["project"],
        session: { key: "c:/proj/feedback-off" },
        getToolGraph: () => graph,
      } as unknown as ToolContext;
      // A summary no native action matches, so the gate lets it through.
      return (await editorTool.actions.execute_python.handler!(ctx, {
        code: "print(1)",
        taskSummary: "qqzx vvwy",
      })) as DirectiveResponse;
    };

    const on = await run(ALL_TOOLS);
    expect(on.__directive).toBe(true);
    expect(on.directive).toContain('feedback(action="submit")');

    const off = await run(ALL_TOOLS.filter((t) => t.name !== "feedback"));
    expect(off.__directive).toBe(true);
    expect(off.directive).not.toMatch(NAMES_FEEDBACK_CALL);
    expect(off.directive).toContain(CORE_ISSUES_URL);
  });
});

describe("server instructions", () => {
  it("are unchanged when feedback is enabled", () => {
    const surfaces = [surface("a", [])];
    expect(composeServerInstructions("full", surfaces, ["a"], "a")).toBe(SERVER_INSTRUCTIONS);
    expect(composeServerInstructions("lean", surfaces, ["a"], "a")).toBe(SERVER_INSTRUCTIONS_LEAN);
    expect(composeServerInstructions("micro", surfaces, ["a"], "a")).toBe(SERVER_INSTRUCTIONS_MICRO);
    expect(SERVER_INSTRUCTIONS).toContain('feedback(action="submit")');
    expect(SERVER_INSTRUCTIONS_LEAN).toContain('feedback(action="submit")');
  });

  it("never name feedback(submit) when every editor disabled it", () => {
    const surfaces = [surface("a", ["feedback"])];
    for (const strategy of ["full", "lean", "micro"] as const) {
      const text = composeServerInstructions(strategy, surfaces, ["a"], "a");
      expect(text, strategy).not.toMatch(NAMES_FEEDBACK_CALL);
      if (strategy !== "micro") expect(text, strategy).toContain(CORE_ISSUES_URL);
    }
  });

  it("keep feedback(submit) while any editor can still run it", () => {
    const surfaces = [surface("a", ["feedback"]), surface("b", [])];
    expect(feedbackAvailable(surfaces)).toBe(true);
    expect(composeServerInstructions("full", surfaces, ["a", "b"], "a")).toContain('feedback(action="submit")');
  });

  it("drop it for a scope without the feedback category", () => {
    const scope = ALL_TOOLS.filter((t) => t.name !== "feedback");
    expect(feedbackAvailable([surface("a", [])], scope)).toBe(false);
    expect(composeServerInstructions("full", [surface("a", [])], ["a"], "a", scope)).not.toMatch(NAMES_FEEDBACK_CALL);
  });
});
