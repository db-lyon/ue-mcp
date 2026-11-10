/**
 * Which part of a graph a diagram draws: an optional focus with a depth, then
 * a node cap so a graph of several hundred nodes stays readable.
 *
 * The cap keeps nodes in breadth-first order from where the graph starts (the
 * focus, else its entry nodes and initial states, else nodes nothing points
 * at), so what survives is the part a reader would look at first, not
 * whichever nodes the reader happened to list first. What was dropped is said
 * twice: a note node in the drawing and a sentence in `notes`.
 */
import type { DiagramModel, DiagramNode } from "./model.js";

export const DEFAULT_MAX_NODES = 80;
export const MAX_NODES_LIMIT = 500;
export const DEFAULT_FOCUS_DEPTH = 3;
export const TRUNCATED_NODE_ID = "__truncated";

export interface SelectOptions {
  /** A node id, title or state name. Exact matches win over substrings. */
  focus?: string;
  /** Hops from the focus, following edges either way. Default 3. */
  depth?: number;
  maxNodes?: number;
}

/** The nodes `focus` names: exact id, then exact label, then a substring of either. */
export function findFocus(nodes: DiagramNode[], focus: string): DiagramNode[] {
  const want = focus.trim().toLowerCase();
  if (!want) return [];
  const real = nodes.filter((n) => n.role !== "note");
  const tiers: Array<(n: DiagramNode) => boolean> = [
    (n) => n.id.toLowerCase() === want,
    (n) => n.label.toLowerCase() === want,
    (n) => n.label.toLowerCase().includes(want) || n.id.toLowerCase().includes(want),
  ];
  for (const tier of tiers) {
    const hit = real.filter(tier);
    if (hit.length) return hit;
  }
  return [];
}

function neighbours(model: DiagramModel): Map<string, string[]> {
  const adj = new Map<string, string[]>();
  for (const n of model.nodes) adj.set(n.id, []);
  for (const e of model.edges) {
    adj.get(e.from)?.push(e.to);
    adj.get(e.to)?.push(e.from);
  }
  // A group's anchor and its members are one place in the drawing: reaching
  // one reaches the other, so a focus on a parent state shows its children.
  const anchors = new Map(model.nodes.filter((n) => n.anchorOf).map((n) => [n.anchorOf!, n.id]));
  for (const n of model.nodes) {
    const anchor = n.group ? anchors.get(n.group) : undefined;
    if (anchor && anchor !== n.id) {
      adj.get(anchor)?.push(n.id);
      adj.get(n.id)?.push(anchor);
    }
  }
  return adj;
}

/** Breadth-first from `seeds`, then from each unvisited node in model order. */
function reachOrder(model: DiagramModel, seeds: string[], adj: Map<string, string[]>, maxDepth = Infinity): string[] {
  const order: string[] = [];
  const seen = new Set<string>();
  const run = (start: string[]) => {
    let frontier = [...new Set(start)].filter((id) => adj.has(id) && !seen.has(id));
    for (const id of frontier) seen.add(id);
    let depth = 0;
    while (frontier.length) {
      order.push(...frontier);
      if (depth >= maxDepth) break;
      const next: string[] = [];
      for (const id of frontier) {
        for (const other of adj.get(id) ?? []) {
          if (seen.has(other)) continue;
          seen.add(other);
          next.push(other);
        }
      }
      frontier = next;
      depth++;
    }
  };
  run(seeds);
  if (maxDepth === Infinity) for (const n of model.nodes) if (!seen.has(n.id)) run([n.id]);
  return order;
}

function startNodes(model: DiagramModel): string[] {
  const seeds = [...(model.initial ?? [])];
  for (const n of model.nodes) if (n.role === "entry") seeds.push(n.id);
  if (seeds.length) return seeds;
  const pointedAt = new Set(model.edges.map((e) => e.to));
  return model.nodes.filter((n) => !pointedAt.has(n.id)).map((n) => n.id);
}

/** Restrict `model` to the focused part and the node cap. Pure: the input is not changed. */
export function selectView(model: DiagramModel, options: SelectOptions = {}): DiagramModel {
  const maxNodes = Math.min(MAX_NODES_LIMIT, Math.max(1, Math.floor(options.maxNodes ?? DEFAULT_MAX_NODES)));
  const adj = neighbours(model);
  const notes = [...model.notes];
  const total = model.nodes.length;

  let order: string[];
  if (options.focus !== undefined && options.focus.trim() !== "") {
    const hits = findFocus(model.nodes, options.focus);
    if (!hits.length) {
      const sample = model.nodes.filter((n) => n.role !== "note").slice(0, 12).map((n) => n.label);
      throw new Error(
        `focus '${options.focus}' matches no node in ${model.title}. Pass a node title, id or state name` +
        (sample.length ? `, such as: ${sample.join(", ")}` : "") + ".",
      );
    }
    const depth = Math.max(0, Math.floor(options.depth ?? DEFAULT_FOCUS_DEPTH));
    // Focusing a parent state means its whole box: every member, at any depth, seeds the walk.
    const groupIds = new Set(hits.filter((n) => n.anchorOf).map((n) => n.anchorOf!));
    for (let grew = true; grew;) {
      grew = false;
      for (const g of model.groups) if (g.parent && groupIds.has(g.parent) && !groupIds.has(g.id)) { groupIds.add(g.id); grew = true; }
    }
    const seeds = [...hits.map((n) => n.id), ...model.nodes.filter((n) => n.group && groupIds.has(n.group)).map((n) => n.id)];
    order = reachOrder(model, seeds, adj, depth);
    notes.push(`Focused on ${hits.map((n) => `'${n.label}'`).join(", ")}: ${order.length} of ${total} nodes within ${depth} hop${depth === 1 ? "" : "s"}.`);
  } else {
    order = reachOrder(model, startNodes(model), adj);
  }

  const kept = new Set(order.slice(0, maxNodes));
  const hidden = order.length - kept.size;
  const nodes = model.nodes.filter((n) => kept.has(n.id));
  const edges = model.edges.filter((e) => kept.has(e.from) && kept.has(e.to));

  const groupById = new Map(model.groups.map((g) => [g.id, g]));
  const liveGroups = new Set<string>();
  for (const n of nodes) {
    let g = n.group ? groupById.get(n.group) : undefined;
    while (g && !liveGroups.has(g.id)) {
      liveGroups.add(g.id);
      g = g.parent ? groupById.get(g.parent) : undefined;
    }
  }
  const groups = model.groups.filter((g) => liveGroups.has(g.id));

  if (hidden > 0) {
    nodes.push({ id: TRUNCATED_NODE_ID, role: "note", label: `${hidden} more node${hidden === 1 ? "" : "s"} not shown` });
    notes.push(
      `Showing ${kept.size} of ${order.length} nodes. Pass focus (a node title, id or state name) with depth to draw another part, ` +
      `or raise maxNodes (up to ${MAX_NODES_LIMIT}).`,
    );
  }

  return {
    ...model,
    nodes,
    edges,
    groups,
    initial: model.initial?.filter((id) => kept.has(id)),
    notes,
  };
}
