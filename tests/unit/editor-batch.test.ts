/**
 * editor(batch): several actions sent to the editor as one game-thread job.
 *
 * What the Node side owes the caller: every step prepared exactly as a direct
 * call would be, the whole list refused before anything is sent when one step
 * cannot run, one `execute_batch` bridge call carrying the steps in order with
 * a budget that covers all of them, and each step's answer shaped by its own
 * projection. Ordering, stop and continue are the editor's job and are covered
 * by the plugin's UE.MCP.Bridge.Batch.* automation tests; here they are what
 * the wire carries and what comes back is read correctly.
 */
import { describe, expect, it } from "vitest";
import { z } from "zod";
import type { IBridge } from "../../src/bridge/bridge.js";
import type { ToolContext, ToolDef } from "../../src/core/types.js";
import { McpError, ErrorCode } from "../../src/core/errors.js";
import { bp, categoryTool } from "../../src/surface/category-tool.js";
import { injectEditorTarget } from "../../src/surface/target-params.js";
import { bridgeMethodEffect } from "../../src/surface/action-effects.js";
import { classifyWrite } from "../../src/flow/write-methods.js";
import { classifyAction } from "../../src/dispatch/locking.js";
import { editorTool } from "../../src/tools/editor.js";
import { assetTool } from "../../src/tools/asset.js";
import { admitBatchStep, batchBudgetMs } from "../../src/tools/editor/batch.js";
import { MAX_BATCH_CALLS } from "../../src/dispatch/batch-steps.js";
import { recordingBridge, type RecordingBridge } from "../fake-bridge.js";

/** A small category the steps can come from, with a read, a write and a long action. */
function probeTool(): ToolDef {
  return categoryTool(
    "probe",
    "Probe.",
    {
      go: bp("read", "Go. Params: assetPath", "probe_go"),
      write: bp("mutate", "Write. Params: assetPath, value", "probe_write"),
      slow: { kind: "bridge", effect: "mutate", description: "Slow. Params: assetPath", bridge: "probe_slow", timeoutMs: 120_000 },
    },
    { assetPath: z.string().optional(), value: z.number().optional() },
  );
}

function contextFor(bridge: IBridge, tools: ToolDef[] = [editorTool, assetTool, probeTool()]): ToolContext {
  return {
    bridge,
    project: {} as ToolContext["project"],
    getToolGraph: () => tools,
  } as ToolContext;
}

type Shaped = {
  success?: boolean;
  error?: string;
  onError: string;
  results: Array<{ index: number; action: string; success: boolean; result: unknown }>;
  [key: string]: unknown;
};

const run = (ctx: ToolContext, params: Record<string, unknown>) =>
  editorTool.handler(ctx, { action: "batch", ...params }) as Promise<Shaped>;

describe("editor(batch) sends one execute_batch call", () => {
  it("carries every step in order, each prepared as a direct call to its action", async () => {
    const bridge = recordingBridge({ success: true, requestedCalls: 2, completedCalls: 2, results: [] });
    await run(contextFor(bridge), {
      ops: [
        { category: "probe", method: "go", args: { assetPath: "\\Game\\A", select: ["kept"] } },
        { category: "probe", method: "write", args: { assetPath: "/Game/B", value: 2, timeoutMs: 45_000 } },
      ],
    });
    expect(bridge.calls).toEqual([{
      method: "execute_batch",
      params: {
        ops: [
          // The backslashes are repaired and the routing keys are consumed, so
          // neither select nor timeoutMs reaches the editor as a step argument.
          { method: "probe_go", params: { assetPath: "/Game/A" } },
          { method: "probe_write", params: { assetPath: "/Game/B", value: 2 } },
        ],
        onError: "stop",
      },
      // 30s default for the first step plus the 45s the second step asked for.
      timeoutMs: 75_000,
    }]);
  });

  it("forwards onError and the transaction label, and the batch's own timeoutMs wins over the sum", async () => {
    const bridge = recordingBridge({ success: true, results: [] });
    await run(contextFor(bridge), {
      timeoutMs: 20_000,
      onError: "continue",
      transactionDescription: "  Wire the door  ",
      ops: [
        { category: "probe", method: "slow", args: { assetPath: "/Game/A" } },
        { category: "probe", method: "slow", args: { assetPath: "/Game/B" } },
      ],
    });
    expect(bridge.calls[0].timeoutMs).toBe(20_000);
    expect(bridge.calls[0].params).toMatchObject({ onError: "continue", transactionDescription: "Wire the door" });
  });

  it("budgets the sum of the steps' own waits, capped at the ceiling", () => {
    expect(batchBudgetMs([{ budgetMs: 30_000 }, { budgetMs: 120_000 }])).toBe(150_000);
    expect(batchBudgetMs([{ budgetMs: 3_000_000 }, { budgetMs: 3_000_000 }])).toBe(3_600_000);
    expect(batchBudgetMs([{ budgetMs: 30_000 }], 5_000)).toBe(5_000);
  });

  it("shapes each step's answer under the action the caller wrote, through that step's own projection", async () => {
    const bridge = recordingBridge({
      success: false,
      requestedCalls: 3,
      completedCalls: 2,
      failedIndex: 1,
      failedCount: 1,
      stoppedEarly: true,
      transaction: { description: "MCP batch (3 calls)", undoEntry: true, nested: false },
      results: [
        { index: 0, method: "probe_go", success: true, result: { kept: 1, dropped: 2 } },
        { index: 1, method: "probe_write", success: false, result: { success: false, error: "no such asset" } },
      ],
    });
    const out = await run(contextFor(bridge), {
      ops: [
        { category: "probe", method: "go", args: { assetPath: "/Game/A", select: ["kept"] } },
        { category: "probe", method: "write", args: { assetPath: "/Game/B" } },
        { category: "probe", method: "write", args: { assetPath: "/Game/C" } },
      ],
    });
    expect(out.success).toBe(false);
    expect(out.results.map((r) => [r.index, r.action, r.success])).toEqual([
      [0, "probe.go", true],
      [1, "probe.write", false],
    ]);
    expect(out.results[0].result).toMatchObject({ kept: 1 });
    expect(out.results[0].result).not.toHaveProperty("dropped");
    expect(out.error).toBe(
      "ops[1] (probe.write) failed: no such asset. ops[2..2] were not run. Nothing is rolled back: the steps that succeeded stay applied.",
    );
    expect(out).toMatchObject({ onError: "stop", failedIndex: 1, completedCalls: 2, transaction: { undoEntry: true } });
  });
});

describe("editor(batch) refuses the whole batch before sending anything", () => {
  const refused = async (params: Record<string, unknown>, code: ErrorCode = ErrorCode.INVALID_PARAMS) => {
    const bridge = recordingBridge({ success: true, results: [] });
    let thrown: unknown;
    try {
      await run(contextFor(bridge), params);
    } catch (e) {
      thrown = e;
    }
    expect(thrown).toBeInstanceOf(McpError);
    expect((thrown as McpError).code).toBe(code);
    expect(bridge.calls, "nothing reached the editor").toEqual([]);
    return (thrown as McpError).message;
  };
  const probe = { category: "probe", method: "go", args: { assetPath: "/Game/A" } };

  it("a nested batch", async () => {
    expect(await refused({ ops: [probe, { category: "editor", method: "batch", args: { ops: [probe] } }] }))
      .toContain("ops[1] (editor.batch) is itself a batch");
  });

  it("the lock actions and the undo-stack actions", async () => {
    expect(await refused({ ops: [{ category: "asset", method: "lock", args: { assetPath: "/Game/A" } }] }))
      .toContain("manages the asset locks");
    expect(await refused({ ops: [probe, { category: "editor", method: "undo" }] }))
      .toContain("ops[1] (editor.undo) acts on the undo stack");
    expect(await refused({ ops: [{ category: "editor", method: "begin_transaction", args: { label: "x" } }] }))
      .toContain("one undo transaction");
  });

  it("an action served by this process, and one that answers a dialog", async () => {
    expect(await refused({ ops: [{ category: "editor", method: "start_editor" }] }))
      .toContain("runs in the server process");
    expect(await refused({ ops: [{ category: "editor", method: "list_dialogs" }] }))
      .toContain("served while the editor is blocked");
  });

  it("an unknown category, an unknown action, and a step that fails its own contract", async () => {
    expect(await refused({ ops: [{ category: "nope", method: "go" }] })).toContain("names category 'nope'");
    expect(await refused({ ops: [{ category: "probe", method: "gone" }] }, ErrorCode.NOT_FOUND))
      .toContain("Unknown action 'gone' on 'probe'");
    // `value` is declared as a number on the probe category.
    expect(await refused({ ops: [probe, { category: "probe", method: "write", args: { assetPath: "/Game/B", value: "two" } }] }))
      .toContain("ops[1] (probe.write)");
  });

  it("an empty list, a malformed step, too many steps, and an onError it does not know", async () => {
    expect(await refused({ ops: [] })).toContain("non-empty");
    expect(await refused({ ops: ["probe.go"] })).toContain("ops[0] must be an object");
    expect(await refused({ ops: [{ category: "probe" }] })).toContain("needs a 'category' and a 'method'");
    expect(await refused({ ops: Array.from({ length: MAX_BATCH_CALLS + 1 }, () => probe) })).toContain("at most 256");
    expect(await refused({ ops: [probe], onError: "retry" })).toContain("onError must be");
  });

  it("a step aimed at another editor", async () => {
    const tool = probeTool();
    injectEditorTarget(tool, ["A", "B"]);
    const bridge = recordingBridge({ success: true, results: [] });
    await expect(run(contextFor(bridge, [editorTool, tool]), {
      ops: [{ category: "probe", method: "go", args: { assetPath: "/Game/A", editor: "B" } }],
    })).rejects.toThrow("targets editor 'B', but a batch runs on one editor");
    expect(bridge.calls).toEqual([]);
  });

  it("a running plugin that does not register execute_batch, naming the upgrade", async () => {
    const bridge = { ...recordingBridge({ success: true }), registeredActions: ["probe_go"] } as unknown as RecordingBridge;
    await expect(run(contextFor(bridge), { ops: [probe] })).rejects.toMatchObject({
      code: ErrorCode.NOT_FOUND,
      message: expect.stringContaining("does not register"),
    });
    expect(bridge.calls).toEqual([]);
  });

  it("states every reason off the action's own spec", () => {
    const go = probeTool().actions.go;
    expect(admitBatchStep("probe.go", go)).toEqual({ admitted: true, spec: go });
    expect(admitBatchStep("editor.batch", editorTool.actions.batch)).toMatchObject({ admitted: false, why: expect.stringContaining("cannot nest") });
    expect(admitBatchStep("editor.execute_python", editorTool.actions.execute_python)).toMatchObject({ admitted: false, why: expect.stringContaining("server process") });
  });
});

describe("a batch that outlives the wait says how far it got", () => {
  const steps = [
    { category: "probe", method: "write", args: { assetPath: "/Game/A" } },
    { category: "probe", method: "write", args: { assetPath: "/Game/B" } },
    { category: "probe", method: "write", args: { assetPath: "/Game/C" } },
  ];
  const progress = { batch: { completedCalls: 2, requestedCalls: 3, currentIndex: 2 } };

  it("when the client stopped waiting, from the status the editor serves off the game thread", async () => {
    const bridge = recordingBridge();
    bridge.call = async (method: string) => {
      if (method === "execute_batch") {
        throw new McpError(ErrorCode.BRIDGE_TIMEOUT, "Bridge call 'execute_batch' timed out after 90s.", { outcome: "unknown", operationId: "7", method });
      }
      expect(method).toBe("get_engine_state");
      return progress;
    };
    let thrown: McpError | undefined;
    try {
      await run(contextFor(bridge), { ops: steps });
    } catch (e) {
      thrown = e as McpError;
    }
    expect(thrown?.code).toBe(ErrorCode.BRIDGE_TIMEOUT);
    expect(thrown?.message).toContain("finished 2 of 3 calls and was still running ops[2] (probe.write)");
    expect(thrown?.details).toMatchObject({ outcome: "unknown", batch: { requestedCalls: 3, completedCalls: 2, currentIndex: 2 } });
  });

  it("when the editor's own executor gave up, from the snapshot it attached", async () => {
    const bridge = recordingBridge({ success: false, error: "Handler execution timed out", reason: "timeout", engineState: progress });
    const out = await run(contextFor(bridge), { ops: steps });
    expect(out.success).toBe(false);
    expect(out.error).toContain("finished 2 of 3 calls");
    expect(out).toMatchObject({ completedCalls: 2, requestedCalls: 3 });
  });
});

describe("the gates see a batch as the union of its steps", () => {
  it("locking takes every mutating step's paths, once, and nothing for a batch of reads", () => {
    const mixed = classifyAction("editor.batch", {
      ops: [
        { category: "asset", method: "set_property", args: { assetPath: "/Game/A", propertyName: "X", value: 1 } },
        { category: "asset", method: "move", args: { sourcePath: "/Game/A", destinationPath: "/Game/B" } },
        { category: "asset", method: "list", args: { directory: "/Game" } },
      ],
    });
    expect(mixed.mutates).toBe(true);
    expect(mixed.paths.sort()).toEqual(["/Game/A", "/Game/B"]);
    expect(classifyAction("editor.batch", {
      ops: [{ category: "asset", method: "list_locks" }, { category: "asset", method: "list", args: { directory: "/Game" } }],
    })).toEqual({ mutates: false, paths: [] });
    // No steps to read: the declaration stands, and the empty list locks nothing.
    expect(classifyAction("editor.batch", {})).toEqual({ mutates: true, paths: [] });
  });

  it("the effect is the strongest step's, with unknown outranking mutate", () => {
    const effect = (ops: unknown[]) => bridgeMethodEffect("execute_batch", { ops }).effect;
    expect(effect([{ method: "list_locks", params: {} }])).toBe("read");
    expect(effect([{ method: "list_locks", params: {} }, { method: "save_asset", params: { assetPath: "/Game/A" } }])).toBe("mutate");
    expect(effect([{ method: "save_asset", params: { assetPath: "/Game/A" } }, { method: "execute_command", params: { command: "stat fps" } }])).toBe("unknown");
    expect(bridgeMethodEffect("execute_batch").effect).toBe("unknown");
  });

  it("source control sees every path the steps write", () => {
    const classified = classifyWrite("execute_batch", {
      ops: [
        { method: "save_asset", params: { assetPath: "/Game/Foo" } },
        { method: "move_asset", params: { sourcePath: "/Game/A", destinationPath: "/Game/B" } },
        { method: "list_locks", params: { assetPath: "/Game/Ignored" } },
      ],
    });
    expect(classified.writes).toBe(true);
    expect(classified.contentPaths.sort()).toEqual(["/Game/A", "/Game/B", "/Game/Foo"]);
    expect(classifyWrite("execute_batch", { ops: [{ method: "list_locks", params: {} }] })).toEqual({ writes: false, contentPaths: [] });
  });
});
