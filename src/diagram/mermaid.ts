/**
 * Mermaid text for a diagram model: `flowchart` for exec and data graphs,
 * `stateDiagram-v2` for state machines and StateTree.
 *
 * Node ids are derived from the reader's own ids (GUIDs, node names, state
 * paths), so the same graph read twice gives the same text and a diff of two
 * diagrams shows what changed. Every label is quoted and every character
 * Mermaid would read as syntax is written as an entity code.
 */
import type { DiagramGroup, DiagramModel, DiagramNode, NodeRole } from "./model.js";
import { ROLE_STYLE } from "./theme.js";

const ENTITY: Record<string, string> = {
  "#": "#35;",
  '"': "#quot;",
  ";": "#59;",
  "<": "#lt;",
  ">": "#gt;",
  "|": "#124;",
  "&": "#amp;",
  "`": "#96;",
  "{": "#123;",
  "}": "#125;",
};

/** One label line, safe inside Mermaid's double quotes and after a transition's colon. */
export function escapeMermaidText(text: string): string {
  return text.replace(/[\r\n\t]+/g, " ").replace(/[#";<>|&`{}]/g, (ch) => ENTITY[ch]);
}

/** A label of one or more lines, lines joined with Mermaid's line break. */
function labelText(lines: Array<string | undefined>): string {
  return lines
    .flatMap((l) => (l ?? "").split("\n"))
    .filter((l) => l.trim() !== "")
    .map(escapeMermaidText)
    .join("<br/>");
}

/**
 * Mermaid ids for every node and group: a prefix, then the reader's id with
 * anything outside [A-Za-z0-9_] folded to `_`. Two ids that fold to the same
 * text get a numeric suffix in model order, which keeps the result stable.
 */
export function mermaidIds(model: DiagramModel): { node: Map<string, string>; group: Map<string, string> } {
  const used = new Set<string>();
  const make = (prefix: string, raw: string) => {
    const base = `${prefix}${raw.replace(/[^A-Za-z0-9_]/g, "_").replace(/_+/g, "_").replace(/^_|_$/g, "") || "x"}`;
    let id = base;
    for (let i = 2; used.has(id); i++) id = `${base}_${i}`;
    used.add(id);
    return id;
  };
  const node = new Map<string, string>();
  const group = new Map<string, string>();
  for (const n of model.nodes) node.set(n.id, make("n_", n.id));
  for (const g of model.groups) {
    // In a state diagram the composite state IS its anchor node, so they share an id.
    const anchor = model.kind === "state" ? model.nodes.find((n) => n.anchorOf === g.id) : undefined;
    group.set(g.id, anchor ? node.get(anchor.id)! : make("g_", g.id));
  }
  return { node, group };
}

function frontmatter(model: DiagramModel): string[] {
  const out = ["---", `title: ${JSON.stringify(model.title)}`, "---"];
  for (const note of model.notes) out.push(`%% ${note.replace(/[\r\n]+/g, " ")}`);
  return out;
}

function classDefs(nodes: DiagramNode[], ids: Map<string, string>, skip?: (n: DiagramNode) => boolean): string[] {
  const byRole = new Map<NodeRole, string[]>();
  for (const n of nodes) {
    if (skip?.(n)) continue;
    const list = byRole.get(n.role) ?? [];
    list.push(ids.get(n.id)!);
    byRole.set(n.role, list);
  }
  // Prefixed: `default` would restyle every node, and `note` and `state` are keywords.
  const out: string[] = [];
  for (const [role, members] of byRole) {
    const s = ROLE_STYLE[role];
    out.push(`  classDef ue_${role} fill:${s.fill},stroke:${s.stroke},color:${s.text}${s.dashed ? ",stroke-dasharray:4 3" : ""}`);
    out.push(`  class ${members.join(",")} ue_${role}`);
  }
  return out;
}

const SHAPE: Record<NodeRole, [string, string]> = {
  default: ['["', '"]'],
  entry: ['(["', '"])'],
  exit: ['(["', '"])'],
  branch: ['{"', '"}'],
  function: ['[["', '"]]'],
  collapsed: ['[["', '"]]'],
  variable: ['("', '")'],
  parameter: ['>"', '"]'],
  output: ['[/"', '"\\]'],
  state: ['("', '")'],
  composite: ['[["', '"]]'],
  task: ['["', '"]'],
  decorator: ['{{"', '"}}'],
  note: ['["', '"]'],
};

function flowchart(model: DiagramModel): string {
  const ids = mermaidIds(model);
  const lines = [...frontmatter(model), `flowchart ${model.direction}`];
  const childrenOf = new Map<string | undefined, DiagramGroup[]>();
  for (const g of model.groups) {
    const list = childrenOf.get(g.parent) ?? [];
    list.push(g);
    childrenOf.set(g.parent, list);
  }
  const emit = (groupId: string | undefined, indent: string) => {
    for (const n of model.nodes.filter((x) => x.group === groupId)) {
      const [open, close] = SHAPE[n.role];
      lines.push(`${indent}${ids.node.get(n.id)}${open}${labelText([n.label, n.detail])}${close}`);
    }
    for (const g of childrenOf.get(groupId) ?? []) {
      lines.push(`${indent}subgraph ${ids.group.get(g.id)}["${labelText([g.label])}"]`);
      lines.push(`${indent}  direction ${model.direction}`);
      emit(g.id, `${indent}  `);
      lines.push(`${indent}end`);
    }
  };
  emit(undefined, "  ");
  for (const e of model.edges) {
    const arrow = e.kind === "data" ? "-.->" : "-->";
    const label = e.label ? `|"${labelText([e.label])}"|` : "";
    lines.push(`  ${ids.node.get(e.from)} ${arrow}${label} ${ids.node.get(e.to)}`);
  }
  lines.push(...classDefs(model.nodes, ids.node));
  return lines.join("\n") + "\n";
}

function stateDiagram(model: DiagramModel): string {
  const ids = mermaidIds(model);
  const lines = [...frontmatter(model), "stateDiagram-v2", `  direction ${model.direction}`];
  const childrenOf = new Map<string | undefined, DiagramGroup[]>();
  for (const g of model.groups) {
    const list = childrenOf.get(g.parent) ?? [];
    list.push(g);
    childrenOf.set(g.parent, list);
  }
  const anchored = new Set(model.groups.map((g) => g.id));
  const isComposite = (n: DiagramNode) => !!n.anchorOf && anchored.has(n.anchorOf);

  const emit = (groupId: string | undefined, indent: string) => {
    const group = groupId ? model.groups.find((g) => g.id === groupId) : undefined;
    if (group?.initial && ids.node.has(group.initial)) lines.push(`${indent}[*] --> ${ids.node.get(group.initial)}`);
    for (const n of model.nodes.filter((x) => x.group === groupId && !isComposite(x))) {
      const id = ids.node.get(n.id)!;
      lines.push(`${indent}state "${labelText([n.label])}" as ${id}`);
      for (const line of (n.detail ?? "").split("\n").filter((l) => l.trim())) lines.push(`${indent}${id} : ${labelText([line])}`);
    }
    for (const g of childrenOf.get(groupId) ?? []) {
      lines.push(`${indent}state "${labelText([g.label])}" as ${ids.group.get(g.id)} {`);
      emit(g.id, `${indent}  `);
      lines.push(`${indent}}`);
    }
  };
  emit(undefined, "  ");
  for (const id of model.initial ?? []) if (ids.node.has(id)) lines.push(`  [*] --> ${ids.node.get(id)}`);
  for (const e of model.edges) {
    const label = e.label ? ` : ${labelText([e.label])}` : "";
    lines.push(`  ${ids.node.get(e.from)} --> ${ids.node.get(e.to)}${label}`);
  }
  // Mermaid does not style composite states by class, so only leaves are listed.
  lines.push(...classDefs(model.nodes, ids.node, isComposite));
  return lines.join("\n") + "\n";
}

export function toMermaid(model: DiagramModel): string {
  return model.kind === "state" ? stateDiagram(model) : flowchart(model);
}
