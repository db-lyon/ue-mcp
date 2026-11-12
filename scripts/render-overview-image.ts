#!/usr/bin/env tsx
/**
 * Render the README's overview image from the counts.
 *
 *   docs/assets/ue-mcp-overview.png   the image
 *   docs/assets/ue-mcp-overview.json  the figures it shows, for the unit test
 *
 * The template is scripts/assets/overview-image/overview.svg. It is rendered
 * with resvg and the fonts committed beside it, so the output does not depend
 * on a browser or on the fonts installed on the machine.
 *
 * The circles are a fixed size and say one thing: Epic's MCP is a subset of
 * ue-mcp. The inner circle is every Epic tool ue-mcp wraps; the outer one is
 * ue-mcp, which holds all of them plus its own actions and the capabilities
 * listed beside it. They are deliberately not scaled by the counts: Epic's
 * wrapped tools outnumber ue-mcp's own on UE 5.9, and a circle sized by that
 * would draw the subset larger than the set that contains it. Only the numbers
 * and the engine range are data.
 *
 * `npm run generate:metadata` calls this. It renders only when the figures,
 * the template or this renderer changed, so an unchanged surface leaves the
 * PNG untouched.
 * `--force` renders regardless.
 */
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";

import type { Counts } from "./generate-tool-metadata.js";

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, "..");
const assetDir = path.join(here, "assets", "overview-image");
const templatePath = path.join(assetDir, "overview.svg");
export const PNG_PATH = path.join(repo, "docs", "assets", "ue-mcp-overview.png");
export const FIGURES_PATH = path.join(repo, "docs", "assets", "ue-mcp-overview.json");
export const WIDTH = 1354;
export const HEIGHT = 586;

const OUTER = { cx: 268, r: 216 };
/** Epic's circle. Fixed, and always well inside ue-mcp's. */
const EPIC_R = 134;
/** Gap between the inner circle and the outer one where they nearly touch. */
const INSET = 4;
const OWN_FONT_SIZE = 34;

/** In column order: the first four fill the left column. */
const CHECKLIST = [
  "Dialog handling",
  "Safe retries & rollback",
  "Custom guardrails",
  "Precise spatial control",
  "Workflow automation",
  "Plugin ecosystem",
  "Multi-editor control",
  "Auto-reported tool gaps",
];

export interface OverviewFigures {
  /** Every Epic tool ue-mcp wraps (`nativeToolActions`). */
  epicActions: number;
  /** ue-mcp's own actions on top of them (`ownActions`). */
  ownActions: number;
  /** From scripts/lib/engine-range.mjs, through the counts. */
  engineRange: { min: string; max: string };
  /** sha256 of the template and this renderer, so a change to how the image
   *  is drawn also requires a re-render. */
  template: string;
}

export function templateHash(): string {
  const hash = crypto.createHash("sha256");
  for (const file of [templatePath, fileURLToPath(import.meta.url)]) {
    hash.update(fs.readFileSync(file, "utf8").replace(/\r\n/g, "\n"));
  }
  return hash.digest("hex");
}

export function overviewFigures(counts: Counts): OverviewFigures {
  return {
    epicActions: counts.nativeToolActions,
    ownActions: counts.ownActions,
    engineRange: counts.engineRange,
    template: templateHash(),
  };
}

export function overviewAltText(f: OverviewFigures): string {
  return (
    `ue-mcp contains Epic's MCP and its ${fmt(f.epicActions)} actions. Around it, ue-mcp adds ${fmt(f.ownActions)} actions of its own, ` +
    "and dialog handling, retries and rollback, guardrails, spatial control, workflow automation, plugins, " +
    `multi-editor control and auto-reported tool gaps. Supports UE ${f.engineRange.min} to ${f.engineRange.max}.`
  );
}

function fmt(n: number): string {
  return n.toLocaleString("en-US");
}

function xml(s: string): string {
  return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
}

type ResvgModule = typeof import("@resvg/resvg-js");

function fontOptions() {
  const fontFiles = fs
    .readdirSync(path.join(assetDir, "fonts"))
    .filter((f) => f.endsWith(".ttf"))
    .map((f) => path.join(assetDir, "fonts", f));
  return { fontFiles, loadSystemFonts: false, defaultFontFamily: "Plus Jakarta Sans" };
}

/** Ink width of one line of text, measured by the renderer that draws it. */
function textWidth(resvg: ResvgModule, text: string, family: string, weight: number, size: number): number {
  const svg =
    `<svg xmlns="http://www.w3.org/2000/svg" width="2000" height="200">` +
    `<text x="10" y="100" font-family="${family}" font-weight="${weight}" font-size="${size}">${xml(text)}</text></svg>`;
  const box = new resvg.Resvg(svg, { font: fontOptions() }).getBBox();
  if (!box) throw new Error(`Could not measure "${text}": is the ${family} font in ${assetDir}/fonts?`);
  return box.width;
}

export function buildSvg(resvg: ResvgModule, f: OverviewFigures): string {
  // Fixed geometry: see the header for why the counts do not size the circles.
  const epicR = EPIC_R;
  const epicCx = OUTER.cx - OUTER.r + INSET + epicR;
  const crescentLeft = epicCx + epicR;
  const crescentRight = OUTER.cx + OUTER.r;
  const ownCx = (crescentLeft + crescentRight) / 2;
  const ownText = `+${fmt(f.ownActions)}`;
  const ownWidth = textWidth(resvg, ownText, "Plus Jakarta Sans", 800, OWN_FONT_SIZE);
  const room = crescentRight - crescentLeft - 24;
  const ownSize = Math.min(OWN_FONT_SIZE, Math.floor((OWN_FONT_SIZE * room) / ownWidth));

  const rows = [224, 275, 326, 377];
  const cols = [576, 970];
  const checklist = CHECKLIST.map((label, i) => {
    const x = cols[Math.floor(i / rows.length)];
    const y = rows[i % rows.length];
    const tx = x + 33;
    const w = textWidth(resvg, label, "Plus Jakarta Sans", 600, 21);
    return [
      `<path d="M${x + 1} ${y - 6} L${x + 6} ${y - 1} L${x + 15} ${y - 13}" fill="none" stroke="#4ade80" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>`,
      `<text x="${tx}" y="${y}" font-family="Plus Jakarta Sans" font-weight="600" font-size="21" fill="#f5f5f4">${xml(label)}</text>`,
      `<line x1="${tx}" y1="${y + 10}" x2="${(tx + w).toFixed(1)}" y2="${y + 10}" stroke="#3a3f47" stroke-width="1"/>`,
    ].join("\n  ");
  }).join("\n  ");

  const values: Record<string, string> = {
    epicCx: epicCx.toFixed(1),
    epicR: epicR.toFixed(1),
    ownCx: ownCx.toFixed(1),
    ownSize: String(ownSize),
    epicActions: fmt(f.epicActions),
    ownActions: fmt(f.ownActions),
    engineMin: xml(f.engineRange.min),
    engineMax: xml(f.engineRange.max),
    checklist,
  };
  const template = fs.readFileSync(templatePath, "utf8");
  return template.replace(/\{\{(\w+)\}\}/g, (_m, key: string) => {
    if (!(key in values)) throw new Error(`overview.svg uses {{${key}}}, which the renderer does not fill.`);
    return values[key];
  });
}

function readFigures(): OverviewFigures | null {
  try {
    return JSON.parse(fs.readFileSync(FIGURES_PATH, "utf8")) as OverviewFigures;
  } catch {
    return null;
  }
}

/** Point the README's alt text at the numbers the image shows. */
function updateReadmeAlt(f: OverviewFigures): void {
  const readme = path.join(repo, "README.md");
  const text = fs.readFileSync(readme, "utf8");
  const next = text.replace(
    /(<img\b[^>]*?\balt=")[^"]*("[^>]*?\bsrc="docs\/assets\/ue-mcp-overview\.png")/,
    (_m, a: string, b: string) => `${a}${xml(overviewAltText(f))}${b}`,
  );
  if (next !== text) fs.writeFileSync(readme, next);
}

export async function renderOverviewImage(
  counts: Counts,
  opts: { force?: boolean } = {},
): Promise<{ png: string; rendered: boolean }> {
  const figures = overviewFigures(counts);
  updateReadmeAlt(figures);
  const rel = path.relative(repo, PNG_PATH).replace(/\\/g, "/");
  const current = readFigures();
  if (!opts.force && fs.existsSync(PNG_PATH) && JSON.stringify(current) === JSON.stringify(figures)) {
    return { png: rel, rendered: false };
  }
  const resvg = await import("@resvg/resvg-js");
  const svg = buildSvg(resvg, figures);
  const png = new resvg.Resvg(svg, { font: fontOptions(), fitTo: { mode: "original" } }).render().asPng();
  fs.mkdirSync(path.dirname(PNG_PATH), { recursive: true });
  fs.writeFileSync(PNG_PATH, png);
  fs.writeFileSync(FIGURES_PATH, JSON.stringify(figures, null, 2) + "\n");
  return { png: rel, rendered: true };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const { computeCounts } = await import("./generate-tool-metadata.js");
  const result = await renderOverviewImage(computeCounts(), { force: process.argv.includes("--force") });
  console.log(result.rendered ? `rendered ${result.png}` : `${result.png} already shows these counts`);
}
