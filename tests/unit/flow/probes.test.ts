/**
 * Probes: editor facts read once per run by `when:`, checks and gates, and a
 * failing probe as an error rather than a false.
 */
import { describe, expect, it } from "vitest";
import { BridgeFacts, PROBE_NAMES, ProbeError } from "../../../src/flow/probes.js";
import { hostNamespaces, makeConditionEvaluator } from "../../../src/flow/condition.js";
import { buildFlowRegistry } from "../../../src/flow/registry.js";
import { buildDefaults } from "../../../src/flow/loader.js";
import { createFlowTool } from "../../../src/flow/flow-tool.js";
import { FlowConfigSchema } from "../../../src/flow/schema.js";
import { bp, categoryTool } from "../../../src/surface/category-tool.js";
import type { ToolContext } from "../../../src/core/types.js";
import type { IBridge } from "../../../src/bridge/bridge.js";

interface World { mode: string; dirtyPackageCount: number }

function editor(world: () => World | Error, connected = true) {
  const calls: string[] = [];
  const ctx = {
    project: { projectName: "Shrine" },
    bridge: {
      isConnected: connected,
      capabilities: { engineVersion: "5.6.1" },
      call: async (method: string) => {
        calls.push(method);
        if (method === "get_world_state") {
          const w = world();
          if (w instanceof Error) throw w;
          return { success: true, ...w };
        }
        return { success: true };
      },
    } as unknown as IBridge,
    getToolGraph: () => [tool],
  } as unknown as ToolContext;
  return { ctx, calls };
}

const tool = categoryTool("asset", "Assets.", {
  read: bp("read", "read_asset"),
  save: bp("mutate", "save_asset"),
});

describe("BridgeFacts", () => {
  it("reads each probe once until invalidated", async () => {
    const { ctx, calls } = editor(() => ({ mode: "editor", dirtyPackageCount: 2 }));
    const facts = new BridgeFacts(ctx);
    expect(await facts.read("dirty")).toBe(2);
    expect(await facts.read("playing")).toBe(false);
    expect(calls).toEqual(["get_world_state"]);
    facts.invalidate();
    await facts.read("world");
    expect(calls).toEqual(["get_world_state", "get_world_state"]);
    expect(await facts.read("engine")).toBe("5.6.1");
    expect(PROBE_NAMES).toEqual(expect.arrayContaining(["connected", "world", "dirty", "playing", "engine"]));
  });

  it("fails a probe it cannot read, naming it", async () => {
    const facts = new BridgeFacts(editor(() => ({ mode: "editor", dirtyPackageCount: 0 }), false).ctx);
    await expect(facts.read("world")).rejects.toThrow(ProbeError);
    await expect(facts.read("dirty")).rejects.toThrow(/probe\.world: no editor is connected/);
    await expect(facts.read("nope")).rejects.toThrow(/no such probe/);
    expect(await facts.read("connected")).toBe(false);
  });
});

describe("probes in expressions", () => {
  const evaluatorFor = (connected: boolean) => {
    const { ctx } = editor(() => ({ mode: "play", dirtyPackageCount: 0 }), connected);
    return makeConditionEvaluator(hostNamespaces(ctx), undefined, new BridgeFacts(ctx));
  };
  const at = { steps: [], context: {} } as never;

  it("reads them by name", async () => {
    expect(await evaluatorFor(true)("probe.world.mode == 'play' && probe.playing", at)).toBe(true);
  });

  it("is an error when the probe fails, never false", async () => {
    await expect(evaluatorFor(false)("probe.world.mode == 'play'", at)).rejects.toThrow(/probe\.world/);
    await expect(evaluatorFor(false)("!probe.playing", at)).rejects.toThrow(/probe\.world/);
  });

  it("never reads a probe a short-circuit skips", async () => {
    expect(await evaluatorFor(false)("probe.connected && probe.world.mode == 'play'", at)).toBe(false);
    expect(await evaluatorFor(false)("!probe.connected || probe.dirty > 0", at)).toBe(true);
  });
});

describe("probes in a flow run", () => {
  function run(flow: Record<string, unknown>, world: () => World | Error, connected = true) {
    const { ctx, calls } = editor(world, connected);
    const config = FlowConfigSchema.parse({ ...buildDefaults([tool]), flows: { go: flow } });
    const tool_ = createFlowTool(buildFlowRegistry([tool]), () => config);
    return { calls, result: tool_.handler(ctx, { action: "run", flowName: "go" }) as Promise<Record<string, unknown>> };
  }

  it("shares one reading between reads, and reads again after a write", async () => {
    const { calls, result } = run({
      steps: {
        1: { task: "asset.read", when: "probe.dirty > 0", options: { assetPath: "/Game/A" } },
        2: { task: "asset.read", when: "probe.dirty > 0", options: { assetPath: "/Game/A" } },
        3: { task: "asset.save", options: { assetPath: "/Game/A" } },
        4: { task: "asset.read", when: "probe.dirty > 0", options: { assetPath: "/Game/A" } },
      },
    }, () => ({ mode: "editor", dirtyPackageCount: 3 }));
    const saved = await result;
    expect(saved.success).toBe(true);
    expect(calls).toEqual(["get_world_state", "read_asset", "read_asset", "save_asset", "get_world_state", "read_asset"]);
  });

  it("gives the gates the same reading of the connection", async () => {
    const { calls, result } = run({
      steps: { 1: { task: "asset.read", when: "!probe.connected", options: { assetPath: "/Game/A" } } },
    }, () => ({ mode: "editor", dirtyPackageCount: 0 }));
    const body = await result;
    expect(body.success).toBe(true);
    expect((body.steps as Array<{ skipped: boolean }>)[0].skipped).toBe(true);
    expect(calls).toEqual([]);
  });

  it("fails the step whose check reads a probe that fails", async () => {
    const { result } = run({
      steps: {
        1: {
          task: "asset.read",
          options: { assetPath: "/Game/A" },
          checks: [{ when: "probe.dirty > 0", action: "error", message: "Unsaved packages." }],
        },
      },
    }, () => new Error("get_world_state exploded"));
    const body = await result;
    expect(body.success).toBe(false);
    expect(JSON.stringify(body)).toMatch(/probe\.world/);
  });
});
