/**
 * blueprint(open_diff_window) (#1483): Unreal's Blueprint Diff window between
 * a Blueprint as it was at a checkpoint or a source control revision (left)
 * and the asset as loaded now (right). `against` is read as blueprint(diff)
 * reads it (#1487). The earlier version is loaded into a /Temp package that
 * the bridge unloads when the window closes.
 */
import type { ActionSpec, ToolContext } from "../core/types.js";
import { blueprintPackageKey, checkpointsFor } from "../checkpoints/service.js";
import { resolveAgainst } from "./blueprint-diff.js";

export interface DiffWindowDeps {
  projectDir: string | undefined;
  /** Whether a checkpoint has this id prefix. Absent without a session. */
  hasCheckpoint?: (id: string) => Promise<boolean>;
  openCheckpoint?: (changedPath: string, from: string) => Promise<Record<string, unknown>>;
  callBridge: (method: string, params: Record<string, unknown>) => Promise<unknown>;
}

export async function openDiffWindow(deps: DiffWindowDeps, p: Record<string, unknown>): Promise<Record<string, unknown>> {
  const assetPath = typeof p.assetPath === "string" ? p.assetPath.trim() : "";
  const raw = typeof p.against === "string" ? p.against.trim() : "";
  if (!assetPath) throw new Error("assetPath is required: the Blueprint to diff");
  if (!raw) throw new Error("against is required: a checkpoint id, or a source control revision as source_control(history) lists it");

  const target = await resolveAgainst(raw, async (id) => (deps.hasCheckpoint ? deps.hasCheckpoint(id) : false));
  if (target.kind === "checkpoint") {
    if (!deps.projectDir || !deps.openCheckpoint) throw new Error("Opening a checkpoint's diff needs an editor session with a project; this call has none");
    const changedPath = blueprintPackageKey(deps.projectDir, assetPath);
    if (!changedPath) throw new Error(`${assetPath} is not under /Game or the content of one of this project's plugins, so the checkpoint history holds no file for it`);
    const opened = await deps.openCheckpoint(changedPath, target.id);
    return { assetPath, against: { kind: "checkpoint", id: target.id }, ...opened };
  }

  const answer = await deps.callBridge("source_control_diff", { path: assetPath, revision: target.revision, openEditor: true });
  const out = (answer && typeof answer === "object" ? answer : {}) as Record<string, unknown>;
  return {
    assetPath,
    ...out,
    against: { kind: "revision", ...(out.against && typeof out.against === "object" ? out.against as object : { revision: target.revision }) },
    opened: out.editorDiffOpened === true ? "window" : out.opened,
  };
}

export const openDiffWindowAction: ActionSpec = {
  kind: "handler",
  effect: "read",
  description: "Open Unreal's Blueprint Diff window between the Blueprint as it was at `against` (left) and the asset as loaded now (right), and bring the editor to the front. against is read as blueprint(diff) reads it: checkpoint:<id> from editor(checkpoint), revision:<rev> from source_control(history) (the provider's id, a unique prefix, its number, or head), or a bare id, tried as a checkpoint first. A checkpoint's version is exported from the shadow history, a revision's is fetched from the provider; either is loaded into a /Temp package that is unloaded when the window closes (unloadsOnClose, tempPackages) (#1483). Params: assetPath, against",
  handler: async (ctx: ToolContext, p: Record<string, unknown>) => {
    const svc = ctx.session ? checkpointsFor(ctx.session) : undefined;
    return openDiffWindow({
      projectDir: ctx.session?.project.projectDir ?? ctx.project.projectDir ?? undefined,
      ...(svc ? {
        hasCheckpoint: (id: string) => svc.hasCheckpoint(id),
        openCheckpoint: async (changedPath: string, from: string) => ({ ...(await svc.openNativeDiff(changedPath, from, null)) }),
      } : {}),
      callBridge: (method, params) => ctx.bridge.call(method, params),
    }, p);
  },
};
