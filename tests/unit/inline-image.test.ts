/**
 * `inline` on the image-writing actions: the server reads the file the editor
 * wrote, scales it to the caller's size, and returns it as image content. The
 * key never reaches the editor, the file on disk is untouched, and a file this
 * machine cannot read is reported rather than failing the capture.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { deflateSync } from "node:zlib";
import { afterAll, describe, expect, it } from "vitest";
import type { ToolContext } from "../../src/core/types.js";
import { decodePng, downscale, encodePng, jpegSize, pngSize, type RgbaImage } from "../../src/core/png.js";
import {
  attachInlineImages,
  inlineRequestOf,
  valuesAt,
  withInlineImage,
  DEFAULT_INLINE_EDGE,
  MAX_INLINE_IMAGES,
} from "../../src/dispatch/inline-image.js";
import { splitMedia } from "../../src/dispatch/server-dispatch.js";
import { bp, categoryTool } from "../../src/surface/category-tool.js";
import { actionSignature } from "../../src/surface/action-signature.js";
import { editorTool } from "../../src/tools/editor.js";
import { materialTool } from "../../src/tools/material.js";
import { assetTool } from "../../src/tools/asset.js";
import { recordingBridge } from "../fake-bridge.js";

const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-inline-"));
afterAll(() => fs.rmSync(dir, { recursive: true, force: true }));

/** A gradient with a transparent corner, so both colour and alpha are checked. */
function gradient(width: number, height: number): RgbaImage {
  const data = new Uint8Array(width * height * 4);
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const o = (y * width + x) * 4;
      data[o] = (x * 255) / Math.max(1, width - 1);
      data[o + 1] = (y * 255) / Math.max(1, height - 1);
      data[o + 2] = 128;
      data[o + 3] = x < width / 4 && y < height / 4 ? 0 : 255;
    }
  }
  return { width, height, data };
}

function writePng(name: string, image: RgbaImage): string {
  const file = path.join(dir, name);
  fs.writeFileSync(file, encodePng(image));
  return file;
}

const ctxFor = (bridge = recordingBridge()): ToolContext =>
  ({ bridge, project: { projectDir: dir } as unknown as ToolContext["project"] }) as ToolContext;

describe("png", () => {
  it("round-trips RGBA through encode and decode", () => {
    const image = gradient(37, 21);
    const decoded = decodePng(encodePng(image));
    expect(decoded.width).toBe(37);
    expect(decoded.height).toBe(21);
    expect(Buffer.from(decoded.data).equals(Buffer.from(image.data))).toBe(true);
  });

  it("writes RGB when every pixel is opaque, and decodes it back to RGBA", () => {
    const image = gradient(8, 8);
    image.data.forEach((_, i) => { if (i % 4 === 3) image.data[i] = 255; });
    const bytes = encodePng(image);
    expect(bytes[25]).toBe(2);
    expect(Buffer.from(decodePng(bytes).data).equals(Buffer.from(image.data))).toBe(true);
  });

  it("decodes a 1x1 written by another encoder", () => {
    const pixel = Buffer.from("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==", "base64");
    const decoded = decodePng(pixel);
    expect([decoded.width, decoded.height]).toEqual([1, 1]);
    expect(decoded.data.length).toBe(4);
  });

  it("decodes a 1-bit palette PNG with transparency", () => {
    const crc = (bytes: Buffer): number => {
      let c = 0xffffffff;
      for (const b of bytes) {
        c ^= b;
        for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
      }
      return (c ^ 0xffffffff) >>> 0;
    };
    const chunk = (type: string, body: Buffer): Buffer => {
      const len = Buffer.alloc(4); len.writeUInt32BE(body.length);
      const tail = Buffer.alloc(4); tail.writeUInt32BE(crc(Buffer.concat([Buffer.from(type, "latin1"), body])));
      return Buffer.concat([len, Buffer.from(type, "latin1"), body, tail]);
    };
    const ihdr = Buffer.alloc(13);
    ihdr.writeUInt32BE(3, 0); ihdr.writeUInt32BE(1, 4); ihdr[8] = 1; ihdr[9] = 3;
    const png = Buffer.concat([
      Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
      chunk("IHDR", ihdr),
      chunk("PLTE", Buffer.from([255, 0, 0, 0, 0, 255])),
      chunk("tRNS", Buffer.from([255, 0])),
      // One row, filter 0, pixels 1 0 1.
      chunk("IDAT", deflateSync(Buffer.from([0, 0b10100000]))),
      chunk("IEND", Buffer.alloc(0)),
    ]);
    expect(Array.from(decodePng(png).data)).toEqual([0, 0, 255, 0, 255, 0, 0, 255, 0, 0, 255, 0]);
  });

  it("downscales to the longer edge and keeps the aspect", () => {
    const small = downscale(gradient(400, 100), 100);
    expect([small.width, small.height]).toEqual([100, 25]);
    // A transparent corner stays transparent; an opaque pixel stays opaque.
    expect(small.data[3]).toBe(0);
    expect(small.data[(24 * 100 + 99) * 4 + 3]).toBe(255);
    expect(downscale(gradient(10, 10), 100).width).toBe(10);
  });

  it("refuses a truncated file with a reason", () => {
    const bytes = encodePng(gradient(16, 16));
    expect(() => decodePng(bytes.subarray(0, bytes.length - 20))).toThrow(/truncated/);
  });

  it("reads a JPEG's size from its frame header", () => {
    const sof = Buffer.from([0xff, 0xd8, 0xff, 0xc0, 0x00, 0x11, 0x08, 0x00, 0x30, 0x00, 0x40, 0x03, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
    expect(jpegSize(sof)).toEqual({ width: 64, height: 48 });
  });
});

describe("inline request", () => {
  it("reads true, a size, and nothing", () => {
    expect(inlineRequestOf(true)).toEqual({ maxEdge: DEFAULT_INLINE_EDGE });
    expect(inlineRequestOf(256)).toEqual({ maxEdge: 256 });
    expect(inlineRequestOf(false)).toBeUndefined();
    expect(inlineRequestOf(undefined)).toBeUndefined();
  });

  it("finds paths at dotted result paths, arrays traversed", () => {
    expect(valuesAt({ outcomes: [{ file: "a.png" }, { status: "blocked" }, { file: "b.png" }] }, "outcomes.file")).toEqual(["a.png", "b.png"]);
    expect(valuesAt({ files: ["x.png", "y.png"] }, "files")).toEqual(["x.png", "y.png"]);
  });
});

describe("attachInlineImages", () => {
  it("inlines a small PNG as written, and splitMedia turns it into image content", async () => {
    const file = writePng("small.png", gradient(32, 16));
    const answered = { success: true, path: file };
    const out = await attachInlineImages(answered, answered, ["path"], { maxEdge: 64 }) as Record<string, unknown>;
    expect(out.inlineImages).toEqual({ inlined: 1, maxEdge: 64 });
    const { data, blocks } = splitMedia(out);
    expect(blocks).toHaveLength(1);
    expect(blocks[0].mimeType).toBe("image/png");
    expect(Buffer.from(blocks[0].data, "base64").equals(fs.readFileSync(file))).toBe(true);
    expect((data as { media: Array<Record<string, unknown>> }).media[0]).toMatchObject({ path: file, width: 32, height: 16, contentBlock: 1 });
  });

  it("scales a large PNG down and leaves the file alone", async () => {
    const file = writePng("large.png", gradient(600, 300));
    const before = fs.readFileSync(file);
    const answered = { success: true, path: file };
    const out = await attachInlineImages(answered, answered, ["path"], { maxEdge: 100 }) as { media: Array<{ data: string; width: number; height: number; originalWidth: number }> };
    expect(out.media[0]).toMatchObject({ width: 100, height: 50, originalWidth: 600 });
    expect(pngSize(Buffer.from(out.media[0].data, "base64"))).toEqual({ width: 100, height: 50 });
    expect(fs.readFileSync(file).equals(before)).toBe(true);
  });

  it("reports a file that is not on this machine, naming a remote editor's host", async () => {
    const missing = path.join(dir, "nowhere.png");
    const answered = { success: true, filename: missing };
    const out = await attachInlineImages(answered, answered, ["filename"], { maxEdge: 64 }, { bridgeHost: "10.0.0.7", waitMs: 0 }) as Record<string, unknown>;
    expect(out.media).toBeUndefined();
    expect(out.inlineImages).toMatchObject({ inlined: 0, notInlined: [{ path: missing, reason: expect.stringContaining("the editor runs on 10.0.0.7") }] });
    const local = await attachInlineImages(answered, answered, ["filename"], { maxEdge: 64 }, { waitMs: 0 }) as Record<string, unknown>;
    expect(JSON.stringify(local.inlineImages)).toContain("not found on this machine");
  });

  it("waits for a file the renderer writes after the editor answered", async () => {
    const file = path.join(dir, "later.png");
    setTimeout(() => fs.writeFileSync(file, encodePng(gradient(4, 4))), 150);
    const answered = { success: true, filename: file };
    const out = await attachInlineImages(answered, answered, ["filename"], { maxEdge: 64 }, { waitMs: 3000 }) as Record<string, unknown>;
    expect(out.inlineImages).toEqual({ inlined: 1, maxEdge: 64 });
  });

  it("says a warm capture has no image yet, and leaves a failed call alone", async () => {
    const warm = { success: true, captureId: "c1", status: "warming", fileWritten: false, path: "x.png" };
    expect(((await attachInlineImages(warm, warm, ["path"], { maxEdge: 64 })) as Record<string, unknown>).inlineImages)
      .toMatchObject({ inlined: 0, note: expect.stringContaining("not written yet") });
    const failed = { success: false, error: "no" };
    expect(await attachInlineImages(failed, failed, ["path"], { maxEdge: 64 })).toEqual(failed);
  });

  it("inlines at most the first images of a sequence and lists the rest", async () => {
    const files = Array.from({ length: MAX_INLINE_IMAGES + 2 }, (_, i) => writePng(`seq_${i}.png`, gradient(4, 4)));
    const answered = { success: true, files };
    const out = await attachInlineImages(answered, answered, ["files"], { maxEdge: 64 }) as { media: unknown[]; inlineImages: { notInlined: unknown[] } };
    expect(out.media).toHaveLength(MAX_INLINE_IMAGES);
    expect(out.inlineImages.notInlined).toHaveLength(2);
  });

  it("refuses a non-image file with a reason", async () => {
    const file = path.join(dir, "notes.txt");
    fs.writeFileSync(file, "hello");
    const answered = { success: true, outputPath: file };
    const out = await attachInlineImages(answered, answered, ["outputPath"], { maxEdge: 64 }) as Record<string, unknown>;
    expect(JSON.stringify(out.inlineImages)).toContain("not a PNG or JPEG file");
  });
});

describe("dispatch", () => {
  const file = writePng("dispatch.png", gradient(200, 100));
  const demo = () => categoryTool("demo", "Demo", {
    shot: withInlineImage(bp("mutate", "Write an image. Params: filename", "write_image"), ["filename"]),
    plain: bp("mutate", "No image. Params: filename", "plain"),
  });

  it("consumes inline before the bridge, and adds the image after the projection", async () => {
    const bridge = recordingBridge({ success: true, filename: file, width: 200 });
    const out = await demo().handler!(ctxFor(bridge), { action: "shot", filename: file, inline: 50, select: ["width"] }) as Record<string, unknown>;
    expect(bridge.calls[0].params).toEqual({ filename: file });
    expect(out.width).toBe(200);
    expect(out.filename).toBeUndefined();
    expect(out.media).toEqual([expect.objectContaining({ type: "image", width: 50, height: 25, path: file })]);
  });

  it("does nothing without inline", async () => {
    const bridge = recordingBridge({ success: true, filename: file });
    const out = await demo().handler!(ctxFor(bridge), { action: "shot", filename: file }) as Record<string, unknown>;
    expect(out.media).toBeUndefined();
    expect(out.inlineImages).toBeUndefined();
  });

  it("advertises inline on every image-writing action's category and summary, leaving the editor's spec alone", () => {
    const cases: Array<[typeof editorTool, string]> = [
      [editorTool, "capture_screenshot"],
      [editorTool, "capture_scene_png"],
      [editorTool, "capture_screenshot_sequence"],
      [editorTool, "render_mesh_studio"],
      [editorTool, "render_sequence_frames"],
      [materialTool, "render_preview"],
      [assetTool, "export_texture"],
      [assetTool, "export_uv_layout"],
    ];
    for (const [tool, action] of cases) {
      const spec = tool.actions[action];
      expect(spec.imageOutputs?.length, `${tool.name}.${action}`).toBeGreaterThan(0);
      expect(spec.description, `${tool.name}.${action}`).toContain("inline=true");
      expect(spec.paramSpec?.map((p) => p.name) ?? [], `${tool.name}.${action}`).not.toContain("inline");
      expect(tool.schema.inline, tool.name).toBeDefined();
      expect(actionSignature(tool, action), `${tool.name}.${action}`).toMatch(/, inline\?:b\/i\)$/);
    }
    expect(actionSignature(editorTool, "get_viewport_state")).not.toContain("inline");
  });

  it("strips inline from the editor capture before it reaches the bridge", async () => {
    const bridge = recordingBridge({ success: true, filename: file, target: "pie" });
    const out = await editorTool.handler!(ctxFor(bridge), { action: "capture_screenshot", filename: file, inline: true }) as Record<string, unknown>;
    expect(bridge.calls[0]).toMatchObject({ method: "capture_screenshot", params: { filename: file } });
    expect(bridge.calls[0].params).not.toHaveProperty("inline");
    expect(out.inlineImages).toEqual({ inlined: 1, maxEdge: DEFAULT_INLINE_EDGE });
  });
});
