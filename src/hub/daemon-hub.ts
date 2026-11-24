// @session per-session: Builds the one hub of the project a daemon serves, over that project's editors and event log.
/**
 * The daemon's hub: a Hub over the daemon's own machinery. It reaches ue-mcp's
 * actions through the runtime (callAction), keeps its files under the
 * project's data folder (~/.ue-mcp/data/<project>/hub), publishes hub.changed
 * and hub.lease on the daemon's event log, and works out what else is using
 * the editor from the events the daemon already publishes: pie.started and
 * pie.ended, call.started and call.finished (other MCP clients), and
 * editor.operation.
 *
 * Everything a client brings (an agent that fixes playtests, a flow that
 * writes them, run cards, the experiment switch, the Manual/Automatic
 * setting) arrives as HubClientOptions through api.hub.configure
 * (hub-client.ts). The hub reads them live, so a client that configures
 * after the hub started is heard; with none, the defaults here stand.
 */
import * as path from "node:path";
import type { DaemonEvent, DaemonEventType, ActionResult } from "../contract/daemon.js";
import type { BusyCall, BusyState, EditorOperation } from "./contract.js";
import { Hub } from "./hub.js";
import type { HubClientOptions, HubPolicy } from "./ports.js";
import { ScriptIndex } from "./script-index.js";
import { ScriptRunBroker } from "./script-broker.js";
import { VerdictHold } from "./verdict-hold.js";

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
  /** The signed-in ue-mcp account's login, who a sign-off is from. */
  signer?: HubPolicy["signer"];
  /** The project's engine version, major.minor. */
  engine?: () => string | null;
  /** Start the hub's timers; tests drive tick() themselves. */
  timers?: boolean;
}

export interface DaemonHub {
  readonly hub: Hub;
  readonly broker: ScriptRunBroker;
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
  const own = new ScriptRunBroker(host);
  // A hold on the runs the broker started: PIE paused at a manual step until a verdict, then stopped with no verdict.
  const hold = new VerdictHold({
    callAction: deps.callAction,
    publish: (type, data) => deps.events.publish(`hub.${type === "qa.held" ? "held" : type}` as DaemonEventType, data),
    log: deps.log,
    stop: (runId, reason) => own.stopRun(runId, reason),
    waitMinutes: (run) => own.waitMinutesOf(run) ?? 10,
    owns: (run) => own.owns(run),
  });
  own.attachHold(hold);
  const c = client();
  const hub = new Hub({
    host,
    broker: c.broker ?? own,
    index: new ScriptIndex(host),
    editor: {
      pie: () => signals.pie(),
      dialogs: () => deps.dialogs(),
      busy: () => signals.busy(),
      inputAgeMs: () => c.inputAgeMs?.() ?? null,
      onPie: (l) => signals.onPie(l),
    },
    signer: c.signer ?? deps.signer,
    ...(c.playtestRuns ? { playtestRuns: c.playtestRuns } : {}),
    ...(c.enabled ? { enabled: c.enabled } : {}),
    ...(c.fixer ? { fixer: c.fixer } : {}),
    ...(c.writePlaytests ? { writePlaytests: c.writePlaytests } : {}),
    ...(c.replay ? { replay: c.replay } : {}),
    ...(deps.engine ? { engine: deps.engine } : {}),
    ...(deps.timers === false ? { timers: false } : {}),
  });
  return {
    hub,
    broker: own,
    dispose: () => {
      offSignals();
      hub.dispose();
      hold.dispose();
    },
  };
}

/** The project's data folder as the daemon names it for extensions, without the extension's own folder. */
export const hubDataDir = (userDir: string, projectKey: string): string => path.join(userDir, "data", projectKey);
