/**
 * The universal layer: the standard library of tasks and flows every project's
 * config merges over, shipped as universal/ue-mcp.universal.yml in the same
 * schema as ue-mcp.yml. scripts/generate-universal.ts writes it; the runtime
 * only reads it.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import yaml from "js-yaml";
import type { ActionSpec } from "../core/types.js";
import { packageRoot } from "../core/package-root.js";
import { builtinClassPath } from "./task-call.js";

export const UNIVERSAL_FILE = "ue-mcp.universal.yml";

export function universalPath(): string {
  return path.join(packageRoot(), "universal", UNIVERSAL_FILE);
}

export interface UniversalLayer {
  tasks: Record<string, Record<string, unknown>>;
  flows: Record<string, unknown>;
}

let parsed: UniversalLayer | undefined;

/** The universal layer, parsed once per process. Shared: copy what you hand out. */
function universalLayer(): Readonly<UniversalLayer> {
  if (!parsed) {
    const doc = yaml.load(fs.readFileSync(universalPath(), "utf-8")) as Partial<UniversalLayer> | null;
    parsed = { tasks: doc?.tasks ?? {}, flows: doc?.flows ?? {} };
  }
  return parsed;
}

/** A copy of one universal task entry, or undefined when the layer has none. */
export function universalTask(name: string): Record<string, unknown> | undefined {
  const tasks = universalLayer().tasks;
  const entry = Object.hasOwn(tasks, name) ? tasks[name] : undefined;
  return entry ? structuredClone(entry) : undefined;
}

/** A copy of the universal flows. */
export function universalFlows(): Record<string, unknown> {
  return structuredClone(universalLayer().flows);
}

/**
 * The task entry an action gets: its base alias for a built-in, so an override
 * of the name reaches the built-in instead of itself, or the name for a
 * registry action, whose plugin registers it under the name only.
 */
export function actionTaskEntry(toolName: string, actionName: string, spec: ActionSpec): Record<string, unknown> {
  const taskName = `${toolName}.${actionName}`;
  const builtin = spec.kind === "handler" || spec.kind === "bridge" || spec.kind === "flow";
  const entry: Record<string, unknown> = {
    class_path: builtin ? builtinClassPath(taskName) : taskName,
    group: toolName,
  };
  if (spec.description) entry.description = spec.description;
  return entry;
}
