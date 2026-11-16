// @session independent: Puts the resolved model on an agent step's context. No editor is involved.
/**
 * flowkit's agent tasks with ue-mcp's model resolution in front of them: a
 * provider on the context wins, then a daemon extension's registered provider.
 * With neither, the step fails with NO_MODEL_MESSAGE and what to do about it.
 */
import { AgentPromptTask, AgentTask, type TaskConstructor, type TaskResult } from "@db-lyon/flowkit";
import { NO_MODEL_MESSAGE, NO_MODEL_WAY_FORWARD, resolveModelProvider } from "./model-provider.js";
import type { BuiltinTask } from "./builtins.js";

/** Run a model step with the resolved provider on the context, and take it off again after. */
async function withModel(ctx: { llm?: unknown }, run: () => Promise<TaskResult>): Promise<TaskResult> {
  if (ctx.llm) return run();
  const provider = resolveModelProvider();
  if (!provider) return { success: false, error: new Error(`${NO_MODEL_MESSAGE} ${NO_MODEL_WAY_FORWARD}`) };
  ctx.llm = provider;
  try {
    return await run();
  } finally {
    delete ctx.llm;
  }
}

export class ModelAgentTask extends AgentTask {
  override async execute(): Promise<TaskResult> {
    return withModel(this.ctx, () => super.execute());
  }
}

class ModelAgentPromptTask extends AgentPromptTask {
  override async execute(): Promise<TaskResult> {
    return withModel(this.ctx, () => super.execute());
  }
}

/** Class paths for flow steps that call a model. */
export const AGENT_BUILTIN_TASKS: readonly BuiltinTask[] = [
  { name: "agent", task: ModelAgentTask as unknown as TaskConstructor, byClassPath: true },
  { name: "agent_prompt", task: ModelAgentPromptTask as unknown as TaskConstructor, byClassPath: true },
];
