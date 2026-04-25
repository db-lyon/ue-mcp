import { describe, it, expect, beforeEach, afterEach, vi } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";

const CACHE_FILE = path.join(os.tmpdir(), "ue-mcp-version-check.json");

describe("version-check", () => {
  let stderr: ReturnType<typeof vi.spyOn>;
  const originalDisable = process.env.UE_MCP_DISABLE_UPDATE_CHECK;

  beforeEach(() => {
    stderr = vi.spyOn(console, "error").mockImplementation(() => {});
    try { fs.unlinkSync(CACHE_FILE); } catch { /* not present */ }
    delete process.env.UE_MCP_DISABLE_UPDATE_CHECK;
    vi.resetModules();
  });

  afterEach(() => {
    stderr.mockRestore();
    if (originalDisable === undefined) delete process.env.UE_MCP_DISABLE_UPDATE_CHECK;
    else process.env.UE_MCP_DISABLE_UPDATE_CHECK = originalDisable;
    vi.unstubAllGlobals();
    try { fs.unlinkSync(CACHE_FILE); } catch { /* not present */ }
  });

  describe("isNewer", () => {
    it("compares major/minor/patch", async () => {
      const { isNewer } = await import("../../src/version-check.js");
      expect(isNewer("1.0.1", "1.0.0")).toBe(true);
      expect(isNewer("1.1.0", "1.0.9")).toBe(true);
      expect(isNewer("2.0.0", "1.99.99")).toBe(true);
      expect(isNewer("1.0.0", "1.0.1")).toBe(false);
      expect(isNewer("1.0.0", "1.0.0")).toBe(false);
    });

    it("treats stable as newer than prerelease at same x.y.z", async () => {
      const { isNewer } = await import("../../src/version-check.js");
      expect(isNewer("1.0.0", "1.0.0-rc.6")).toBe(true);
      expect(isNewer("1.0.0-rc.6", "1.0.0")).toBe(false);
    });

    it("orders prereleases lexicographically", async () => {
      const { isNewer } = await import("../../src/version-check.js");
      expect(isNewer("1.0.0-rc.7", "1.0.0-rc.6")).toBe(true);
      expect(isNewer("1.0.0-rc.6", "1.0.0-rc.6")).toBe(false);
    });
  });

  describe("consumeUpgradeNotice", () => {
    it("returns null when nothing pending", async () => {
      const { consumeUpgradeNotice } = await import("../../src/version-check.js");
      expect(consumeUpgradeNotice()).toBeNull();
    });

    it("returns the notice once and then null", async () => {
      const { consumeUpgradeNotice, _setNoticeForTests } = await import("../../src/version-check.js");
      _setNoticeForTests("UPGRADE!");
      expect(consumeUpgradeNotice()).toBe("UPGRADE!");
      expect(consumeUpgradeNotice()).toBeNull();
    });
  });

  describe("startVersionCheck", () => {
    it("respects UE_MCP_DISABLE_UPDATE_CHECK=1", async () => {
      process.env.UE_MCP_DISABLE_UPDATE_CHECK = "1";
      const fetchSpy = vi.fn();
      vi.stubGlobal("fetch", fetchSpy);
      const { startVersionCheck, consumeUpgradeNotice } = await import("../../src/version-check.js");
      startVersionCheck("1.0.0-rc.6");
      await new Promise((r) => setImmediate(r));
      expect(fetchSpy).not.toHaveBeenCalled();
      expect(consumeUpgradeNotice()).toBeNull();
    });

    it("stashes a notice when registry reports a newer version", async () => {
      vi.stubGlobal("fetch", vi.fn(async () => ({
        ok: true,
        json: async () => ({ version: "1.0.0" }),
      })));
      const { startVersionCheck, consumeUpgradeNotice } = await import("../../src/version-check.js");
      startVersionCheck("1.0.0-rc.6");
      // Allow the background promise chain to settle.
      for (let i = 0; i < 5; i++) await new Promise((r) => setImmediate(r));
      const notice = consumeUpgradeNotice();
      expect(notice).not.toBeNull();
      expect(notice).toContain("UE_MCP_UPGRADE_AVAILABLE");
      expect(notice).toContain("1.0.0-rc.6");
      expect(notice).toContain("1.0.0");
    });

    it("does not stash a notice when current is up-to-date", async () => {
      vi.stubGlobal("fetch", vi.fn(async () => ({
        ok: true,
        json: async () => ({ version: "1.0.0" }),
      })));
      const { startVersionCheck, consumeUpgradeNotice } = await import("../../src/version-check.js");
      startVersionCheck("1.0.0");
      for (let i = 0; i < 5; i++) await new Promise((r) => setImmediate(r));
      expect(consumeUpgradeNotice()).toBeNull();
    });

    it("never throws on network failure", async () => {
      vi.stubGlobal("fetch", vi.fn(async () => { throw new Error("offline"); }));
      const { startVersionCheck, consumeUpgradeNotice } = await import("../../src/version-check.js");
      expect(() => startVersionCheck("1.0.0-rc.6")).not.toThrow();
      for (let i = 0; i < 5; i++) await new Promise((r) => setImmediate(r));
      expect(consumeUpgradeNotice()).toBeNull();
    });

    it("uses cache within TTL instead of refetching", async () => {
      fs.writeFileSync(CACHE_FILE, JSON.stringify({ checkedAt: Date.now(), latest: "1.0.0" }));
      const fetchSpy = vi.fn();
      vi.stubGlobal("fetch", fetchSpy);
      const { startVersionCheck, consumeUpgradeNotice } = await import("../../src/version-check.js");
      startVersionCheck("1.0.0-rc.6");
      for (let i = 0; i < 5; i++) await new Promise((r) => setImmediate(r));
      expect(fetchSpy).not.toHaveBeenCalled();
      expect(consumeUpgradeNotice()).toContain("1.0.0");
    });
  });
});
