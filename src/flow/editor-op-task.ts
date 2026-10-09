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

export type EditorOperation = "restart" | "rebuild";

const STEPS: Record<EditorOperation, string[]> = {
  restart: ["editor.restart_editor"],
  rebuild: ["editor.stop_editor", "project.build", "editor.start_editor"],
};

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
      const steps: Array<{ step: string; ok: boolean }> = [];
      for (const step of STEPS[op]) {
        const r = await this.call(step);
        steps.push({ step, ok: r.success });
        if (!r.success) {
          return { success: false, error: r.error ?? new Error(`${step} failed`), data: { op, steps } };
        }
      }
      return { success: true, data: { op, steps } };
    }
  } as unknown as TaskConstructor;
}
