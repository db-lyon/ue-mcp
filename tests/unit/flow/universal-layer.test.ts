/**
 * The universal layer is shipped config, and the config is derivable.
 *
 * universal/ue-mcp.universal.yml is what the runtime reads; the generator
 * writes it from the tool declarations, the beacon and demo builders and the
 * hand-authored universal/flows.yml. Regenerating must reproduce the committed
 * bytes, so neither side can drift without this failing.
 */
import { describe, it, expect } from "vitest";
import * as fs from "node:fs";
import yaml from "js-yaml";
import { generateUniversal, OUTPUT } from "../../../scripts/generate-universal.js";
import { ALL_TOOLS } from "../../../src/tools.js";
import { buildDefaults, builtinFlows } from "../../../src/flow/loader.js";
import { universalPath } from "../../../src/flow/universal.js";
import { FlowConfigSchema } from "../../../src/flow/schema.js";
import { buildFlowRegistry } from "../../../src/flow/registry.js";
import { createFlowTool } from "../../../src/flow/flow-tool.js";
import type { PlanPreflight } from "../../../src/flow/preflight.js";
import type { ToolContext } from "../../../src/core/types.js";
import { repoPath } from "../../helpers/repo-root.js";

describe("universal/ue-mcp.universal.yml", () => {
  it("is exactly what the generator produces", () => {
    expect(fs.readFileSync(OUTPUT, "utf-8") === generateUniversal(), "stale: run npm run generate:universal").toBe(true);
  });

  it("is the file the runtime reads, and ships in the package", () => {
    expect(universalPath()).toBe(OUTPUT);
    const pkg = JSON.parse(fs.readFileSync(repoPath("package.json"), "utf-8")) as { files: string[] };
    expect(pkg.files).toContain("universal/ue-mcp.universal.yml");
  });

  it("parses as a flow config with a task for every shipped action", () => {
    const doc = yaml.load(fs.readFileSync(OUTPUT, "utf-8")) as { tasks: Record<string, unknown> };
    expect(FlowConfigSchema.safeParse(doc).success).toBe(true);
    for (const tool of ALL_TOOLS) {
      for (const action of Object.keys(tool.actions)) expect(doc.tasks[`${tool.name}.${action}`], `${tool.name}.${action}`).toBeDefined();
    }
  });

  it("keeps the hand-authored flows as written", () => {
    const authored = (yaml.load(fs.readFileSync(repoPath("universal", "flows.yml"), "utf-8")) as { flows: Record<string, unknown> }).flows;
    const flows = builtinFlows();
    for (const [name, def] of Object.entries(authored)) expect(flows[name], name).toEqual(def);
  });

  it("hands out copies, so a caller cannot edit the shared layer", () => {
    const a = builtinFlows() as Record<string, { description?: string }>;
    a.beacon.description = "changed";
    expect((builtinFlows() as Record<string, { description?: string }>).beacon.description).not.toBe("changed");
  });
});

describe("the universal flows, planned", () => {
  const registry = buildFlowRegistry(ALL_TOOLS);
  const config = FlowConfigSchema.parse(buildDefaults(ALL_TOOLS));
  const ctx = {
    bridge: {
      isConnected: true,
      capabilities: undefined,
      call: async () => { throw new Error("a plan never calls the editor"); },
      getTarget: () => ({ projectPath: null, port: 0, portSource: "default" }),
    },
    project: { ensureLoaded: () => {} },
    getToolGraph: () => ALL_TOOLS,
  } as unknown as ToolContext;
  const flow = createFlowTool(registry, () => config);

  for (const name of Object.keys(builtinFlows())) {
    it(`${name} plans with nothing refused`, async () => {
      const plan = await flow.handler(ctx, { action: "plan", flowName: name }) as {
        steps: Array<{ name: string }>;
        preflight: PlanPreflight;
      };
      expect(plan.preflight.refused, `${name} would be refused`).toEqual([]);
      expect(plan.preflight.ok).toBe(true);
      const declared = Object.values((config.flows[name] as { steps: Record<string, { task?: string; flow?: string }> }).steps)
        .map((s) => s.task ?? s.flow);
      expect(plan.steps.map((s) => s.name)).toEqual(declared);
    });
  }
});
