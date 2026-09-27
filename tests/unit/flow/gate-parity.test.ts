/**
 * The gates are enforced from their declarations (src/flow/gates.ts), and
 * refuse exactly what the hard-coded conditions they replaced refused, in the
 * same words. Each oracle below is the removed logic, kept verbatim.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import { ALL_TOOLS } from "../../../src/tools.js";
import { taskEffect } from "../../../src/surface/action-effects.js";
import { requiresExplicitEditor, type ActionClass } from "../../../src/surface/action-class.js";
import { refuseUntargetedCall, refuseUntargetedInRegistry } from "../../../src/dispatch/editor-gate.js";
import { dialogGate, GATES, gateFires } from "../../../src/flow/gates.js";
import { gateChecks } from "../../../src/flow/preflight.js";
import {
  evaluateGate,
  gateCandidates,
  gateRefusalMessage,
  notInRunningPlugin,
  resetRulings,
} from "../../../src/dispatch/python-gate.js";
import { getWorkarounds, pushWorkaround, resetAllWorkarounds } from "../../../src/dispatch/workaround-tracker.js";
import { editorTool } from "../../../src/tools/editor.js";
import { PLUGIN_UPGRADE_POINTER } from "../../../src/bridge/bridge.js";
import type { SessionRegistry } from "../../../src/sessions/session.js";
import type { GuardDecision } from "../../../src/editor/dialog-guard.js";
import type { IBridge } from "../../../src/bridge/bridge.js";
import type { ToolContext } from "../../../src/core/types.js";

// ── The untargeted gate ──────────────────────────────────────────────────────

const OLD_ADDRESSES_THE_SERVER = new Set([
  "project.list_editors",
  "project.use_editor",
  "project.add_editor",
  "project.drop_editor",
]);

function oldDescribeWhy(taskName: string, cls: ActionClass): string {
  return cls === "unknown"
    ? `'${taskName}' does whatever its parameters say, so it is treated as a change.`
    : `'${taskName}' changes the editor it runs in.`;
}

function oldRefuseUntargetedCall(call: { taskName: string; editors: string[]; activeEditor: string; targetParam: string; graph?: readonly never[] | typeof ALL_TOOLS }): string | null {
  if (OLD_ADDRESSES_THE_SERVER.has(call.taskName)) return null;
  const { effect: cls } = taskEffect(call.taskName, call.graph);
  if (!requiresExplicitEditor(cls)) return null;
  const others = call.editors.filter((n) => n !== call.activeEditor);
  return (
    `${oldDescribeWhy(call.taskName, cls)} This server drives ${call.editors.length} editors ` +
    `(${call.editors.join(", ")}), so it will not pick one for you: an untargeted run would go to ` +
    `'${call.activeEditor}'${others.length > 0 ? `, leaving ${others.join(", ")} untouched` : ""}. ` +
    `Re-send it with ${call.targetParam}="<name>". ` +
    `project(action='list_editors') reports what each one is. Reads do not need this.`
  );
}

function oldRefuseUntargetedInRegistry(sessions: SessionRegistry, taskName: string, targeted: boolean, graph?: typeof ALL_TOOLS): string | null {
  if (sessions.size <= 1 || targeted) return null;
  return oldRefuseUntargetedCall({
    taskName,
    editors: sessions.list().map((s) => s.name),
    activeEditor: sessions.active.name,
    targetParam: "editor",
    graph,
  });
}

function registry(names: string[]): SessionRegistry {
  const list = names.map((name) => ({ name }));
  return { size: list.length, list: () => list, active: list[0] } as unknown as SessionRegistry;
}

const TASKS = [
  ...ALL_TOOLS.flatMap((t) => Object.keys(t.actions).map((a) => `${t.name}.${a}`)),
  "flow.run", "flow.plan", "flow.list", "someplugin.frobnicate", "shell",
];

describe("the untargeted gate, declared", () => {
  it("refuses exactly what the hard-coded condition refused, in the same words", () => {
    let refusals = 0;
    for (const names of [["alpha"], ["alpha", "beta"], ["alpha", "beta", "gamma"]]) {
      const sessions = registry(names);
      for (const task of TASKS) {
        for (const targeted of [false, true]) {
          const expected = oldRefuseUntargetedInRegistry(sessions, task, targeted, ALL_TOOLS);
          expect(refuseUntargetedInRegistry(sessions, task, targeted, ALL_TOOLS), `${names.length} ${task} ${targeted}`).toBe(expected);
          if (expected) refusals++;
        }
        if (names.length > 1) {
          const call = { taskName: task, editors: names, activeEditor: names[0], targetParam: "editor" };
          expect(refuseUntargetedCall(call), task).toBe(oldRefuseUntargetedCall(call));
        }
      }
    }
    expect(refusals).toBeGreaterThan(1000);
  });

  it("reads the session count from the session namespace", () => {
    const facts = (count: number) => ({ session: { count }, call: { targeted: false, needs_target: true } });
    expect(gateFires("untargeted", facts(1))).toBe(false);
    expect(gateFires("untargeted", facts(2))).toBe(true);
  });

  it("gives a plan the run's refusal, and the old wording otherwise", () => {
    const ctx = { sessions: registry(["alpha", "beta"]) } as unknown as ToolContext;
    const flow = gateChecks(ctx, ALL_TOOLS).flow;
    expect(flow).toEqual([{
      when: "gate.untargeted",
      action: "error",
      message: oldRefuseUntargetedCall({ taskName: "flow.run", editors: ["alpha", "beta"], activeEditor: "alpha", targetParam: "editor", graph: ALL_TOOLS }),
    }]);
    expect(gateChecks({} as ToolContext, []).flow[0].message).toBe("An untargeted run would be refused.");
    expect(gateChecks({} as ToolContext, []).step.map((c) => [c.when, c.message])).toEqual([
      ["gate.editor", GATES.editor.message],
      ["gate.dialog", GATES.dialog.message],
      ["gate.python", GATES.python.message],
    ]);
  });
});

// ── The dialog gate ─────────────────────────────────────────────────────────

describe("the dialog gate, declared", () => {
  const refusal = { success: false, dialogBlocking: true, error: "a modal is up" };
  const guard = (decision: GuardDecision) => {
    const asked: unknown[][] = [];
    return { asked, check: async (...args: unknown[]) => { asked.push(args); return decision; } };
  };
  const oldGate = (decision: GuardDecision) => (decision.allow ? null : decision.refusal);

  for (const decision of [{ allow: true } as GuardDecision, { allow: false, refusal } as GuardDecision]) {
    it(`answers ${decision.allow ? "a clear editor" : "a blocked one"} as the guard's decision did`, async () => {
      for (const [kind, opts] of [["action", undefined], ["bridge", undefined], ["action", { canElicit: false }]] as const) {
        const g = guard(decision);
        expect(await dialogGate(g, "asset.list", kind, opts)).toEqual(oldGate(decision));
        // The guard is asked exactly as the routes asked it.
        expect(g.asked).toEqual([opts === undefined ? ["asset.list", kind] : ["asset.list", kind, opts]]);
      }
    });
  }
});

// ── The Python gate ─────────────────────────────────────────────────────────

describe("the python gate, declared", () => {
  const bridge = {
    isConnected: true,
    call: async () => ({ success: true }),
    getTarget: () => ({ projectPath: null, port: 0, portSource: "default", verified: false }),
  } as unknown as IBridge;
  const ctx = {
    bridge,
    project: {} as ToolContext["project"],
    session: { key: "c:/proj/parity" },
    getToolGraph: () => ALL_TOOLS,
  } as unknown as ToolContext;

  /** The handler's gate as it was, up to the point Python runs. */
  async function oldHandlerGate(params: Record<string, unknown>): Promise<Record<string, unknown> | null> {
    const code = (params.code as string) ?? "";
    const taskSummary = ((params.taskSummary as string) ?? "").trim();
    if (!taskSummary) {
      return {
        blocked: true,
        reason: "missing_task_summary",
        message: "execute_python requires a 'taskSummary' (plain-words intent). It is searched against the tool registry and gated behind ruling out every candidate. Re-call with taskSummary.",
      };
    }
    const candidates = gateCandidates(ctx, taskSummary);
    if (candidates.length > 0) {
      const verdict = evaluateGate(candidates, params.ruledOut, ctx, await notInRunningPlugin(ctx));
      if (verdict.unresolved.length > 0) {
        pushWorkaround({ code, timestamp: "t", taskSummary, suggestedTool: candidates.map((c) => `${c.tool}(${c.action})`).join(", ") }, ctx);
        return {
          blocked: true,
          reason: "candidates_not_ruled_out",
          taskSummary,
          candidates,
          needReasonFor: verdict.unresolved.map((c) => `${c.tool}(${c.action})`),
          sendThisBack: { ruledOut: verdict.ruledOutTemplate },
          alreadyRuledOut: verdict.satisfied,
          ...(verdict.notInPlugin.length > 0
            ? { notInRunningPlugin: verdict.notInPlugin.map((c) => `${c.tool}(${c.action})`), upgrade: PLUGIN_UPGRADE_POINTER }
            : {}),
          ignoredEntries: verdict.rejected,
          message: gateRefusalMessage(taskSummary, candidates, verdict),
        };
      }
    }
    return null;
  }

  async function newHandler(params: Record<string, unknown>): Promise<Record<string, unknown> | null> {
    const out = await editorTool.actions.execute_python.handler!(ctx, params) as Record<string, unknown>;
    return out.blocked ? out : null;
  }

  const summary = "invoke a ufunction on an actor component";
  const REASON = "the function is on a component this call cannot reach";

  beforeEach(() => { resetRulings(); resetAllWorkarounds(); });
  afterEach(() => { resetRulings(); resetAllWorkarounds(); });

  /** Run one sequence through a gate, from clean state, recording what it answered and remembered. */
  async function replay(gate: (p: Record<string, unknown>) => Promise<Record<string, unknown> | null>, seq: Array<(prev: Record<string, unknown> | null) => Record<string, unknown>>) {
    resetRulings();
    resetAllWorkarounds();
    const out: Array<Record<string, unknown> | null> = [];
    let prev: Record<string, unknown> | null = null;
    for (const next of seq) {
      prev = await gate(next(prev));
      out.push(prev);
    }
    return { out, workarounds: getWorkarounds(ctx).filter((w) => w.suggestedTool !== undefined).map((w) => w.suggestedTool) };
  }

  it("refuses, remembers and clears in the same order and shape", async () => {
    const seq: Array<(prev: Record<string, unknown> | null) => Record<string, unknown>> = [
      () => ({ code: "print(1)" }),
      () => ({ code: "print(1)", taskSummary: "   " }),
      () => ({ code: "print(1)", taskSummary: summary }),
      () => ({ code: "print(1)", taskSummary: summary, ruledOut: ["editor(invoke_function)", { action: "x", reason: "short" }] }),
      (prev) => ({
        code: "print(1)",
        taskSummary: summary,
        ruledOut: ((prev?.sendThisBack as { ruledOut?: Array<{ action: string }> } | undefined)?.ruledOut ?? []).slice(0, 1).map((e) => ({ action: e.action, reason: REASON })),
      }),
      () => ({ code: "print(1)", taskSummary: `${summary} please` }),
    ];
    const before = await replay(oldHandlerGate, seq);
    const after = await replay(newHandler, seq);
    expect(after.out).toEqual(before.out);
    expect(after.workarounds).toEqual(before.workarounds);
    expect(before.out.filter((o) => o !== null).length).toBeGreaterThan(3);
  });
});
