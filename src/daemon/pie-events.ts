/**
 * Play session events for the daemon's event stream. The editor pushes a
 * notifications/pie frame whenever a session starts, pauses, resumes or ends,
 * however it was started, and the daemon republishes each as `pie.<state>`.
 * On every attach it also reads pie_control(status) once and publishes the
 * state it finds, marked `resync`, so a client that missed events while the
 * editor was away is put right.
 */
import { PIE_STATES, type PieEventData, type PieState } from "../contract/daemon.js";
import type { BridgeNotification } from "../bridge/bridge.js";

/** The frame method the bridge plugin sends. */
export const PIE_NOTIFICATION_METHOD = "notifications/pie";

/** How long the attach-time status read may take. The editor may be busy loading. */
export const PIE_RESYNC_TIMEOUT_MS = 15_000;

function isPieState(value: unknown): value is PieState {
  return typeof value === "string" && (PIE_STATES as readonly string[]).includes(value);
}

/** The event data for one pushed frame, or null when it is not a PIE frame or is malformed. */
export function pieEventFromNotification(editor: string, n: BridgeNotification): PieEventData | null {
  if (n.method !== PIE_NOTIFICATION_METHOD || !isPieState(n.params.state)) return null;
  return {
    editor,
    state: n.params.state,
    simulate: n.params.simulate === true,
    at: typeof n.params.at === "string" && n.params.at ? n.params.at : new Date().toISOString(),
  };
}

/**
 * The event data for a pie_control(status) answer: started (with `paused` when
 * the session is paused) or ended. Null when the answer does not say.
 */
export function pieEventFromStatus(editor: string, status: unknown): PieEventData | null {
  if (!status || typeof status !== "object") return null;
  const s = status as { isPlaying?: unknown; isSimulating?: unknown; isPaused?: unknown };
  if (typeof s.isPlaying !== "boolean") return null;
  const playing = s.isPlaying;
  return {
    editor,
    state: playing ? "started" : "ended",
    simulate: playing && s.isSimulating === true,
    at: new Date().toISOString(),
    resync: true,
    ...(playing ? { paused: s.isPaused === true } : {}),
  };
}
