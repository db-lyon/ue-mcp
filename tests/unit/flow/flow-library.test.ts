/**
 * The small flow library in the universal layer: every flow plans cleanly with
 * its params, freezes into valid input, and the ones that only talk to the
 * bridge do what they say against a fake editor.
 */
import { describe, expect, it } from "vitest";
import { FlowDefinitionSchema } from "@db-lyon/flowkit";
import { ALL_TOOLS } from "../../../src/tools.js";
import { buildDefaults, builtinFlows } from "../../../src/flow/loader.js";
import { FlowConfigSchema } from "../../../src/flow/schema.js";
import { buildFlowRegistry } from "../../../src/flow/registry.js";
import { createFlowTool } from "../../../src/flow/flow-tool.js";
import type { PlanPreflight } from "../../../src/flow/preflight.js";
import type { PlanValidation } from "../../../src/flow/validate-plan.js";
import type { FrozenPlan } from "../../../src/flow/freeze.js";
import type { ToolContext } from "../../../src/core/types.js";

/** Each library flow, and the params a caller passes it. */
const LIBRARY: Record<string, Record<string, unknown> | undefined> = {
  commit_blueprint: { assetPath: "/Game/Characters/BP_Hero" },
  commit_widget: { assetPath: "/Game/UI/WBP_Hud" },
  commit_niagara: { assetPath: "/Game/VFX/NS_Spark" },
  commit_statetree: { assetPath: "/Game/AI/ST_Guard" },
  commit_material: { assetPath: "/Game/Materials/M_Rock" },
  save_and_verify_clean: undefined,
  editor_up: undefined,
  clean_restart: undefined,
  cpp_iterate: undefined,
  qa_gate: { directory: "/Game/Characters" },
  playtest_smoke: undefined,
  sandbox: { folder: "/Game/Sandbox" },
  sandbox_body: undefined,
};

const registry = buildFlowRegistry(ALL_TOOLS);
const config = FlowConfigSchema.parse(buildDefaults(ALL_TOOLS));
const flow = createFlowTool(registry, () => config);

interface Editor {
  world: { mode: string; dirtyPackageCount: number };
  answers?: Record<string, (p: Record<string, unknown>) => unknown>;
}

function editor(state: Editor) {
  const calls: Array<[string, Record<string, unknown>]> = [];
  const ctx = {
    bridge: {
      isConnected: true,
      capabilities: { engineVersion: "5.6.1" },
      call: async (method: string, p: Record<string, unknown>) => {
        calls.push([method, p]);
        if (method === "get_world_state") return { success: true, ...state.world };
        return state.answers?.[method]?.(p) ?? { success: true };
      },
      getTarget: () => ({ projectPath: null, port: 0, portSource: "default" }),
    },
    project: { ensureLoaded: () => {} },
    getToolGraph: () => ALL_TOOLS,
  } as unknown as ToolContext;
  return { ctx, calls, methods: () => calls.map(([m]) => m).filter((m) => m !== "get_world_state") };
}

describe("the flow library", () => {
  it("ships every flow in the universal layer", () => {
    const universal = builtinFlows();
    for (const name of Object.keys(LIBRARY)) expect(universal[name], name).toBeDefined();
  });

  for (const [name, params] of Object.entries(LIBRARY)) {
    it(`${name} plans cleanly and freezes into valid input`, async () => {
      const { ctx } = editor({ world: { mode: "editor", dirtyPackageCount: 0 } });
      const plan = await flow.handler(ctx, { action: "plan", flowName: name, ...(params ? { params } : {}) }) as {
        preflight: PlanPreflight;
        validation: PlanValidation;
        frozen: FrozenPlan;
      };
      expect(plan.validation, `${name} validation`).toEqual({ ok: true, problems: [] });
      expect(plan.preflight.refused, `${name} would be refused`).toEqual([]);
      expect(plan.preflight.ok).toBe(true);
      expect(plan.frozen.definition, `${name} did not freeze`).toBeDefined();
      expect(FlowDefinitionSchema.safeParse(plan.frozen.definition).success).toBe(true);
    });
  }

  it("names what a param-taking flow is missing when run without it", async () => {
    const { ctx, calls } = editor({ world: { mode: "editor", dirtyPackageCount: 0 } });
    const err = await flow.handler(ctx, { action: "run", flowName: "commit_blueprint" }).catch((e: Error) => e);
    expect(String(err)).toMatch(/params\.assetPath/);
    expect(calls).toEqual([]);
  });
});

describe("the library against a fake editor", () => {
  it("commit_blueprint compiles, then saves the same asset", async () => {
    const e = editor({ world: { mode: "editor", dirtyPackageCount: 0 } });
    const body = await flow.handler(e.ctx, { action: "run", flowName: "commit_blueprint", params: LIBRARY.commit_blueprint }) as { success: boolean };
    expect(body.success).toBe(true);
    expect(e.methods()).toEqual(["compile_blueprint", "save_asset"]);
    expect(e.calls.find(([m]) => m === "save_asset")?.[1].assetPath).toBe("/Game/Characters/BP_Hero");
  });

  it("commit_niagara hands the system to the compile and saves it", async () => {
    const e = editor({ world: { mode: "editor", dirtyPackageCount: 0 } });
    const body = await flow.handler(e.ctx, { action: "run", flowName: "commit_niagara", params: LIBRARY.commit_niagara }) as { success: boolean };
    expect(body.success).toBe(true);
    expect(e.calls.find(([m]) => m === "compile_niagara_system")?.[1].systemPath).toBe("/Game/VFX/NS_Spark");
  });

  it("a failed compile stops the commit before the save", async () => {
    const e = editor({
      world: { mode: "editor", dirtyPackageCount: 0 },
      answers: { compile_blueprint: () => ({ success: false, error: "Blueprint has compile errors" }) },
    });
    const body = await flow.handler(e.ctx, { action: "run", flowName: "commit_blueprint", params: LIBRARY.commit_blueprint }) as {
      success: boolean;
      error: { class: string; step: string };
    };
    expect(body.success).toBe(false);
    expect(e.methods()).toEqual(["compile_blueprint"]);
    expect(body.error).toMatchObject({ class: "failure", step: "1" });
  });

  it("save_and_verify_clean fails when a package is still dirty after the save", async () => {
    const e = editor({ world: { mode: "editor", dirtyPackageCount: 2 } });
    const body = await flow.handler(e.ctx, { action: "run", flowName: "save_and_verify_clean" }) as {
      success: boolean;
      error: { class: string; message: string };
    };
    expect(body.success).toBe(false);
    expect(body.error).toMatchObject({ class: "precondition", message: expect.stringMatching(/still dirty/) });
    expect(e.methods()).toEqual(["save_dirty"]);
  });

  it("save_and_verify_clean passes when the save left nothing dirty", async () => {
    const e = editor({ world: { mode: "editor", dirtyPackageCount: 0 } });
    const body = await flow.handler(e.ctx, { action: "run", flowName: "save_and_verify_clean" }) as { success: boolean };
    expect(body.success).toBe(true);
    expect(e.methods()).toEqual(["save_dirty", "list_dirty_packages"]);
  });

  it("playtest_smoke refuses to start over a running session", async () => {
    const e = editor({ world: { mode: "play", dirtyPackageCount: 0 } });
    const body = await flow.handler(e.ctx, { action: "run", flowName: "playtest_smoke" }) as { success: boolean };
    expect(body.success).toBe(false);
    expect(e.methods()).not.toContain("pie_control");
  });

  it("playtest_smoke starts PIE, reads the pawn, and always stops PIE", async () => {
    const e = editor({
      world: { mode: "editor", dirtyPackageCount: 0 },
      answers: { check_for_crashes: () => ({ success: false, error: "A crash was recorded" }) },
    });
    const body = await flow.handler(e.ctx, { action: "run", flowName: "playtest_smoke" }) as { success: boolean };
    expect(body.success).toBe(false);
    expect(e.methods()).toEqual(["pie_control", "get_pie_pawn", "check_for_crashes", "pie_control"]);
    expect(e.calls.filter(([m]) => m === "pie_control").map(([, p]) => p.pieAction)).toEqual(["start", "stop"]);
  });

  it("sandbox builds its scratch level, runs the body, and cleans up", async () => {
    const e = editor({
      world: { mode: "editor", dirtyPackageCount: 0 },
      answers: { get_current_level: () => ({ success: true, mapPackagePath: "/Game/Maps/Start" }) },
    });
    const body = await flow.handler(e.ctx, { action: "run", flowName: "sandbox", params: LIBRARY.sandbox }) as { success: boolean };
    expect(body.success).toBe(true);
    expect(e.methods()).toEqual([
      "get_current_level", "create_folder", "create_new_level", "get_current_level", "load_level", "delete_folder",
    ]);
    expect(e.calls.find(([m]) => m === "create_new_level")?.[1].levelPath).toBe("/Game/Sandbox/L_Sandbox");
    expect(e.calls.find(([m]) => m === "load_level")?.[1].levelPath).toBe("/Game/Maps/Start");
  });
});
