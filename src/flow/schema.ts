import { z } from "zod";
import { EngineConfigSchema } from "@db-lyon/flowkit";

export const FlowVersionSchema = z.object({
  version: z.literal(1),
});

export const FlowProjectSchema = z.object({
  name: z.string().optional(),
  engine: z.string().optional(),
}).optional();

export const GitSnapshotSchema = z.object({
  enabled: z.boolean().default(false),
  paths: z.array(z.string()).default(["Content", "Config"]),
  snapshot_dir: z.string().default(".ue-mcp/snapshot.git"),
  max_age_hours: z.number().default(24),
}).optional();

/**
 * A plugin entry in the user's ue-mcp.yml.
 * `name` is the npm package; `version` is optional and honored at resolve time.
 */
export const PluginEntrySchema = z.object({
  name: z.string().min(1),
  version: z.string().optional(),
});

export type PluginEntry = z.infer<typeof PluginEntrySchema>;

export const FlowConfigSchema = EngineConfigSchema.extend({
  "ue-mcp": FlowVersionSchema.optional(),
  project: FlowProjectSchema,
  git_snapshot: GitSnapshotSchema,
  plugins: z.array(PluginEntrySchema).default([]),
});

export type FlowConfig = z.infer<typeof FlowConfigSchema>;
