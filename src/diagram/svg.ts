/**
 * An SVG drawing of a diagram model, laid out and drawn in this process.
 *
 * Layout is dagre's layered (Sugiyama) layout, which also routes each edge
 * around the nodes between its ends and places its label. Text is measured
 * rather than guessed: node labels are set in a monospace face, so a label's
 * width is its length times a fixed advance, and the box always fits it.
 *
 * The output is deterministic for a given model, which is what lets the tests
 * snapshot its structure, and it carries its own background and fonts list so
 * it reads the same in a browser, an image viewer or a PNG rasteriser.
 */
import dagre from "@dagrejs/dagre";
import type { DiagramModel, DiagramNode, EdgeKind } from "./model.js";
import { clip } from "./model.js";
import { EDGE_STYLE, ROLE_STYLE, THEME } from "./theme.js";

const LABEL_SIZE = 13;
const DETAIL_SIZE = 11;
const EDGE_LABEL_SIZE = 10.5;
/** JetBrains Mono's advance is 0.6em; the fallbacks are within a few percent. */
const MONO_ADVANCE = 0.6;
const PAD_X = 14;
const PAD_Y = 9;
const LINE_GAP = 4;
const LABEL_WRAP = 34;
const DETAIL_WRAP = 46;
const MAX_DETAIL_LINES = 4;
const MARGIN = 28;
const HEADER = 62;
const NOTE_LINE = 18;
const START_SIZE = 14;

export interface SvgDiagram {
  svg: string;
  width: number;
  height: number;
}

export function escapeXml(text: string): string {
  return text.replace(/[&<>"']/g, (ch) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[ch]!);
}

const r1 = (v: number) => Math.round(v * 10) / 10;
const monoWidth = (text: string, size: number) => text.length * size * MONO_ADVANCE;

/** Break `text` at spaces into lines of at most `width` characters, at most `max` lines. */
export function wrap(text: string, width: number, max: number): string[] {
  const words = text.replace(/\s+/g, " ").trim().split(" ").filter(Boolean);
  const lines: string[] = [];
  let line = "";
  for (const word of words) {
    const piece = word.length > width ? clip(word, width) : word;
    if (!line) line = piece;
    else if (line.length + 1 + piece.length <= width) line += ` ${piece}`;
    else { lines.push(line); line = piece; }
  }
  if (line) lines.push(line);
  if (lines.length > max) {
    const kept = lines.slice(0, max);
    kept[max - 1] = clip(`${kept[max - 1]} ${lines.slice(max).join(" ")}`, width);
    return kept;
  }
  return lines.length ? lines : [""];
}

interface TextLine { text: string; size: number; color: string; weight: number }

function nodeLines(n: DiagramNode): TextLine[] {
  const style = ROLE_STYLE[n.role];
  const lines: TextLine[] = wrap(n.label, LABEL_WRAP, 2).map((text) => ({ text, size: LABEL_SIZE, color: style.text, weight: 600 }));
  if (n.detail) {
    const detail = n.detail.split("\n").flatMap((l) => wrap(l, DETAIL_WRAP, 2)).slice(0, MAX_DETAIL_LINES);
    for (const text of detail) lines.push({ text, size: DETAIL_SIZE, color: THEME.muted, weight: 400 });
  }
  return lines;
}

function boxOf(lines: TextLine[]): { width: number; height: number } {
  const width = Math.max(...lines.map((l) => monoWidth(l.text, l.size))) + PAD_X * 2;
  const height = lines.reduce((h, l) => h + l.size, 0) + LINE_GAP * (lines.length - 1) + PAD_Y * 2;
  return { width: Math.max(width, 56), height };
}

/** A smooth path through dagre's routed points (Catmull-Rom as cubic Beziers). */
function curve(points: Array<{ x: number; y: number }>): string {
  if (points.length < 2) return "";
  let d = `M${r1(points[0].x)},${r1(points[0].y)}`;
  for (let i = 0; i < points.length - 1; i++) {
    const p0 = points[i - 1] ?? points[i];
    const p1 = points[i];
    const p2 = points[i + 1];
    const p3 = points[i + 2] ?? p2;
    const c1 = { x: p1.x + (p2.x - p0.x) / 6, y: p1.y + (p2.y - p0.y) / 6 };
    const c2 = { x: p2.x - (p3.x - p1.x) / 6, y: p2.y - (p3.y - p1.y) / 6 };
    d += ` C${r1(c1.x)},${r1(c1.y)} ${r1(c2.x)},${r1(c2.y)} ${r1(p2.x)},${r1(p2.y)}`;
  }
  return d;
}

function textBlock(lines: TextLine[], cx: number, top: number, anchor: "middle" | "start" = "middle"): string {
  let y = top;
  return lines.map((l) => {
    y += l.size;
    const out = `<text x="${r1(cx)}" y="${r1(y - l.size * 0.2)}" text-anchor="${anchor}" font-size="${l.size}" font-weight="${l.weight}" fill="${l.color}">${escapeXml(l.text)}</text>`;
    y += LINE_GAP;
    return out;
  }).join("");
}

const EDGE_KINDS: EdgeKind[] = ["exec", "data", "transition", "child"];

export function toSvg(model: DiagramModel): SvgDiagram {
  const g = new dagre.graphlib.Graph({ multigraph: true, compound: true });
  g.setGraph({
    rankdir: model.direction,
    nodesep: model.direction === "LR" ? 22 : 30,
    ranksep: model.direction === "LR" ? 64 : 50,
    edgesep: 14,
    marginx: 0,
    marginy: 0,
  });
  g.setDefaultEdgeLabel(() => ({}));

  const lineCache = new Map<string, TextLine[]>();
  for (const n of model.nodes) {
    const lines = nodeLines(n);
    lineCache.set(n.id, lines);
    g.setNode(`n:${n.id}`, boxOf(lines));
  }
  for (const grp of model.groups) g.setNode(`g:${grp.id}`, { width: 0, height: 0 });
  for (const grp of model.groups) if (grp.parent) g.setParent(`g:${grp.id}`, `g:${grp.parent}`);
  for (const n of model.nodes) if (n.group) g.setParent(`n:${n.id}`, `g:${n.group}`);

  // Start markers: one for the diagram, and one inside each group that names
  // the member it enters first.
  const starts: Array<{ id: string; to: string }> = [];
  if (model.kind === "state") {
    for (const to of model.initial ?? []) starts.push({ id: `s:root:${to}`, to });
    for (const grp of model.groups) {
      if (!grp.initial || !model.nodes.some((n) => n.id === grp.initial)) continue;
      // A group drawn with its own state node enters its first member from
      // that node, which keeps the parent above its children.
      const anchor = model.nodes.find((n) => n.anchorOf === grp.id);
      if (anchor) g.setEdge(`n:${anchor.id}`, `n:${grp.initial}`, { width: 0, height: 0, kind: "child" }, `enter:${grp.id}`);
      else starts.push({ id: `s:${grp.id}`, to: grp.initial });
    }
    for (const s of starts) {
      g.setNode(s.id, { width: START_SIZE, height: START_SIZE });
      const member = model.nodes.find((n) => n.id === s.to);
      if (member?.group && s.id !== `s:root:${s.to}`) g.setParent(s.id, `g:${member.group}`);
      g.setEdge(s.id, `n:${s.to}`, { width: 0, height: 0 }, s.id);
    }
  }

  model.edges.forEach((e, i) => {
    const label = e.label ? clip(e.label, 40) : "";
    g.setEdge(`n:${e.from}`, `n:${e.to}`, {
      width: label ? monoWidth(label, EDGE_LABEL_SIZE) + 8 : 0,
      height: label ? EDGE_LABEL_SIZE + 6 : 0,
      labelpos: "c",
      label,
      kind: e.kind,
    }, `e${i}`);
  });

  dagre.layout(g);
  const layout = g.graph();
  const graphW = Math.max(1, layout.width ?? 1);
  const graphH = Math.max(1, layout.height ?? 1);

  const subtitle = `${model.kind === "state" ? "state diagram" : "flowchart"} · ${model.nodes.filter((n) => n.role !== "note").length} nodes · ${model.edges.length} edges`;
  const noteLines = model.notes.flatMap((n) => wrap(n, Math.max(60, Math.floor((graphW + MARGIN * 2) / (12 * MONO_ADVANCE))), 3));
  const titleW = clip(model.title, 90).length * 17 * 0.58;
  const width = Math.ceil(Math.max(graphW + MARGIN * 2, titleW + MARGIN * 2, 360));
  const height = Math.ceil(HEADER + graphH + MARGIN * 2 + noteLines.length * NOTE_LINE);
  const ox = (width - graphW) / 2;
  const oy = HEADER + MARGIN;

  const parts: string[] = [];
  parts.push(`<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}" font-family="${escapeXml(THEME.monoFont)}">`);
  parts.push(`<title>${escapeXml(model.title)}</title>`);
  parts.push("<defs>");
  for (const kind of EDGE_KINDS) {
    parts.push(`<marker id="arrow-${kind}" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="${EDGE_STYLE[kind].stroke}"/></marker>`);
  }
  parts.push("</defs>");
  parts.push(`<rect x="0.5" y="0.5" width="${width - 1}" height="${height - 1}" rx="10" fill="${THEME.background}" stroke="${THEME.frame}"/>`);
  parts.push(`<text x="${MARGIN}" y="34" font-family="${escapeXml(THEME.sansFont)}" font-size="17" font-weight="700" fill="${THEME.text}">${escapeXml(clip(model.title, 90))}</text>`);
  parts.push(`<text x="${MARGIN}" y="53" font-size="11" fill="${THEME.orange}">${escapeXml(subtitle)}</text>`);

  // Groups, outermost first so inner boxes draw over outer ones.
  const depthOf = (id: string): number => {
    const grp = model.groups.find((x) => x.id === id);
    return grp?.parent ? 1 + depthOf(grp.parent) : 0;
  };
  const groups = [...model.groups].sort((a, b) => depthOf(a.id) - depthOf(b.id));
  parts.push('<g class="groups">');
  for (const grp of groups) {
    const box = g.node(`g:${grp.id}`);
    if (!box || box.x === undefined || box.y === undefined) continue;
    const x = ox + box.x - box.width / 2 - 8;
    const y = oy + box.y - box.height / 2 - 18;
    parts.push(`<g data-group="${escapeXml(grp.id)}"><rect x="${r1(x)}" y="${r1(y)}" width="${r1(box.width + 16)}" height="${r1(box.height + 26)}" rx="8" fill="${THEME.groupFill}" stroke="${THEME.groupStroke}"/>` +
      `<text x="${r1(x + 10)}" y="${r1(y + 13)}" font-size="10.5" font-weight="600" fill="${THEME.groupLabel}">${escapeXml(clip(grp.label, 60))}</text></g>`);
  }
  parts.push("</g>");

  parts.push('<g class="edges" fill="none">');
  for (const ref of g.edges()) {
    const edge = g.edge(ref) as { points?: Array<{ x: number; y: number }>; label?: string; kind?: EdgeKind; x?: number; y?: number };
    const kind: EdgeKind = edge.kind ?? "transition";
    const style = EDGE_STYLE[kind];
    const points = (edge.points ?? []).map((p) => ({ x: ox + p.x, y: oy + p.y }));
    parts.push(`<path d="${curve(points)}" stroke="${style.stroke}" stroke-width="${style.width}"${style.dashed ? ' stroke-dasharray="5 4"' : ""} marker-end="url(#arrow-${kind})"/>`);
  }
  parts.push("</g>");

  parts.push('<g class="edge-labels">');
  for (const ref of g.edges()) {
    const edge = g.edge(ref) as { label?: string; x?: number; y?: number };
    if (!edge.label || edge.x === undefined || edge.y === undefined) continue;
    const w = monoWidth(edge.label, EDGE_LABEL_SIZE) + 8;
    const cx = ox + edge.x;
    const cy = oy + edge.y;
    parts.push(`<rect x="${r1(cx - w / 2)}" y="${r1(cy - EDGE_LABEL_SIZE / 2 - 3)}" width="${r1(w)}" height="${r1(EDGE_LABEL_SIZE + 6)}" rx="3" fill="${THEME.background}" fill-opacity="0.92"/>` +
      `<text x="${r1(cx)}" y="${r1(cy + EDGE_LABEL_SIZE * 0.35)}" text-anchor="middle" font-size="${EDGE_LABEL_SIZE}" fill="${THEME.muted}">${escapeXml(edge.label)}</text>`);
  }
  parts.push("</g>");

  parts.push('<g class="nodes">');
  for (const s of starts) {
    const box = g.node(s.id);
    if (box?.x === undefined || box.y === undefined) continue;
    parts.push(`<circle data-start="${escapeXml(s.to)}" cx="${r1(ox + box.x)}" cy="${r1(oy + box.y)}" r="${START_SIZE / 2}" fill="${THEME.text}"/>`);
  }
  for (const n of model.nodes) {
    const box = g.node(`n:${n.id}`);
    if (box?.x === undefined || box.y === undefined) continue;
    const style = ROLE_STYLE[n.role];
    const x = ox + box.x - box.width / 2;
    const y = oy + box.y - box.height / 2;
    const rx = n.role === "entry" || n.role === "exit" ? Math.min(box.height / 2, 16) : n.role === "state" || n.role === "composite" ? 12 : 6;
    parts.push(`<g data-node="${escapeXml(n.id)}" data-role="${n.role}">` +
      `<rect x="${r1(x)}" y="${r1(y)}" width="${r1(box.width)}" height="${r1(box.height)}" rx="${rx}" fill="${style.fill}" stroke="${style.stroke}" stroke-width="1.5"${style.dashed ? ' stroke-dasharray="4 3"' : ""}/>` +
      textBlock(lineCache.get(n.id)!, ox + box.x, y + PAD_Y) + "</g>");
  }
  parts.push("</g>");

  if (noteLines.length) {
    let y = HEADER + graphH + MARGIN * 2 - 4;
    parts.push('<g class="notes">');
    for (const line of noteLines) {
      y += NOTE_LINE;
      parts.push(`<text x="${MARGIN}" y="${r1(y - 4)}" font-size="12" fill="${THEME.faint}">${escapeXml(line)}</text>`);
    }
    parts.push("</g>");
  }
  parts.push("</svg>");
  return { svg: parts.join("\n") + "\n", width, height };
}
