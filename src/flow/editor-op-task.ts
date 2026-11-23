// @session per-session: Restarts or rebuilds the editor of the session a flow runs in.
/**
 * `editor.restart` and `editor.rebuild` as flow tasks, so a flow can change
 * C++, rebuild, relaunch and carry on.
 *
 * In the daemon they go through its editor operations, which announce the
 * restart or rebuild first so the disconnect is classified as that. In process
 * they run the same actions directly: restart_editor, or stop_editor, build
 * and start_editor, leaving the editor down when the build fails.
 */
import { BaseTask, type TaskConstructor, type TaskResult } from "@db-lyon/flowkit";
import type { FlowContext } from "./context.js";
import { askConsent, consentError, withDisruptionGrant } from "../dispatch/disruption-gate.js";

export type EditorOperation = "restart" | "rebuild";

const STEPS: Record<EditorOperation, string[]> = {
  restart: ["editor.restart_editor"],
  rebuild: ["editor.stop_editor", "project.build", "editor.start_editor"],
};

/** Task names of the operations above, which manage the editor rather than call it. */
export const EDITOR_OPERATION_TASKS: ReadonlySet<string> = new Set(Object.keys(STEPS).map((op) => `editor.${op}`));

export function editorOperationTask(op: EditorOperation): TaskConstructor {
  return class EditorOperationTask extends BaseTask {
    protected declare readonly ctx: FlowContext;

    get taskName(): string {
      return `editor.${op}`;
    }

    async execute(): Promise<TaskResult> {
      const viaDaemon = this.ctx.sessions?.editorOperation;
      if (viaDaemon) {
        const r = await viaDaemon(op);
        const failed = r.steps.find((s) => !s.ok);
        return failed
          ? { success: false, error: new Error(`${failed.step} failed: ${failed.result}`), data: r }
          : { success: true, data: r };
      }
      // One consent request for the whole operation, so its steps do not each ask (#2075).
      const decision = askConsent(this.ctx.sessions?.disruptions, op, `flow task editor.${op}`);
      if (!decision.proceed) return { success: false, error: consentError(decision), data: { op, steps: [], request: decision.request } };
      const run = async (): Promise<TaskResult> => {
        const steps: Array<{ step: string; ok: boolean }> = [];
        for (const step of STEPS[op]) {
          const r = await this.call(step);
          steps.push({ step, ok: r.success });
          if (!r.success) {
            return { success: false, error: r.error ?? new Error(`${step} failed`), data: { op, steps } };
          }
        }
        return { success: true, data: { op, steps } };
      };
      const request = decision.request;
      if (!request) return run();
      try {
        return await withDisruptionGrant(request.id, run);
      } finally {
        this.ctx.sessions?.disruptions.complete(request.id);
      }
    }
  } as unknown as TaskConstructor;
}
