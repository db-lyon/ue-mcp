import { describe, it, expect, afterEach } from "vitest";
import * as net from "node:net";
import { proxyPortForProject, readProxyLockfile, relayCall, type RelayBridge } from "../../src/proxy.js";
import { isPortOpen } from "../../src/proxy-client.js";

describe("proxyPortForProject", () => {
  it("is deterministic for the same path", () => {
    const a = proxyPortForProject("C:/games/MyGame.uproject");
    const b = proxyPortForProject("C:/games/MyGame.uproject");
    expect(a).toBe(b);
  });

  it("lands in the private port range", () => {
    for (const p of ["C:/a.uproject", "/home/x/b.uproject", "D:/deep/nest/c.uproject"]) {
      const port = proxyPortForProject(p);
      expect(port).toBeGreaterThanOrEqual(49152);
      expect(port).toBeLessThan(65535);
    }
  });

  it("gives different projects different ports (usually)", () => {
    expect(proxyPortForProject("C:/one.uproject")).not.toBe(proxyPortForProject("C:/two.uproject"));
  });

  it("honours an explicit configured port", () => {
    expect(proxyPortForProject("C:/whatever.uproject", 9876)).toBe(9876);
  });

  it("ignores an out-of-range configured port and derives instead", () => {
    const derived = proxyPortForProject("C:/whatever.uproject");
    expect(proxyPortForProject("C:/whatever.uproject", 0)).toBe(derived);
    expect(proxyPortForProject("C:/whatever.uproject", 999999)).toBe(derived);
  });
});

describe("readProxyLockfile", () => {
  it("returns null for null or missing", () => {
    expect(readProxyLockfile(null)).toBeNull();
    expect(readProxyLockfile("C:/does/not/exist.uproject")).toBeNull();
  });
});

describe("isPortOpen", () => {
  let server: net.Server | null = null;
  afterEach(() => {
    server?.close();
    server = null;
  });

  it("resolves true for an open port and false for a closed one", async () => {
    const port: number = await new Promise((resolve) => {
      server = net.createServer();
      server.listen(0, "127.0.0.1", () => resolve((server!.address() as net.AddressInfo).port));
    });
    expect(await isPortOpen("127.0.0.1", port)).toBe(true);
    server!.close();
    server = null;
    // A port nothing is listening on.
    expect(await isPortOpen("127.0.0.1", 1, 300)).toBe(false);
  });
});

/** A fake bridge whose connection state and call behaviour we script. */
function fakeBridge(init: Partial<RelayBridge> & { connectAfter?: number } = {}): RelayBridge & { calls: string[]; probes: number } {
  let connected = init.isConnected ?? false;
  let probes = 0;
  const connectAfter = init.connectAfter ?? Infinity;
  const calls: string[] = [];
  return {
    calls,
    get probes() { return probes; },
    get isConnected() { return connected; },
    async ensureConnected() {
      probes++;
      if (probes >= connectAfter) connected = true;
      if (!connected) throw new Error("not connected");
    },
    async call(method: string) {
      calls.push(method);
      if (!connected) throw new Error("NOT_CONNECTED");
      return { ok: method };
    },
  };
}

describe("relayCall", () => {
  it("relays immediately when connected", async () => {
    const b = fakeBridge({ isConnected: true });
    const out = await relayCall(b, "level_get_outliner", {}, { everConnected: () => true });
    expect(out).toEqual({ ok: "level_get_outliner" });
    expect(b.probes).toBe(0); // no reconnect probing needed
  });

  it("fails fast when the editor has never connected", async () => {
    const b = fakeBridge({ isConnected: false });
    await expect(
      relayCall(b, "asset_list", {}, { everConnected: () => false, graceMs: 5000 }),
    ).rejects.toThrow("NOT_CONNECTED");
    // One quick probe, then straight to the (failing) call - no grace looping.
    expect(b.probes).toBe(1);
  });

  it("holds through a blip and succeeds once the editor returns", async () => {
    // Connects on the 3rd ensureConnected probe.
    const b = fakeBridge({ isConnected: false, connectAfter: 3 });
    const out = await relayCall(b, "blueprint_compile", {}, {
      everConnected: () => true,
      graceMs: 2000,
      probeMs: 10,
    });
    expect(out).toEqual({ ok: "blueprint_compile" });
    expect(b.probes).toBeGreaterThanOrEqual(3);
  });
});
