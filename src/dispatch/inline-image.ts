/**
 * `inline` on the actions that write an image: the server reads the file the
 * editor wrote, shrinks it to the caller's size, and returns it as MCP image
 * content beside the result, so an agent can look at a capture without a
 * second tool to open the file. The file is written as before.
 *
 * The editor reports where it wrote, and this process reads that path. When
 * the editor runs on another machine the path names a file there, and the
 * result says it could not be read here instead of failing the capture.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { z } from "zod";
import type { ActionSpec } from "../core/types.js";
import { decodePng, downscale, encodePng, isPng, jpegSize, pngSize } from "../core/png.js";

export const INLINE_PARAM = "inline";

/** Longest edge, in pixels, of an image inlined with `inline: true`. */
export const DEFAULT_INLINE_EDGE = 1024;
export const MIN_INLINE_EDGE = 16;
export const MAX_INLINE_EDGE = 4096;
/** Images one call inlines at most; the rest are listed as not inlined. */
export const MAX_INLINE_IMAGES = 8;
/** Encoded bytes one inlined image may take; a larger result is halved again. */
export const MAX_INLINE_BYTES = 3_500_000;

const DESCRIPTION =
  "Also return the written image as image content: true for at most 1024 px on the longer edge, "
  + "or a number for that edge in pixels (16-4096). Larger images are scaled down; the file on disk is unchanged";

/** The category schema entry, for every category that has an image-writing action. */
export const inlineSchema: Record<string, z.ZodType> = {
  [INLINE_PARAM]: z
    .union([z.boolean(), z.number().int().min(MIN_INLINE_EDGE).max(MAX_INLINE_EDGE)])
    .optional()
    .describe(`Image-writing actions (capture_screenshot, capture_scene_png, render_preview and the like): ${DESCRIPTION}`),
};

export interface InlineRequest {
  maxEdge: number;
}

/** The request `inline` makes, or undefined when it asks for nothing. */
export function inlineRequestOf(value: unknown): InlineRequest | undefined {
  if (value === undefined || value === null || value === false) return undefined;
  if (value === true) return { maxEdge: DEFAULT_INLINE_EDGE };
  if (typeof value === "number" && Number.isFinite(value)) {
    return { maxEdge: Math.min(MAX_INLINE_EDGE, Math.max(MIN_INLINE_EDGE, Math.round(value))) };
  }
  return undefined;
}

/**
 * Mark an action as writing images at these result paths (dotted, arrays
 * traversed: `files`, `outcomes.file`), which makes it take `inline`.
 *
 * `inline` is a routing parameter like `select`: the call pipeline consumes
 * it before the action's own contract is checked, so the editor's recorded
 * spec and its Params clause stay exactly what the editor declares. The
 * category schema advertises it (inlineSchema), and the action's summary
 * says it takes it.
 */
export function withInlineImage<T extends ActionSpec>(spec: T, outputs: readonly string[]): T {
  return { ...spec, imageOutputs: outputs };
}

/** Every string a dotted path reaches in a value, arrays traversed. */
export function valuesAt(value: unknown, dotted: string): string[] {
  const keys = dotted.split(".");
  let level: unknown[] = [value];
  for (const key of keys) {
    const next: unknown[] = [];
    for (const v of level) {
      const items = Array.isArray(v) ? v : [v];
      for (const item of items) {
        if (item && typeof item === "object" && !Array.isArray(item) && key in item) {
          next.push((item as Record<string, unknown>)[key]);
        }
      }
    }
    level = next;
  }
  return level.flatMap((v) => (Array.isArray(v) ? v : [v])).filter((v): v is string => typeof v === "string" && v !== "");
}

/** One image as a result's `media` entry, which dispatch turns into MCP image content. */
export interface InlinedImage {
  type: "image";
  mimeType: string;
  data: string;
  path: string;
  width?: number;
  height?: number;
  originalWidth?: number;
  originalHeight?: number;
}

export interface InlineReport {
  inlined: number;
  maxEdge: number;
  notInlined?: Array<{ path: string; reason: string }>;
}

export interface InlineOptions {
  /** Resolves a relative path the editor reported. */
  projectDir?: string | null;
  /** The bridge host, named when a file is not on this machine. */
  bridgeHost?: string;
  /** How long to wait for a file the editor writes after answering. */
  waitMs?: number;
}

const LOOPBACK = new Set(["127.0.0.1", "localhost", "::1", "[::1]", "0.0.0.0"]);

function notHereReason(file: string, host: string | undefined): string {
  const remote = host && !LOOPBACK.has(host.toLowerCase());
  return remote
    ? `The file is not on this machine: the editor runs on ${host}, so ${file} is a path there. Read it on that machine, or run the server beside the editor.`
    : `The file was not found on this machine (${file}). If the editor runs elsewhere, the path is on that machine.`;
}

async function waitForFile(file: string, waitMs: number): Promise<boolean> {
  const deadline = Date.now() + waitMs;
  for (;;) {
    if (fs.existsSync(file)) return true;
    if (Date.now() >= deadline) return false;
    await new Promise((r) => setTimeout(r, 100));
  }
}

/**
 * Shrink one image to the request: the sizing every inlined image shares,
 * captures and rendered diagrams alike. Throws with the reason on anything
 * unreadable.
 */
export function encodeForInline(bytes: Buffer, request: InlineRequest, file: string): InlinedImage {
  if (isPng(bytes)) {
    const size = pngSize(bytes);
    if (size && Math.max(size.width, size.height) <= request.maxEdge && bytes.length <= MAX_INLINE_BYTES) {
      return { type: "image", mimeType: "image/png", data: bytes.toString("base64"), path: file, ...size };
    }
    const decoded = decodePng(bytes);
    let edge = request.maxEdge;
    let encoded = encodePng(downscale(decoded, edge));
    let shown = pngSize(encoded)!;
    while (encoded.length > MAX_INLINE_BYTES && edge > MIN_INLINE_EDGE) {
      edge = Math.max(MIN_INLINE_EDGE, Math.floor(edge / 2));
      encoded = encodePng(downscale(decoded, edge));
      shown = pngSize(encoded)!;
    }
    return {
      type: "image",
      mimeType: "image/png",
      data: encoded.toString("base64"),
      path: file,
      width: shown.width,
      height: shown.height,
      originalWidth: decoded.width,
      originalHeight: decoded.height,
    };
  }
  const jpeg = jpegSize(bytes);
  if (jpeg) {
    // No JPEG decoder ships with the server, so a JPEG is passed through whole.
    if (Math.max(jpeg.width, jpeg.height) > request.maxEdge) {
      throw new Error(`a ${jpeg.width}x${jpeg.height} JPEG is larger than ${request.maxEdge} px and only PNG can be scaled down here; write a .png instead`);
    }
    if (bytes.length > MAX_INLINE_BYTES) throw new Error(`the JPEG is ${bytes.length} bytes, over the ${MAX_INLINE_BYTES} byte limit`);
    return { type: "image", mimeType: "image/jpeg", data: bytes.toString("base64"), path: file, ...jpeg };
  }
  throw new Error("not a PNG or JPEG file");
}

/**
 * Read the images a result names at `outputs` and add them to it as `media`,
 * with an `inlineImages` report. Nothing here fails the call: a file that
 * cannot be read is listed under notInlined with the reason.
 */
export async function attachInlineImages(
  result: unknown,
  answered: unknown,
  outputs: readonly string[],
  request: InlineRequest,
  opts: InlineOptions = {},
): Promise<unknown> {
  if (result === null || typeof result !== "object" || Array.isArray(result)) return result;
  const record = result as Record<string, unknown>;
  const body = answered !== null && typeof answered === "object" ? (answered as Record<string, unknown>) : record;
  const report: InlineReport = { inlined: 0, maxEdge: request.maxEdge };
  const notInlined: Array<{ path: string; reason: string }> = [];
  const media: InlinedImage[] = [];

  if (body.success === false) return result;
  // A warm capture_scene_png answers before its file exists.
  if (body.fileWritten === false || body.status === "warming") {
    return {
      ...record,
      inlineImages: { ...report, note: "The image is not written yet. Query again with the same inline value once it is complete." },
    };
  }

  const files = [...new Set(outputs.flatMap((o) => valuesAt(body, o)))];
  if (files.length === 0) {
    return { ...record, inlineImages: { ...report, note: "The result names no image file to inline." } };
  }
  const waitMs = opts.waitMs ?? 3000;
  for (const [index, reported] of files.entries()) {
    const file = path.isAbsolute(reported) || !opts.projectDir ? reported : path.resolve(opts.projectDir, reported);
    if (index >= MAX_INLINE_IMAGES) {
      notInlined.push({ path: file, reason: `Only the first ${MAX_INLINE_IMAGES} images are inlined.` });
      continue;
    }
    // A viewport screenshot is written by the renderer after the editor answers.
    if (!(await waitForFile(file, waitMs))) {
      notInlined.push({ path: file, reason: notHereReason(file, opts.bridgeHost) });
      continue;
    }
    let lastError = "";
    const deadline = Date.now() + waitMs;
    for (;;) {
      try {
        media.push(encodeForInline(fs.readFileSync(file), request, file));
        lastError = "";
        break;
      } catch (e) {
        lastError = e instanceof Error ? e.message : String(e);
        // A file still being written reads as truncated; give the writer a moment.
        if (!/truncated/.test(lastError) || Date.now() >= deadline) break;
        await new Promise((r) => setTimeout(r, 100));
      }
    }
    if (lastError) notInlined.push({ path: file, reason: `Could not inline the image: ${lastError}.` });
  }
  report.inlined = media.length;
  if (notInlined.length) report.notInlined = notInlined;
  const existing = Array.isArray(record.media) ? record.media : [];
  return { ...record, ...(media.length ? { media: [...existing, ...media] } : {}), inlineImages: report };
}
