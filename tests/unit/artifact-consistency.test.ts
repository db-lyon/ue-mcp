/**
 * Every version and count this repo writes down, held against its source.
 *
 * This repo ships ONE product from ONE repo: the npm package carries the C++
 * plugin source and a user compiles it out of that tarball. So a version or a
 * surface count appears in a dozen artifacts, each written at a different
 * time, and nothing compared them.
 *
 * What that cost, all found in one release audit:
 *
 *   - The MCP server reported `serverInfo.version` as a literal frozen at
 *     0.6.4 while the package said 1.3.0. Nine minors of every client being
 *     told the wrong version, while `doctor` read package.json and disagreed.
 *   - The .uplugin said 0.3.0, and its description advertised 185 tools
 *     against more than a thousand. A user who compiled the plugin out of the
 *     tarball got something that named itself a version that was never
 *     published.
 *   - Eight count stamps sat 243 actions stale, including the package.json
 *     description that npmjs.org displays.
 *   - A shipped skill told agents there were 19 category tools. There are 24.
 *
 * Every one of those passed CI, because CI had nothing that looked. The
 * generator can restamp them all (`npm run generate:metadata`), but a
 * generator only helps if someone runs it, and the failure mode is precisely
 * that nobody did. So this fails the build instead.
 *
 * When this test fails the fix is almost always `npm run generate:metadata`,
 * then commit what it changed. The message says so.
 */
import { describe, it, expect } from "vitest";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { ALL_TOOLS } from "../../src/tools.js";

const repo = path.join(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const read = (rel: string): string => fs.readFileSync(path.join(repo, rel), "utf8");
const readJson = (rel: string): any => JSON.parse(read(rel));

/**
 * The plugin descriptor, found by scanning rather than by name.
 *
 * The working tree and the git index had disagreed on the case of this
 * filename. Windows hides that; a case-sensitive filesystem does not, so a
 * hardcoded spelling reads a missing file on Linux and this whole gate passes
 * vacuously on the one platform CI runs.
 */
function readUplugin(): { path: string; json: any } {
  const dir = path.join(repo, "plugin", "ue_mcp_bridge");
  const hit = fs.readdirSync(dir).find((f) => f.toLowerCase().endsWith(".uplugin"));
  if (!hit) throw new Error(`no .uplugin found in ${dir}`);
  return { path: hit, json: JSON.parse(fs.readFileSync(path.join(dir, hit), "utf8")) };
}

const REGENERATE = "Run `npm run generate:metadata` and commit what it changes.";

const pkg = readJson("package.json");

/** The live surface, which is what every count is supposed to describe. */
const counts = (() => {
  let actions = 0;
  let bridgeActions = 0;
  for (const tool of ALL_TOOLS) {
    for (const spec of Object.values(tool.actions)) {
      actions += 1;
      if ((spec as { bridge?: string }).bridge) bridgeActions += 1;
    }
  }
  return { tools: ALL_TOOLS.length, actions, bridgeActions };
})();

describe("one product, one version", () => {
  it("ships a plugin descriptor that names the package version", () => {
    const plugin = readUplugin().json;
    expect(
      plugin.VersionName,
      `UE_MCP_Bridge.uplugin says VersionName ${plugin.VersionName} and package.json says `
        + `${pkg.version}. The tarball carries the plugin source, so a user compiles THIS `
        + `descriptor out of THAT package: they must agree. ${REGENERATE}`,
    ).toBe(pkg.version);
  });

  it("reports the package version to MCP clients rather than a literal", () => {
    // The bug this catches was a hardcoded "0.6.4" that survived nine minor
    // releases, so asserting the value is not enough: assert that no literal
    // version is written at the construction site at all.
    const src = read("src/index.ts");
    const ctor = src.match(/new McpServer\(\s*\{[^}]*\}/s)?.[0] ?? "";
    expect(ctor, "could not find the McpServer construction in src/index.ts").not.toBe("");
    expect(
      /version:\s*["'`]\d/.test(ctor),
      "src/index.ts writes a literal version into the McpServer options. It must read "
        + "package.json, or it will freeze at whatever was true the day it was typed.",
    ).toBe(false);
    expect(/version:\s*pkg\.version/.test(ctor)).toBe(true);
  });
});

describe("one surface, one count", () => {
  const stampedFiles = [
    "CLAUDE.md",
    "README.md",
    "docs/index.md",
    "docs/architecture.md",
    "docs/configuration.md",
    "docs/development.md",
    "docs/flows.md",
    "docs/tool-reference.md",
  ];

  it("has no stale count marker in any file that carries one", () => {
    const stale: string[] = [];
    for (const rel of stampedFiles) {
      const text = read(rel);
      const pairs: Array<[RegExp, number]> = [
        [/<!--\s*count:tools\s*-->([^<]*)<!--\s*\/count\s*-->/g, counts.tools],
        [/<!--\s*count:actions\s*-->([^<]*)<!--\s*\/count\s*-->/g, counts.actions],
        [/<!--\s*count:bridgeActions\s*-->([^<]*)<!--\s*\/count\s*-->/g, counts.bridgeActions],
      ];
      for (const [re, want] of pairs) {
        for (const m of text.matchAll(re)) {
          const got = parseInt(m[1].replace(/\D/g, ""), 10);
          if (got !== want) stale.push(`${rel}: marker says ${m[1].trim()}, surface is ${want}`);
        }
      }
    }
    expect(stale, [`Stale count markers.`, ...stale, REGENERATE].join(String.fromCharCode(10)))
      .toEqual([]);
  });

  it("keeps the npm description and the plugin description current", () => {
    expect(pkg.description, `package.json description is stale. ${REGENERATE}`)
      .toContain(`${counts.actions}+`);
    expect(pkg.description).toContain(`${counts.tools} tools`);
    const plugin = readUplugin().json;
    expect(plugin.Description, `.uplugin description is stale. ${REGENERATE}`)
      .toContain(`${counts.bridgeActions}+`);
  });

  it("has no unstamped tool count loose in a shipped artifact", () => {
    // `skills/` is in the package `files` list and is injected into agent
    // context, and no generator touched it, so it told agents 19 for a long
    // time. Anything that states a tool count in prose has to be right.
    const offenders: string[] = [];
    for (const rel of ["skills/ue-mcp-workflow/SKILL.md", "CLAUDE.md", "README.md"]) {
      for (const m of read(rel).matchAll(/(\d+)\s+category tools/g)) {
        if (parseInt(m[1], 10) !== counts.tools) {
          offenders.push(`${rel}: says ${m[1]} category tools, there are ${counts.tools}`);
        }
      }
    }
    expect(offenders, offenders.join(String.fromCharCode(10))).toEqual([]);
  });
});
