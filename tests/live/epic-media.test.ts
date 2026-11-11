// @live-editor render
/**
 * An engine tool whose output schema marks an FToolsetImage: the bridge moves
 * the image into media and leaves a urn in its slot, the shape the server turns
 * into MCP image content. Needs a renderer for the thumbnail.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

type Json = Record<string, unknown>;

let bridge: EditorBridge;

beforeAll(async () => {
  bridge = await getBridge();
}, 60_000);

afterAll(() => {
  if (bridge) disconnectBridge();
});

describe("epic_call_tool media", () => {
  it("returns CaptureAssetImage's thumbnail as media with a urn in its slot", async () => {
    const r = await callBridge(bridge, "epic_call_tool", {
      toolset: "EditorToolset.EditorAppToolset",
      tool: "CaptureAssetImage",
      input: { assetPath: "/Engine/BasicShapes/Cube" },
    });
    const answer = (r.result ?? {}) as Json;
    expect(r.ok && answer.success !== false, JSON.stringify(answer).slice(0, 2000)).toBe(true);

    const media = answer.media as Array<{ type: string; mimeType: string; data: string; uri: string; pointer: string }>;
    expect(media).toHaveLength(1);
    expect(media[0].type).toBe("image");
    expect(media[0].pointer).toBe("/returnValue");
    const bytes = Buffer.from(media[0].data, "base64");
    expect(bytes.length).toBeGreaterThan(100);
    if (media[0].mimeType === "image/png") expect(bytes.subarray(1, 4).toString("latin1")).toBe("PNG");
    else expect(media[0].mimeType).toMatch(/^image\//);

    const result = answer.result as Json;
    expect(result.returnValue).toBe(media[0].uri);
    expect(media[0].uri).toMatch(/^urn:toolresult:EditorToolset\.EditorAppToolset\.CaptureAssetImage:[0-9a-f-]+:\/returnValue$/);
    expect(JSON.stringify(result)).not.toContain(media[0].data.slice(0, 64));
  }, 120_000);
});
