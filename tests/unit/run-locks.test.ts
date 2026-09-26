/**
 * Asset locks are taken inside runAction, so a flow step locks exactly as a
 * live call does, and the scope is the whole run: a flow holds what its steps
 * wrote until the run ends, a live call until it returns.
 */
import { describe, expect, it } from "vitest";
import { buildFlowRegistry } from "../../src/flow/registry.js";
import { buildDefaults } from "../../src/flow/loader.js";
import { createFlowTool } from "../../src/flow/flow-tool.js";
import { FlowConfigSchema } from "../../src/flow/schema.js";
import { createLiveTask } from "../../src/flow/live-task.js";
import { lockScopeOpener, resolveLockingConfig } from "../../src/dispatch/locking.js";
import { bp, categoryTool } from "../../src/surface/category-tool.js";
import { runAction } from "../../src/flow/run-action.js";
import type { FlowContext } from "../../src/flow/context.js";
import type { IBridge } from "../../src/bridge/bridge.js";
import type { ToolContext } from "../../src/core/types.js";

const assetTool = categoryTool("asset", "Assets.", {
  list: bp("read", "list_assets"),
  delete: bp("mutate", "delete_asset"),
  rename: bp("mutate", "rename_asset"),
  author: {
    kind: "handler",
    effect: "mutate",
    description: "Delete then rename, through the actions.",
    handler: async (ctx, p) => {
      const del = assetTool.actions.delete;
      const ren = assetTool.actions.rename;
      await runAction(ctx as FlowContext, "asset.delete", del, { assetPath: p.assetPath });
      await runAction(ctx as FlowContext, "asset.rename", ren, { assetPath: p.assetPath, newName: "B" });
      return { ok: true };
    },
  },
});

const graph = [assetTool];

function context(fail: (method: string, params: Record<string, unknown>) => unknown = () => undefined) {
  const calls: string[] = [];
  const bridge = {
    isConnected: true,
    call: async (method: string, params: Record<string, unknown>) => {
      calls.push(`${method}:${String(params.path ?? params.assetPath ?? "")}`);
      const failed = fail(method, params);
      if (failed !== undefined) return failed;
      if (method === "acquire_lock") return { acquired: true };
      return { success: true };
    },
  } as unknown as IBridge;
  const ctx = {
    project: {},
    bridge,
    openAssetLocks: lockScopeOpener(resolveLockingConfig({ enabled: true }), () => graph),
  } as unknown as FlowContext;
  return { ctx, calls };
}

function flowTool(flows: Record<string, unknown>) {
  const registry = buildFlowRegistry([assetTool]);
  const config = FlowConfigSchema.parse({ ...buildDefaults([assetTool]), flows });
  return createFlowTool(registry, () => config);
}

const twoWrites = {
  go: {
    steps: {
      1: { task: "asset.delete", options: { assetPath: "/Game/A" } },
      2: { task: "asset.list", options: { directory: "/Game" } },
      3: { task: "asset.delete", options: { assetPath: "/Game/B" } },
      4: { task: "asset.rename", options: { assetPath: "/Game/A", newName: "C" } },
    },
  },
};

describe("flow steps take asset locks", () => {
  it("locks each write as its step starts and releases everything when the run ends", async () => {
    const { ctx, calls } = context();
    const result = await flowTool(twoWrites).handler(ctx as ToolContext, { action: "run", flowName: "go" }) as Record<string, unknown>;
    expect(result.success).toBe(true);
    expect(calls).toEqual([
      "acquire_lock:/Game/A",
      "delete_asset:/Game/A",
      "list_assets:",
      "acquire_lock:/Game/B",
      "delete_asset:/Game/B",
      // /Game/A is already held by this run, so it is not taken twice.
      "rename_asset:/Game/A",
      "release_lock:/Game/A",
      "release_lock:/Game/B",
    ]);
  });

  it("releases what the run holds after a step fails", async () => {
    const { ctx, calls } = context((method, params) =>
      method === "delete_asset" && params.assetPath === "/Game/B" ? { success: false, error: "nope" } : undefined);
    const result = await flowTool(twoWrites).handler(ctx as ToolContext, { action: "run", flowName: "go" }) as Record<string, unknown>;
    expect(result.success).toBe(false);
    expect(calls.slice(-2)).toEqual(["release_lock:/Game/A", "release_lock:/Game/B"]);
    expect(calls).not.toContain("rename_asset:/Game/A");
  });

  it("fails the step on a busy asset and still releases what it holds", async () => {
    const { ctx, calls } = context((method, params) =>
      method === "acquire_lock" && params.path === "/Game/B"
        ? { acquired: false, holder: { sessionId: "other", ttlSecondsRemaining: 9 } }
        : undefined);
    const result = await flowTool(twoWrites).handler(ctx as ToolContext, { action: "run", flowName: "go" }) as Record<string, unknown>;
    expect(result.success).toBe(false);
    expect(String((result.steps as Array<{ error?: { message: string } }>)[2].error?.message)).toMatch(/locked by other/);
    expect(calls).not.toContain("delete_asset:/Game/B");
    expect(calls.slice(-1)).toEqual(["release_lock:/Game/A"]);
  });

  it("takes no locks when locking is off", async () => {
    const { ctx, calls } = context();
    const off = { ...ctx, openAssetLocks: undefined } as ToolContext;
    await flowTool(twoWrites).handler(off, { action: "run", flowName: "go" });
    expect(calls.some((c) => c.includes("_lock"))).toBe(false);
  });
});

describe("live calls take asset locks", () => {
  it("locks a write for the length of the call and releases it after", async () => {
    const { ctx, calls } = context();
    const registry = buildFlowRegistry([assetTool]);
    const task = await createLiveTask({ registry }, ctx, "asset.delete", { assetPath: "/Game/A" });
    expect((await task.run()).success).toBe(true);
    expect(calls).toEqual(["acquire_lock:/Game/A", "delete_asset:/Game/A", "release_lock:/Game/A"]);
  });

  it("holds the parent's lock across its children instead of letting a child release it", async () => {
    const { ctx, calls } = context();
    const registry = buildFlowRegistry([assetTool]);
    const task = await createLiveTask({ registry }, ctx, "asset.author", { assetPath: "/Game/A" });
    expect((await task.run()).success).toBe(true);
    expect(calls).toEqual([
      "acquire_lock:/Game/A",
      "delete_asset:/Game/A",
      "rename_asset:/Game/A",
      "release_lock:/Game/A",
    ]);
  });

  it("reports a modal that refuses the lock request as the dialog refusal", async () => {
    const refusal = { success: false, dialogBlocking: true, error: "A modal is up." };
    const { ctx, calls } = context((method) => (method === "acquire_lock" ? refusal : undefined));
    const registry = buildFlowRegistry([assetTool]);
    const result = await (await createLiveTask({ registry }, ctx, "asset.delete", { assetPath: "/Game/A" })).run();
    expect(result.success).toBe(false);
    expect(result.data).toMatchObject({ dialogBlocking: true });
    expect(calls).toEqual(["acquire_lock:/Game/A"]);
  });
});
