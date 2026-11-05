/**
 * Plugins whose functionality now ships in ue-mcp core. The loader skips them,
 * `plugin install` refuses them, and the registry catalog hides them.
 * `plugin remove` keeps working so a project can drop the old native module.
 */
export interface SupersededPlugin {
  /** The core tool that replaces the plugin. */
  builtIn: string;
  /** The ue-mcp version the core tool first shipped in. */
  since: string;
  /** The plugin's native UE plugin folder under the project's Plugins/. */
  uePluginName?: string;
}

export const SUPERSEDED_PLUGINS: Readonly<Record<string, SupersededPlugin>> = {
  "pie-studio": { builtIn: "pie", since: "1.4.11", uePluginName: "PIE_Studio" },
};

/** The superseded entry for a package name, if any. */
export function supersededByName(name: string): [string, SupersededPlugin] | undefined {
  return isSuperseded(name) ? [name, SUPERSEDED_PLUGINS[name]] : undefined;
}

/** The superseded entry whose native UE plugin folder matches, if any. */
export function supersededByUePlugin(uePluginName: string | undefined): [string, SupersededPlugin] | undefined {
  if (!uePluginName) return undefined;
  for (const [name, s] of Object.entries(SUPERSEDED_PLUGINS)) {
    if (s.uePluginName === uePluginName) return [name, s];
  }
  return undefined;
}

export function isSuperseded(name: string): boolean {
  return Object.prototype.hasOwnProperty.call(SUPERSEDED_PLUGINS, name);
}

/** The one message every surface prints for a superseded plugin. */
export function supersededMessage(
  name: string,
  s: SupersededPlugin,
  outcome: "loaded" | "installed" = "loaded",
): string {
  const removal = s.uePluginName
    ? `Remove it with \`ue-mcp plugin remove ${name}\`, which also deletes Plugins/${s.uePluginName} from the project.`
    : `Remove it with \`ue-mcp plugin remove ${name}\`.`;
  return `${name} is built into ue-mcp since ${s.since} as the \`${s.builtIn}\` tool; this plugin was not ${outcome}. ${removal}`;
}
