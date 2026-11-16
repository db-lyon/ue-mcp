// @session independent: Types and pure conversions of an asset diff the bridge returned. The service that calls the editor passes its answer in.
/**
 * Asset diffs for checkpoints: each version of a package loaded in the editor
 * with DiffUtils::LoadPackageForDiff, and Unreal's own diff code run on them
 * (the bridge's checkpoint_diff_asset). The records are the engine's, as it
 * produces them: FSingleObjectDiffEntry from DiffUtils::CompareUnrelatedObjects
 * with both values exported as text, FDiffSingleResult from
 * FGraphDiffControl::DiffGraphs with its DisplayString and ToolTip, and
 * FSCSDiffEntry from DiffUtils::CompareUnrelatedSCS, and the Blueprint type's own
 * FDiffSingleResults from UBlueprint::FindDiffs (a Widget Blueprint's widget
 * tree, each paired widget compared property by property). Facts about each version
 * (LOD counts, texture size) are context beside them, never a diff of their own.
 *
 * Each version also gets a picture rendered with ThumbnailTools::RenderThumbnail
 * on the object loaded for diff, kept per blob id, so a version always has the
 * same picture. A level is drawn from the level itself, framed on the actors
 * that changed, so its pictures are kept per pair (pictureId). A Blueprint changed only in its
 * graphs is pictured by the changed graph instead, drawn for the pair.
 */
import { unifiedDiff } from "./text-diff.js";
import type { AssetDiff, AssetDiffRecord, AssetDiffSection, AssetFact, AssetThumbnail, AssetVersion } from "../contract/daemon.js";
import * as crypto from "node:crypto";
import * as path from "node:path";

/** Bumped when the bridge's records or pictures change, so cached diffs are recomputed. */
export const ASSET_DIFF_FORMAT = 5;

/**
 * Bumped when the bridge's pictures change, so a picture kept for a version is
 * rendered again. 2: the bridge no longer writes a blank picture (a Blueprint
 * loaded for diff is not compiled, so its renderer drew nothing but black).
 * 3: an Actor Blueprint loaded for diff is drawn, rather than its saved thumbnail.
 * 4: a level is drawn from the level, framed on the actors that changed.
 */
export const THUMBNAIL_FORMAT = 4;

/** Where the picture (png) of a version, or what is known about it (json), is kept. */
export function thumbnailFile(dir: string, version: string, ext: "png" | "json" = "png"): string {
  return path.join(dir, `${version}.v${THUMBNAIL_FORMAT}.${ext}`);
}

/**
 * The id one side's picture is kept under. Most pictures depend on the version
 * alone, so they are kept per blob id. A level's picture is framed on the actors
 * that changed between the two versions, so it is kept per pair and side.
 */
export function pictureId(packagePath: string, a: string | null, b: string | null, side: "before" | "after"): string | null {
  const version = side === "before" ? a : b;
  if (!version) return null;
  if (!/\.umap$/i.test(packagePath)) return version;
  return crypto.createHash("sha1").update(["level", a ?? "-", b ?? "-", side].join("\n")).digest("hex");
}

/**
 * The id of a graph picture: one side of one diffed pair, since the region drawn
 * depends on both versions. Forty hex digits, as the thumbnail route requires.
 */
export function graphPictureId(pairKey: string, side: "before" | "after"): string {
  return crypto.createHash("sha1").update(`graph\n${pairKey}\n${side}`).digest("hex");
}

/** How many packages one list_changes diffs up front; the rest are diffed on request, per path. */
export const MAX_ASSET_DIFFS = 25;

type Json = Record<string, unknown>;

const str = (v: unknown): string | undefined => (typeof v === "string" ? v : undefined);

function short(v: string | null | undefined, max = 60): string {
  if (v === null || v === undefined) return "none";
  const one = v.replace(/\s+/g, " ").trim();
  return one.length > max ? `${one.slice(0, max - 3)}...` : one;
}

function plural(n: number, one: string, many = `${one}s`): string {
  return `${n} ${n === 1 ? one : many}`;
}

/** A property record's name: its group, then the details panel's label for the path. */
function recordName(r: AssetDiffRecord): string {
  const name = r.label ?? r.path ?? r.displayString;
  return r.group && r.group !== r.path && r.group !== name ? `${r.group} > ${name}` : name;
}

/** The record's own words, with the values when it has them. */
export function describeRecord(r: AssetDiffRecord): string {
  const label = r.path ? recordName(r) : r.displayString;
  if (r.path && (r.before !== undefined || r.after !== undefined)) {
    if (r.diffType === "PropertyAddedToB") return `${label} added: ${short(r.after)}`;
    if (r.diffType === "PropertyAddedToA") return `${label} removed: ${short(r.before)}`;
    return `${label} ${short(r.before)} to ${short(r.after)}`;
  }
  const set = nodeValues(r);
  return set ? `${r.displayString}: ${set}` : r.displayString;
}

/** An added or removed node's properties that differ from their class defaults, as "Value 0.5". */
function nodeValues(r: AssetDiffRecord): string {
  if (r.diffType !== "NODE_ADDED" && r.diffType !== "NODE_REMOVED") return "";
  const set = (r.node1?.properties ?? []).filter((p) => !p.isDefault);
  const shown = set.slice(0, 3).map((p) => `${p.displayName || p.name} ${short(p.value, 40)}`);
  return shown.length < set.length ? `${shown.join(", ")}, and ${set.length - shown.length} more` : shown.join(", ");
}

const REMOVALS = new Set(["PropertyAddedToA", "OBJECT_REMOVED", "NODE_REMOVED"]);
const isRemoval = (r: AssetDiffRecord) => REMOVALS.has(r.diffType) || r.category === "SUBTRACTION";
const hasValues = (r: AssetDiffRecord) => r.path !== undefined && (r.before !== undefined || r.after !== undefined);

/**
 * A one-line account of a diff, from its records, for deciding Keep or Revert
 * at a glance: the count, then removals (the changes a Keep loses least
 * visibly), then changes with values, then the rest, each named once.
 */
export function summarizeAssetDiff(d: AssetDiff): string {
  if (d.state === "error") return `Could not diff: ${short(d.error ?? "no reason given", 120)}`;
  if (d.state === "deferred") return "Not diffed yet: over the per-turn limit, diffed when opened.";
  const cls = d.assetClass ?? "asset";
  const sections = d.sections ?? [];
  const all = sections.flatMap((s) => s.entries);
  const records = all.filter((r) => !r.hidden);
  const hidden = all.length - records.length;
  const hiddenNote = hidden ? `; plus ${plural(hidden, "difference")} in properties the details panel does not show` : "";
  if (d.status === "added" || d.status === "deleted") {
    const v = d.status === "added" ? d.after : d.before;
    const facts = (v?.facts ?? []).filter((f) => f.value !== "").slice(0, 3).map((f) => `${f.name} ${short(f.value, 40)}`);
    const set = records.length ? `, ${plural(records.length, "property", "properties")} set` : "";
    return `${d.status === "added" ? "New" : "Deleted"} ${cls}${set}${facts.length ? `: ${facts.join("; ")}` : ""}`;
  }
  if (records.length === 0) {
    if (d.textDiff?.diff) return `${cls}: the engine's ${d.textDiff.format.toUpperCase()} export changed`;
    if (!hidden && widgetTreeNotCompared(d)) return `${cls}: the file changed; this bridge does not compare the widget tree, so open Unreal's diff to see it`;
    // No property differs, but a fact does (a sound's duration, a mesh's triangles): that is the change.
    const before = new Map((d.before?.facts ?? []).map((f) => [f.name, f.value]));
    const facts = (d.after?.facts ?? []).filter((f) => before.has(f.name) && before.get(f.name) !== f.value);
    if (!hidden && facts.length) {
      const shown = facts.slice(0, 2).map((f) => `${f.name}: ${short(before.get(f.name), 40)} to ${short(f.value, 40)}`);
      const more = facts.length - shown.length;
      return `${cls} changed. ${shown.join("; ")}${more > 0 ? `; and ${plural(more, "more fact")}` : ""}`;
    }
    return hidden
      ? `${cls}: ${plural(hidden, "difference")}, all in properties the details panel does not show`
      : `${cls}: the file changed, and Unreal's diff finds no difference`;
  }
  // A graph record's values are in the property records behind it, named by the node they belong to.
  const leaves = records.flatMap((r) => (r.properties?.length
    ? r.properties.filter((p) => !p.hidden).map((p) => ({ ...p, group: p.group ?? r.node2?.title ?? r.node1?.title ?? r.group }))
    : [r]));
  const ordered = [...leaves.filter(isRemoval), ...leaves.filter((r) => !isRemoval(r) && hasValues(r)), ...leaves.filter((r) => !isRemoval(r) && !hasValues(r))];
  const shown: string[] = [];
  for (const r of ordered) {
    const line = describeRecord(r);
    const name = r.path ? recordName(r) : line;
    if (shown.some((x) => x.startsWith(name))) continue;
    shown.push(line);
    if (shown.length === 2) break;
  }
  const total = records.length + (d.recordsDropped ?? 0);
  const rest = total - shown.length;
  return `${plural(total, "difference")}: ${shown.join("; ")}${rest > 0 ? `; and ${rest} more` : ""}${hiddenNote}`;
}

/** A Widget Blueprint diffed by a bridge that does not run UBlueprint::FindDiffs, which is where its widget tree is compared. */
export function widgetTreeNotCompared(d: Pick<AssetDiff, "assetClass" | "engine">): boolean {
  return /WidgetBlueprint/.test(d.assetClass ?? "") && d.engine?.blueprintTypeDiff === undefined;
}

/** Whether Unreal's own diff view can open this item, and why not, as the bridge decided it. */
export function nativeDiffOf(d: Pick<AssetDiff, "state" | "engine">): AssetDiff["nativeDiff"] {
  if (d.state !== "ready" || !d.engine) return { available: false, reason: "The asset has not been diffed." };
  return d.engine.nativeAvailable === false ? { available: false, reason: d.engine.nativeReason ?? "Unreal cannot open a diff of this type." } : { available: true };
}

/**
 * The pictures of one version: the changed graph when the bridge drew one (a
 * graph-only Blueprint change), with the asset's own picture kept beside it;
 * otherwise the asset's picture, with the reason the graph could not be drawn.
 */
export function versionPictures(
  thumbnail: AssetThumbnail | undefined,
  graph: AssetThumbnail | undefined,
): Pick<AssetVersion, "thumbnail" | "classThumbnail"> {
  if (graph && "id" in graph) return { thumbnail: graph, ...(thumbnail ? { classThumbnail: thumbnail } : {}) };
  if (!graph) return thumbnail ? { thumbnail } : {};
  const why = `${graph.error.replace(/\.$/, "")}. The asset's own picture is shown, and a graph edit does not change it.`;
  if (!thumbnail) return { thumbnail: { error: why } };
  if ("error" in thumbnail) return { thumbnail: { error: `${thumbnail.error}; ${why}` } };
  return { thumbnail: { ...thumbnail, note: thumbnail.note ? `${thumbnail.note}. ${why}` : why } };
}

/** The bridge's answer as an AssetDiff, with the CSV export turned into a unified diff and the pictures mapped to ids. */
export function assetDiffFromBridge(
  raw: Json,
  key: string,
  picture: (side: "before" | "after", thumb: Json | undefined) => AssetThumbnail | undefined,
  graphPicture?: (side: "before" | "after", graph: Json) => AssetThumbnail | undefined,
): AssetDiff {
  const version = (side: "before" | "after"): AssetVersion | null => {
    const v = raw[side];
    if (!v || typeof v !== "object") return null;
    const o = v as Json;
    const thumbnail = picture(side, o.thumbnail as Json | undefined);
    const graph = graphPicture && o.graphPicture && typeof o.graphPicture === "object" ? graphPicture(side, o.graphPicture as Json) : undefined;
    return {
      class: str(o.class) ?? "",
      ...(str(o.objectName) ? { objectName: str(o.objectName) } : {}),
      facts: Array.isArray(o.facts) ? (o.facts as AssetFact[]) : [],
      ...versionPictures(thumbnail, graph),
    };
  };
  const d: AssetDiff = {
    state: "ready",
    packageName: str(raw.packageName),
    assetClass: str(raw.assetClass),
    status: str(raw.status) as AssetDiff["status"],
    engine: raw.engine as AssetDiff["engine"],
    before: version("before"),
    after: version("after"),
    sections: Array.isArray(raw.sections) ? (raw.sections as AssetDiffSection[]) : [],
    recordCount: typeof raw.recordCount === "number" ? raw.recordCount : 0,
    summary: "",
    nativeDiff: { available: false },
  };
  if (str(raw.classChanged)) d.classChanged = str(raw.classChanged);
  if (typeof raw.recordsDropped === "number") d.recordsDropped = raw.recordsDropped;
  if (raw.blueprint && typeof raw.blueprint === "object") d.blueprint = raw.blueprint as Json;
  const text = raw.engineText as Json | undefined;
  if (text && typeof text.before === "string" && typeof text.after === "string") {
    d.textDiff = { format: str(text.format) ?? "text", diff: unifiedDiff(text.before, text.after, key) ?? "The two exports differ in too many lines to show line by line." };
  }
  d.summary = summarizeAssetDiff(d);
  d.nativeDiff = nativeDiffOf(d);
  return d;
}

export function assetDiffError(error: string): AssetDiff {
  const d: AssetDiff = { state: "error", error, summary: "", nativeDiff: { available: false, reason: error } };
  d.summary = summarizeAssetDiff(d);
  return d;
}

export function assetDiffDeferred(): AssetDiff {
  const d: AssetDiff = { state: "deferred", summary: "", nativeDiff: { available: false, reason: "Diff it first." } };
  d.summary = summarizeAssetDiff(d);
  return d;
}
