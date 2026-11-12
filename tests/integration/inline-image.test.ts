/**
 * `inline` on an image-writing action, through the real server: the fake
 * editor writes a real PNG where it was asked to, and the client gets that
 * image back as MCP image content, scaled to the size it asked for, through
 * both the micro gateway and a full category tool. The editor never sees the
 * key, and a path this machine cannot read comes back as a reason.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { FakeBridge } from "../fake-bridge.js";
import { encodePng, pngSize } from "../../src/core/png.js";
import { json, McpServer, Sandbox } from "./harness.js";

let sb: Sandbox;
let bridge: FakeBridge;

/** A 300x150 opaque gradient, the size a small viewport capture might be. */
function capturePng(): Buffer {
  const width = 300, height = 150;
  const data = new Uint8Array(width * height * 4);
  for (let i = 0; i < width * height; i++) {
    data[i * 4] = i % 256; data[i * 4 + 1] = (i >> 8) % 256; data[i * 4 + 2] = 64; data[i * 4 + 3] = 255;
  }
  return encodePng({ width, height, data });
}

beforeAll(async () => {
  sb = new Sandbox();
  bridge = await FakeBridge.start({
    projectDir: sb.path("editor"),
    handlers: {
      capture_screenshot: (p) => {
        const file = String(p.filename);
        fs.mkdirSync(path.dirname(file), { recursive: true });
        fs.writeFileSync(file, capturePng());
        return { success: true, filename: file, target: "pie", width: 300, height: 150 };
      },
      render_material_preview: (p) => ({ success: true, outputPath: String(p.outputPath), width: 256, height: 256 }),
    },
  });
}, 60_000);

afterAll(async () => {
  await bridge?.stop();
  sb.cleanup();
});

describe("inline images", () => {
  it("returns a capture as image content, scaled, with the file left as written", async () => {
    covers("server.inline-image");
    const server = await McpServer.start(sb, { projects: [], port: bridge.port });
    try {
      const file = sb.path("shots", "pie.png");
      const r = await server.call("tools", {
        action: "call", category: "editor", method: "capture_screenshot",
        args: { filename: file, inline: 100 },
      });
      expect(r.isError, r.text).toBe(false);
      expect(r.media).toHaveLength(1);
      expect(r.media[0]).toMatchObject({ type: "image", mimeType: "image/png" });
      expect(pngSize(Buffer.from(r.media[0].data, "base64"))).toEqual({ width: 100, height: 50 });
      const body = json<{ filename: string; inlineImages: unknown; media: Array<Record<string, unknown>> }>(r);
      expect(body.filename).toBe(file);
      expect(body.inlineImages).toEqual({ inlined: 1, maxEdge: 100 });
      expect(body.media[0]).toMatchObject({ path: file, width: 100, height: 50, originalWidth: 300, originalHeight: 150, contentBlock: 1 });
      expect(body.media[0]).not.toHaveProperty("data");
      expect(pngSize(fs.readFileSync(file))).toEqual({ width: 300, height: 150 });

      const sent = bridge.calls.filter((c) => c.method === "capture_screenshot").at(-1)!;
      expect(sent.params).toEqual({ filename: file });

      // Without inline, the result is the path alone, as before.
      const plain = await server.call("tools", { action: "call", category: "editor", method: "capture_screenshot", args: { filename: file } });
      expect(plain.media).toEqual([]);
      expect(json(plain)).not.toHaveProperty("inlineImages");
    } finally {
      await server.close();
    }
  });

  it("takes inline on the full category tool, and reports a file this machine cannot read", async () => {
    covers("server.inline-image");
    const server = await McpServer.start(sb, { projects: [], port: bridge.port, strategy: "full" });
    try {
      const missing = sb.path("elsewhere", "preview.png");
      const r = await server.call("material", { action: "render_preview", assetPath: "/Game/M_Test", outputPath: missing, inline: true });
      expect(r.isError, r.text).toBe(false);
      expect(r.media).toEqual([]);
      const body = json<{ outputPath: string; inlineImages: { inlined: number; notInlined: Array<{ path: string; reason: string }> } }>(r);
      expect(path.resolve(body.outputPath)).toBe(path.resolve(missing));
      expect(body.inlineImages.inlined).toBe(0);
      expect(path.resolve(body.inlineImages.notInlined[0].path)).toBe(path.resolve(missing));
      expect(body.inlineImages.notInlined[0].reason).toContain("not found on this machine");
    } finally {
      await server.close();
    }
  });
});
