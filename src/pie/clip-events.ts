// @session independent: One process-wide bus for clip run events. The run id rides on the event.
/**
 * Live progress of a playtest clip run (#2097), one module-level bus for every run in this process.
 *
 * The clip watcher (clip-watch.ts) polls pie_clip_status while a run plays and emits what changed between two
 * answers; the daemon republishes each one on its event stream as `clip.<type>`. `runId` lets a subscriber follow one
 * run, and `seq` numbers a run's events in the order they were emitted.
 */

import type { LiveTakeDelta } from "./clip-take-live.js";

export type ClipEventType =
  | "run_started"
  | "position"
  | "check"
  | "region"
  | "event_fired"
  | "take_progress"
  | "verdict_requested"
  | "run_finished";

interface ClipEventBase {
  runId: string;
  /** The clip's asset path, or "inline". */
  clip: string;
  /** Wall time the watcher saw the change, ISO 8601. */
  at: string;
  /** Increasing within a run. */
  seq: number;
}

/** Where playback stands: frames at the clip's rate, game ms since frame 0, and the hold check it stands at. */
export interface ClipPosition {
  state: string;
  position: number;
  length: number;
  gameMs: number;
  heldMs: number;
  frames: number;
  holding?: string;
}

export type ClipEvent =
  | (ClipEventBase & {
      type: "run_started";
      clipName?: string;
      /** play or record. */
      mode: string;
      region?: string;
      rate?: number;
      resultsPath?: string;
    } & ClipPosition)
  | (ClipEventBase & { type: "position" } & ClipPosition)
  /** A check note's entry of clip_status, whenever its status changes. */
  | (ClipEventBase & { type: "check"; check: Record<string, unknown> })
  /** A region's entry of clip_status, whenever its status or drift changes. */
  | (ClipEventBase & { type: "region"; region: Record<string, unknown> })
  /** An event the run fired: {id, frame, game_ms, ok, error?}. */
  | (ClipEventBase & { type: "event_fired"; event: Record<string, unknown> })
  /**
   * A recording's take in flight (clip-take-live.ts): each armed track's notes as they now stand, the last one open
   * while held, and the axis keys appended since the previous take_progress, starting at index keysFrom.
   */
  | (ClipEventBase & { type: "take_progress"; takeId?: string; punchIn?: number } & LiveTakeDelta)
  /** The manual checks the run is paused on, for clip_verdict. */
  | (ClipEventBase & { type: "verdict_requested"; awaiting: string[] })
  | (ClipEventBase & {
      type: "run_finished";
      /** finished, stopped, or unreachable when the editor stopped answering. */
      state: string;
      stopReason?: string;
      passed?: boolean;
      summary?: Record<string, unknown>;
      drift?: Record<string, unknown>;
      resultsPath?: string;
    });

type Listener = (event: ClipEvent) => void;

const listeners = new Set<Listener>();

export function emitClipEvent(event: ClipEvent): void {
  for (const fn of listeners) {
    try {
      fn(event);
    } catch {
      // A broken subscriber (an SSE writer on a dead connection) must not stop the others or the watcher.
    }
  }
}

export function subscribeClipEvents(listener: Listener): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}
