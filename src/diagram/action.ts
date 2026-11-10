/**
 * The `diagram` action each graph category carries.
 *
 * It is an in-process handler rather than a format option on the readers:
 * the readers are spec'd bridge actions whose parameters are the C++
 * registration's, recorded and held to it by the handler-spec tests, and a
 * diagram is drawn here, not in the editor. So the action calls the reader
 * over the bridge, turns the answer into a DiagramModel (adapters.ts), and
 * renders that. The bridge sees an ordinary read.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { z } from "zod";
import type { HandlerActionSpec, ToolContext } from "../core/types.js";
import type { DiagramModel } from "./model.js";
import { toMermaid } from "./mermaid.js";
import { svgToPng } from "./png.js";
import { DEFAULT_MAX_NODES, MAX_NODES_LIMIT, selectView } from "./select.js";
import { toSvg } from "./svg.js";

export const DIAGRAM_FORMATS = ["json", "mermaid", "svg", "png"] as const;
export type DiagramFormat = (typeof DIAGRAM_FORMATS)[number];

/** What the diagram parameters do, said once in each action's description. */
export const DIAGRAM_HELP =
  `format: mermaid (default; Mermaid text), svg (an image file drawn by ue-mcp, its path returned), png (the same image rasterised, ` +
  `returned inline as image content and written to a file), json (the normalised nodes and edges). focus: a node title, id or state ` +
  `name to centre on, with depth hops either way (default 3). maxNodes caps the drawing (default ${DEFAULT_MAX_NODES}, max ` +
  `${MAX_NODES_LIMIT}); what is cut is named in a note node and in notes. outputPath: where svg, png or mermaid is written ` +
  `(default <Project>/Saved/UE_MCP/Diagrams/).`;

/**
 * The shared parameter shapes, for the keys a category does not declare
 * already. A key a spec'd reader declares (asset's format and maxNodes) keeps
 * its spec'd shape, which the handler-spec tests hold it to.
 */
export function diagramSchema(taken: Record<string, unknown>): Record<string, z.ZodType> {
  const shapes: Record<string, z.ZodType> = {
    format: z.string().optional().describe("diagram: json | mermaid | svg | png (default mermaid)"),
    focus: z.string().optional().describe("diagram: node title, id or state name to centre the drawing on"),
    depth: z.number().int().nonnegative().optional().describe("diagram: hops from focus, either direction (default 3)"),
    maxNodes: z.number().int().positive().optional().describe(`diagram: node cap (default ${DEFAULT_MAX_NODES}, max ${MAX_NODES_LIMIT})`),
    outputPath: z.string().optional().describe("diagram: file the svg, png or mermaid output is written to"),
  };
  return Object.fromEntries(Object.entries(shapes).filter(([key]) => !(key in taken)));
}

/** A bridge read whose refusal is thrown, so the handler fails with the reader's own message. */
export async function readFromBridge(ctx: ToolContext, method: string, params: Record<string, unknown>): Promise<Record<string, unknown>> {
  const answer = await ctx.bridge.call(method, params, ctx.callTimeoutMs);
  const body = (answer && typeof answer === "object" ? answer : {}) as Record<string, unknown>;
  if (body.success === false) throw new Error(typeof body.error === "string" ? body.error : `${method} failed`);
  return body;
}

function parseFormat(value: unknown): DiagramFormat {
  if (value === undefined || value === null || value === "") return "mermaid";
  const format = String(value).toLowerCase();
  if ((DIAGRAM_FORMATS as readonly string[]).includes(format)) return format as DiagramFormat;
  throw new Error(`format '${String(value)}' is not one of ${DIAGRAM_FORMATS.join(", ")}`);
}

const optionalNumber = (v: unknown): number | undefined => (typeof v === "number" && Number.isFinite(v) ? v : undefined);

function slug(title: string): string {
  return title.replace(/[^A-Za-z0-9]+/g, "_").replace(/^_+|_+$/g, "").slice(0, 80) || "diagram";
}

/** Where a diagram file goes: outputPath as given (relative to the default folder), else the default folder. */
export function diagramFilePath(ctx: Pick<ToolContext, "project" | "session">, title: string, ext: string, outputPath?: string): string {
  const projectDir = ctx.session?.project.projectDir ?? ctx.project?.projectDir;
  const dir = projectDir ? path.join(projectDir, "Saved", "UE_MCP", "Diagrams") : path.join(os.tmpdir(), "ue-mcp-diagrams");
  if (outputPath && outputPath.trim()) return path.resolve(dir, outputPath.trim());
  return path.join(dir, `${slug(title)}.${ext}`);
}

function writeFile(file: string, data: string | Buffer): void {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, data);
}

/**
 * Inline image content for a rendered PNG, through the result's `media`
 * entries, which dispatch turns into MCP image content (splitMedia). This is
 * the one place a diagram becomes an inline image; an inline sizing option
 * shared with the capture actions would hook in here.
 */
function inlinePng(png: Buffer, width: number, height: number, file: string): Record<string, unknown> {
  return { type: "image", mimeType: "image/png", data: png.toString("base64"), path: file, width, height };
}

/** Select, render and (for file formats) write a model, as the action's answer. */
export async function finishDiagram(ctx: Pick<ToolContext, "project" | "session">, model: DiagramModel, params: Record<string, unknown>): Promise<Record<string, unknown>> {
  const format = parseFormat(params.format);
  const view = selectView(model, {
    focus: typeof params.focus === "string" ? params.focus : undefined,
    depth: optionalNumber(params.depth),
    maxNodes: optionalNumber(params.maxNodes),
  });
  const shown = view.nodes.filter((n) => n.role !== "note").length;
  const outputPath = typeof params.outputPath === "string" ? params.outputPath : undefined;
  const result: Record<string, unknown> = {
    format,
    diagramType: view.kind === "state" ? "stateDiagram-v2" : "flowchart",
    title: view.title,
    nodeCount: shown,
    edgeCount: view.edges.length,
    totalNodeCount: model.nodes.length,
    truncated: shown < model.nodes.length,
    ...(view.notes.length ? { notes: view.notes } : {}),
  };

  if (format === "json") {
    return { ...result, graph: { direction: view.direction, nodes: view.nodes, edges: view.edges, groups: view.groups, ...(view.initial ? { initial: view.initial } : {}) } };
  }
  if (format === "mermaid") {
    const mermaid = toMermaid(view);
    if (outputPath) {
      const file = diagramFilePath(ctx, view.title, "mmd", outputPath);
      writeFile(file, mermaid);
      result.path = file;
    }
    return { ...result, mermaid };
  }

  const drawn = toSvg(view);
  if (format === "svg") {
    const file = diagramFilePath(ctx, view.title, "svg", outputPath);
    writeFile(file, drawn.svg);
    return { ...result, path: file, width: drawn.width, height: drawn.height };
  }

  const png = await svgToPng(drawn.svg, drawn.width, drawn.height);
  if (!png.ok) {
    const file = diagramFilePath(ctx, view.title, "svg", outputPath?.replace(/\.png$/i, ".svg"));
    writeFile(file, drawn.svg);
    return { ...result, format: "svg", path: file, width: drawn.width, height: drawn.height, pngUnavailable: png.reason };
  }
  const file = diagramFilePath(ctx, view.title, "png", outputPath);
  writeFile(file, png.png);
  return { ...result, path: file, width: png.width, height: png.height, media: [inlinePng(png.png, png.width, png.height, file)] };
}

export interface DiagramActionOptions {
  description: string;
  /** Reads the graph over the bridge and adapts it. */
  build: (ctx: ToolContext, params: Record<string, unknown>) => Promise<DiagramModel>;
}

export function diagramAction(options: DiagramActionOptions): HandlerActionSpec {
  return {
    kind: "handler",
    effect: "read",
    description: options.description,
    handler: async (ctx, params) => finishDiagram(ctx, await options.build(ctx, params), params),
  };
}
