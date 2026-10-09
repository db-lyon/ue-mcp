/**
 * What a flow does when the editor goes away mid-run (`on_editor_loss`).
 *
 * The runner is paused at the step boundary: a step never starts against an
 * editor that dropped during this run until it is back, or the wait times out.
 * A step that failed while the editor dropped has an unknown outcome. Once the
 * editor returns, a read-only step is run again as its own verification; any
 * other step stays unknown. Then the policy decides:
 *
 *   resume    carry on, the step reported with `editorLoss.outcome`
 *   rollback  fail the step; the run unwinds as rollback_on_failure does
 *   fail      fail the step and stop
 *
 * Rollback steps that hit a drop wait for the editor and run once more. Under
 * `rollback`, the git snapshot is restored while the editor is still away, so
 * it never holds stale copies of what was reset.
 */
import type { TaskRegistry, TaskResult } from "@db-lyon/flowkit";

export type EditorLossPolicy = "resume" | "rollback" | "fail";
export const EDITOR_LOSS_POLICIES: readonly EditorLossPolicy[] = ["resume", "rollback", "fail"];
export const DEFAULT_EDITOR_LOSS_TIMEOUT_MS = 300_000;

/** The slice of EditorBridge the guard reads. */
export interface ConnectionSource {
  readonly isConnected: boolean;
  onConnectionChanged(listener: (c: { connected: boolean; detail?: string }) => void): () => void;
  waitForConnection(timeoutMs: number): Promise<boolean>;
  readonly lastDisconnectCause?: string | null;
}

export type EditorLossOutcome = "unknown" | "verified" | "failed-verification" | "editor-did-not-return";

export interface EditorLossEvent {
  step: string;
  phase: string;
  outcome: EditorLossOutcome;
  /** Classified cause when the daemon gave one, else the close detail. */
  cause: string | null;
  waitedMs: number;
}

export interface EditorLossReport {
  policy: EditorLossPolicy;
  events: EditorLossEvent[];
  /** Completed steps whose result said their change was not saved when the editor went away. */
  unsavedBeforeLoss: string[];
  /** Set when the git snapshot was restored while the editor was away. */
  snapshotRestoredWhileDown?: boolean;
}

export interface EditorLossOptions {
  policy: EditorLossPolicy;
  timeoutMs: number;
  /** True for a task whose action is declared read-only, so running it again is its verification. */
  isReadOnly(taskName: string): boolean;
  /** Restore the git snapshot now. Called at most once, while the editor is away, under `rollback`. */
  restoreWhileDown?(): boolean;
}

type Runnable = { run(): Promise<TaskResult> };

export class EditorLossGuard {
  private drops = 0;
  private lastDetail: string | null = null;
  private readonly events: EditorLossEvent[] = [];
  private readonly completed: Array<{ name: string; data?: Record<string, unknown> }> = [];
  private unsaved: string[] = [];
  private restored = false;
  private readonly unsubscribe: () => void;

  constructor(private readonly source: ConnectionSource, private readonly options: EditorLossOptions) {
    this.unsubscribe = source.onConnectionChanged((c) => {
      if (c.connected) return;
      this.drops += 1;
      this.lastDetail = c.detail ?? null;
      // Whatever completed and was not saved is what a crash just lost.
      this.unsaved = this.completed.filter((s) => s.data?.saved === false || s.data?.dirty === true).map((s) => s.name);
    });
  }

  /** Feed a completed step, for the unsaved-edits report. */
  stepCompleted(name: string, data?: Record<string, unknown>): void {
    this.completed.push({ name, data });
  }

  /** The registry the runner should use: every task it creates runs under the guard. */
  wrap(registry: TaskRegistry): TaskRegistry {
    const create = async (name: string, ctx: unknown, options: Record<string, unknown>): Promise<Runnable> => {
      const task = (await registry.create(name, ctx as never, options)) as unknown as Runnable;
      const phase = String((ctx as { executionPhase?: string })?.executionPhase ?? "task");
      const original = task.run.bind(task);
      const again = async (): Promise<TaskResult> => ((await registry.create(name, ctx as never, options)) as unknown as Runnable).run();
      task.run = () => this.guardedRun(name, phase, original, again);
      return task;
    };
    return new Proxy(registry, {
      get(target, prop, receiver) {
        if (prop === "create") return create;
        const value = Reflect.get(target, prop, receiver);
        return typeof value === "function" ? value.bind(target) : value;
      },
    });
  }

  report(): EditorLossReport | undefined {
    if (this.events.length === 0 && this.drops === 0) return undefined;
    return {
      policy: this.options.policy,
      events: this.events,
      unsavedBeforeLoss: this.unsaved,
      snapshotRestoredWhileDown: this.restored || undefined,
    };
  }

  dispose(): void {
    this.unsubscribe();
  }

  private cause(): string | null {
    return this.source.lastDisconnectCause ?? this.lastDetail;
  }

  private async waitBack(): Promise<{ back: boolean; waitedMs: number }> {
    const started = Date.now();
    const back = await this.source.waitForConnection(this.options.timeoutMs);
    return { back, waitedMs: Date.now() - started };
  }

  private async guardedRun(
    name: string,
    phase: string,
    run: () => Promise<TaskResult>,
    again: () => Promise<TaskResult>,
  ): Promise<TaskResult> {
    // The step boundary. Only once the editor dropped during this run: a flow
    // of offline tasks against no editor at all must not wait for one.
    if (this.drops > 0 && !this.source.isConnected) await this.waitBack();

    const before = this.drops;
    const result = await run();
    if (result.success || this.drops === before) return result;

    // The step failed and the editor dropped while it ran: its outcome is unknown.
    const cause = this.cause();
    if (phase === "rollback") {
      const { back } = await this.waitBack();
      return back ? again() : result;
    }
    if (this.options.policy === "rollback" && !this.restored && !this.source.isConnected && this.options.restoreWhileDown) {
      this.restored = this.options.restoreWhileDown();
    }
    const { back, waitedMs } = await this.waitBack();
    if (!back) {
      this.events.push({ step: name, phase, outcome: "editor-did-not-return", cause, waitedMs });
      return {
        success: false,
        error: new Error(
          `The editor went away while ${name} ran (${cause ?? "cause unknown"}) and did not come back within ` +
            `${Math.round(this.options.timeoutMs / 1000)}s. Whether the step applied is unknown.`,
        ),
      };
    }

    if (this.options.policy === "resume") {
      if (this.options.isReadOnly(name)) {
        const verified = await again();
        this.events.push({ step: name, phase, outcome: verified.success ? "verified" : "failed-verification", cause, waitedMs });
        return verified;
      }
      this.events.push({ step: name, phase, outcome: "unknown", cause, waitedMs });
      return {
        success: true,
        data: {
          ...(result.data ?? {}),
          editorLoss: {
            outcome: "unknown",
            cause,
            note: "The editor went away while this step ran, so whether it applied is unknown. Read the state back before relying on it.",
          },
        },
      };
    }

    this.events.push({ step: name, phase, outcome: "unknown", cause, waitedMs });
    return {
      success: false,
      data: result.data,
      error: new Error(`The editor went away while ${name} ran (${cause ?? "cause unknown"}); its outcome is unknown.`),
    };
  }
}
