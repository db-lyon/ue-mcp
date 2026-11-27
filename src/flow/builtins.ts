// @session independent: Tables of what ships built in; nothing here reads a session.
/**
 * Everything ue-mcp ships beside the per-action tasks: its task classes, the
 * task definitions the default config lists for them, and its built-in flows.
 * The registry (registry.ts) registers from this table and the loader
 * (loader.ts) writes the defaults from it, so the two cannot disagree about
 * what is built in.
 */
import { createRequire } from "node:module";
import { ShellTask, type OptionSpecs, type TaskConstructor, type TaskRegistry } from "@db-lyon/flowkit";
import { BridgeTask } from "./bridge-task.js";
import { editorOperationTask } from "./editor-op-task.js";
import { AGENT_BUILTIN_TASKS } from "./agent-step.js";

/** One built-in task class. */
export interface BuiltinTask {
  readonly name: string;
  readonly task: TaskConstructor;
  /** Registered under a class path (what a task's `class_path:` names) rather than a short name. */
  readonly byClassPath?: boolean;
  /**
   * The task the default config lists for it. Without one the class is
   * reachable by name and `class_path` but not listed.
   */
  readonly definition?: { readonly group: string; readonly description: string; readonly options_schema?: OptionSpecs };
}

export const BUILTIN_TASKS: readonly BuiltinTask[] = [
  { name: "ue-mcp.bridge", task: BridgeTask as unknown as TaskConstructor, byClassPath: true },
  {
    name: "shell",
    task: ShellTask as unknown as TaskConstructor,
    definition: {
      group: "util",
      description: "Run a shell command.",
      options_schema: { command: { type: "string", required: true }, cwd: { type: "string" }, timeout: { type: "number" } },
    },
  },
  { name: "editor.restart", task: editorOperationTask("restart") },
  { name: "editor.rebuild", task: editorOperationTask("rebuild") },
  ...AGENT_BUILTIN_TASKS,
];

export function registerBuiltinTasks(registry: TaskRegistry): void {
  for (const builtin of BUILTIN_TASKS) {
    if (builtin.byClassPath) registry.registerClassPath(builtin.name, builtin.task);
    else registry.register(builtin.name, builtin.task);
  }
}

/** The default config's task entries for the built-ins that have a definition. */
export function builtinTaskDefinitions(): Record<string, unknown> {
  const tasks: Record<string, unknown> = {};
  for (const builtin of BUILTIN_TASKS) {
    if (builtin.definition) tasks[builtin.name] = { class_path: builtin.name, ...builtin.definition };
  }
  return tasks;
}

/** The demo and starting-point flows, as data: builtin-flows.json, shipped beside this module in dist. */
const FLOW_DATA = createRequire(import.meta.url)("./builtin-flows.json") as Record<string, unknown>;

/** Built-in flows that ship with ue-mcp. */
export function builtinFlows(): Record<string, unknown> {
  return structuredClone(FLOW_DATA);
}
