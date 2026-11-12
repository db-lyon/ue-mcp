/**
 * The ue-mcp look, shared by the Mermaid class definitions and the SVG
 * renderer: the dark ground, orange and blue accents and type of the overview
 * image (scripts/assets/overview-image/overview.svg).
 */
import type { EdgeKind, NodeRole } from "./model.js";

export const THEME = {
  background: "#0f1217",
  frame: "#2b3038",
  groupFill: "#141920",
  groupStroke: "#2b3038",
  groupLabel: "#a1a1aa",
  text: "#f5f5f4",
  muted: "#a1a1aa",
  faint: "#8b8b8b",
  orange: "#e8720c",
  blue: "#60a5fa",
  monoFont: "'JetBrains Mono', ui-monospace, SFMono-Regular, Menlo, Consolas, monospace",
  sansFont: "'Plus Jakarta Sans', 'Segoe UI', Helvetica, Arial, sans-serif",
} as const;

export interface RoleStyle {
  fill: string;
  stroke: string;
  text: string;
  /** Dashed outline, for things attached to a node rather than steps of the graph. */
  dashed?: boolean;
}

export const ROLE_STYLE: Record<NodeRole, RoleStyle> = {
  default: { fill: "#181d25", stroke: "#3a414c", text: THEME.text },
  entry: { fill: "#2a1b0e", stroke: THEME.orange, text: THEME.text },
  exit: { fill: "#122019", stroke: "#34d399", text: THEME.text },
  branch: { fill: "#1f1a2b", stroke: "#a78bfa", text: THEME.text },
  function: { fill: "#132033", stroke: THEME.blue, text: THEME.text },
  collapsed: { fill: "#132033", stroke: THEME.blue, text: THEME.text, dashed: true },
  variable: { fill: "#161b22", stroke: "#4b5563", text: THEME.muted },
  parameter: { fill: "#1c1a12", stroke: "#facc15", text: THEME.text },
  output: { fill: "#2a1b0e", stroke: THEME.orange, text: THEME.text },
  state: { fill: "#132033", stroke: THEME.blue, text: THEME.text },
  composite: { fill: "#132033", stroke: THEME.blue, text: THEME.text },
  task: { fill: "#181d25", stroke: "#3a414c", text: THEME.text },
  decorator: { fill: "#1f1a2b", stroke: "#a78bfa", text: THEME.text, dashed: true },
  note: { fill: "#0f1217", stroke: THEME.faint, text: THEME.muted, dashed: true },
};

export const EDGE_STYLE: Record<EdgeKind, { stroke: string; dashed: boolean; width: number }> = {
  exec: { stroke: "#e5e7eb", dashed: false, width: 1.6 },
  data: { stroke: "#6b7280", dashed: true, width: 1.2 },
  transition: { stroke: THEME.blue, dashed: false, width: 1.5 },
  child: { stroke: "#9ca3af", dashed: false, width: 1.3 },
};
