/**
 * flow(plan) hands back the flow as declared, beside the ordered plan: each
 * step's task, options and the ${steps.N...} references between steps, which is
 * what a reader needs to draw how the steps connect.
 */
import { describe, expect, it } from "vitest";
import { FlowConfigSchema } from "../../src/flow/schema.js";
import { buildFlowRegistry } from "../../src/flow/registry.js";
import { createFlowTool } from "../../src/flow/flow-tool.js";
import { ProjectContext } from "../../src/config/project.js";
import { categoryTool } from "../../src/surface/category-tool.js";
import type { IBridge } from "../../src/bridge/bridge.js";

const bridge: IBridge = {
  isConnected: false,
  connect: async () => {},
  retargetProject: () => ({ projectPath: null, port: 0, portSource: "default" as const, verified: true }),
  getTarget: () => ({ projectPath: null, port: 0, portSource: "default" as const, verified: true }),
  call: async () => ({}),
};

const assetTool = categoryTool("asset", "Test-only category.", {
  create: { kind: "handler", effect: "mutate", description: "Create.", handler: async () => ({ path: "/Game/X" }) },
  rename: { kind: "handler", effect: "mutate", description: "Rename.", handler: async () => ({}) },
}, {});

const config = () => FlowConfigSchema.parse({
  "ue-mcp": { version: 1 },
  flows: {
    make_and_rename: {
      description: "creates an asset, then renames it",
      steps: {
        "1": { task: "asset.create", options: { name: "X" } },
        "2": { task: "asset.rename", options: { path: "${steps.1.path}", newName: "Y" } },
      },
    },
  },
});

describe("flow plan definition", () => {
  const tool = createFlowTool(buildFlowRegistry([assetTool]), () => config());

  it("returns each step as declared, references unresolved", async () => {
    const out = (await tool.handler({ bridge, project: new ProjectContext() }, { action: "plan", flowName: "make_and_rename" })) as {
      steps: unknown[];
      definition: { description: string; steps: Record<string, { task: string; options?: Record<string, unknown> }> };
    };
    expect(out.steps).toHaveLength(2);
    expect(out.definition.description).toBe("creates an asset, then renames it");
    expect(out.definition.steps["1"].task).toBe("asset.create");
    expect(out.definition.steps["2"].options?.path).toBe("${steps.1.path}");
  });
});
