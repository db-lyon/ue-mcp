import type { ActionSpec, ToolDef } from "../core/types.js";

/**
 * The old address of an action that moved to another category, kept for one
 * release. It is the canonical spec with `movedTo` set and the description
 * saying where the action lives now, so it dispatches exactly as the
 * canonical address does and tool search can leave it out.
 */
export function movedAction(canonical: ToolDef, action: string): ActionSpec {
  const spec = canonical.actions[action];
  if (!spec) throw new Error(`movedAction: ${canonical.name} has no action '${action}'`);
  const where = `${canonical.name}(${action})`;
  return {
    ...spec,
    movedTo: { category: canonical.name, action },
    description: `Deprecated address, removed in the next release: use ${where}. ${spec.description ?? ""}`.trimEnd(),
  };
}

/** True for a deprecated alias that `movedAction` built. */
export function isMovedAction(spec: ActionSpec | undefined): boolean {
  return !!spec?.movedTo;
}
