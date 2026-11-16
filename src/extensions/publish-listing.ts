// @session independent: Pure listing choice, derived fields and diff for plugin publish.
/**
 * Pure pieces of `ue-mcp plugin publish`: which registry row a package
 * publishes to, the listing fields its manifest already declares, and the
 * field-by-field change a publish would make.
 */
import type { PluginManifest } from "./manifest.js";

export interface ListingRow {
  slug: string;
  packageName?: string;
  [k: string]: unknown;
}

export type ListingChoice =
  | { ok: true; slug: string; existing?: ListingRow; renamed?: string }
  | { ok: false; error: string };

/**
 * The row a package publishes to. A row already listing this package keeps
 * its own slug unless --slug names another, which needs --rename. A slug owned
 * by a different package is refused.
 */
export function chooseListing(
  catalog: readonly ListingRow[],
  pkgName: string,
  slugFlag: string | undefined,
  rename: boolean,
): ListingChoice {
  const byPackage = catalog.find((r) => r.packageName === pkgName);
  if (byPackage) {
    if (slugFlag === undefined || slugFlag === byPackage.slug) return { ok: true, slug: byPackage.slug, existing: byPackage };
    if (!rename) {
      return {
        ok: false,
        error:
          `${pkgName} is listed as '${byPackage.slug}'; --slug ${slugFlag} would rename it. ` +
          `Pass --rename to rename the listing, or drop --slug to keep '${byPackage.slug}'.`,
      };
    }
    const taken = catalog.find((r) => r.slug === slugFlag && r !== byPackage);
    if (taken) return { ok: false, error: `slug '${slugFlag}' is already listed for ${taken.packageName ?? "another plugin"}` };
    return { ok: true, slug: slugFlag, existing: byPackage, renamed: byPackage.slug };
  }
  const slug = slugFlag ?? pkgName.replace(/^ue-mcp-/, "");
  const bySlug = catalog.find((r) => r.slug === slug);
  if (bySlug && bySlug.packageName && bySlug.packageName !== pkgName) {
    return { ok: false, error: `slug '${slug}' is listed for ${bySlug.packageName}, not ${pkgName}; pass --slug to choose another` };
  }
  return { ok: true, slug, existing: bySlug };
}

/** Provides values the manifest cannot tell, so a curated row keeps them. */
const CURATED_PROVIDES = new Set(["subagents"]);

/**
 * The listing fields the package declares: what it provides, from its
 * manifest and skills/, and its engine versions, from its native module.
 */
export function derivedListingFields(
  manifest: PluginManifest | undefined,
  hasSkills: boolean,
  existingProvides: readonly string[] = [],
): { provides: string[]; ueVersions?: string[] } {
  const provides = new Set<string>();
  if (manifest) {
    const actions = Object.keys(manifest.inject).length > 0 || Object.keys(manifest.provides).length > 0;
    if (manifest.nativeModule || actions) provides.add("tools");
    if (Object.keys(manifest.flows).length > 0) provides.add("flows");
  }
  if (hasSkills) provides.add("skills");
  for (const p of existingProvides) if (CURATED_PROVIDES.has(p)) provides.add(p);
  const order = ["skills", "subagents", "flows", "tasks", "tools"];
  const sorted = [...provides].sort((a, b) => order.indexOf(a) - order.indexOf(b));
  const engines = manifest?.nativeModule?.supportedEngineVersions;
  return engines && engines.length > 0 ? { provides: sorted, ueVersions: [...engines] } : { provides: sorted };
}

const show = (v: unknown): string => {
  if (v === undefined) return "(unset)";
  const text = JSON.stringify(v);
  return text.length > 80 ? `${text.slice(0, 77)}...` : text;
};

/** One line per field a publish would change, the slug first. */
export function listingDiff(before: Record<string, unknown> | undefined, after: Record<string, unknown>): string[] {
  const keys = [...new Set([...Object.keys(before ?? {}), ...Object.keys(after)])]
    .filter((k) => !["status", "rating", "ratingCount"].includes(k))
    .sort((a, b) => (a === "slug" ? -1 : b === "slug" ? 1 : a.localeCompare(b)));
  const lines: string[] = [];
  for (const k of keys) {
    const was = before?.[k];
    const now = after[k];
    if (JSON.stringify(was) === JSON.stringify(now)) continue;
    if (k === "readme") {
      const len = (v: unknown) => (typeof v === "string" ? v.length : 0);
      lines.push(`  readme: ${len(was)} chars -> ${len(now)} chars${len(now) === 0 ? " (rendered live from the repo)" : ""}`);
      continue;
    }
    lines.push(`  ${k}: ${show(was)} -> ${show(now)}${k === "slug" && before ? "  (RENAMES the listing)" : ""}`);
  }
  return lines;
}
