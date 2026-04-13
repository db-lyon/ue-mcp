import { z } from "zod";
import { FlowRunner } from "@db-lyon/flowkit";
import type { TaskRegistry, FlowRunResult, TaskDefinition, FlowDefinition } from "@db-lyon/flowkit";
import type { FlowContext } from "./context.js";
import type { FlowConfig } from "./schema.js";
import type { ToolDef, ToolContext } from "../types.js";

export function createFlowTool(
  registry: TaskRegistry,
  config: FlowConfig,
): ToolDef {
  const flowNames = Object.keys(config.flows);

  return {
    name: "flow",
    description:
      `Run or inspect named flows defined in ue-mcp.yml.\n\n` +
      `Available flows: ${flowNames.length > 0 ? flowNames.join(", ") : "(none — add flows to ue-mcp.yml)"}` +
      `\n\nActions:\n` +
      `- run: Execute a flow. Params: flowName, skip?\n` +
      `- plan: Show execution plan without running. Params: flowName\n` +
      `- list: List available flows`,
    schema: {
      action: z.enum(["run", "plan", "list"]).describe("Action to perform"),
      flowName: z.string().optional().describe("Flow name from ue-mcp.yml"),
      skip: z.array(z.string()).optional().describe("Step names or numbers to skip"),
    },
    actions: {
      run: { handler: async (ctx, params) => runFlow(registry, config, ctx, params) },
      plan: { handler: async (ctx, params) => planFlow(registry, config, ctx, params) },
      list: { handler: async () => listFlows(config) },
    },
    handler: async (ctx, params) => {
      const action = params.action as string;
      if (action === "list") return listFlows(config);
      if (action === "plan") return planFlow(registry, config, ctx, params);
      if (action === "run") return runFlow(registry, config, ctx, params);
      throw new Error(`Unknown flow action: ${action}`);
    },
  };
}

function listFlows(config: FlowConfig): Record<string, unknown> {
  const flows = Object.entries(config.flows).map(([name, def]) => ({
    name,
    description: def.description,
    stepCount: Object.keys(def.steps).length,
  }));
  return { flowCount: flows.length, flows };
}

async function planFlow(
  registry: TaskRegistry,
  config: FlowConfig,
  ctx: ToolContext,
  params: Record<string, unknown>,
): Promise<unknown> {
  const flowName = params.flowName as string;
  if (!flowName) throw new Error("flowName is required");

  const runner = makeRunner(registry, config, ctx);
  return runner.run({ flowName, plan: true });
}

async function runFlow(
  registry: TaskRegistry,
  config: FlowConfig,
  ctx: ToolContext,
  params: Record<string, unknown>,
): Promise<unknown> {
  const flowName = params.flowName as string;
  if (!flowName) throw new Error("flowName is required");
  const skip = (params.skip as string[] | undefined) ?? [];

  const runner = makeRunner(registry, config, ctx);
  const result = await runner.run({ flowName, skip });

  return formatFlowResult(result);
}

function makeRunner(registry: TaskRegistry, config: FlowConfig, ctx: ToolContext): FlowRunner {
  const flowCtx: FlowContext = {
    bridge: ctx.bridge,
    project: ctx.project,
  };

  return new FlowRunner({
    tasks: config.tasks as Record<string, TaskDefinition>,
    flows: config.flows as Record<string, FlowDefinition>,
    registry,
    context: flowCtx,
  });
}

function formatFlowResult(result: FlowRunResult): Record<string, unknown> {
  const lines: string[] = [];
  const icon = result.success ? "✓" : "✗";
  lines.push(`${icon} Flow ${result.success ? "completed" : "failed"} in ${formatDuration(result.duration)}`);
  lines.push("");

  for (const s of result.steps) {
    const stepIcon = s.skipped ? "○" : s.result?.success ? "✓" : "✗";
    const status = s.skipped ? "skipped" : s.result?.success ? formatDuration(s.duration) : "FAILED";
    lines.push(`  ${stepIcon} ${s.stepNumber}. ${s.name} (${s.type}) — ${status}`);

    if (s.result?.error) {
      lines.push(`      ${s.result.error.message}`);
    }

    // Show shell output if present
    if (s.result?.data?.output && typeof s.result.data.output === "string") {
      const output = s.result.data.output;
      if (output.length > 0) {
        const truncated = output.length > 500 ? output.slice(-500) + "\n      ..." : output;
        for (const line of truncated.split("\n")) {
          lines.push(`      ${line}`);
        }
      }
    }
  }

  if (result.error && !result.steps.some((s) => s.result?.error)) {
    lines.push("");
    lines.push(`  Error: ${result.error.message}`);
  }

  return {
    summary: lines.join("\n"),
    success: result.success,
    duration: result.duration,
    stepCount: result.steps.length,
    failedStep: result.steps.find((s) => s.result?.success === false)?.name,
  };
}

function formatDuration(ms: number): string {
  if (ms < 1000) return `${ms}ms`;
  if (ms < 60_000) return `${(ms / 1000).toFixed(1)}s`;
  return `${Math.floor(ms / 60_000)}m ${Math.round((ms % 60_000) / 1000)}s`;
}
