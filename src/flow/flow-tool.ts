import { z } from "zod";
import { FlowRunner } from "@db-lyon/flowkit";
import type {
  TaskRegistry,
  FlowRunResult,
  FlowStepResult,
  FlowRunnerHooks,
  PlanStep,
  TaskDefinition,
  FlowDefinition,
} from "@db-lyon/flowkit";
import type { FlowContext } from "./context.js";
import type { FlowConfig } from "./schema.js";
import type { ToolDef, ToolContext } from "../types.js";
import {
  takeSnapshot,
  restoreSnapshot,
  reloadAffectedPackages,
  pruneOldSnapshots,
  type Snapshot,
} from "./git-snapshot.js";
import {
  emitFlowEvent,
  nextRunId,
  trimStepResult,
  trimError,
} from "./events.js";

export function createFlowTool(
  registry: TaskRegistry,
  reloadConfig: () => FlowConfig,
): ToolDef {
  return {
    name: "flow",
    description:
      `Run pre-built named sequences for this project. ALWAYS check ` +
      `project(action="get_status") first - its 'flows' field lists what's available. ` +
      `If a flow matches the user's request, run it via ` +
      `flow(action="run", flowName="...") instead of composing the sequence by hand. ` +
      `Config reloads on every call - no restart needed.\n\n` +
      `Actions:\n` +
      `- run: Execute a flow. Params: flowName, skip?, params?, rollback_on_failure?\n` +
      `- plan: Show execution plan without running. Params: flowName\n` +
      `- list: List available flows\n\n` +
      `Step types supported in YAML flows: any MCP action (category.action), nested flows (flow:),\n` +
      `and 'shell' for running shell/exec commands. Example shell step:\n` +
      `  steps:\n` +
      `    1: { task: shell, options: { command: "npm run up:build" } }\n\n` +
      `params: Runtime options merged into every step's options (highest priority). ` +
      `Use to override YAML-hardcoded values like levelPath, directory, configuration, etc.\n\n` +
      `rollback_on_failure: When true, rollback records from completed steps are invoked ` +
      `in reverse order if a subsequent step fails.`,
    schema: {
      action: z.enum(["run", "plan", "list"]).describe("Action to perform"),
      flowName: z.string().optional().describe("Flow name from ue-mcp.yml"),
      skip: z.array(z.string()).optional().describe("Step names or numbers to skip"),
      params: z.record(z.unknown()).optional().describe("Runtime options merged into every step's options (highest priority)"),
      rollback_on_failure: z.boolean().optional().describe("Invoke inverse tasks in reverse order on failure"),
    },
    actions: {
      run: { handler: async (ctx, params) => runFlow(registry, reloadConfig(), ctx, params) },
      plan: { handler: async (ctx, params) => planFlow(registry, reloadConfig(), ctx, params) },
      list: { handler: async () => listFlows(reloadConfig()) },
    },
    handler: async (ctx, params) => {
      const action = params.action as string;
      if (action === "list") return listFlows(reloadConfig());
      if (action === "plan") return planFlow(registry, reloadConfig(), ctx, params);
      if (action === "run") return runFlow(registry, reloadConfig(), ctx, params);
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

  // Plan mode short-circuits inside the runner before any hooks fire,
  // so the runId placeholder we pass here is never observed.
  const runner = makeRunner(registry, config, ctx, nextRunId(), flowName);
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
  const flowParams = params.params as Record<string, unknown> | undefined;
  const rollback_on_failure = params.rollback_on_failure as boolean | undefined;

  // One runId per top-level call. Every per-step / per-run event we
  // emit carries this id so SSE subscribers can filter to a specific
  // run; the response includes it so callers can correlate.
  const runId = nextRunId();
  const runner = makeRunner(registry, config, ctx, runId, flowName);
  const result = await runner.run({ flowName, skip, params: flowParams, rollback_on_failure });

  const formatted = formatFlowResult(result);
  return { ...formatted, runId };
}

function makeRunner(
  registry: TaskRegistry,
  config: FlowConfig,
  ctx: ToolContext,
  runId: string,
  flowName: string,
): FlowRunner {
  const flowCtx: FlowContext = {
    bridge: ctx.bridge,
    project: ctx.project,
  };

  // Opt-in git snapshot: capture Content/ + Config/ on start; reset on failure.
  // Handler-level rollbacks cover in-memory state (selection, PIE, unsaved
  // actors); the snapshot covers anything that touched disk.
  const snapCfg = config.git_snapshot;
  let activeSnapshot: Snapshot | undefined;
  let flowFailed = false;
  const snapshotEnabled = !!(snapCfg?.enabled && ctx.project.projectDir);

  // Always-on per-step observation. Each hook emits a single event on
  // the module-level bus that the HTTP server's /flows/events SSE
  // endpoint pipes to subscribed clients.
  const hooks: FlowRunnerHooks = {
    beforeRun: async (_name, plan) => {
      emitFlowEvent({
        type: "run_started",
        runId,
        flowName,
        plan,
        timestamp: Date.now(),
      });
      if (!snapshotEnabled) return;
      const projectDir = ctx.project.projectDir!;
      const snapshotDir = snapCfg!.snapshot_dir ?? ".ue-mcp/snapshot.git";
      const absSnap = snapshotDir.startsWith(".") || !snapshotDir.match(/^([a-zA-Z]:|\/)/)
        ? `${projectDir}/${snapshotDir}`
        : snapshotDir;
      pruneOldSnapshots(absSnap, (snapCfg!.max_age_hours ?? 24) * 3_600_000);
      try {
        activeSnapshot = takeSnapshot(
          projectDir,
          snapCfg!.paths ?? ["Content", "Config"],
          snapshotDir,
        );
      } catch (e) {
        // Don't fail the flow on snapshot failure - just log. Handler
        // rollbacks still apply.
        console.error(`[ue-mcp] git snapshot failed: ${(e as Error).message}`);
      }
    },
    beforeStep: async (step: PlanStep) => {
      emitFlowEvent({
        type: "step_started",
        runId,
        flowName,
        step,
        timestamp: Date.now(),
      });
    },
    afterStep: async (step: PlanStep, result: FlowStepResult) => {
      emitFlowEvent({
        type: "step_completed",
        runId,
        flowName,
        step,
        result: trimStepResult(result),
        timestamp: Date.now(),
      });
    },
    onStepError: async (step: PlanStep, error: Error) => {
      emitFlowEvent({
        type: "step_failed",
        runId,
        flowName,
        step,
        error: trimError(error),
        timestamp: Date.now(),
      });
    },
    afterRun: async (result: FlowRunResult) => {
      // Restore the git snapshot first (if enabled and the flow failed)
      // so the run_completed event is the last thing observers see.
      if (snapshotEnabled) {
        flowFailed = !result.success;
        if (activeSnapshot && flowFailed) {
          try {
            const { changedPaths } = restoreSnapshot(activeSnapshot);
            if (ctx.bridge.isConnected) {
              await reloadAffectedPackages(ctx.bridge, activeSnapshot.projectDir, changedPaths);
            }
            (result as unknown as { snapshotRestore?: unknown }).snapshotRestore = {
              restored: true,
              changedCount: changedPaths.length,
            };
          } catch (e) {
            (result as unknown as { snapshotRestore?: unknown }).snapshotRestore = {
              restored: false,
              error: (e as Error).message,
            };
          }
        }
      }
      emitFlowEvent({
        type: "run_completed",
        runId,
        flowName,
        success: result.success,
        duration: result.duration,
        stepCount: result.steps.length,
        failedStep: result.steps.find((s) => s.result?.success === false)?.name,
        timestamp: Date.now(),
      });
    },
  };

  return new FlowRunner({
    tasks: config.tasks as Record<string, TaskDefinition>,
    flows: config.flows as Record<string, FlowDefinition>,
    registry,
    context: flowCtx,
    hooks,
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
    const attempts = s.attempts && s.attempts > 1 ? ` [${s.attempts} attempts]` : "";
    lines.push(`  ${stepIcon} ${s.stepNumber}. ${s.name} (${s.type}) — ${status}${attempts}`);

    if (s.result?.error) {
      lines.push(`      ${s.result.error.message}`);
    }

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

  if (result.rollback) {
    lines.push("");
    lines.push(
      `  Rollback: ${result.rollback.succeeded}/${result.rollback.attempted} inverses succeeded` +
        (result.rollback.errors.length ? ` — ${result.rollback.errors.length} failed` : ""),
    );
    for (const e of result.rollback.errors) {
      lines.push(`      ✗ ${e.taskName}: ${e.error.message}`);
    }
  }

  const snap = (result as unknown as { snapshotRestore?: { restored: boolean; changedCount?: number; error?: string } }).snapshotRestore;
  if (snap) {
    lines.push("");
    if (snap.restored) {
      lines.push(`  Git snapshot restored: ${snap.changedCount} files reset`);
    } else {
      lines.push(`  Git snapshot restore FAILED: ${snap.error}`);
    }
  }

  if (result.hookErrors && result.hookErrors.length > 0) {
    lines.push("");
    lines.push(`  Hook errors (${result.hookErrors.length}):`);
    for (const h of result.hookErrors) {
      lines.push(`      ✗ ${h.phase}:${h.name} — ${h.error.message}`);
    }
  }

  return {
    summary: lines.join("\n"),
    success: result.success,
    duration: result.duration,
    stepCount: result.steps.length,
    failedStep: result.steps.find((s) => s.result?.success === false)?.name,
    rollback: result.rollback,
    hookErrors: result.hookErrors,
  };
}

function formatDuration(ms: number): string {
  if (ms < 1000) return `${ms}ms`;
  if (ms < 60_000) return `${(ms / 1000).toFixed(1)}s`;
  return `${Math.floor(ms / 60_000)}m ${Math.round((ms % 60_000) / 1000)}s`;
}
