/**
 * Compose multiple parsed .kant fragments into a single view.
 *
 * Priority model: fragments are supplied in ascending priority order.
 * Later fragments override earlier ones at matching paths. Missing
 * subtrees fall through; present subtrees replace. Field maps merge
 * (shallow) so overriding layers can set one field without wiping the
 * rest of a point.
 *
 * This is a deliberately simple subset of kantext's k-way merge.
 * Kantext's full composer does cryptographic sealing and preserves
 * layer provenance per fragment. We skip both for now; both will come
 * back when we route composition through the kantext binary.
 */

import type { KantPoint } from "./types.js";
import type { MeaningEntry, ParsedFragment } from "./parse.js";

export interface Layer {
  readonly priority: number;
  readonly fragment: ParsedFragment;
}

export interface ComposedView {
  readonly root: KantPoint;
  readonly meanings: Readonly<Record<string, MeaningEntry>>;
  readonly sources: ReadonlyArray<{ priority: number; source: string }>;
}

function mergePoints(a: KantPoint | undefined, b: KantPoint): KantPoint {
  if (!a) return b;
  const out: {
    meaning?: string;
    purpose?: string;
    category?: string;
    fields?: Record<string, NonNullable<KantPoint["fields"]>[string]>;
    children?: Record<string, KantPoint>;
  } = {
    meaning: b.meaning ?? a.meaning,
    purpose: b.purpose ?? a.purpose,
    category: b.category ?? a.category,
  };

  if (a.fields || b.fields) {
    out.fields = { ...(a.fields ?? {}), ...(b.fields ?? {}) };
  }

  const aChildren = a.children ?? {};
  const bChildren = b.children ?? {};
  const childKeys = new Set([...Object.keys(aChildren), ...Object.keys(bChildren)]);
  if (childKeys.size > 0) {
    const children: Record<string, KantPoint> = {};
    for (const k of childKeys) {
      children[k] = mergePoints(aChildren[k], bChildren[k] ?? {});
    }
    out.children = children;
  }
  return out;
}

/**
 * Compose fragments into a single rooted KantPoint at `/UE`.
 *
 * Each fragment's blocks attach as named children under the root.
 * Anchor names ("UE", "Registry") are mapped by rule:
 *   - A block named `UE` becomes the root itself (fields + children merge in).
 *   - Any other block becomes a child under the root with that name.
 * This matches our emission convention (root.kant declares `UE@UE:` at
 * the top; projectors emit `Registry@Registry:` etc.).
 */
export function compose(layers: readonly Layer[]): ComposedView {
  const ordered = [...layers].sort((a, b) => a.priority - b.priority);

  let root: KantPoint = {};
  const meanings: Record<string, MeaningEntry> = {};
  const sources: Array<{ priority: number; source: string }> = [];

  for (const layer of ordered) {
    sources.push({ priority: layer.priority, source: layer.fragment.source });
    for (const [name, entry] of Object.entries(layer.fragment.meanings)) {
      meanings[name] = entry;
    }
    for (const [blockName, block] of Object.entries(layer.fragment.blocks)) {
      if (blockName === "UE") {
        root = mergePoints(root, block);
      } else {
        root = mergePoints(root, { children: { [blockName]: block } });
      }
    }
  }

  return { root, meanings, sources };
}
