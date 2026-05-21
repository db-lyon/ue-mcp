import { z } from "zod";
import { EngineConfigSchema } from "@db-lyon/flowkit";

/**
 * The `ue-mcp:` block at the top of ue-mcp.yml. Historically just had
 * `version: 1`. As of 1.0.30 also hosts the project-level config that
 * used to live in `.ue-mcp.json` (killed for one-config-format
 * consistency). Everything in this block is tracked — user-machine-only
 * state (e.g. installedHooks for the feedback prompt hook) lives in
 * `~/.ue-mcp/state.json`, not here.
 *
 *   ue-mcp:
 *     version: 1
 *     contentRoots: ["/Game/"]
 *     disable: ["gas"]
 *     http: { enabled: false, port: 7723 }
 *     feedback: { mode: "interactive" }
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
  feedback: z
    .object({
      mode: z.enum(["interactive", "auto-approve", "defer"]).optional(),
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
});

export type FlowConfig = z.infer<typeof FlowConfigSchema>;
