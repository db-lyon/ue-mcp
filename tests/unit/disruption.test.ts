/**
 * Consent before a session takes the editor away from the others (#2075):
 * how each session's answer is read, how answers add up, the requester's own
 * subagents, holds, people overriding, and the scope that keeps an approved
 * operation's own steps from asking again.
 */
import { describe, expect, it } from "vitest";
import {
  DisruptionRegistry,
  DisruptionError,
  answerFor,
  resolveDisruptionConfig,
  stateOf,
  type SessionActivity,
} from "../../src/dispatch/disruption.js";
import {
  askConsent,
  consentRefusalMessage,
  disruptiveBridgeOp,
  disruptiveCallOf,
  isQuitCommand,
  processCaller,
  withDisruptionCall,
  withDisruptionCaller,
  withEditorConsent,
} from "../../src/dispatch/disruption-gate.js";
import { McpError, ErrorCode } from "../../src/core/errors.js";

const A = { id: "session-a", label: "agent A" };
const B = { id: "session-b", label: "agent B" };
const C = { id: "session-c", label: "agent C" };
const MIN = 60_000;

function clock(start = 10_000_000) {
  let t = start;
  return { now: () => t, advance: (ms: number) => (t += ms) };
}

function registry(c = clock()): DisruptionRegistry {
  const r = new DisruptionRegistry();
  r.now = c.now;
  return r;
}

function activity(over: Partial<SessionActivity> = {}): SessionActivity {
  return {
    id: "s", label: "s", connected: true, firstSeen: 0, lastCallAt: null, inflight: new Map(), reported: [],
    pie: false, transactions: 0, hold: null, ...over,
  };
}

describe("one session's answer", () => {
  const idle = 3 * MIN;
  it("is its vote when it gave one, whatever it is doing", () => {
    const busy = activity({ inflight: new Map([[1, "blueprint.compile"]]) });
    expect(answerFor(busy, { vote: "go", reason: "saved" }, 0, idle)).toMatchObject({ answer: "go", auto: false, reason: "saved" });
    expect(answerFor(activity(), { vote: "block", reason: "" }, 0, idle).answer).toBe("block");
  });

  it("waits while it holds the editor, has a call in flight, started PIE or has a transaction open", () => {
    const t = 100 * MIN;
    expect(answerFor(activity({ hold: { reason: "saving", at: 0, expiresAt: t + 1 } }), undefined, t, idle)).toMatchObject({ answer: "wait_until_released", auto: false });
    expect(answerFor(activity({ inflight: new Map([[1, "level.place"]]) }), undefined, t, idle).reason).toContain("level.place");
    expect(answerFor(activity({ reported: ["studio: editing BP_Door"] }), undefined, t, idle).answer).toBe("wait_until_released");
    expect(answerFor(activity({ pie: true }), undefined, t, idle).reason).toContain("PIE");
    expect(answerFor(activity({ transactions: 2 }), undefined, t, idle).reason).toContain("2 editor transaction");
  });

  it("goes once idle long enough, and has not answered before then", () => {
    expect(answerFor(activity({ lastCallAt: 0 }), undefined, idle, idle)).toMatchObject({ answer: "go", auto: true });
    expect(answerFor(activity({ lastCallAt: 0 }), undefined, idle - 1, idle).answer).toBe("unanswered");
    // A session that connected and never called is idle from when it connected.
    expect(answerFor(activity({ firstSeen: 0 }), undefined, idle, idle).answer).toBe("go");
  });

  it("ignores a hold that ran out", () => {
    expect(answerFor(activity({ lastCallAt: 0, hold: { reason: "x", at: 0, expiresAt: 10 } }), undefined, idle, idle).answer).toBe("go");
  });
});

describe("how answers add up", () => {
  it("blocks on any block, waits on any wait or missing answer, and is approved when all go", () => {
    expect(stateOf([{ answer: "go" }, { answer: "block" }, { answer: "wait_until_released" }])).toBe("blocked");
    expect(stateOf([{ answer: "go" }, { answer: "unanswered" }])).toBe("pending");
    expect(stateOf([{ answer: "go" }, { answer: "go" }])).toBe("approved");
    expect(stateOf([])).toBe("approved");
  });

  it("asks the requester to account for its own subagents, and waits on its own calls in flight", () => {
    expect(stateOf([], { attestationRequired: true, attested: null, inflight: [] })).toBe("confirm");
    expect(stateOf([], { attestationRequired: true, attested: "paused", inflight: ["level.place"] })).toBe("pending");
    expect(stateOf([], { attestationRequired: true, attested: "none", inflight: [] })).toBe("approved");
    expect(stateOf([{ answer: "block" }], { attestationRequired: true, attested: null, inflight: [] })).toBe("blocked");
  });
});

describe("requests", () => {
  it("lets a lone session with no subagents through once it says so, and asks it first", () => {
    const r = registry();
    const first = r.request(A, "rebuild", "editor.restart_editor");
    expect(first.state).toBe("confirm");
    expect(consentRefusalMessage(first)).toContain('subagents="paused"');
    // The same request is re-read, not a new one.
    const again = r.request(A, "rebuild", "editor.restart_editor", { subagents: "none" });
    expect(again.id).toBe(first.id);
    expect(again.state).toBe("approved");
  });

  it("goes straight through for a person, who has no hidden subagents", () => {
    expect(registry().request({ id: "http:a person", label: "a person" }, "restart", "POST /v1/editor/restart", { person: true }).state).toBe("approved");
  });

  it("holds a lone session on its own other calls in flight, whatever it attests", () => {
    const r = registry();
    const own = r.callStarted(A, "editor.stop_editor");
    r.callStarted(A, "blueprint.add_variable");
    const view = r.request(A, "stop", "editor.stop_editor", { subagents: "paused", ownCalls: [own] });
    expect(view.state).toBe("pending");
    expect(view.self.inflight).toEqual(["blueprint.add_variable"]);
    expect(consentRefusalMessage(view)).toContain("blueprint.add_variable");
  });

  it("counts a subagent with its own connection as a participant attributed to its primary", () => {
    const c = clock();
    const r = registry(c);
    r.opened({ id: "sub-1", label: "agent A subagent", parent: A.id });
    r.callStarted({ id: "sub-1", label: "agent A subagent", parent: A.id }, "level.place_actor");
    const view = r.request(A, "restart", "editor.restart_editor", { subagents: "paused" });
    expect(view.state).toBe("pending");
    expect(view.answers[0]).toMatchObject({ session: "sub-1", parent: A.id, answer: "wait_until_released" });
    expect(consentRefusalMessage(view)).toContain("your own subagent");
  });

  it("walks the incident: B goes, C waits until released, D blocks, and only a person overrides", () => {
    const c = clock();
    const r = registry(c);
    const D = { id: "session-d", label: "agent D" };
    for (const s of [B, C, D]) r.opened(s);
    const asked = r.request(A, "rebuild", "editor.restart_editor", { subagents: "none" });
    expect(asked.state).toBe("pending");

    r.vote(B, asked.id, "go", "nothing open");
    r.vote(C, asked.id, "wait_until_released", "pausing my subagents");
    expect(r.vote(D, asked.id, "block", "sensitive operations underway").state).toBe("blocked");
    expect(() => r.vote(A, asked.id, "go")).toThrow(DisruptionError);

    // D changes its mind; C still waits until it releases.
    r.vote(D, asked.id, "go");
    expect(r.get(asked.id)!.state).toBe("pending");
    expect(r.release(C).votesReleased).toEqual([asked.id]);
    expect(r.get(asked.id)!.state).toBe("approved");

    // A blocked request is approved by a person and nobody else.
    const next = r.request(C, "stop", "editor.stop_editor", { subagents: "none" });
    r.vote(B, next.id, "block", "mid-save");
    expect(r.override(next.id, "approve", "the user").state).toBe("approved");
    expect(r.override(next.id, "deny", "the user").state).toBe("denied");
  });

  it("drops a session's votes and hold when it disconnects", () => {
    const r = registry();
    r.opened(B);
    r.hold(B, "unsaved level");
    const asked = r.request(A, "stop", "editor.stop_editor", { subagents: "none" });
    expect(asked.answers[0].reason).toContain("unsaved level");
    r.closed(B.id);
    expect(r.get(asked.id)!.state).toBe("approved");
  });

  it("closes a session's open requests when it disconnects, so no one is told about them", () => {
    const r = registry();
    r.opened(A);
    r.opened(B);
    r.callStarted(B, "x.y");
    const asked = r.request(A, "stop", "editor.stop_editor");
    expect(asked.state).not.toBe("approved");
    r.closed(A.id);
    expect(r.get(asked.id)!.state).toBe("expired");
    expect(r.noticesFor(C.id)).toEqual([]);
  });

  it("expires a request after its lifetime, and a new ask opens a new one", () => {
    const c = clock();
    const r = registry(c);
    r.config = resolveDisruptionConfig({ requestTtlSeconds: 60 });
    r.opened(B);
    r.callStarted(B, "x.y");
    const first = r.request(A, "stop", "editor.stop_editor", { subagents: "none" });
    c.advance(61_000);
    expect(r.get(first.id)!.state).toBe("expired");
    expect(r.request(A, "stop", "editor.stop_editor").id).not.toBe(first.id);
  });

  it("tells every other session once, in its next tool response, and not the requester", () => {
    const r = registry();
    r.opened(B);
    r.opened(C);
    const asked = r.request(A, "rebuild", "editor.restart_editor", { reason: "new UCLASS" });
    const told = r.noticesFor(B.id);
    expect(told).toHaveLength(1);
    expect(told[0]).toContain(asked.id);
    expect(told[0]).toContain("disruption_vote");
    expect(r.noticesFor(B.id)).toEqual([]);
    expect(r.noticesFor(A.id)).toEqual([]);
    r.vote(C, asked.id, "go");
    expect(r.noticesFor(C.id)).toEqual([]);
  });

  it("tracks PIE a session started and transactions it opened from its calls", () => {
    const r = registry();
    let t = r.callStarted(B, "editor.play_in_editor");
    r.callFinished(B.id, t, "editor.play_in_editor", { pieAction: "start" }, true);
    t = r.callStarted(B, "editor.begin_editor_transaction");
    r.callFinished(B.id, t, "editor.begin_editor_transaction", {}, true);
    const b = r.activities().find((a) => a.id === B.id)!;
    expect(b).toMatchObject({ pie: true, transactions: 1 });
    r.pieEnded();
    t = r.callStarted(B, "editor.end_editor_transaction");
    r.callFinished(B.id, t, "editor.end_editor_transaction", {}, true);
    expect(r.activities().find((a) => a.id === B.id)).toMatchObject({ pie: false, transactions: 0 });
  });

  it("takes an external runtime's reports for a participant it drives", () => {
    const r = registry();
    r.report({ id: "studio-agent", label: "Studio" }, { busy: ["editing BP_Door"] });
    const view = r.request(A, "stop", "editor.stop_editor", { subagents: "none" });
    expect(view.answers[0]).toMatchObject({ session: "studio-agent", answer: "wait_until_released" });
    r.report({ id: "studio-agent", label: "Studio" }, { busy: [] });
    r.vote({ id: "studio-agent", label: "Studio" }, view.id, "go");
    expect(r.get(view.id)!.state).toBe("approved");
  });

  it("counts this process's own calls only while they run", () => {
    const r = registry();
    r.processId = "daemon";
    const daemon = { id: "daemon", label: "this server" };
    const t = r.callStarted(daemon, "editor.start_editor");
    const person = { id: "http:a person", label: "a person" };
    expect(r.request(person, "rebuild", "POST /v1/editor/rebuild", { person: true }).state).toBe("pending");
    r.callFinished("daemon", t, "editor.start_editor", {}, true);
    expect(r.request(person, "rebuild", "POST /v1/editor/rebuild", { person: true }).state).toBe("approved");
  });

  it("asks nobody with the mode off", () => {
    const r = registry();
    r.config = resolveDisruptionConfig({ mode: "off" });
    expect(askConsent(r, "stop", "editor.stop_editor", { caller: A })).toEqual({ proceed: true });
  });
});

describe("the gate", () => {
  it("knows which calls take the editor away", () => {
    expect(disruptiveCallOf("editor.stop_editor", {})).toEqual({ op: "stop" });
    expect(disruptiveCallOf("editor.restart_editor", {})).toEqual({ op: "restart" });
    expect(disruptiveCallOf("project.build", {})).toEqual({ op: "build", whileRunning: true });
    expect(disruptiveCallOf("project.live_coding_compile", {})).toEqual({ op: "live_coding" });
    expect(disruptiveCallOf("editor.execute_command", { command: "QUIT_EDITOR" })).toEqual({ op: "stop" });
    expect(disruptiveCallOf("editor.execute_command", { command: "stat fps" })).toBeNull();
    expect(disruptiveCallOf("editor.start_editor", {})).toBeNull();
    expect(disruptiveBridgeOp("hot_reload", {})).toBe("live_coding");
    expect(disruptiveBridgeOp("execute_command", { command: "exit" })).toBe("stop");
    expect(isQuitCommand("  quit")).toBe(true);
    expect(isQuitCommand("quitter")).toBe(false);
  });

  it("refuses with EDITOR_IN_USE and runs nothing while another session is busy", async () => {
    const r = registry();
    r.callStarted(B, "level.place_actor");
    let ran = false;
    const attempt = withDisruptionCaller(A, () =>
      withEditorConsent(r, "stop", "editor.stop_editor", async () => { ran = true; }, { subagents: "none" }));
    await expect(attempt).rejects.toSatisfy((e: unknown) => e instanceof McpError && e.code === ErrorCode.EDITOR_IN_USE);
    expect(ran).toBe(false);
  });

  it("runs an approved operation's own steps under its grant, so they do not ask again", async () => {
    const r = registry();
    const steps: string[] = [];
    await withDisruptionCaller(A, () =>
      withEditorConsent(r, "rebuild", "flow task editor.rebuild", async () => {
        // Each step would be a disruptive call of its own.
        await withEditorConsent(r, "stop", "editor.stop_editor", async () => { steps.push("stop"); });
        await withEditorConsent(r, "build", "project.build", async () => { steps.push("build"); });
      }, { subagents: "none" }));
    expect(steps).toEqual(["stop", "build"]);
    expect(r.list().map((v) => v.state)).toEqual(["done"]);
  });

  it("attributes a call made in process on a session's behalf to that session, and does not count the calls it runs inside", () => {
    const r = registry();
    const view = withDisruptionCaller(A, () => {
      const outer = r.callStarted(A, "flow.run");
      return withDisruptionCall(outer, () =>
        withDisruptionCaller(processCaller(), () => askConsent(r, "restart", "editor.restart_editor", { subagents: "paused" })));
    });
    // The flow call it runs inside does not hold it, and the request is A's.
    expect(view.proceed).toBe(true);
    expect(r.list()[0].requester.id).toBe(A.id);
  });
});
