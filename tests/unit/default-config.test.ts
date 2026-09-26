import { describe, it, expect } from "vitest";
import yaml from "js-yaml";
import { generateDefaultConfig } from "../../scripts/generate-default-config.js";
import { ALL_TOOLS } from "../../src/tools.js";
import { FlowConfigSchema } from "../../src/flow/schema.js";

describe("dist/ue-mcp.default.yml", () => {
  const text = generateDefaultConfig();
  const doc = yaml.load(text) as { tasks: Record<string, { class_path: string; group: string; options?: unknown }> };

  it("parses as a flow config", () => {
    expect(FlowConfigSchema.safeParse(doc).success).toBe(true);
  });

  it("declares a task for every action of every category the server ships", () => {
    for (const tool of ALL_TOOLS) {
      for (const action of Object.keys(tool.actions)) {
        expect(doc.tasks[`${tool.name}.${action}`], `${tool.name}.${action}`).toBeDefined();
      }
    }
  });

  it("uses the per-action class path, not the generic bridge task with a method option", () => {
    for (const [name, task] of Object.entries(doc.tasks)) {
      if (name === "shell") continue;
      expect(task.class_path, name).toBe(name);
      expect(task.options, name).toBeUndefined();
    }
  });
});
