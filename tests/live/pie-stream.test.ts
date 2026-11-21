// @live-editor render
/**
 * pie(stream_start | stream_status | stream_stop) against a local signalling
 * stub that speaks the streamer side of Pixel Streaming 2's protocol: it sends
 * config and identify when the streamer connects, confirms its endpointId, then
 * announces a player and waits for the SDP offer the streamer answers with.
 * The offer carrying a video section is the proof that the editor's streamer
 * reached the signalling server the caller named and began a WebRTC session.
 *
 * A PIE stream is connected only while PIE runs, and a client tells a waiting
 * stream from a live one by that connection, so one PIE stop and start cycle
 * is asserted as a disconnect and a reconnect.
 *
 * Needs a rendering editor: Pixel Streaming 2 does not start under -nullrhi.
 * Refs #2052.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { WebSocketServer, type WebSocket } from "ws";
import type { AddressInfo } from "node:net";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

/** A path segment shaped like the secret a real signalling server puts in its URL. */
const SECRET_PATH = "/streamer/Xy9_kQ-2fA~b.c%2Fd";

/** The streamer side of a Pixel Streaming 2 signalling server, recording what each streamer sent. */
class SignallingStub {
  readonly server: WebSocketServer;
  readonly paths: string[] = [];
  readonly endpointIds: string[] = [];
  readonly offers: { playerId: string; sdp: string }[] = [];
  readonly sockets = new Set<WebSocket>();
  private nextPlayer = 100;

  constructor() {
    this.server = new WebSocketServer({ host: "127.0.0.1", port: 0 });
    this.server.on("connection", (socket, req) => {
      this.paths.push(req.url ?? "");
      this.sockets.add(socket);
      socket.on("close", () => this.sockets.delete(socket));
      socket.send(JSON.stringify({ type: "config", peerConnectionOptions: { iceServers: [] } }));
      socket.send(JSON.stringify({ type: "identify" }));
      socket.on("message", (data) => {
        let msg: Json;
        try {
          msg = JSON.parse(String(data)) as Json;
        } catch {
          return;
        }
        if (msg.type === "endpointId") {
          this.endpointIds.push(String(msg.id));
          socket.send(JSON.stringify({ type: "endpointIdConfirm", committedId: msg.id }));
          const playerId = String(this.nextPlayer++);
          socket.send(JSON.stringify({ type: "playerConnected", playerId, dataChannel: true, sfu: false, sendOffer: true }));
        } else if (msg.type === "offer") {
          this.offers.push({ playerId: String(msg.playerId), sdp: String(msg.sdp ?? "") });
        } else if (msg.type === "ping") {
          socket.send(JSON.stringify({ type: "pong", time: msg.time }));
        }
      });
    });
  }

  async ready(): Promise<string> {
    if (!this.server.address()) await new Promise<void>((r) => this.server.once("listening", () => r()));
    return `ws://127.0.0.1:${(this.server.address() as AddressInfo).port}`;
  }

  async close(): Promise<void> {
    for (const s of this.sockets) s.terminate();
    await new Promise<void>((r) => this.server.close(() => r()));
  }
}

let bridge: EditorBridge;
let stub: SignallingStub;
let url = "";

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function pieRunning(): Promise<boolean> {
  return body(await callBridge(bridge, "pie_control", { pieAction: "status" })).isPlaying === true;
}

async function waitFor(check: () => boolean | Promise<boolean>, ms: number): Promise<boolean> {
  const end = Date.now() + ms;
  while (Date.now() < end) {
    if (await check()) return true;
    await sleep(250);
  }
  return false;
}

async function startPie(): Promise<void> {
  await call("pie_control", { pieAction: "start" });
  expect(await waitFor(pieRunning, 60_000), "PIE did not start").toBe(true);
}

async function stopPie(): Promise<void> {
  if (await pieRunning()) {
    await callBridge(bridge, "pie_control", { pieAction: "stop" });
    await waitFor(async () => !(await pieRunning()), 30_000);
  }
}

beforeAll(async () => {
  bridge = await getBridge();
  stub = new SignallingStub();
  url = (await stub.ready()) + SECRET_PATH;
  await callBridge(bridge, "pie_stream_stop", {});
  await stopPie();
}, 120_000);

afterAll(async () => {
  try {
    if (bridge) {
      await callBridge(bridge, "pie_stream_stop", {});
      await stopPie();
    }
  } finally {
    await stub?.close();
    disconnectBridge();
  }
}, 120_000);

describe("pie stream", () => {
  it("stream_status reports Pixel Streaming 2 enabled and nothing streaming", async () => {
    const status = await call("pie_stream_status", {});
    expect(status).toMatchObject({ pluginEnabled: true, available: true, streaming: false, armed: false, connectedPeers: 0 });
  });

  it("stream_start refuses a URL that is not ws:// or wss://", async () => {
    const r = await callBridge(bridge, "pie_stream_start", { signallingUrl: "http://127.0.0.1:1" });
    expect(succeeded(r)).toBe(false);
    expect(JSON.stringify(r.error ?? r.result)).toContain("ws://");
  });

  it("stream_start with no PIE arms without connecting", async () => {
    const started = await call("pie_stream_start", { signallingUrl: url, streamerId: "studio", fps: 24, maxBitrateKbps: 2000, codec: "VP8" });
    expect(started).toMatchObject({ streaming: false, armed: true, streamerId: "studio", signallingUrl: url, source: "pie", codec: "VP8", fps: 24 });
    expect((started.rollback as Json | undefined)?.method).toBe("pie_stream_stop");
    await sleep(3000);
    expect(stub.paths.length, "an armed stream connected before PIE started").toBe(0);
    expect(await call("pie_stream_status", {})).toMatchObject({ streaming: false, armed: true, streamerId: "studio", codec: "VP8" });
  }, 60_000);

  it("PIE start connects the streamer to the exact URL, and it answers a player with an SDP offer carrying video", async () => {
    await startPie();
    expect(await waitFor(() => stub.endpointIds.includes("studio"), 45_000), "streamer never sent endpointId").toBe(true);
    expect(stub.paths[0]).toBe(SECRET_PATH);
    expect(await waitFor(() => stub.offers.length > 0, 45_000), "no SDP offer reached the signalling stub").toBe(true);
    expect(stub.offers[0].sdp.startsWith("v=0")).toBe(true);
    expect(stub.offers[0].sdp).toContain("m=video");
    expect(await call("pie_stream_status", {})).toMatchObject({ streaming: true, armed: false, signallingConnected: true });
  }, 120_000);

  it("PIE stop disconnects the streamer and the next PIE start connects it again", async () => {
    await stopPie();
    expect(await waitFor(() => stub.sockets.size === 0, 30_000), "streamer stayed connected after PIE stopped").toBe(true);
    expect(await call("pie_stream_status", {})).toMatchObject({ streaming: false, armed: true, streamerId: "studio" });

    const connections = stub.paths.length;
    const offers = stub.offers.length;
    await startPie();
    expect(await waitFor(() => stub.paths.length > connections && stub.sockets.size === 1, 45_000), "streamer did not reconnect on PIE start").toBe(true);
    expect(stub.paths.at(-1)).toBe(SECRET_PATH);
    expect(await waitFor(() => stub.offers.length > offers, 45_000), "no SDP offer after reconnecting").toBe(true);
  }, 150_000);

  it("stream_stop disconnects the streamer and a second stop is a no-op", async () => {
    const stopped = await call("pie_stream_stop", {});
    expect(stopped).toMatchObject({ streaming: false, armed: false, stopped: true, alreadyStopped: false });
    expect(await waitFor(() => stub.sockets.size === 0, 30_000), "streamer stayed connected after stream_stop").toBe(true);
    expect(await call("pie_stream_stop", {})).toMatchObject({ stopped: false, alreadyStopped: true });
    expect(await call("pie_stream_status", {})).toMatchObject({ streaming: false, armed: false, streamerId: null });
  }, 60_000);

  it("stream_start while PIE runs connects at once instead of arming", async () => {
    expect(await pieRunning()).toBe(true);
    const offers = stub.offers.length;
    const started = await call("pie_stream_start", { signallingUrl: url, streamerId: "studio" });
    expect(started).toMatchObject({ streaming: true, armed: false, codec: "H264", fps: 30, source: "pie" });
    expect(await waitFor(() => stub.offers.length > offers, 45_000), "no SDP offer after starting during PIE").toBe(true);
    await call("pie_stream_stop", {});
  }, 90_000);
});
