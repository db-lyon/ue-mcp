/**
 * Per-plugin runtime configuration and flow-group logic.
 *
 * A plugin (e.g. `ue-mcp-recipes`) ships many flows keyed by a domain prefix
 * (`niagara_fire`, `pcg_scatter_surface`, `gas_ability_scaffold`). Users toggle
 * whole domains on/off without touching a source-tracked file: the toggles live
 * in the `ue-mcp.pluginConfig.<slug>.groups` map, valid in any config layer
 * (user-global `~/.ue-mcp/config.yml`, tracked `ue-mcp.yml`, per-machine
 * `ue-mcp.local.yml`) and merged by the existing deep-merge cascade.
 *
 * This module is pure and disk-free so it can be unit-tested directly and
 * shared by both the server-side loader (which filters flows) and the CLI
 * (which renders the toggle menu).
 */

/** Runtime config for one plugin, read from `ue-mcp.pluginConfig.<slug>`. */
export interface PluginRuntimeConfig {
  /** Group enable/disable. Absent group = enabled (opt-out model). */
  groups?: Record<string, boolean>;
  [k: string]: unknown;
}

/** The whole `ue-mcp.pluginConfig` map, keyed by plugin slug. */
export type PluginConfigMap = Record<string, PluginRuntimeConfig>;

/**
 * A plugin's short config key: the npm package name minus the conventional
 * `ue-mcp-` prefix. `ue-mcp-recipes` -> `recipes`. Matches the publish slug and
 * the registry resolver, so `ue-mcp plugin config recipes` and the config key
 * line up. A package without the prefix keeps its full name.
 */
export function pluginSlug(pkgName: string): string {
  return pkgName.replace(/^ue-mcp-/, "");
}

/**
 * The group a flow belongs to: an explicit `group:` on the flow entry, else the
 * name prefix before the first underscore. `niagara_fire` -> `niagara`. A name
 * with no underscore is its own group.
 */
export function flowGroup(flowName: string, explicit?: string): string {
  if (explicit && explicit.trim()) return explicit.trim();
  const i = flowName.indexOf("_");
  return i > 0 ? flowName.slice(0, i) : flowName;
}

/** Distinct, sorted groups across a plugin's flows (for menus / --list-groups). */
export function deriveGroups(flows: Record<string, { group?: string }>): string[] {
  const set = new Set<string>();
  for (const [name, def] of Object.entries(flows)) {
    set.add(flowGroup(name, def?.group));
  }
  return [...set].sort();
}

/**
 * Is a group enabled given a plugin's runtime config? Opt-out model: a group is
 * enabled unless explicitly set to `false`. No config at all = everything on.
 */
export function isGroupEnabled(cfg: PluginRuntimeConfig | undefined, group: string): boolean {
  const g = cfg?.groups;
  if (!g) return true;
  return g[group] !== false;
}

/**
 * Look up one plugin's runtime config from the merged `pluginConfig` map,
 * tolerating either the slug key (`recipes`, the canonical form the CLI writes)
 * or the full package name (`ue-mcp-recipes`, if a user hand-keyed it).
 */
export function runtimeConfigFor(
  pluginConfig: PluginConfigMap | undefined,
  pkgName: string,
): PluginRuntimeConfig | undefined {
  if (!pluginConfig) return undefined;
  return pluginConfig[pluginSlug(pkgName)] ?? pluginConfig[pkgName];
}

/**
 * Partition a plugin's flows into enabled/disabled by group, given its runtime
 * config. Used by the loader to drop disabled-group flows before they reach the
 * flow registry.
 */
export function partitionFlowsByGroup(
  flows: Record<string, { group?: string }>,
  cfg: PluginRuntimeConfig | undefined,
): { enabled: string[]; disabled: string[] } {
  const enabled: string[] = [];
  const disabled: string[] = [];
  for (const [name, def] of Object.entries(flows)) {
    const group = flowGroup(name, def?.group);
    (isGroupEnabled(cfg, group) ? enabled : disabled).push(name);
  }
  return { enabled, disabled };
}
