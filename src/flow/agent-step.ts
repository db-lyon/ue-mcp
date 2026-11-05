/**
 * flowkit's agent tasks with ue-mcp's model resolution in front of them: a
 * provider on the context wins, then a daemon extension's registered provider,
 * then ANTHROPIC_API_KEY. With none, the step fails with NO_MODEL_MESSAGE.
 */
import { AgentPromptTask, AgentTask, type TaskConstructor, type TaskResult } from "@db-lyon/flowkit";
import { NO_MODEL_MESSAGE, resolveModelProvider } from "./model-provider.js";

/** Run a model step with the resolved provider on the context, and take it off again after. */
async function withModel(ctx: { llm?: unknown }, run: () => Promise<TaskResult>): Promise<TaskResult> {
  if (ctx.llm) return run();
  const provider = resolveModelProvider();
  if (!provider) return { success: false, error: new Error(NO_MODEL_MESSAGE) };
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
export const AGENT_TASK_CLASSES: Record<string, TaskConstructor> = {
  agent: ModelAgentTask as unknown as TaskConstructor,
  agent_prompt: ModelAgentPromptTask as unknown as TaskConstructor,
};
