import { describe, it, expect } from "vitest";
import { ALL_TOOLS } from "../../src/tools.js";
import { BUILTIN_TASKS, builtinFlows } from "../../src/flow/builtins.js";
import { buildDefaults } from "../../src/flow/loader.js";
import { buildFlowRegistry } from "../../src/flow/registry.js";
import { GUARD_SCOPES, GuardScopeSchema } from "../../src/flow/guard-schema.js";
import { EDITOR_LOSS_POLICIES } from "../../src/flow/editor-loss.js";
import { UeMcpFlowDefinitionSchema } from "../../src/flow/schema.js";
import { assertNoLegacyGuardTasks, LEGACY_GUARD_TASKS_REMOVED_IN } from "../../src/flow/guards.js";
import { FLOW_HTTP_PORT } from "../../src/core/flow-http-port.js";
import { FlowVersionSchema } from "../../src/flow/schema.js";

describe("built-in tasks are declared once", () => {
  it("registers every built-in in the registry", async () => {
    const registered = buildFlowRegistry(ALL_TOOLS).listRegistered();
    for (const builtin of BUILTIN_TASKS) expect(registered, builtin.name).toContain(builtin.name);
  });

  it("lists in the default config exactly the built-ins that have a definition", () => {
    const tasks = buildDefaults(ALL_TOOLS).tasks as Record<string, { class_path: string }>;
    for (const builtin of BUILTIN_TASKS) {
      if (builtin.definition) expect(tasks[builtin.name]?.class_path, builtin.name).toBe(builtin.name);
      else expect(tasks[builtin.name], builtin.name).toBeUndefined();
    }
  });

  it("ships the demo flows from the data file", () => {
    const flows = Object.keys(builtinFlows());
    expect(flows).toEqual(
      expect.arrayContaining(["beacon", "neon_shrine", "neon_shrine_cleanup", "texture_bomb", "niagara_fire", "blockout_to_assets"]),
    );
    expect(flows).not.toContain("playtest");
  });

  it("hands out a copy of the flow data, so a layer that edits a flow cannot change the next load", () => {
    const first = builtinFlows() as Record<string, { description: string }>;
    first.beacon.description = "changed";
    expect((builtinFlows() as Record<string, { description: string }>).beacon.description).not.toBe("changed");
  });
});

describe("one declaration of scopes, policies and the port", () => {
  it("accepts every declared guard scope and no other", () => {
    for (const scope of GUARD_SCOPES) expect(GuardScopeSchema.safeParse(scope).success).toBe(true);
    expect(GuardScopeSchema.safeParse("everything").success).toBe(false);
  });

  it("accepts every declared editor-loss policy and no other", () => {
    for (const policy of EDITOR_LOSS_POLICIES) {
      expect(UeMcpFlowDefinitionSchema.safeParse({ steps: {}, on_editor_loss: policy }).success).toBe(true);
    }
    expect(UeMcpFlowDefinitionSchema.safeParse({ steps: {}, on_editor_loss: "ignore" }).success).toBe(false);
  });

  it("keeps the flow HTTP port in one constant", () => {
    expect(FLOW_HTTP_PORT).toBe(7723);
    expect(FlowVersionSchema.parse({ http: { port: FLOW_HTTP_PORT } }).http?.port).toBe(FLOW_HTTP_PORT);
  });
});

describe("the legacy guard task check", () => {
  it("names the release that removes it and what to rename the task to", () => {
    let message = "";
    try {
      assertNoLegacyGuardTasks(["guard.sandbox.beforeWrite"], { label: "ue-mcp.yml" });
    } catch (e) {
      message = (e as Error).message;
    }
    expect(message).toContain(`ue-mcp ${LEGACY_GUARD_TASKS_REMOVED_IN}`);
    expect(message).toContain("sandbox:");
    expect(message).toContain("delete the old");
  });
});
