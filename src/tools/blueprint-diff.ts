/**
 * blueprint(diff) with `against` (#1487). A checkpoint lives in this process's
 * shadow history, so the call is answered here: the checkpoint's copy is
 * exported and diffed in the editor with checkpoint_diff_blueprint. Every other
 * form, a source control revision included, goes to diff_blueprint, which
 * fetches the revision through the editor's provider.
 */
import type { BridgeActionSpec, ToolContext } from "../core/types.js";
import { checkpointsFor } from "../checkpoints/service.js";

export type DiffAgainst = { kind: "checkpoint"; id: string } | { kind: "revision"; revision: string };

/**
 * What `against` names. checkpoint:<id> and revision:<rev> say so; a bare id is a
 * checkpoint when the project's history has one with that prefix, else a revision.
 */
export async function resolveAgainst(against: string, hasCheckpoint: (id: string) => Promise<boolean>): Promise<DiffAgainst> {
  const value = against.trim();
  const prefixed = /^(checkpoint|revision):\s*(.*)$/i.exec(value);
  if (prefixed) {
    const id = prefixed[2].trim();
    if (!id) throw new Error(`against '${value}' names no ${prefixed[1].toLowerCase()}`);
    return prefixed[1].toLowerCase() === "checkpoint" ? { kind: "checkpoint", id } : { kind: "revision", revision: id };
  }
  return (await hasCheckpoint(value)) ? { kind: "checkpoint", id: value } : { kind: "revision", revision: value };
}

/** The checkpoint form's answer; undefined sends the call to the editor as it is. */
export async function interceptCheckpointDiff(ctx: ToolContext, params: Record<string, unknown>): Promise<unknown> {
  const against = typeof params.against === "string" ? params.against.trim() : "";
  if (!against) return undefined;
  const checkpoints = ctx.session ? checkpointsFor(ctx.session) : null;
  const target = await resolveAgainst(against, async (id) => (checkpoints ? checkpoints.hasCheckpoint(id) : false));
  if (target.kind !== "checkpoint") return undefined;
  if (!checkpoints) throw new Error("Diffing against a checkpoint needs an editor session; this call has none");
  const assetPath = String(params.assetPath ?? params.path ?? "");
  if (!assetPath) throw new Error("assetPath is required: the Blueprint to diff");
  return checkpoints.blueprintDiffAgainst(target.id, assetPath);
}

export function blueprintDiffAction(spec: BridgeActionSpec): BridgeActionSpec {
  return { ...spec, intercept: interceptCheckpointDiff };
}
