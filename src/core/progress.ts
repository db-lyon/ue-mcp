export interface ProgressUpdate {
  /** Monotonic units done. With `total`, clients render a bar. */
  progress: number;
  total?: number;
  /** One line describing what is happening right now. */
  message: string;
}

export type ProgressFn = (update: ProgressUpdate) => void;

/**
 * A terminal rendering of a long wait, such as `startProgress`.
 * Handed in by the caller, so a module that waits never draws on its own.
 */
export interface ProgressDisplay {
  update(state: { fraction: number | null; message: string; detail?: string }): void;
  stop(finalLine?: string): void;
}
