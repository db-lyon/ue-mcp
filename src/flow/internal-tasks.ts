/**
 * Bridge primitives the universal flows call and no action exposes.
 *
 * Each is registered as the task `internal.<name>` and never as a category
 * action, so tools/list, describe_action and search do not show it and the
 * advertised surface does not change. A flow step names it like any task.
 * The C++ side registers these methods under the `internal` category, so
 * their recorded specs never reach a category schema.
 *
 * Leaf module: action-effects and the audits read it, so it imports nothing.
 */

export const INTERNAL_NAMESPACE = "internal";

/** The C++ registration category of every internal method. */
export const INTERNAL_CATEGORY = "internal";

export interface InternalBridgeTask {
  /** The bridge method the task calls. */
  method: string;
  /** What it does to the editor. Every internal primitive writes. */
  effect: "mutate";
  /** Why no public action covers it. */
  reason: string;
}

/** Keyed by the task's short name; the task is `internal.<key>`. */
export const INTERNAL_BRIDGE_TASKS: Readonly<Record<string, InternalBridgeTask>> = {
  spawn_actor: {
    method: "internal_spawn_actor",
    effect: "mutate",
    reason: "Spawns under a label another actor already carries, and answers with the new actor's path. "
      + "place_actor skips a taken label, and the demo steps' documented replay spawns a second set.",
  },
  create_constant_material: {
    method: "internal_create_constant_material",
    effect: "mutate",
    reason: "Builds a material from linear constants with a coloured emissive and saves it once. "
      + "material(create_simple) takes a grey emissive, and composing the rest compiles and saves per node.",
  },
};

export function internalTaskName(name: string): string {
  return `${INTERNAL_NAMESPACE}.${name}`;
}

/** Every internal task, as [task name, declaration]. */
export function internalTasks(): Array<[string, InternalBridgeTask]> {
  return Object.entries(INTERNAL_BRIDGE_TASKS).map(([name, task]) => [internalTaskName(name), task]);
}

/** The declaration behind an internal bridge method, or undefined. */
export function internalMethod(method: string): InternalBridgeTask | undefined {
  return Object.values(INTERNAL_BRIDGE_TASKS).find((t) => t.method === method);
}
