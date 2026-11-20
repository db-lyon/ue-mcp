/**
 * An agent turn interrupted mid-call through a daemon extension (#1902): the
 * call in flight answers CANCELLED at once and the editor is asked to cancel
 * it, nothing the interrupted turn sends after that reaches the editor, and
 * the next turn runs as if nothing happened.
 *
 * The extension stands in for an agent: each turn has its own AbortController,
 * every call it makes passes that turn's signal to api.callAction, and
 * interrupting the turn aborts it.
 */
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeBridge } from "../fake-bridge.js";
import { Sandbox, stopSandboxDaemons, waitFor } from "./harness.js";
import { call, startDaemonCli, writeExtension, type Discovery } from "./daemon-fakes.js";

const TURNS = `
  const turns = new Map();
  const turn = (id) => {
    let c = turns.get(id);
    if (!c) turns.set(id, (c = new AbortController()));
    return c;
  };
  api.route("GET", "/features", () => ({ body: { features: api.features } }));
  api.route("POST", "/call", async (req) => {
    const b = req.body;
    const started = Date.now();
    const r = await api.callAction(b.category, b.action, b.args ?? {}, { signal: turn(b.turn).signal });
    return { body: { isError: r.isError, text: r.content.map((c) => c.text ?? "").join("\\n"), ms: Date.now() - started } };
  });
  api.route("POST", "/interrupt/:turn", (req) => {
    turn(req.params.turn).abort();
    return { body: { interrupted: req.params.turn } };
  });
`;

interface CallAnswer {
  isError: boolean;
  text: string;
  ms: number;
}

let sb: Sandbox;
let bridge: FakeBridge;
let d: Discovery;
/** Set while the editor holds its outliner answer, as a long editor call does. */
let hold: { release: () => void } | null = null;
let holding = false;

beforeAll(async () => {
  sb = new Sandbox();
  const project = sb.makeProject("Turns");
  bridge = await FakeBridge.start({
    projectDir: path.dirname(project),
    handlers: {
      list_dialogs: () => ({ success: true, dialogs: [] }),
      get_world_outliner: () => {
        const answer = { success: true, actors: [{ label: "Floor", class: "StaticMeshActor" }], count: 1 };
        if (!holding) return answer;
        return new Promise((resolve) => (hold = { release: () => resolve(answer) }));
      },
      place_actor: (p) => ({ success: true, actorLabel: String(p.label ?? "Cube") }),
    },
  });
  writeExtension(path.join(sb.home, ".ue-mcp", "extensions"), { name: "turns", version: "1.0.0", activate: TURNS });
  d = await startDaemonCli(sb, project, { UE_MCP_PORT: undefined });
  await waitFor(async () => (await call(d, "GET", "/v1/health")).body.editors[0]?.connected === true, 15_000, "the daemon to reach the fake editor");
}, 60_000);

afterAll(async () => {
  hold?.release();
  await stopSandboxDaemons(sb);
  await bridge?.stop();
  sb.cleanup();
});

const turnCall = async (turn: string, category: string, action: string, args: Record<string, unknown> = {}): Promise<CallAnswer> => {
  const r = await call(d, "POST", "/v1/ext/turns/call", { turn, category, action, args });
  expect(r.status, JSON.stringify(r.body)).toBe(200);
  return r.body as CallAnswer;
};

describe("an interrupted turn through a daemon extension", () => {
  it("offers callAction.signal to extensions", async () => {
    covers("daemon.extension-host");
    const r = await call(d, "GET", "/v1/ext/turns/features");
    expect(r.body.features).toContain("callAction.signal");
  });

  it("answers the call in flight as cancelled at once, sends nothing more, and leaves the next turn clean", async () => {
    covers("daemon.extension-host");
    holding = true;
    const inFlight = turnCall("one", "level", "get_outliner");
    await waitFor(() => hold !== null, 15_000, "the editor to receive the outliner call");

    const interrupted = await call(d, "POST", "/v1/ext/turns/interrupt/one");
    expect(interrupted.body).toEqual({ interrupted: "one" });

    // Answered without waiting for the editor, which still holds the call.
    const cancelled = await inFlight;
    expect(cancelled.isError).toBe(true);
    expect(cancelled.text).toMatch(/CANCELLED/);
    expect(cancelled.text).toMatch(/level\(get_outliner\) was cancelled by its caller while it ran/);
    // The editor is asked to cancel the request it is running.
    await waitFor(() => bridge.methods.includes("cancel_request"), 10_000, "the cancel request to reach the editor");

    // The interrupted turn's next call is refused before dispatch: the editor never sees it.
    const after = await turnCall("one", "level", "place_actor", { label: "LateCube" });
    expect(after.isError).toBe(true);
    expect(after.text).toMatch(/level\(place_actor\) was not run: its caller cancelled it first/);
    expect(bridge.methods).not.toContain("place_actor");

    // The editor answers late: the answer is dropped and nobody is left waiting on it.
    holding = false;
    hold!.release();

    // The next turn runs on its own signal, untouched by the interrupt.
    const next = await turnCall("two", "level", "get_outliner");
    expect(next.isError, next.text).toBe(false);
    expect(next.text).toContain("Floor");
    const placed = await turnCall("two", "level", "place_actor", { label: "NextCube" });
    expect(placed.isError, placed.text).toBe(false);
    expect(bridge.calls.filter((c) => c.method === "place_actor").map((c) => c.params.label)).toEqual(["NextCube"]);
  });
});
