/**
 * Asset diffs for checkpoints: each version of a package loaded in the editor
 * with DiffUtils::LoadPackageForDiff, and Unreal's own diff code run on them
 * (the bridge's checkpoint_diff_asset). The records are the engine's, as it
 * produces them: FSingleObjectDiffEntry from DiffUtils::CompareUnrelatedObjects
 * with both values exported as text, FDiffSingleResult from
 * FGraphDiffControl::DiffGraphs with its DisplayString and ToolTip, and
 * FSCSDiffEntry from DiffUtils::CompareUnrelatedSCS. Facts about each version
 * (LOD counts, texture size) are context beside them, never a diff of their own.
 *
 * Each version also gets a picture rendered with ThumbnailTools::RenderThumbnail
 * on the object loaded for diff, kept per blob id, so a version always has the
 * same picture.
 */
import { unifiedDiff } from "./text-diff.js";

/** Bumped when the bridge's records change shape, so cached diffs are recomputed. */
export const ASSET_DIFF_FORMAT = 2;

/** How many packages one list_changes diffs up front; the rest are diffed on request, per path. */
export const MAX_ASSET_DIFFS = 25;

/** One record of Unreal's diff, as the engine produced it. */
export interface AssetDiffRecord {
  /** The engine's own message for it, such as "Base Color changed value in M_Blue" or "Added Node Constant". */
  displayString: string;
  /** EPropertyDiffType (PropertyValueChanged, PropertyAddedToA, PropertyAddedToB), EDiffType (NODE_ADDED, PIN_LINKEDTO_NODE...) or ETreeDiffType (NODE_ADDED...). */
  diffType: string;
  /** EDiffType::Category of a graph record: ADDITION, SUBTRACTION, MODIFICATION, MINOR. */
  category?: string;
  toolTip?: string;
  /** The actor, component, variable or row a record belongs to. */
  group?: string;
  /** A difference under a property the details panel does not show. */
  hidden?: boolean;
  /** FPropertySoftPath::ToDisplayName of a property record. */
  path?: string;
  property?: string;
  /** Both values exported with FProperty::ExportText; null where the path does not exist on that side. */
  before?: string | null;
  after?: string | null;
  graph?: string;
  node1?: AssetDiffNode;
  node2?: AssetDiffNode;
  pin1?: { name: string; direction: string; nodeGuid?: string; defaultValue: string; linkedTo: string[] };
  pin2?: { name: string; direction: string; nodeGuid?: string; defaultValue: string; linkedTo: string[] };
  /** For a NODE_PROPERTY graph record: the property records behind it. */
  properties?: AssetDiffRecord[];
  /** For an added or removed actor: its class, label and transform. */
  facts?: AssetFact[];
  treeLocation?: number[];
  owningObjectPath?: string;
  propertyDiffType?: string;
}

/** A graph node a record names. */
export interface AssetDiffNode {
  guid: string;
  name: string;
  class: string;
  /** ENodeTitleType::ListView, the short title the engine's DisplayString uses. */
  title: string;
  /** ENodeTitleType::FullTitle. Set on NODE_ADDED and NODE_REMOVED records only, as are pins and properties. */
  fullTitle?: string;
  /** Visible input pins (exec pins left out) with their default values. */
  pins?: AssetDiffNodePin[];
  /** Class of the object whose properties are listed: the UMaterialExpression for a material node, the node otherwise. */
  propertiesOf?: string;
  /** The details panel's properties (CPF_Edit) of that object, exported as text, in class order (most derived first). */
  properties?: AssetDiffNodeProperty[];
  /** Properties past the per-node limit, counted. */
  propertiesDropped?: number;
}

export interface AssetDiffNodePin {
  name: string;
  displayName: string;
  defaultValue: string;
  /** Wired to another node, so the default is not what the node reads. */
  linked: boolean;
}

export interface AssetDiffNodeProperty {
  name: string;
  /** The details panel's label, such as "Value" for a Constant's R. */
  displayName: string;
  /** FProperty::ExportText of the value. */
  value: string;
  /** Still the class default. */
  isDefault: boolean;
}

/** A group of records under one of the engine's own diff categories (Graph: EventGraph, Components, Defaults, Details...). */
export interface AssetDiffSection {
  category: string;
  kind: "properties" | "graph" | "components";
  /** The engine API the records came from. */
  engineSource: string;
  entries: AssetDiffRecord[];
  /** How many of the records are under properties the details panel does not show (marked hidden). */
  hiddenChanges?: number;
}

export interface AssetFact {
  name: string;
  value: string;
}

export type AssetThumbnail =
  | { id: string; url: string; source: "rendered" | "saved"; note?: string }
  | { error: string };

/** One version of the package, as the editor loaded it. */
export interface AssetVersion {
  class: string;
  objectName?: string;
  facts: AssetFact[];
  thumbnail?: AssetThumbnail;
}

export interface AssetDiff {
  /** ready: diffed. deferred: over the up-front limit, diff it by path. error: see error. */
  state: "ready" | "deferred" | "error";
  error?: string;
  packageName?: string;
  assetClass?: string;
  status?: "added" | "modified" | "deleted";
  /** Which of Unreal's diffs serves this class, and what these records are. */
  engine?: { tool: string; note: string; native: "window" | "details" | "text"; nativeAvailable?: boolean; nativeReason?: string };
  /** null: the version did not exist. */
  before?: AssetVersion | null;
  after?: AssetVersion | null;
  classChanged?: string;
  sections?: AssetDiffSection[];
  /** The engine's own text form of the diff where its diff is a text diff (a DataTable's CSV), as a unified diff. */
  textDiff?: { format: string; diff: string };
  /** blueprint(diff)'s structural summary, for a Blueprint. */
  blueprint?: Record<string, unknown>;
  recordCount?: number;
  /** Records past maxRecords, counted. */
  recordsDropped?: number;
  /** One line to decide on. */
  summary: string;
  /** Whether Unreal's own diff view can open this, and why not. */
  nativeDiff: { available: boolean; reason?: string };
}

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

/** The record's own words, with the values when it has them. */
export function describeRecord(r: AssetDiffRecord): string {
  const label = r.path ? (r.group && r.group !== r.path ? `${r.group} ${r.path}` : r.path) : r.displayString;
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
    const name = r.path ? (r.group && r.group !== r.path ? `${r.group} ${r.path}` : r.path) : line;
    if (shown.some((x) => x.startsWith(name))) continue;
    shown.push(line);
    if (shown.length === 2) break;
  }
  const total = records.length + (d.recordsDropped ?? 0);
  const rest = total - shown.length;
  return `${plural(total, "difference")}: ${shown.join("; ")}${rest > 0 ? `; and ${rest} more` : ""}${hiddenNote}`;
}

/** Whether Unreal's own diff view can open this item, and why not, as the bridge decided it. */
export function nativeDiffOf(d: Pick<AssetDiff, "state" | "engine">): AssetDiff["nativeDiff"] {
  if (d.state !== "ready" || !d.engine) return { available: false, reason: "The asset has not been diffed." };
  return d.engine.nativeAvailable === false ? { available: false, reason: d.engine.nativeReason ?? "Unreal cannot open a diff of this type." } : { available: true };
}

/** The bridge's answer as an AssetDiff, with the CSV export turned into a unified diff and the pictures mapped to ids. */
export function assetDiffFromBridge(
  raw: Json,
  key: string,
  picture: (side: "before" | "after", thumb: Json | undefined) => AssetThumbnail | undefined,
): AssetDiff {
  const version = (side: "before" | "after"): AssetVersion | null => {
    const v = raw[side];
    if (!v || typeof v !== "object") return null;
    const o = v as Json;
    const thumbnail = picture(side, o.thumbnail as Json | undefined);
    return {
      class: str(o.class) ?? "",
      ...(str(o.objectName) ? { objectName: str(o.objectName) } : {}),
      facts: Array.isArray(o.facts) ? (o.facts as AssetFact[]) : [],
      ...(thumbnail ? { thumbnail } : {}),
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
