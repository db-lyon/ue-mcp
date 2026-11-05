import * as fs from "node:fs";
import { GuardsSchema } from "../flow/guard-schema.js";
import { FlowStepKeySchema } from "../flow/schema.js";
import * as path from "node:path";
import { z } from "zod";
import yaml from "js-yaml";

/**
 * Schema for ue-mcp.plugin.yml, the author-side declaration shipped inside
 * each plugin npm package. Resolved from node_modules; never authored by the
 * end user.
 */

const ParamTypeSchema = z.enum(["string", "number", "boolean", "object", "array"]);

export type ManifestParamType = z.infer<typeof ParamTypeSchema>;

const SchemaFieldSchema = z.object({
  // Omitted means "any JSON value". Some handler params genuinely have no one
  // type: a property write coerced through UE's ImportText takes a number, a
  // bool, a string or an enum name with equal validity, and forcing the author
  // to pick one of them would lie about the surface. A list of type names
  // narrows that to a union when the author does know the alternatives.
  type: z.union([ParamTypeSchema, z.array(ParamTypeSchema).nonempty()]).optional(),
  required: z.boolean().optional(),
  description: z.string().optional(),
});

export type ManifestSchemaField = z.infer<typeof SchemaFieldSchema>;

/**
 * What a plugin action does to the addressed editor, in the plugin author's
 * own words. Optional because manifests written before this existed are still
 * valid; a manifest that stays silent has its effect inferred from the action
 * name, which is a guess and is recorded as one.
 */
export const ActionEffectSchema = z.enum(["read", "mutate", "unknown"]);

const InjectActionSchema = z.object({
  task: z.string().min(1),
  description: z.string().optional(),
  effect: ActionEffectSchema.optional(),
  schema: z.record(z.string(), SchemaFieldSchema).optional(),
});

export type ManifestInjectAction = z.infer<typeof InjectActionSchema>;

/**
 * One action contributed by a `provides:` entry. Same shape as inject but
 * lives under a plugin-owned category, not a built-in one, so action names
 * are NOT prefixed - the category itself is the namespace.
 */
const ProvidedActionSchema = z.object({
  task: z.string().min(1),
  description: z.string().optional(),
  effect: ActionEffectSchema.optional(),
  schema: z.record(z.string(), SchemaFieldSchema).optional(),
});

export type ManifestProvidedAction = z.infer<typeof ProvidedActionSchema>;

const ProvidedCategorySchema = z.object({
  description: z.string().optional(),
  actions: z.record(z.string(), ProvidedActionSchema),
});

export type ManifestProvidedCategory = z.infer<typeof ProvidedCategorySchema>;

const TaskEntrySchema = z.object({
  class_path: z.string().min(1),
  description: z.string().optional(),
});

const FlowStepEntrySchema = z.object({
  task: z.string().optional(),
  flow: z.string().optional(),
  options: z.record(z.string(), z.unknown()).optional(),
  retries: z.number().optional(),
  retryDelay: z.number().optional(),
  retryOn: z.string().optional(),
});

const FlowEntrySchema = z.object({
  description: z.string(),
  rollback_on_failure: z.boolean().optional(),
  // Toggle group this flow belongs to. Absent = derived from the flow name's
  // prefix before the first underscore (`niagara_fire` -> `niagara`). Users
  // enable/disable whole groups via `ue-mcp.pluginConfig.<slug>.groups`.
  group: z.string().optional(),
  // zod 4 reports a bad key as "Invalid key in record"; surface the key schema's own message.
  steps: z.record(FlowStepKeySchema, FlowStepEntrySchema, {
    error: (issue) => (issue.code === "invalid_key" ? issue.issues[0]?.message : undefined),
  }),
});

/**
 * Native UE C++ module that ships with this plugin. When present, the CLI
 * copies `source/` into the user's project Plugins/ at install time and
 * tracks the deposit for clean uninstall. The plugin's StartupModule is
 * expected to register handlers via UEMCP::RegisterExternalHandler (see
 * MCPHandlerRegistration.h shipped under the bridge's Public/).
 *
 *   nativeModule:
 *     uePluginName: Sample_Recorder
 *     minBridgeApi: 1
 *     source: ue/Plugins/Sample_Recorder
 *     handlers:
 *       record_start: { description: "..." }
 */
const NativeModuleSchema = z.object({
  uePluginName: z.string().min(1),
  minBridgeApi: z.number().int().nonnegative(),
  source: z.string().min(1),
  supportedEngineVersions: z.array(z.string().min(1)).default([]),
  // Category to surface this module's handlers into as MCP actions. When it
  // names a built-in category, each handler `h` is injected as
  // `<category>(action="<actionPrefix>_h")`. When it names a new (non-built-in)
  // category, that category is provisioned as a top-level tool the plugin owns
  // and handlers surface unprefixed: `<category>(action="h")`. Either way the
  // action dispatches to the bare bridge method `h`. Omitted → handlers stay
  // registered on the bridge but exposed as no action (back-compat).
  category: z.string().min(1).optional(),
  // Summary shown on a provisioned (new) category's tool. Ignored when
  // `category` is a built-in.
  categoryDescription: z.string().min(1).optional(),
  // Package-relative path to the parameter contracts the module's handlers
  // register with in C++ (UEMCP::RegisterExternalHandler with params), as
  // `ue-mcp plugin record-specs` records them (#1282). When set, every surfaced
  // handler takes its parameters from its spec and each call is validated
  // against it; a handler with no recorded spec is not surfaced.
  specs: z.string().min(1).optional(),
  handlers: z
    .record(
      z.string(), z.object({
        description: z.string().optional(),
        timeoutSeconds: z.number().positive().optional(),
        // Param declarations for the surfaced action. Required for any param
        // the handler reads: the MCP SDK strips keys absent from the action's
        // schema before they reach the bridge.
        schema: z.record(z.string(), SchemaFieldSchema).optional(),
      }),
    )
    .default({}),
});

/**
 * The plugin's own words for its registry listing, published by
 * `ue-mcp plugin publish` so a release can update the listing in the same
 * commit as the code. Every field is optional; an absent one keeps the row's.
 */
const RegistryListingSchema = z.object({
  tagline: z.string().min(4).max(140).optional(),
  description: z.string().max(4000).optional(),
  category: z.string().min(1).optional(),
  tags: z.array(z.string().min(1).max(24)).max(12).optional(),
});

export type ManifestRegistryListing = z.infer<typeof RegistryListingSchema>;

export const PluginManifestSchema = z.object({
  actionPrefix: z.string().regex(/^[a-z][a-z0-9_]*$/, {
    message: "actionPrefix must be a lowercase identifier (letters, digits, underscore; must start with a letter)",
  }),
  minServerVersion: z.string().optional(),
  uePluginDependency: z.string().optional(),
  inject: z.record(z.string(), z.record(z.string(), InjectActionSchema)).default({}),
  provides: z
    .record(
      z.string().regex(/^[a-z][a-z0-9_]*$/, {
        message:
          "provided category name must be a lowercase identifier (letters, digits, underscore; must start with a letter)",
      }),
      ProvidedCategorySchema,
    )
    .default({}),
  nativeModule: NativeModuleSchema.optional(),
  knowledge: z.record(z.string(), z.string()).default({}),
  tasks: z.record(z.string(), TaskEntrySchema).default({}),
  flows: z.record(z.string(), FlowEntrySchema).default({}),
  guards: GuardsSchema,
  registry: RegistryListingSchema.optional(),
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

/** One manifest subtree removed to keep the rest of the plugin loadable. */
export interface DroppedUnit {
  /** Dotted manifest path, e.g. `nativeModule.handlers.actor_set`. */
  path: string;
  /** The validation error that cost it, already rendered for a log line. */
  reason: string;
}

export interface ManifestParseResult {
  manifest: PluginManifest;
  manifestPath: string;
  /** Units dropped by salvage. Empty when the manifest validated as authored. */
  dropped: DroppedUnit[];
}

/**
 * The smallest manifest subtree a validation error can be confined to. An
 * error inside one of these costs that unit and nothing else, so a single
 * malformed handler no longer takes its whole category off the surface.
 * Returns null for anything structural (actionPrefix, nativeModule.source,
 * a manifest that is not an object at all), which still fails the plugin.
 */
function salvageableUnit(issuePath: ReadonlyArray<string | number>): Array<string | number> | null {
  const p = issuePath;
  if (p[0] === "nativeModule" && p[1] === "handlers" && p.length >= 3) return p.slice(0, 3);
  if (p[0] === "inject" && p.length >= 3) return p.slice(0, 3);
  if (p[0] === "inject" && p.length === 2) return p.slice(0, 2);
  if (p[0] === "provides" && p[2] === "actions" && p.length >= 4) return p.slice(0, 4);
  if (p[0] === "provides" && p.length >= 2) return p.slice(0, 2);
  if (p[0] === "flows" && p.length >= 2) return p.slice(0, 2);
  if (p[0] === "tasks" && p.length >= 2) return p.slice(0, 2);
  if (p[0] === "knowledge" && p.length >= 2) return p.slice(0, 2);
  if (p[0] === "registry") return ["registry"];
  return null;
}

/** Delete `path` from a nested plain object. No-op if the path is not there. */
function deleteAt(root: unknown, path: Array<string | number>): void {
  let cur: unknown = root;
  for (const key of path.slice(0, -1)) {
    if (typeof cur !== "object" || cur === null) return;
    cur = (cur as Record<string | number, unknown>)[key];
  }
  if (typeof cur !== "object" || cur === null) return;
  delete (cur as Record<string | number, unknown>)[path[path.length - 1]];
}

/**
 * Validate a manifest, dropping individually-salvageable units rather than
 * rejecting the whole plugin. Throws (with Zod's full issue dump, as before)
 * when any error lands outside a droppable unit.
 */
export function parseManifest(raw: unknown): { manifest: PluginManifest; dropped: DroppedUnit[] } {
  const dropped: DroppedUnit[] = [];
  let current = raw;
  // Each pass removes at least one unit, so the loop is bounded by the number
  // of units in the manifest; the cap is a backstop against a pathological
  // schema where pruning cannot make progress.
  for (let pass = 0; pass < 100; pass++) {
    const result = PluginManifestSchema.safeParse(current);
    if (result.success) return { manifest: result.data, dropped };

    // Keyed by the rendered path only for dedup; the pruning itself uses the
    // segment array, since a segment (a task name, say) may contain a dot.
    const units = new Map<string, { unit: Array<string | number>; reason: string }>();
    for (const issue of result.error.issues) {
      const unit = salvageableUnit(issue.path as Array<string | number>);
      if (!unit) throw result.error;
      const key = unit.join(".");
      if (!units.has(key)) {
        units.set(key, { unit, reason: `${issue.path.map(String).join(".")}: ${issue.message}` });
      }
    }
    if (units.size === 0) throw result.error;

    current = structuredClone(current);
    for (const [key, { unit, reason }] of units) {
      deleteAt(current, unit);
      dropped.push({ path: key, reason });
    }
  }
  // Unreachable in practice: pruning that never converges means the schema
  // rejects the pruned shape too, which is a bug in the schema, not the input.
  throw new Error("manifest validation did not converge after 100 salvage passes");
}

export function loadManifest(pkgDir: string): ManifestParseResult {
  const manifestPath = findManifestPath(pkgDir);
  if (!manifestPath) {
    throw new Error(`ue-mcp.plugin.yml not found in ${pkgDir}`);
  }
  const raw = yaml.load(fs.readFileSync(manifestPath, "utf-8")) as unknown;
  const { manifest, dropped } = parseManifest(raw);
  return { manifest, manifestPath, dropped };
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
    let zod = zodForDeclaredType(def.type, def.required === true);
    if (def.description) zod = zod.describe(def.description);
    if (!def.required) zod = zod.optional();
    out[key] = zod;
  }
  return out;
}

function zodForParamType(type: ManifestParamType): z.ZodType {
  switch (type) {
    case "string": return z.string();
    case "number": return z.number();
    case "boolean": return z.boolean();
    case "object": return z.record(z.string(), z.unknown());
    case "array": return z.array(z.unknown());
  }
}

/**
 * Zod for one declared `type`, which is a single name, a list of names, or
 * absent. Absent compiles to `z.any()`, which accepts undefined on its own, so
 * a required untyped param carries a refinement that puts the presence check
 * back: `.isOptional()` is what the SDK reads to build the JSON Schema
 * `required` list.
 */
function zodForDeclaredType(
  type: ManifestSchemaField["type"],
  required: boolean,
): z.ZodType {
  if (type === undefined) {
    return required
      ? z.any().refine((v) => v !== undefined, { message: "Required" })
      : z.any();
  }
  if (Array.isArray(type)) {
    const members = type.map(zodForParamType);
    return members.length === 1
      ? members[0]
      : z.union(members as [z.ZodType, z.ZodType, ...z.ZodType[]]);
  }
  return zodForParamType(type);
}

/**
 * Compute the prefixed action name as it appears on the injected category
 * tool: `<actionPrefix>_<bareAction>`.
 */
export function prefixedActionName(prefix: string, action: string): string {
  return `${prefix}_${action}`;
}

/**
 * Every `<category>.<action>` the manifest adds, as a client calls it: inject
 * actions prefixed, provided actions bare, and native handlers prefixed in a
 * built-in category or bare in the category the plugin provisions.
 */
export function manifestActionNames(manifest: PluginManifest, builtInCategories: ReadonlySet<string>): string[] {
  const names: string[] = [];
  for (const [category, actions] of Object.entries(manifest.inject)) {
    for (const a of Object.keys(actions)) names.push(`${category}.${prefixedActionName(manifest.actionPrefix, a)}`);
  }
  for (const [category, spec] of Object.entries(manifest.provides)) {
    for (const a of Object.keys(spec.actions)) names.push(`${category}.${a}`);
  }
  const native = manifest.nativeModule;
  if (native?.category) {
    const injected = builtInCategories.has(native.category);
    for (const h of Object.keys(native.handlers)) {
      names.push(`${native.category}.${injected ? prefixedActionName(manifest.actionPrefix, h) : h}`);
    }
  }
  return names;
}
