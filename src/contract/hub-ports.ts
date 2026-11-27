// @session independent: Interface declarations for what the hub is handed; no runtime code.
/**
 * What the hub is handed instead of owning it. The daemon wires the defaults
 * (src/hub/daemon-hub.ts); a client such as Studio adds what only it knows
 * through api.hub.configure (HubClientOptions). Studio is UI only: it brings
 * signals and, optionally, a runner, never data.
 */
import type { ActionResult } from "./daemon.js";
import type { BusyCall, PlayRequest, PlayStatus, RunnerKind } from "./hub.js";

/** What the hub needs of the daemon: the project, its data folder, calls into ue-mcp's own actions, editors and events. */
export interface HubHost {
  projectDir: string;
  /** The project's data folder: the hub keeps this machine's runs, queue and schedules under <dataDir>/hub. */
  dataDir: string;
  callAction(category: string, action: string, args?: Record<string, unknown>): Promise<ActionResult>;
  editors(): Array<{ connected: boolean }>;
  events: {
    publish(type: string, data?: Record<string, unknown>): unknown;
    subscribe(listener: (e: { type: string; data: Record<string, unknown> }) => void): () => void;
  };
  log: { warn(message: string, detail?: unknown): void };
}

/**
 * Something that plays arrangements: the open editor (ue-mcp's own, over
 * pie(arrangement_play)) or a headless editor a client launches. Each method
 * answers or throws with a message a person can act on.
 */
export interface HubRunner {
  readonly kind: RunnerKind;
  /** Why it cannot take a run now, or null when it can. The open editor's answer comes from the lease instead. */
  unavailable(): string | null;
  play(request: PlayRequest): Promise<{ pieStarted?: boolean }>;
  status(runId: string): Promise<PlayStatus>;
  stop(runId: string, reason: string): Promise<void>;
  verdict(runId: string, check: string, verdict: "pass" | "fail", note?: string): Promise<void>;
}

/** What a client such as Studio brings to the hub (api.hub.configure). Each member is optional and has a default. */
export interface HubClientOptions {
  /** The hub's switch: off, the queue starts nothing and the editor is not asked. Default: on. */
  enabled?: () => boolean;
  /** How long since someone used this computer, when a window reports it; null when none does. */
  inputAgeMs?: () => number | null;
  /**
   * The editor calls the client's own agents have in flight, each with
   * source "agent" and its sessionId. They reach the editor through
   * api.callAction, which publishes no call events. Default: none.
   */
  agentCalls?: () => BusyCall[];
  /** A runner that plays on a headless editor. Default: none, so a headless run is refused with the way to run it on the open editor. */
  headless?: HubRunner;
}

/** A call into ue-mcp with its options, as a client's host makes it. */
export interface CallingHost<O = unknown> {
  callAction(category: string, action: string, args?: Record<string, unknown>, options?: O): Promise<ActionResult>;
}
