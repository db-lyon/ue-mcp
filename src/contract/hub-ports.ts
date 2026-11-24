// @session independent: Interface declarations for what the hub is handed; no runtime code.
/**
 * What the hub is handed instead of owning it. Each interface here is an
 * injection point: ue-mcp provides a default where one makes sense (the daemon
 * wires them in src/hub/daemon-hub.ts), and a client such as Studio provides
 * its own where it has concepts ue-mcp does not (chat sessions, run cards,
 * its settings, its signed-in account), through api.hub.configure
 * (HubClientOptions).
 */
import type { ActionResult } from "./daemon.js";
import type { PlanItem, PlanRun, PlaytestRunsMode } from "./hub.js";
import type { QaRun } from "./hub-qa.js";

/** What the hub needs of the daemon: the project, its data folder, calls into ue-mcp's own actions, editors and events. */
export interface HubHost {
  projectDir: string;
  /** The project's data folder: the hub keeps its files under <dataDir>/hub. */
  dataDir: string;
  callAction(category: string, action: string, args?: Record<string, unknown>): Promise<ActionResult>;
  editors(): Array<{ connected: boolean }>;
  events: {
    publish(type: string, data?: Record<string, unknown>): unknown;
    subscribe(listener: (e: { type: string; data: Record<string, unknown> }) => void): () => void;
  };
  log: { warn(message: string, detail?: unknown): void };
}

/** A script the editor lists. */
export interface ListedScript {
  path: string;
  name: string;
  map?: string;
}

/** When a recorded run started and ended, wall time, as ue-mcp wrote it. */
export interface RunTimes {
  startedAt?: string;
  endedAt?: string;
}

/** The project's playtest scripts and the runs ue-mcp has a record of. */
export interface HubScriptIndex {
  /** Every playtest script the editor lists; fresh skips the short reuse of the last list. */
  listScripts(fresh?: boolean): Promise<ListedScript[]>;
  /** The newest runs ue-mcp has a record of, with when each started and ended. */
  recorded(limit: number): Promise<Array<{ run: QaRun; times: RunTimes }>>;
}

/**
 * Starts, follows and stops script runs, and tells the hub of every run it
 * saves. ue-mcp's own ScriptRunBroker is the default (script-broker.ts); a
 * client with its own idea of a run (Studio's cards and chat sessions) brings
 * its own.
 */
export interface HubBroker {
  all(): QaRun[];
  get(runId: string): QaRun | undefined;
  /** Hear every run as it is saved. Returns the unsubscribe. */
  onSaved(listener: (run: QaRun, replaces?: string) => void): () => void;
  /** Read a run again from ue-mcp. */
  status(runId: string): Promise<{ answer: Record<string, unknown>; run: QaRun | null } | { error: string }>;
  /** A saved script's steps as a draft run, for what its steps need (manual steps). */
  readScriptAt(script: string): Promise<{ run: QaRun } | { error: string; status?: number }>;
  /**
   * Start a script for a plan item, with PIE: the client decides where the run
   * lives (Studio: the script's card in a chat session). The hub tags the run.
   */
  startScript(script: string, start: { planRunId: string; title: string }): Promise<{ run: QaRun } | { error: string }>;
  /** End a run, and PIE with it when the run started it. */
  stopRun(runId: string, reason: string): Promise<{ run: QaRun } | { error: string; status?: number }>;
  /** A plan's own verdict wait for the runs it starts. */
  setPlanWait?(wait: (run: QaRun) => number | undefined): void;
}

/**
 * Fix with an agent. Entirely a client's: ue-mcp has no agent loop, so
 * without one a fix request answers 409. Studio runs its Assign loop here.
 */
export interface HubFixer {
  /** Start the fix loop for an item; the item follows the run it returns. */
  start(item: PlanItem, plan: PlanRun): Promise<{ runId: string; sessionId?: string } | { error: string }>;
  /** Stop an item's loop. */
  stop(item: PlanItem): string | null;
  /** The scripts whose fix loops run now. */
  running(): Set<string>;
  /** The run, when a loop holds it (so a refresh of other runs skips it). */
  holds?(run: QaRun): boolean;
}

/** Write playtests for a goal: a client runs its own flow (Studio: the /playtest flow in a chat session). */
export type HubWritePlaytests = (request: { title: string; text: string }) => { sessionId: string; done: Promise<void> };

/** The identity and the policy a client supplies. Every member has a default. */
export interface HubPolicy {
  /** The account a sign-off is from; null when none is signed in, which refuses the sign-off. The daemon supplies the signed-in ue-mcp account. Default: none. */
  signer?: () => string | null | Promise<string | null>;
  /** Whether an agent's plan may start. Default: always (Automatic). */
  playtestRuns?: () => PlaytestRunsMode;
  /** The hub's experiment switch: off, the queue starts nothing and the editor is not asked. Default: on. */
  enabled?: () => boolean;
}

/** What a client such as Studio brings to the hub (api.hub.configure). Each member is optional and has a default. */
export interface HubClientOptions extends HubPolicy {
  /** Starts, follows and stops runs. Default: ue-mcp's own broker, which runs scripts with pie(script_run). */
  broker?: HubBroker;
  fixer?: HubFixer;
  writePlaytests?: HubWritePlaytests;
  /** A replay that gives the run a client's card: pie(script_replay) with these args. Default: the call, directly. */
  replay?: (script: string, args: Record<string, unknown>) => Promise<ActionResult>;
  /** How long since someone used this computer, when a window reports it; null when none does. */
  inputAgeMs?: () => number | null;
}
