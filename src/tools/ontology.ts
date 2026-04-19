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
    "Projected kantext ontology: emit .kant layers from live ue-mcp state. Query via HQL lands when the kantext sidecar is wired.",
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
    },
  );
}
