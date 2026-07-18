// Regression: #731 — malformed WebSocket frames for JSON-RPC responses >= 64 KiB.
// CreateWebSocketFrame encoded the 8-byte extended payload length from an int32,
// so shifts of 32-56 bits were undefined and the frame length was corrupt. The
// client saw a bogus size and closed the socket for any response >= 65536 bytes.
import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge } from "../setup.js";
import type { EditorBridge } from "../../src/bridge.js";

let bridge: EditorBridge;
beforeAll(async () => { bridge = await getBridge(); });
afterAll(() => disconnectBridge());

describe("bridge — large payload framing (#731)", () => {
  it("execute_python output >= 64 KiB round-trips without dropping the bridge", async () => {
    const r = await callBridge(bridge, "execute_python", { code: `print("x" * 70000)` });
    expect(r.ok, r.error).toBe(true);
    expect(JSON.stringify(r.result)).toContain("xxxx");
  });

  it("bridge stays connected after a large response", async () => {
    const still = await callBridge(bridge, "get_build_status");
    expect(still.ok, still.error).toBe(true);
  });
});
