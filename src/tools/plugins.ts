import { z } from "zod";
import { categoryTool, type ToolDef, type PluginInfo } from "../types.js";

export const pluginsTool: ToolDef = categoryTool(
  "plugins",
  "Introspect npm-distributed plugins that contribute actions into other categories. Read-only.",
  {
    list: {
      description: "Every plugin loaded from ue-mcp.yml: name, version, prefix, status, and injected actions",
      handler: async (ctx) => {
        const all = ctx.getPlugins?.() ?? [];
        return {
          pluginCount: all.length,
          active: all.filter((p) => p.status === "active").length,
          plugins: all.map(summarise),
        };
      },
    },
    describe: {
      description: "Full detail for one plugin including knowledge files and flows. Params: name",
      handler: async (ctx, p) => {
        const target = p.name as string;
        const all = ctx.getPlugins?.() ?? [];
        const found = all.find((x) => x.name === target);
        if (!found) {
          return {
            error: `plugin '${target}' not found`,
            available: all.map((x) => x.name),
          };
        }
        return detail(found);
      },
    },
  },
  undefined,
  {
    name: z.string().optional().describe("Plugin npm package name (describe action)"),
  },
);

function summarise(p: PluginInfo): Record<string, unknown> {
  return {
    name: p.name,
    version: p.version,
    actionPrefix: p.actionPrefix,
    status: p.status,
    statusReason: p.statusReason,
    categories: Object.keys(p.injected),
    injectedActions: Object.values(p.injected).reduce((acc, arr) => acc + arr.length, 0),
    flows: p.flows.length,
    uePluginDependency: p.uePluginDependency,
    uePluginPresent: p.uePluginPresent,
  };
}

function detail(p: PluginInfo): Record<string, unknown> {
  return {
    name: p.name,
    version: p.version,
    actionPrefix: p.actionPrefix,
    status: p.status,
    statusReason: p.statusReason,
    minServerVersion: p.minServerVersion,
    uePluginDependency: p.uePluginDependency,
    uePluginPresent: p.uePluginPresent,
    pkgDir: p.pkgDir,
    manifestPath: p.manifestPath,
    injected: p.injected,
    knowledge: p.knowledge,
    flows: p.flows,
    tasks: p.tasks,
  };
}
