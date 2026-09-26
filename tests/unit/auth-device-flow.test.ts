import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { pollDeviceFlow, readUserAuth, type PendingDeviceFlow } from "../../src/feedback/github-auth.js";

let dir: string;
const pending = (expiresInS: number): PendingDeviceFlow => ({
  device_code: "dc",
  user_code: "UC-1",
  verification_uri: "https://github.com/login/device",
  expires_at: Math.floor(Date.now() / 1000) + expiresInS,
  interval: 0,
});
const json = (body: unknown) => new Response(JSON.stringify(body), { status: 200 });

beforeEach(() => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-auth-"));
  vi.stubEnv("UE_MCP_AUTH_DIR", dir);
});

afterEach(() => {
  vi.unstubAllEnvs();
  vi.unstubAllGlobals();
  fs.rmSync(dir, { recursive: true, force: true });
});

describe("pollDeviceFlow", () => {
  it("keeps polling while GitHub says pending, then caches the authorization", async () => {
    const fetchMock = vi.fn()
      .mockResolvedValueOnce(json({ error: "authorization_pending" }))
      .mockResolvedValueOnce(json({ access_token: "gho_x" }))
      .mockResolvedValueOnce(json({ login: "octo" }));
    vi.stubGlobal("fetch", fetchMock);
    const ticks = vi.fn();

    const result = await pollDeviceFlow(pending(60), ticks);

    expect(result.kind).toBe("auth");
    expect(ticks).toHaveBeenCalledTimes(1);
    expect((await readUserAuth())?.login).toBe("octo");
  });

  it("stops on a denial", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue(json({ error: "access_denied" })));
    expect((await pollDeviceFlow(pending(60))).kind).toBe("denied");
  });

  it("reports a timeout once the code has expired", async () => {
    vi.stubGlobal("fetch", vi.fn());
    expect((await pollDeviceFlow(pending(-1))).kind).toBe("timeout");
  });
});
