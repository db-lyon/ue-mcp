import * as fs from "node:fs";
import * as path from "node:path";
import { z } from "zod";
import yaml from "js-yaml";

/**
 * Schema for ue-mcp.plugin.yml, the author-side declaration shipped inside
 * each plugin npm package. Resolved from node_modules; never authored by the
 * end user.
 */

const SchemaFieldSchema = z.object({
  type: z.enum(["string", "number", "boolean", "object", "array"]),
  required: z.boolean().optional(),
  description: z.string().optional(),
});

export type ManifestSchemaField = z.infer<typeof SchemaFieldSchema>;

const InjectActionSchema = z.object({
  task: z.string().min(1),
  description: z.string().optional(),
  schema: z.record(SchemaFieldSchema).optional(),
});

export type ManifestInjectAction = z.infer<typeof InjectActionSchema>;

const TaskEntrySchema = z.object({
  class_path: z.string().min(1),
  description: z.string().optional(),
});

const FlowStepEntrySchema = z.object({
  task: z.string().optional(),
  flow: z.string().optional(),
  options: z.record(z.unknown()).optional(),
  retries: z.number().optional(),
  retryDelay: z.number().optional(),
  retryOn: z.string().optional(),
});

const FlowEntrySchema = z.object({
  description: z.string(),
  rollback_on_failure: z.boolean().optional(),
  steps: z.record(FlowStepEntrySchema),
});

export const PluginManifestSchema = z.object({
  actionPrefix: z.string().regex(/^[a-z][a-z0-9_]*$/, {
    message: "actionPrefix must be a lowercase identifier (letters, digits, underscore; must start with a letter)",
  }),
  minServerVersion: z.string().optional(),
  uePluginDependency: z.string().optional(),
  inject: z.record(z.record(InjectActionSchema)).default({}),
  knowledge: z.record(z.string()).default({}),
  tasks: z.record(TaskEntrySchema).default({}),
  flows: z.record(FlowEntrySchema).default({}),
});

export type PluginManifest = z.infer<typeof PluginManifestSchema>;

/**
 * Locate the manifest file inside a plugin package directory. Convention is
 * `ue-mcp.plugin.yml`, but `.yaml` is accepted as a fallback.
 */
export function findManifestPath(pkgDir: string): string | null {
  const candidates = ["ue-mcp.plugin.yml", "ue-mcp.plugin.yaml"];
  for (const c of candidates) {
    const full = path.join(pkgDir, c);
    if (fs.existsSync(full)) return full;
  }
  return null;
}

export interface ManifestParseResult {
  manifest: PluginManifest;
  manifestPath: string;
}

export function loadManifest(pkgDir: string): ManifestParseResult {
  const manifestPath = findManifestPath(pkgDir);
  if (!manifestPath) {
    throw new Error(`ue-mcp.plugin.yml not found in ${pkgDir}`);
  }
  const raw = yaml.load(fs.readFileSync(manifestPath, "utf-8")) as unknown;
  const manifest = PluginManifestSchema.parse(raw);
  return { manifest, manifestPath };
}

/**
 * Compile a manifest schema field map into a Zod object schema. Used to merge
 * plugin action params into the host category tool's schema.
 */
export function compileSchemaFields(
  fields: Record<string, ManifestSchemaField> | undefined,
): Record<string, z.ZodType> {
  if (!fields) return {};
  const out: Record<string, z.ZodType> = {};
  for (const [key, def] of Object.entries(fields)) {
    let zod: z.ZodType;
    switch (def.type) {
      case "string": zod = z.string(); break;
      case "number": zod = z.number(); break;
      case "boolean": zod = z.boolean(); break;
      case "object": zod = z.record(z.unknown()); break;
      case "array": zod = z.array(z.unknown()); break;
    }
    if (def.description) zod = zod.describe(def.description);
    if (!def.required) zod = zod.optional();
    out[key] = zod;
  }
  return out;
}

/**
 * Compute the prefixed action name as it appears on the injected category
 * tool: `<actionPrefix>_<bareAction>`.
 */
export function prefixedActionName(prefix: string, action: string): string {
  return `${prefix}_${action}`;
}
