// @session per-session: Builds the one hub of the project a daemon serves, over that project's editors and event log.
/**
 * The daemon's hub: a Hub over the daemon's own machinery. It reaches ue-mcp's
 * actions through the runtime (callAction), keeps this machine's runs, queue
 * and schedules under the project's data folder (~/.ue-mcp/data/<project>/hub),
 * publishes hub.changed and hub.lease on the daemon's event log, and works out
 * what else is using the editor from the events the daemon already publishes:
 * pie.started and pie.ended, call.started and call.finished (other MCP
 * clients), and editor.operation.
 *
 * What a client brings (the switch, its agents' calls, the desktop's input
 * age, a headless runner) arrives as HubClientOptions through
 * api.hub.configure (hub-client.ts).
 */
import * as path from "node:path";
import type { DaemonEvent, DaemonEventType, ActionResult } from "../contract/daemon.js";
import type { BusyCall, BusyState, EditorOperation } from "./contract.js";
import { Hub } from "./hub.js";
import type { OccupancySignals } from "./lease.js";
import type { HubClientOptions, HubRunner } from "./ports.js";

/** What the daemon gives the hub. */
export interface DaemonHubDeps {
  projectDir: string;
  /** The project's data folder: the hub keeps its files under <dataDir>/hub. */
  dataDir: string;
  events: {
    publish(type: DaemonEventType, data?: Record<string, unknown>): unknown;
    subscribe(listener: (e: DaemonEvent) => void): () => void;
  };
  callAction(category: string, action: string, args?: Record<string, unknown>): Promise<ActionResult>;
  editors(): Array<{ connected: boolean }>;
  /** Modal dialogs open in the project's editors now. */
  dialogs(): number;
  log: { warn(message: string, detail?: unknown): void };
  /** Start the hub's timers; tests drive tick() themselves. */
  timers?: boolean;
  /** The consent ledger's view (src/dispatch/disruption.ts): other sessions' holds and work, and open requests to stop, restart or rebuild the editor. */
  consent?: Pick<OccupancySignals, "sessions" | "disruptions">;
  /** The headless runner when no client brings one: the daemon hands ue-mcp's own (src/daemon/hub-headless.ts). Its close, when it has one, runs on dispose. */
  headless?: HubRunner;
}

export interface DaemonHub {
  readonly hub: Hub;
  dispose(): void;
}

/** Tracks what is using the editor from the daemon's events. */
export class EditorSignals {
  private pieRunning = false;
  private readonly pieListeners = new Set<(running: boolean) => void>();
  private calls: BusyCall[] = [];
  private nextCall = 1;
  private operation: EditorOperation | undefined;
  private failure: BusyState["failure"];

  constructor(private readonly now: () => number = Date.now) {}

  pie(): boolean {
    return this.pieRunning;
  }

  onPie(listener: (running: boolean) => void): () => void {
    this.pieListeners.add(listener);
    return () => this.pieListeners.delete(listener);
  }

  busy(): BusyState {
    return { now: this.now(), calls: this.calls, ...(this.operation ? { operation: this.operation } : {}), ...(this.failure ? { failure: this.failure } : {}) };
  }

  /** Take one daemon event in. */
  observe(e: Pick<DaemonEvent, "type" | "data">): void {
    const d = e.data;
    const str = (v: unknown): string => (typeof v === "string" ? v : "");
    switch (e.type) {
      case "pie.started":
      case "pie.ended": {
        const running = e.type === "pie.started";
        if (running === this.pieRunning) return;
        this.pieRunning = running;
        for (const l of this.pieListeners) l(running);
        return;
      }
      case "call.started": {
        // The hub's own category is the hub asking, not another client using the editor.
        if (str(d.category) === "hub") return;
        this.calls.push({ id: this.nextCall++, category: str(d.category), action: str(d.action), startedAt: this.now(), source: "client", client: str(d.client) || undefined, stopped: false });
        return;
      }
      case "call.finished": {
        const at = this.calls.findIndex((c) => c.category === str(d.category) && c.action === str(d.action) && (c.client ?? "") === str(d.client));
        if (at >= 0) this.calls = this.calls.filter((_, i) => i !== at);
        return;
      }
      case "editor.operation": {
        const op = str(d.op) as EditorOperation["op"];
        const step = str(d.step);
        const phase = str(d.phase);
        if (!op || !step) return;
        if (phase === "started") {
          const t = this.now();
          this.operation = { op, step, startedAt: this.operation?.op === op ? this.operation.startedAt : t, stepStartedAt: t };
          this.failure = undefined;
        } else if (phase === "failed") {
          this.operation = undefined;
          this.failure = { op, step, reason: str(d.reason) || "the step failed", at: this.now() };
        } else if (phase === "done" && (step === "editor.start_editor" || step === "editor.restart_editor" || (op === "stop" && step === "editor.stop_editor"))) {
          this.operation = undefined;
        }
        return;
      }
      case "editor.connected":
        this.failure = undefined;
        return;
    }
  }
}

/** The project's hub, wired to the daemon. */
export function createDaemonHub(deps: DaemonHubDeps, client: () => HubClientOptions = () => ({})): DaemonHub {
  const signals = new EditorSignals();
  const offSignals = deps.events.subscribe((e) => signals.observe(e));
  const host = {
    projectDir: deps.projectDir,
    dataDir: deps.dataDir,
    callAction: deps.callAction,
    editors: deps.editors,
    log: deps.log,
    events: {
      publish: (type: string, data?: Record<string, unknown>) => deps.events.publish(type as DaemonEventType, data),
      subscribe: (listener: (e: { type: string; data: Record<string, unknown> }) => void) => deps.events.subscribe(listener),
    },
  };
  const c = client();
  const own = deps.headless;
  const hub = new Hub({
    host,
    editor: {
      pie: () => signals.pie(),
      dialogs: () => deps.dialogs(),
      busy: () => {
        const busy = signals.busy();
        return c.agentCalls ? { ...busy, calls: [...c.agentCalls(), ...busy.calls] } : busy;
      },
      inputAgeMs: () => c.inputAgeMs?.() ?? null,
      sessions: () => deps.consent?.sessions?.() ?? [],
      disruptions: () => deps.consent?.disruptions?.() ?? [],
    },
    ...(c.enabled ? { enabled: c.enabled } : {}),
    headless: () => c.headless ?? own,
    ...(deps.timers === false ? { timers: false } : {}),
  });
  return {
    hub,
    dispose: () => {
      offSignals();
      hub.dispose();
      void own?.close?.("the hub stopped");
    },
  };
}

/** The project's data folder as the daemon names it for extensions, without the extension's own folder. */
export const hubDataDir = (userDir: string, projectKey: string): string => path.join(userDir, "data", projectKey);
