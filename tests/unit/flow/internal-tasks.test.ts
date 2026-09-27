/**
 * The internal primitives are tasks a flow can name and never actions a
 * client can call: registered in the flow registry, absent from every tool.
 */
import { describe, expect, it } from "vitest";
import { ALL_TOOLS } from "../../../src/tools.js";
import { buildFlowRegistry } from "../../../src/flow/registry.js";
import { internalTasks } from "../../../src/flow/internal-tasks.js";
import { bridgeMethodEffect, taskEffect } from "../../../src/surface/action-effects.js";

const registry = buildFlowRegistry(ALL_TOOLS);

describe("internal tasks", () => {
  it("are registered as tasks", () => {
    const registered = new Set(registry.listRegistered());
    for (const [name] of internalTasks()) expect(registered.has(name), name).toBe(true);
  });

  it("are no tool's actions, so no surface lists them", () => {
    const tools = new Set(ALL_TOOLS.map((t) => t.name));
    expect(tools.has("internal")).toBe(false);
    for (const [, task] of internalTasks()) {
      for (const tool of ALL_TOOLS) {
        for (const spec of Object.values(tool.actions)) {
          expect(spec.kind === "bridge" && spec.bridge === task.method, `${tool.name} exposes ${task.method}`).toBe(false);
        }
      }
    }
  });

  it("declare their effect to every gate", () => {
    for (const [name, task] of internalTasks()) {
      expect(taskEffect(name)).toEqual({ effect: task.effect, source: "declared" });
      expect(bridgeMethodEffect(task.method)).toEqual({ effect: task.effect, source: "declared" });
    }
  });

  it("describe their options from the recorded spec", async () => {
    for (const [name] of internalTasks()) {
      const described = await registry.describe(name);
      expect(Object.keys(described.options_schema ?? {}).length, name).toBeGreaterThan(0);
    }
  });
});
