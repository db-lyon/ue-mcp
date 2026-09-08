import { z } from "zod";
import { EngineConfigSchema } from "@db-lyon/flowkit";
import { GuardsSchema } from "./guard-schema.js";

/**
 * The `ue-mcp:` block at the top of ue-mcp.yml. Hosts project-level config
 * that every collaborator should share. Per-user-per-device preferences
 * (e.g. feedback approval mode) and machine-only state (e.g. installedHooks)
 * live in `~/.ue-mcp/state.json`, not here.
 *
 *   ue-mcp:
 *     version: 1
 *     contentRoots: ["/Game/"]
 *     disable: ["gas"]
 *     http: { enabled: false, port: 7723 }
 */
export const FlowVersionSchema = z.object({
  version: z.literal(1),
  contentRoots: z.array(z.string()).optional(),
  disable: z.array(z.string()).optional(),
  http: z
    .object({
      enabled: z.boolean().optional(),
      port: z.number().int().min(1).max(65535).optional(),
      host: z.string().optional(),
    })
    .optional(),
}).passthrough();

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
  guards: GuardsSchema,
});

export type FlowConfig = z.infer<typeof FlowConfigSchema>;
