/**
 * The one graph shape every diagram is drawn from.
 *
 * Each graph reader answers in its own JSON (a Blueprint summary's execEdges,
 * a material's connectedExpressionIndex, a StateTree's nested children). The
 * adapters in adapters.ts turn each into this model, and the Mermaid and SVG
 * renderers only ever read this model, so a renderer change is tested once and
 * a new reader needs only an adapter.
 *
 * Everything here is plain data: no editor, no I/O.
 */

/** flowchart for exec and data graphs, state for state machines and StateTree. */
export type DiagramKind = "flowchart" | "state";

export type DiagramDirection = "TB" | "LR";

/** What a node is, which picks its shape and colour. */
export type NodeRole =
  | "default"
  | "entry"
  | "exit"
  | "branch"
  | "function"
  | "collapsed"
  | "variable"
  | "parameter"
  | "output"
  | "state"
  | "composite"
  | "task"
  | "decorator"
  | "note";

export interface DiagramNode {
  /** The reader's own id for the node (a GUID, a node name, a state path). Stable across reads. */
  id: string;
  label: string;
  /** A second, smaller line: a class, an animation asset, a task list. */
  detail?: string;
  role: NodeRole;
  /** The group this node is drawn inside. */
  group?: string;
  /**
   * This node stands for the group with the same id: a StateTree parent state
   * is both a box of children and a state transitions can target.
   */
  anchorOf?: string;
}

export type EdgeKind = "exec" | "data" | "transition" | "child";

export interface DiagramEdge {
  from: string;
  to: string;
  label?: string;
  kind: EdgeKind;
}

export interface DiagramGroup {
  id: string;
  label: string;
  parent?: string;
  /** State diagrams: the member entered first when the group is entered. */
  initial?: string;
}

export interface DiagramModel {
  kind: DiagramKind;
  title: string;
  direction: DiagramDirection;
  nodes: DiagramNode[];
  edges: DiagramEdge[];
  groups: DiagramGroup[];
  /** State diagrams: the states entered first, drawn from a start marker. */
  initial?: string[];
  /** Said under the diagram: what was left out, and how to see it. */
  notes: string[];
}

/** Collapse whitespace, then cut to `max` characters with an ellipsis. */
export function clip(text: string, max: number): string {
  const flat = text.replace(/\s+/g, " ").trim();
  return flat.length > max ? `${flat.slice(0, Math.max(1, max - 1)).trimEnd()}…` : flat;
}
