/**
 * The supported engine range is written down once, in
 * scripts/lib/engine-range.mjs, and every other statement of it follows.
 *
 * It used to be typed into the README, the overview image, five docs pages, the
 * release template, CLAUDE.md and the prose lint, and each had to be found by
 * hand when an engine was added. This holds the stamped copies to the source,
 * fails on a range stated in prose without a marker, and checks the places
 * that cannot read the source (the engine probe list, the release binaries
 * table, the recorded Epic catalogs) against it.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { MARKER_FILES, applyCountMarkers, computeCounts } from "../../scripts/generate-tool-metadata.js";
import { SUPPORTED_ENGINE_RANGE, compareEngines } from "../../scripts/lib/engine-range.mjs";
import { ENGINES as RELEASE_ENGINES } from "../../scripts/release-binaries.mjs";
import { DEFAULT_ENGINE_VERSIONS } from "../../src/editor/engine-root.js";
import { EPIC_CATALOG_ENGINES } from "../../src/tools/epic/catalog-engines.js";

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const read = (rel: string) => fs.readFileSync(path.join(repo, rel), "utf8");
const REGENERATE = "Run `npm run generate:metadata` and commit what it changes.";

/** Every tracked Markdown page a reader sees: README and the docs site. */
function publicPages(): string[] {
  const out = ["README.md", ".github/RELEASE_TEMPLATE.md"];
  const walk = (dir: string) => {
    for (const e of fs.readdirSync(path.join(repo, dir), { withFileTypes: true })) {
      const rel = `${dir}/${e.name}`;
      if (e.isDirectory()) walk(rel);
      else if (e.name.endsWith(".md")) out.push(rel);
    }
  };
  walk("docs");
  return out;
}

describe("supported engine range", () => {
  it("is stamped current in every marker the generator owns", () => {
    const counts = computeCounts();
    const stale = MARKER_FILES.filter((rel) => fs.existsSync(path.join(repo, rel))).filter(
      (rel) => applyCountMarkers(read(rel), counts) !== read(rel),
    );
    expect(stale, `Stale count or engine-range markers in: ${stale.join(", ")}. ${REGENERATE}`).toEqual([]);
  });

  it("is never stated in public prose outside a marker", () => {
    const offenders: string[] = [];
    for (const rel of publicPages()) {
      let fenced = false;
      read(rel)
        .split(/\r?\n/)
        .forEach((line, i) => {
          // A fenced block is an example (a past release's notes), not a claim.
          if (/^\s*(```|~~~)/.test(line)) fenced = !fenced;
          if (fenced) return;
          const bare = line.replace(/<!--\s*count:\w+\s*-->[^<]*<!--\s*\/count\s*-->/g, "");
          if (/\b5\.\d\s+(to|through)\s+5\.\d\b/.test(bare)) offenders.push(`${rel}:${i + 1}: ${line.trim()}`);
        });
    }
    // The README's image alt text is written by the renderer from the counts.
    const loose = offenders.filter((o) => !o.includes('src="docs/assets/ue-mcp-overview.png"'));
    expect(
      loose,
      ["An engine range typed by hand. Use <!-- count:engineRange --><!-- /count --> instead.", ...loose].join("\n"),
    ).toEqual([]);
  });

  it("is probed for in the default install locations", () => {
    expect(DEFAULT_ENGINE_VERSIONS).toContain(SUPPORTED_ENGINE_RANGE.min);
    expect(DEFAULT_ENGINE_VERSIONS).toContain(SUPPORTED_ENGINE_RANGE.max);
  });

  it("has its newest engine in the release binaries table", () => {
    expect(RELEASE_ENGINES.map((e: { version: string }) => e.version)).toContain(SUPPORTED_ENGINE_RANGE.max);
  });

  it("covers every engine with a recorded Epic catalog", () => {
    for (const engine of EPIC_CATALOG_ENGINES) {
      expect(compareEngines(engine, SUPPORTED_ENGINE_RANGE.min)).toBeGreaterThanOrEqual(0);
      expect(compareEngines(engine, SUPPORTED_ENGINE_RANGE.max)).toBeLessThanOrEqual(0);
    }
  });
});
