/** #1317: plugin publish keeps the matched slug, derives declared facts, and diffs on --dry-run. */
import { describe, expect, it } from "vitest";
import { chooseListing, derivedListingFields, listingDiff } from "../../src/extensions/publish-listing.js";
import { PluginManifestSchema, parseManifest } from "../../src/extensions/manifest.js";

const catalog = [
  { slug: "voxel", packageName: "voxel-plugin-tools" },
  { slug: "meshy", packageName: "ue-mcp-meshy" },
];

describe("chooseListing", () => {
  it("keeps the slug of the row that lists this package", () => {
    expect(chooseListing(catalog, "voxel-plugin-tools", undefined, false)).toMatchObject({ ok: true, slug: "voxel" });
  });

  it("refuses a --slug that would rename it without --rename", () => {
    const r = chooseListing(catalog, "voxel-plugin-tools", "voxel-plugin-tools", false);
    expect(r.ok).toBe(false);
    expect(!r.ok && r.error).toMatch(/--rename/);
  });

  it("renames with --rename, unless the new slug is taken", () => {
    expect(chooseListing(catalog, "voxel-plugin-tools", "voxel-tools", true)).toMatchObject({ ok: true, slug: "voxel-tools", renamed: "voxel" });
    expect(chooseListing(catalog, "voxel-plugin-tools", "meshy", true).ok).toBe(false);
  });

  it("defaults a new listing's slug and refuses one another package owns", () => {
    expect(chooseListing(catalog, "ue-mcp-recipes", undefined, false)).toMatchObject({ ok: true, slug: "recipes", existing: undefined });
    expect(chooseListing(catalog, "other-pkg", "meshy", false).ok).toBe(false);
  });
});

describe("derivedListingFields", () => {
  it("derives provides and ueVersions from the manifest and skills", () => {
    const m = PluginManifestSchema.parse({
      actionPrefix: "vox",
      nativeModule: { uePluginName: "Vox", minBridgeApi: 2, source: "ue", supportedEngineVersions: ["5.8"] },
    });
    expect(derivedListingFields(m, true, ["flows", "subagents"])).toEqual({ provides: ["skills", "subagents", "tools"], ueVersions: ["5.8"] });
  });

  it("lists flows only when the manifest ships some", () => {
    const m = PluginManifestSchema.parse({ actionPrefix: "x", flows: { f: { description: "d", steps: { "1": { task: "t" } } } } });
    expect(derivedListingFields(m, false).provides).toEqual(["flows"]);
  });
});

describe("the manifest registry block", () => {
  it("parses, and a bad one is dropped instead of failing the manifest", () => {
    expect(parseManifest({ actionPrefix: "x", registry: { tagline: "Voxel terrain", tags: ["voxel"] } }).manifest.registry)
      .toEqual({ tagline: "Voxel terrain", tags: ["voxel"] });
    const bad = parseManifest({ actionPrefix: "x", registry: { tagline: "no" } });
    expect(bad.manifest.registry).toBeUndefined();
    expect(bad.dropped.map((d) => d.path)).toEqual(["registry"]);
  });
});

describe("listingDiff", () => {
  it("names each changed field, the slug first, and flags a rename", () => {
    const lines = listingDiff(
      { slug: "voxel", provides: ["tools", "flows"], readme: "abc", tagline: "same" },
      { slug: "voxel-tools", provides: ["tools"], readme: "", tagline: "same" },
    );
    expect(lines[0]).toMatch(/slug: "voxel" -> "voxel-tools".*RENAMES/);
    expect(lines).toContain('  provides: ["tools","flows"] -> ["tools"]');
    expect(lines).toContain("  readme: 3 chars -> 0 chars (rendered live from the repo)");
    expect(lines.some((l) => l.includes("tagline"))).toBe(false);
  });
});
