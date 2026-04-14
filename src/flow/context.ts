import type { TaskContext, LLMProvider } from "@db-lyon/flowkit";
import type { IBridge } from "../bridge.js";
import type { ProjectContext } from "../project.js";

export interface FlowContext extends TaskContext {
  bridge: IBridge;
  project: ProjectContext;
  /** Optional LLM provider used by the built-in `agent_prompt` task. */
  llm?: LLMProvider;
}
