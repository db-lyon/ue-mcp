// @session independent: One registry per server process, shared by every editor it drives; a request names the operation, not an editor.
/**
 * Consent before a session takes the editor away from the others (#2075).
 *
 * Several agents can share one editor through the daemon. Stopping,
 * restarting or rebuilding it, building while it runs, or a Live Coding patch
 * takes away every other session's editor state, open PIE session and unsaved
 * work, so one session never decides that alone while others are active.
 *
 * A disruptive operation opens a request. Every other known session answers
 * it, explicitly (editor(disruption_vote)) or automatically from what it is
 * doing: a hold, a call in flight, a PIE session it started or an open
 * transaction is a wait, and a session idle for `idleSeconds` is a go. Any
 * block blocks the request, any wait or missing answer keeps it pending, and
 * otherwise it is approved. With nobody else active it is approved at once.
 * The requester cannot answer its own request; a person can approve or deny
 * any request through the daemon.
 *
 * Every server process holds its own registry, so the sharing point is the
 * daemon, exactly as for asset leases (leases.ts).
 */

export type DisruptiveOp = "stop" | "restart" | "rebuild" | "build" | "live_coding";
export const DISRUPTIVE_OPS: readonly DisruptiveOp[] = ["stop", "restart", "rebuild", "build", "live_coding"];

export type DisruptionVote = "go" | "wait_until_released" | "block";
export const DISRUPTION_VOTES: readonly DisruptionVote[] = ["go", "wait_until_released", "block"];

/** What one session says about one request: a vote, or none yet. */
export type DisruptionAnswerKind = DisruptionVote | "unanswered";

/**
 * pending: another session waits or has not answered. confirm: the requesting
 * agent has not said its own subagents are stopped. blocked: a session blocked
 * it. approved: it may run. done, denied, expired: closed.
 */
export type DisruptionState = "pending" | "confirm" | "blocked" | "approved" | "done" | "denied" | "expired";

/**
 * What the requesting agent says about subagents ue-mcp cannot see (ones that
 * share its connection): it has none, or it paused them.
 */
export type SubagentAttestation = "none" | "paused";
export const SUBAGENT_ATTESTATIONS: readonly SubagentAttestation[] = ["none", "paused"];

export type DisruptionMode = "auto" | "off";

export interface DisruptionConfig {
  mode: DisruptionMode;
  /** A session with no call for this long answers go by itself, in seconds. */
  idleSeconds: number;
  /** How long a request stays open before it expires, in seconds. */
  requestTtlSeconds: number;
}

export const DEFAULT_IDLE_SECONDS = 180;
export const DEFAULT_REQUEST_TTL_SECONDS = 900;
/** The longest a hold may be taken for. A holder that never releases frees the editor within this. */
export const MAX_HOLD_TTL_SECONDS = 24 * 60 * 60;
export const DEFAULT_HOLD_TTL_SECONDS = 60 * 60;
/** A session known only from its calls (no connection to close) is forgotten after this long without one. */
const FORGET_AFTER_MS = 60 * 60_000;

export function resolveDisruptionConfig(cfg?: { mode?: string; idleSeconds?: number; requestTtlSeconds?: number }): DisruptionConfig {
  const positive = (n: unknown, fallback: number): number => (typeof n === "number" && Number.isFinite(n) && n > 0 ? n : fallback);
  return {
    mode: cfg?.mode === "off" ? "off" : "auto",
    idleSeconds: positive(cfg?.idleSeconds, DEFAULT_IDLE_SECONDS),
    requestTtlSeconds: positive(cfg?.requestTtlSeconds, DEFAULT_REQUEST_TTL_SECONDS),
  };
}

/** Who is calling: an MCP session, an extension's agent, an HTTP caller, or this process. */
export interface DisruptionCaller {
  id: string;
  label: string;
  /** The session that started this one, when the client says so. */
  parent?: string;
}

export interface DisruptionHold {
  reason: string;
  at: number;
  expiresAt: number;
}

/** What the registry knows about one session's use of the editor. */
export interface SessionActivity {
  id: string;
  label: string;
  parent?: string;
  /** True for a session with a connection the daemon closes when it ends. */
  connected: boolean;
  firstSeen: number;
  lastCallAt: number | null;
  /** Calls in flight, by token, with the action each one runs. */
  inflight: Map<number, string>;
  /**
   * Work an external runtime reports for a participant it drives (Studio's
   * agent, say): what it is busy with. Empty is idle. Counted like calls in flight.
   */
  reported: string[];
  /** It started a PIE session that has not ended. */
  pie: boolean;
  /** Editor transactions it began and has not ended or cancelled. */
  transactions: number;
  hold: DisruptionHold | null;
}

export interface DisruptionAnswer {
  session: string;
  label: string;
  parent?: string;
  answer: DisruptionAnswerKind;
  reason: string;
  /** False when the session said it; true when it was read from what the session is doing. */
  auto: boolean;
  /** Assets the session holds leases on, which is the work it has touched. */
  touched?: string[];
}

export interface DisruptionRequest {
  id: string;
  op: DisruptiveOp;
  /** The `category.action` or route that asked. */
  action: string;
  requester: DisruptionCaller;
  reason?: string;
  createdAt: number;
  expiresAt: number;
  /** Explicit votes, by session id. */
  votes: Map<string, { vote: DisruptionVote; reason: string; at: number }>;
  override: { decision: "approve" | "deny"; by: string; at: number } | null;
  /** Set once the request ran (done), was denied, or expired. */
  closed: { state: "done" | "denied" | "expired"; at: number } | null;
  /** Sessions already told about this request in a tool response. */
  notified: Set<string>;
  /** The state last published, so a change that moves nothing is not announced. */
  announced?: DisruptionState;
  /**
   * A person asked (an HTTP route), so there are no hidden subagents to
   * account for. Otherwise the requester must attest to its own.
   */
  person: boolean;
  attested: SubagentAttestation | null;
  /** The requester's own calls in flight beside the one asking, as of the last ask. */
  ownInflight: string[];
}

/** The requesting session's own side of a request. */
export interface DisruptionSelf {
  /** False for a person's request, which has no subagents to attest to. */
  attestationRequired: boolean;
  attested: SubagentAttestation | null;
  /** Its other calls in flight on its own connection (from its subagents, typically), which hold the request regardless. */
  inflight: string[];
}

/** A request as callers, routes and events see it. */
export interface DisruptionView {
  id: string;
  op: DisruptiveOp;
  action: string;
  requester: DisruptionCaller;
  reason?: string;
  state: DisruptionState;
  createdAt: string;
  expiresAt: string;
  answers: DisruptionAnswer[];
  self: DisruptionSelf;
  override?: { decision: "approve" | "deny"; by: string; at: string };
}

const OP_WORDS: Record<DisruptiveOp, string> = {
  stop: "stop the editor",
  restart: "restart the editor",
  rebuild: "rebuild the editor (stop it, build, start it again)",
  build: "build the project's C++ while the editor runs",
  live_coding: "patch the running editor with Live Coding",
};

/** What an operation does, in a few words, for messages. */
export function describeOp(op: DisruptiveOp): string {
  return OP_WORDS[op];
}

/**
 * One session's answer to a request, from its vote or from what it is doing.
 * Pure, so the rule is testable without a registry.
 */
export function answerFor(
  activity: SessionActivity,
  vote: { vote: DisruptionVote; reason: string } | undefined,
  now: number,
  idleMs: number,
): { answer: DisruptionAnswerKind; reason: string; auto: boolean } {
  if (vote) return { answer: vote.vote, reason: vote.reason || `voted ${vote.vote}`, auto: false };
  if (activity.hold && activity.hold.expiresAt > now) {
    return { answer: "wait_until_released", reason: `holds the editor: ${activity.hold.reason}`, auto: false };
  }
  if (activity.inflight.size > 0 || activity.reported.length > 0) {
    const actions = [...new Set([...activity.inflight.values(), ...activity.reported])];
    return { answer: "wait_until_released", reason: `busy (${actions.join(", ")})`, auto: true };
  }
  if (activity.pie) return { answer: "wait_until_released", reason: "it started a PIE session that is still running", auto: true };
  if (activity.transactions > 0) {
    return { answer: "wait_until_released", reason: `it has ${activity.transactions} editor transaction(s) open`, auto: true };
  }
  const last = activity.lastCallAt ?? activity.firstSeen;
  const idle = now - last;
  if (idle >= idleMs) return { answer: "go", reason: `idle for ${Math.round(idle / 1000)}s`, auto: true };
  return {
    answer: "unanswered",
    reason: `active ${Math.round(idle / 1000)}s ago and has not answered; it counts as go after ${Math.round(idleMs / 1000)}s without a call`,
    auto: true,
  };
}

/**
 * The state answers add up to, before any override. Pure.
 *
 * Any block blocks. A requester that has not attested to its own subagents
 * is asked to (confirm), since ue-mcp cannot see the ones sharing its
 * connection. A wait, a missing answer, or a call still in flight on the
 * requester's own connection keeps it pending. Otherwise it is approved.
 */
export function stateOf(
  answers: readonly { answer: DisruptionAnswerKind }[],
  self: DisruptionSelf = { attestationRequired: false, attested: null, inflight: [] },
): "pending" | "confirm" | "blocked" | "approved" {
  if (answers.some((a) => a.answer === "block")) return "blocked";
  if (self.attestationRequired && !self.attested) return "confirm";
  if (self.inflight.length > 0) return "pending";
  if (answers.some((a) => a.answer !== "go")) return "pending";
  return "approved";
}

export interface DisruptionChange {
  type: "requested" | "updated" | "hold";
  request?: DisruptionView;
  session?: { id: string; label: string; hold: { reason: string; expiresAt: string } | null };
}

let nextToken = 1;

export class DisruptionRegistry {
  config: DisruptionConfig = resolveDisruptionConfig();
  /** The clock, replaceable in tests. */
  now: () => number = () => Date.now();
  /** The id calls made by this process itself are counted under. The runtime sets it. */
  processId: string | null = null;
  /** The assets a session holds leases on. The runtime wires it to the lease registry. */
  touched: (sessionId: string) => string[] = () => [];
  private readonly sessions = new Map<string, SessionActivity>();
  private readonly requests = new Map<string, DisruptionRequest>();
  private readonly listeners = new Set<(change: DisruptionChange) => void>();

  get enforcing(): boolean {
    return this.config.mode !== "off";
  }

  /** Follow requests and holds. Returns an unsubscribe. */
  onChange(listener: (change: DisruptionChange) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  private emit(change: DisruptionChange): void {
    for (const l of this.listeners) {
      try {
        l(change);
      } catch {
        // A listener's failure is its own.
      }
    }
  }

  private activity(caller: DisruptionCaller): SessionActivity {
    let a = this.sessions.get(caller.id);
    if (!a) {
      a = {
        id: caller.id,
        label: caller.label,
        ...(caller.parent ? { parent: caller.parent } : {}),
        connected: false,
        firstSeen: this.now(),
        lastCallAt: null,
        inflight: new Map(),
        reported: [],
        pie: false,
        transactions: 0,
        hold: null,
      };
      this.sessions.set(caller.id, a);
    } else {
      // The label follows the client once it has introduced itself.
      if (caller.label && caller.label !== "unknown client") a.label = caller.label;
      if (caller.parent) a.parent = caller.parent;
    }
    return a;
  }

  // ── Sessions ─────────────────────────────────────────────────────

  /** A connection opened: the session is known before its first call. */
  opened(caller: DisruptionCaller): void {
    this.activity(caller).connected = true;
  }

  /**
   * What an external runtime reports for a participant it drives: register it
   * (it stays known until closed), and set what it is busy with, whether it
   * started a PIE session, and its open transactions. Fields left out keep
   * their value. Reporting work counts as activity.
   */
  report(caller: DisruptionCaller, state: { busy?: string[]; pie?: boolean; transactions?: number } = {}): void {
    const a = this.activity(caller);
    a.connected = true;
    if (state.busy) {
      a.reported = state.busy.filter((b) => typeof b === "string" && b !== "");
      a.lastCallAt = this.now();
    }
    if (typeof state.pie === "boolean") a.pie = state.pie;
    if (typeof state.transactions === "number" && state.transactions >= 0) a.transactions = Math.floor(state.transactions);
    this.reevaluate();
  }

  /** Whether a participant is known. */
  has(id: string): boolean {
    return this.sessions.has(id);
  }

  /**
   * A connection closed: the session, its hold and its votes go with it, and so
   * do its open requests. Nobody is left to run them, so other sessions are not
   * asked about them, and are not told about them in every later response.
   */
  closed(id: string): void {
    const had = this.sessions.delete(id);
    for (const r of this.requests.values()) {
      if (!r.closed && r.requester.id === id) {
        r.closed = { state: "expired", at: this.now() };
        this.emit({ type: "updated", request: this.view(r) });
      } else if (r.votes.delete(id)) this.emit({ type: "updated", request: this.view(r) });
    }
    if (had) this.reevaluate();
  }

  /** A call started. Returns the token its end is reported with. */
  callStarted(caller: DisruptionCaller, task: string): number {
    const a = this.activity(caller);
    const token = nextToken++;
    a.inflight.set(token, task);
    a.lastCallAt = this.now();
    return token;
  }

  /**
   * A call ended. What it was tells the registry about PIE and transactions:
   * the session that started PIE is using it until a stop, and a transaction
   * it began is open until it ends or cancels it.
   */
  callFinished(callerId: string, token: number, task: string, params: Record<string, unknown>, ok: boolean): void {
    const a = this.sessions.get(callerId);
    if (!a) return;
    a.inflight.delete(token);
    a.lastCallAt = this.now();
    if (!ok) return;
    const action = task.slice(task.indexOf(".") + 1);
    if (startsPie(task, params)) a.pie = true;
    else if (stopsPie(task, params)) this.pieEnded();
    if (/^begin_[a-z_]*transaction$/.test(action)) a.transactions += 1;
    else if (/^(end|cancel)_[a-z_]*transaction$/.test(action)) a.transactions = Math.max(0, a.transactions - 1);
    this.reevaluate();
  }

  /** The editor said its PIE session ended: nobody is using one any more. */
  pieEnded(): void {
    for (const a of this.sessions.values()) a.pie = false;
  }

  // ── Holds ────────────────────────────────────────────────────────

  hold(caller: DisruptionCaller, reason: string, ttlSeconds?: number): DisruptionHold {
    const a = this.activity(caller);
    const ttl = Math.min(Math.max(1, ttlSeconds ?? DEFAULT_HOLD_TTL_SECONDS), MAX_HOLD_TTL_SECONDS);
    const at = this.now();
    a.hold = { reason, at, expiresAt: at + ttl * 1000 };
    this.emit({ type: "hold", session: this.holdView(a) });
    this.reevaluate();
    return a.hold;
  }

  /** Clear the caller's hold, and turn its wait votes into go. Returns what changed. */
  release(caller: DisruptionCaller): { hadHold: boolean; votesReleased: string[] } {
    const a = this.activity(caller);
    const hadHold = a.hold !== null;
    a.hold = null;
    const votesReleased: string[] = [];
    for (const r of this.requests.values()) {
      const v = r.votes.get(caller.id);
      if (r.closed || v?.vote !== "wait_until_released") continue;
      r.votes.set(caller.id, { vote: "go", reason: "released", at: this.now() });
      votesReleased.push(r.id);
    }
    if (hadHold) this.emit({ type: "hold", session: this.holdView(a) });
    this.reevaluate(true);
    return { hadHold, votesReleased };
  }

  private holdView(a: SessionActivity): NonNullable<DisruptionChange["session"]> {
    return {
      id: a.id,
      label: a.label,
      hold: a.hold ? { reason: a.hold.reason, expiresAt: new Date(a.hold.expiresAt).toISOString() } : null,
    };
  }

  // ── Requests ─────────────────────────────────────────────────────

  private prune(): void {
    const t = this.now();
    for (const r of this.requests.values()) {
      if (!r.closed && r.expiresAt <= t) {
        r.closed = { state: "expired", at: t };
        this.emit({ type: "updated", request: this.view(r) });
      }
    }
    // Closed requests are kept a while for the status listing, then dropped.
    for (const [id, r] of this.requests) if (r.closed && t - r.closed.at > FORGET_AFTER_MS) this.requests.delete(id);
    for (const [id, a] of this.sessions) {
      if (a.hold && a.hold.expiresAt <= t) a.hold = null;
      if (!a.connected && a.inflight.size === 0 && !a.hold && t - (a.lastCallAt ?? a.firstSeen) > FORGET_AFTER_MS) this.sessions.delete(id);
    }
  }

  /** The open request this caller made for this operation, if any. */
  private openRequest(callerId: string, op: DisruptiveOp): DisruptionRequest | undefined {
    for (const r of this.requests.values()) if (!r.closed && r.requester.id === callerId && r.op === op) return r;
    return undefined;
  }

  /**
   * Ask for an operation: reuse the caller's open request for it, or open one.
   * The view says whether it may run now (`state: "approved"`).
   *
   * `ownCalls` are the tokens of the calls this ask is made from (the call
   * itself and any call it runs inside), so they are not counted against it;
   * every other call in flight on the caller's own connection is. `person`
   * marks a request a person made over HTTP, which needs no attestation.
   */
  request(
    caller: DisruptionCaller,
    op: DisruptiveOp,
    action: string,
    opts: { reason?: string; subagents?: SubagentAttestation; person?: boolean; ownCalls?: readonly number[] } = {},
  ): DisruptionView {
    this.prune();
    const me = this.activity(caller);
    const reason = opts.reason;
    let r = this.openRequest(caller.id, op);
    const isNew = !r;
    if (!r) {
      const t = this.now();
      r = {
        id: `r-${Math.random().toString(16).slice(2, 8)}`,
        op,
        action,
        requester: { ...caller },
        ...(reason ? { reason } : {}),
        createdAt: t,
        expiresAt: t + this.config.requestTtlSeconds * 1000,
        votes: new Map(),
        override: null,
        closed: null,
        notified: new Set(),
        person: opts.person === true,
        attested: null,
        ownInflight: [],
      };
      this.requests.set(r.id, r);
    } else if (reason) {
      r.reason = reason;
    }
    if (opts.subagents) r.attested = opts.subagents;
    const own = new Set(opts.ownCalls ?? []);
    r.ownInflight = [...[...me.inflight].filter(([token]) => !own.has(token)).map(([, task]) => task), ...me.reported];
    const view = this.view(r);
    // Approved at once (nobody else, or everyone idle) is not worth announcing.
    if (isNew && view.state !== "approved") this.emit({ type: "requested", request: view });
    else if (!isNew && view.state !== r.announced) this.emit({ type: "updated", request: view });
    r.announced = view.state;
    return view;
  }

  /** The operation ran (or was attempted): the request is spent. */
  complete(id: string): void {
    const r = this.requests.get(id);
    if (!r || r.closed) return;
    r.closed = { state: "done", at: this.now() };
    this.emit({ type: "updated", request: this.view(r) });
  }

  vote(caller: DisruptionCaller, id: string, vote: DisruptionVote, reason = ""): DisruptionView {
    this.prune();
    const r = this.requests.get(id);
    if (!r) throw new DisruptionError("not_found", `No disruption request '${id}'. editor(action='disruption_status') lists the open ones.`);
    if (r.closed) throw new DisruptionError("closed", `Request '${id}' is ${r.closed.state}; there is nothing to vote on. A new request opens when its session asks again.`);
    if (r.requester.id === caller.id) {
      throw new DisruptionError(
        "own_request",
        `Request '${id}' is this session's own; it cannot answer it. The other sessions answer, and a person can approve it through the daemon.`,
      );
    }
    this.activity(caller);
    r.votes.set(caller.id, { vote, reason, at: this.now() });
    const view = this.view(r);
    r.announced = view.state;
    this.emit({ type: "updated", request: view });
    return view;
  }

  /** A person's decision, which outranks every answer. */
  override(id: string, decision: "approve" | "deny", by: string): DisruptionView {
    this.prune();
    const r = this.requests.get(id);
    if (!r) throw new DisruptionError("not_found", `No disruption request '${id}'.`);
    if (r.closed && r.closed.state !== "denied") throw new DisruptionError("closed", `Request '${id}' is ${r.closed.state}.`);
    r.override = { decision, by, at: this.now() };
    if (decision === "deny") r.closed = { state: "denied", at: this.now() };
    else r.closed = null;
    const view = this.view(r);
    r.announced = view.state;
    this.emit({ type: "updated", request: view });
    return view;
  }

  get(id: string): DisruptionView | null {
    this.prune();
    const r = this.requests.get(id);
    return r ? this.view(r) : null;
  }

  list(): DisruptionView[] {
    this.prune();
    return [...this.requests.values()].sort((a, b) => b.createdAt - a.createdAt).map((r) => this.view(r));
  }

  /** Every known session's activity, for the status listing. */
  activities(): Array<{
    id: string;
    label: string;
    parent?: string;
    connected: boolean;
    lastCallAt: string | null;
    inflight: string[];
    pie: boolean;
    transactions: number;
    hold: { reason: string; expiresAt: string } | null;
    touched: string[];
  }> {
    this.prune();
    return [...this.sessions.values()].map((a) => ({
      id: a.id,
      label: a.label,
      ...(a.parent ? { parent: a.parent } : {}),
      connected: a.connected,
      lastCallAt: a.lastCallAt === null ? null : new Date(a.lastCallAt).toISOString(),
      inflight: [...a.inflight.values(), ...a.reported],
      pie: a.pie,
      transactions: a.transactions,
      hold: a.hold ? { reason: a.hold.reason, expiresAt: new Date(a.hold.expiresAt).toISOString() } : null,
      touched: this.touched(a.id),
    }));
  }

  private answersFor(r: DisruptionRequest): DisruptionAnswer[] {
    const t = this.now();
    const idleMs = this.config.idleSeconds * 1000;
    const out: DisruptionAnswer[] = [];
    for (const a of this.sessions.values()) {
      if (a.id === r.requester.id) continue;
      // Known only from asking (a person's request, the daemon's own): it uses nothing.
      if (!a.connected && a.lastCallAt === null && !a.hold && r.votes.get(a.id) === undefined) continue;
      // This process's own calls (the daemon's operation steps, an extension
      // call that names no session) hold a request only while they run.
      const answered = answerFor(a, r.votes.get(a.id), t, a.id === this.processId ? 0 : idleMs);
      const touched = this.touched(a.id);
      out.push({
        session: a.id,
        label: a.label,
        ...(a.parent ? { parent: a.parent } : {}),
        ...answered,
        ...(touched.length > 0 ? { touched } : {}),
      });
    }
    return out;
  }

  private selfOf(r: DisruptionRequest): DisruptionSelf {
    return { attestationRequired: !r.person, attested: r.attested, inflight: [...r.ownInflight] };
  }

  private stateFor(r: DisruptionRequest, answers: DisruptionAnswer[], self: DisruptionSelf): DisruptionState {
    if (r.closed) return r.closed.state;
    if (r.override?.decision === "approve") return "approved";
    return stateOf(answers, self);
  }

  /** The requester attests to its own subagents on an open request, by id. */
  attest(caller: DisruptionCaller, id: string, subagents: SubagentAttestation): DisruptionView {
    this.prune();
    const r = this.requests.get(id);
    if (!r) throw new DisruptionError("not_found", `No disruption request '${id}'. editor(action='disruption_status') lists the open ones.`);
    if (r.closed) throw new DisruptionError("closed", `Request '${id}' is ${r.closed.state}. Run the operation again to open a new one.`);
    if (r.requester.id !== caller.id) {
      throw new DisruptionError("not_requester", `Request '${id}' belongs to '${r.requester.label}'. Only the session that asked attests to its own subagents; answer it with editor(action='disruption_vote').`);
    }
    r.attested = subagents;
    const view = this.view(r);
    r.announced = view.state;
    this.emit({ type: "updated", request: view });
    return view;
  }

  private view(r: DisruptionRequest): DisruptionView {
    const answers = this.answersFor(r);
    const self = this.selfOf(r);
    return {
      id: r.id,
      op: r.op,
      action: r.action,
      requester: { ...r.requester },
      ...(r.reason ? { reason: r.reason } : {}),
      state: this.stateFor(r, answers, self),
      createdAt: new Date(r.createdAt).toISOString(),
      expiresAt: new Date(r.expiresAt).toISOString(),
      answers,
      self,
      ...(r.override ? { override: { ...r.override, at: new Date(r.override.at).toISOString() } } : {}),
    };
  }

  /** Announce each open request whose state a change moved. */
  private reevaluate(force = false): void {
    if (this.listeners.size === 0) return;
    for (const r of this.requests.values()) {
      if (r.closed) continue;
      const view = this.view(r);
      if (!force && view.state === r.announced) continue;
      r.announced = view.state;
      this.emit({ type: "updated", request: view });
    }
  }

  /**
   * What a session is told in its next tool response: each open request it
   * has not been told about and has not voted on, once.
   */
  noticesFor(sessionId: string): string[] {
    if (!this.enforcing) return [];
    this.prune();
    const out: string[] = [];
    for (const r of this.requests.values()) {
      if (r.closed || r.requester.id === sessionId || r.notified.has(sessionId) || r.votes.has(sessionId)) continue;
      const view = this.view(r);
      if (view.state === "approved") continue;
      r.notified.add(sessionId);
      const mine = view.answers.find((a) => a.session === sessionId);
      out.push(requestNotice(view, mine));
    }
    return out;
  }
}

export class DisruptionError extends Error {
  constructor(public readonly kind: "not_found" | "closed" | "own_request" | "not_requester", message: string) {
    super(message);
    this.name = "DisruptionError";
  }
}

/** A call that starts a PIE session the caller then uses. */
function startsPie(task: string, params: Record<string, unknown>): boolean {
  if (task === "editor.play_in_editor" || task === "editor.play_in_editor_ignore_blueprint_errors") return params.pieAction === "start";
  return task === "pie.replay_run" || task === "pie.uat_run";
}

function stopsPie(task: string, params: Record<string, unknown>): boolean {
  if (task === "editor.play_in_editor" || task === "editor.play_in_editor_ignore_blueprint_errors") return params.pieAction === "stop";
  return task === "pie.uat_abort";
}

const ANSWER_WORDS: Record<DisruptionAnswerKind, string> = {
  go: "go",
  wait_until_released: "wait",
  block: "block",
  unanswered: "no answer yet",
};

/** One line per answer, for a summary. */
export function describeAnswers(view: DisruptionView): string[] {
  return view.answers.map((a) => {
    const parent = a.parent === view.requester.id ? ", your own subagent" : a.parent ? `, started by ${a.parent}` : "";
    const who = `'${a.label}' (${a.session}${parent})`;
    const touched = a.touched?.length ? `; it holds ${a.touched.slice(0, 5).join(", ")}${a.touched.length > 5 ? ` and ${a.touched.length - 5} more` : ""}` : "";
    return `- ${who}: ${ANSWER_WORDS[a.answer]}, ${a.reason}${touched}`;
  });
}

/** The vote command, written out for a session to send. */
export function voteCommand(id: string): string {
  return `editor(action="disruption_vote", requestId="${id}", vote="go" | "wait_until_released" | "block", reason="...")`;
}

/** What another session reads about a request in its next tool response. */
export function requestNotice(view: DisruptionView, mine: DisruptionAnswer | undefined): string {
  const asks = `[EDITOR DISRUPTION REQUEST ${view.id}] '${view.requester.label}' (session ${view.requester.id}) asks to ${describeOp(view.op)}`
    + `${view.reason ? `: "${view.reason}"` : ""}. That ends every session's editor state, PIE session and unsaved work in this editor.`;
  const current = mine ? ` Your answer is currently: ${ANSWER_WORDS[mine.answer]} (${mine.reason}).` : "";
  return `${asks}${current} Answer with ${voteCommand(view.id)}: go lets it run, wait_until_released holds it until you call `
    + `editor(action="release"), block refuses it. Save what you need first. editor(action="hold", reason="...") holds the editor `
    + "against every request until you release it.";
}
