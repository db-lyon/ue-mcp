/**
 * What a flow run hands back, engine-free (#817).
 *
 * Two properties, and they only mean something together:
 *
 *  1. A step sees the whole tool context. `makeRunner` spreads the context it
 *     was given rather than rebuilding it field by field, which used to drop
 *     `elicit`, `getFlows`, `getPlugins` and the editor session, so an action
 *     called inside a flow saw a different server than the same action called
 *     directly.
 *  2. The run response carries what each step answered. Without that, the
 *     first property is unobservable from outside, and a flow that reads
 *     anything returns a summary line and no data at all.
 *
 * The live tests asserts the same pair against a real editor
 * (tests/live/single-editor.test.ts, "hands a flow the whole context").
 */
import { describe, expect, it } from "vitest";
import { z } from "zod";
import { FlowConfigSchema, type FlowConfig } from "../../src/flow/schema.js";
import { buildFlowRegistry } from "../../src/flow/registry.js";
import { createFlowTool } from "../../src/flow/flow-tool.js";
import { categoryTool, type ToolContext } from "../../src/types.js";
import { ProjectContext } from "../../src/project.js";
import type { IBridge } from "../../src/bridge.js";

const bridge: IBridge = {
  isConnected: false,
  connect: async () => {},
  retargetProject: () => ({ projectPath: null, port: 0, portSource: "default" as const, verified: true }),
  getTarget: () => ({ projectPath: null, port: 0, portSource: "default" as const, verified: true }),
  call: async () => ({}),
};

/** Reports the accessors it can see, which is the whole point of the probe. */
const probeTool = categoryTool(
  "probe",
  "Test-only category that reports what the context handed it.",
  {
    context: {
      description: "Report the context accessors this call was given.",
      handler: async (ctx) => ({
        flows: ctx.getFlows?.().map((f) => f.name) ?? null,
        plugins: ctx.getPlugins?.().map((p) => p.name) ?? null,
        canElicit: typeof ctx.elicit === "function",
      }),
    },
  },
  undefined,
  { note: z.string().optional() } as Record<string, z.ZodType>,
);

const FLOWS = [{ name: "context_probe", description: "asks the context what it is" }];

function contextWithAccessors(): ToolContext {
  return {
    bridge,
    project: new ProjectContext(),
    getFlows: () => FLOWS,
    getPlugins: () => [{ name: "ue-mcp-example", active: true }] as never,
    elicit: async () => ({ action: "decline" as const }),
  };
}

function config(): FlowConfig {
  return FlowConfigSchema.parse({
    "ue-mcp": { version: 1 },
    flows: {
      context_probe: {
        description: "asks the context what it is",
        steps: { "1": { task: "probe.context" } },
      },
    },
  });
}

async function runProbe(ctx: ToolContext): Promise<Record<string, unknown>> {
  const flowTool = createFlowTool(buildFlowRegistry([probeTool]), () => config());
  const result = await flowTool.handler(ctx, { action: "run", flowName: "context_probe" });
  return result as Record<string, unknown>;
}

interface RunResponse {
  success: boolean;
  steps: Array<{
    name: string;
    success: boolean;
    data?: { flows?: string[] | null; plugins?: string[] | null; canElicit?: boolean };
  }>;
}

describe("flow run result", () => {
  it("carries every step's data, so what the step answered is readable", async () => {
    const body = (await runProbe(contextWithAccessors())) as unknown as RunResponse;

    expect(body.success).toBe(true);
    expect(body.steps).toHaveLength(1);
    expect(body.steps[0].success).toBe(true);
    expect(body.steps[0].data).toBeTruthy();
  });

  it("hands the step the accessors the context had, not a rebuilt subset", async () => {
    const body = (await runProbe(contextWithAccessors())) as unknown as RunResponse;
    const data = body.steps[0].data!;

    expect(data.flows).toEqual(["context_probe"]);
    expect(data.plugins).toEqual(["ue-mcp-example"]);
    expect(data.canElicit).toBe(true);
  });

  it("reports the absence of an accessor as absence, not as an empty answer", async () => {
    // A context genuinely without them still runs: the flow path adds nothing
    // and takes nothing away.
    const bare: ToolContext = { bridge, project: new ProjectContext() };
    const body = (await runProbe(bare)) as unknown as RunResponse;

    expect(body.success).toBe(true);
    expect(body.steps[0].data!.flows).toBeNull();
    expect(body.steps[0].data!.canElicit).toBe(false);
  });
});
