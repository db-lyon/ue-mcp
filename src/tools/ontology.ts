import { z } from "zod";
import { categoryTool, type ToolDef, type ToolContext } from "../types.js";
import { McpError, ErrorCode } from "../errors.js";
import type { OntologyRegistry, ProjectorEvent } from "../ontology/index.js";

const VALID_EVENTS: ReadonlyArray<ProjectorEvent> = [
  "startup",
  "handler-registry-changed",
  "module-loaded",
  "hot-reload",
  "asset-registry-changed",
  "gameplay-tag-changed",
  "flow-completed",
  "manual",
];

export function createOntologyTool(registry: OntologyRegistry): ToolDef {
  return categoryTool(
    "ontology",
    "Kantext-shaped ontology: project live ue-mcp state into .kant layers, compose kernel + projected + repo-local, query with path selectors.",
    {
      project_all: {
        description: "Run every registered projector and write fragments to the ontology cache",
        handler: async (_ctx: ToolContext) => {
          const results = registry.projectAll();
          return {
            outputDir: registry.outputDirectory,
            projectionCount: results.length,
            results,
          };
        },
      },
      compose: {
        description: "Parse and compose all layers (kernel + projected + repo-local) into a single view",
        handler: async (_ctx: ToolContext) => {
          const view = registry.composeView();
          return {
            layerCount: view.layerCount,
            sources: view.sources,
            meaningCount: Object.keys(view.meanings).length,
            rootChildren: Object.keys(view.root.children ?? {}),
          };
        },
      },
      query: {
        description: "Evaluate a path selector against the composed ontology. Params: selector (e.g. '/UE/Mediation/Registry/Tools/**/Actions/*@classification=destructive')",
        handler: async (_ctx: ToolContext, params: Record<string, unknown>) => {
          const selector = params.selector as string;
          if (!selector) {
            throw new McpError(ErrorCode.INVALID_PARAMS, "Missing required parameter 'selector'");
          }
          const result = registry.query(selector);
          return {
            selector: result.selector,
            matchCount: result.matches.length,
            matches: result.matches.map((m) => ({
              path: m.path,
              meaning: m.point.meaning,
              purpose: m.point.purpose,
              fields: m.point.fields,
            })),
          };
        },
      },
      project_by_event: {
        description: "Run projectors subscribed to a specific event. Params: event",
        handler: async (_ctx: ToolContext, params: Record<string, unknown>) => {
          const event = params.event as ProjectorEvent;
          if (!VALID_EVENTS.includes(event)) {
            throw new McpError(
              ErrorCode.INVALID_PARAMS,
              `Unknown event '${event}'. Valid: ${VALID_EVENTS.join(", ")}`,
            );
          }
          const results = registry.projectByEvent(event);
          return {
            event,
            outputDir: registry.outputDirectory,
            projectionCount: results.length,
            results,
          };
        },
      },
      list_projectors: {
        description: "List registered projectors with their base path and trigger events",
        handler: async (_ctx: ToolContext) => {
          return {
            projectorCount: registry.projectorCount,
            projectors: registry.listProjectors(),
          };
        },
      },
      list_layers: {
        description: "List emitted .kant fragments in the ontology cache",
        handler: async (_ctx: ToolContext) => {
          return {
            outputDir: registry.outputDirectory,
            layers: registry.listLayers(),
          };
        },
      },
    },
    undefined,
    {
      event: z
        .string()
        .optional()
        .describe(`Projector event for project_by_event (${VALID_EVENTS.join("|")})`),
      selector: z
        .string()
        .optional()
        .describe("Path selector for query action (e.g. '/UE/Mediation/Registry/Tools/**')"),
    },
  );
}
