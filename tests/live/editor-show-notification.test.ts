// @live-editor render
/**
 * editor(show_notification): the toast is read back from the notification
 * windows, and the parameters it refuses are refused before anything is shown.
 * Refs db-lyon/ue-mcp-studio#43.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

beforeAll(async () => {
  bridge = await getBridge();
}, 60_000);

afterAll(() => {
  if (bridge) disconnectBridge();
});

describe("editor(show_notification)", () => {
  it("shows a warning toast with a link and reads it back on screen", async () => {
    const text = `UE-MCP live toast ${Date.now()}`;
    const r = await callBridge(bridge, "show_notification", {
      text,
      subText: "Playtest: live notification",
      durationS: 2,
      severity: "warning",
      hyperlinkText: "Open docs",
      hyperlinkUrl: "https://example.com/",
    });
    expect(succeeded(r), r.error ?? JSON.stringify(r.result)).toBe(true);
    const shown = body(r);
    expect(shown.text).toBe(text);
    expect(shown.subText).toBe("Playtest: live notification");
    expect(shown.severity).toBe("warning");
    expect(shown.durationS).toBe(2);
    expect(shown.hyperlinkText).toBe("Open docs");
    expect(shown.hyperlinkUrl).toBe("https://example.com/");
    expect(shown.onScreen).toBe(true);
    expect(shown.notificationWindows as number).toBeGreaterThan(0);
  }, 60_000);

  it("defaults to an info toast and refuses a link without a URL or a non-http URL", async () => {
    const plain = await callBridge(bridge, "show_notification", { text: `UE-MCP info toast ${Date.now()}`, durationS: 1 });
    expect(succeeded(plain), plain.error ?? JSON.stringify(plain.result)).toBe(true);
    expect(body(plain).severity).toBe("info");
    expect(body(plain).onScreen).toBe(true);
    expect(body(plain).hyperlinkUrl).toBeUndefined();

    const noUrl = await callBridge(bridge, "show_notification", { text: "no url", hyperlinkText: "Open" });
    expect(succeeded(noUrl)).toBe(false);
    expect(JSON.stringify(noUrl.result ?? noUrl.error)).toContain("hyperlinkUrl");

    const fileUrl = await callBridge(bridge, "show_notification", { text: "file url", hyperlinkUrl: "file:///C:/Windows" });
    expect(succeeded(fileUrl)).toBe(false);
    expect(JSON.stringify(fileUrl.result ?? fileUrl.error)).toContain("http or https");
  }, 60_000);
});
