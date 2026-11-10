/**
 * The README's overview image shows the counts the server advertises.
 *
 * It was drawn by hand once and its numbers and circles went stale. It is now
 * rendered by scripts/render-overview-image.ts, which writes the figures it drew
 * beside the PNG. This fails when those figures, or the template, no longer
 * match the current surface: run `npm run generate:metadata` and commit the result.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { describe, expect, it } from "vitest";
import { computeCounts } from "../../scripts/generate-tool-metadata.js";
import {
  FIGURES_PATH,
  HEIGHT,
  PNG_PATH,
  WIDTH,
  overviewAltText,
  overviewFigures,
} from "../../scripts/render-overview-image.js";

const counts = computeCounts();
const expected = overviewFigures(counts);

describe("overview image", () => {
  it("was rendered from the current counts and template", () => {
    const committed = JSON.parse(fs.readFileSync(FIGURES_PATH, "utf8"));
    expect(committed).toEqual(expected);
  });

  it("shows Epic's tools for the newest engine and ue-mcp's own actions", () => {
    expect(expected.epicActions).toBe(counts.nativeEngineToolActions);
    expect(expected.ownActions).toBe(counts.ownActions);
    expect(expected.engineRange.max).toBe(counts.nativeEngine);
  });

  it("is a PNG of the size the README declares", () => {
    const png = fs.readFileSync(PNG_PATH);
    expect(png.subarray(1, 4).toString("ascii")).toBe("PNG");
    expect(png.readUInt32BE(16)).toBe(WIDTH);
    expect(png.readUInt32BE(20)).toBe(HEIGHT);
  });

  it("is described in the README by the numbers it shows", () => {
    const readme = fs.readFileSync(path.resolve(PNG_PATH, "..", "..", "..", "README.md"), "utf8");
    const img = /<img\b[^>]*src="docs\/assets\/ue-mcp-overview\.png"[^>]*>/.exec(readme)?.[0];
    expect(img).toBeDefined();
    expect(img).toContain(`width="${WIDTH}"`);
    expect(img).toContain(`height="${HEIGHT}"`);
    expect(img).toContain(`alt="${overviewAltText(expected).replace(/&/g, "&amp;").replace(/"/g, "&quot;")}"`);
  });
});
