/**
 * SVG to PNG, through the same rasteriser the overview image is rendered with
 * (scripts/render-overview-image.ts): resvg, no browser.
 *
 * @resvg/resvg-js is a native package and stays out of the runtime
 * dependencies, so it is loaded on demand. Where it is installed (a checkout
 * of this repository, or a user who added it) a PNG comes back; where it is
 * not, the caller gets the reason and keeps the SVG.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { packageRoot } from "../core/package-root.js";

/** Longest edge of a rendered PNG, in pixels. */
export const PNG_MAX_EDGE = 4096;

export type PngResult = { ok: true; png: Buffer; width: number; height: number } | { ok: false; reason: string };

type ResvgModule = typeof import("@resvg/resvg-js");

let loaded: Promise<ResvgModule | null> | undefined;

/** Swapped by tests to exercise the missing-package path. */
export function setResvgLoader(loader: (() => Promise<ResvgModule | null>) | undefined): void {
  loaded = loader ? loader() : undefined;
}

function loadResvg(): Promise<ResvgModule | null> {
  loaded ??= import("@resvg/resvg-js").then((m) => m as ResvgModule, () => null);
  return loaded;
}

/** The fonts committed beside the overview template, when this is a checkout that has them. */
function fontFiles(): string[] {
  const dir = path.join(packageRoot(), "scripts", "assets", "overview-image", "fonts");
  try {
    return fs.readdirSync(dir).filter((f) => f.endsWith(".ttf")).map((f) => path.join(dir, f));
  } catch {
    return [];
  }
}

/** Rasterise at up to 2x, keeping the longest edge within PNG_MAX_EDGE. */
export async function svgToPng(svg: string, width: number, height: number): Promise<PngResult> {
  const resvg = await loadResvg();
  if (!resvg) {
    return { ok: false, reason: "PNG needs the optional @resvg/resvg-js package, which is not installed here (npm install @resvg/resvg-js beside ue-mcp). The SVG was written instead." };
  }
  const zoom = Math.min(2, PNG_MAX_EDGE / Math.max(width, height, 1));
  const files = fontFiles();
  const rendered = new resvg.Resvg(svg, {
    fitTo: { mode: "zoom", value: zoom },
    font: { loadSystemFonts: true, defaultFontFamily: "JetBrains Mono", ...(files.length ? { fontFiles: files } : {}) },
  }).render();
  return { ok: true, png: Buffer.from(rendered.asPng()), width: rendered.width, height: rendered.height };
}
