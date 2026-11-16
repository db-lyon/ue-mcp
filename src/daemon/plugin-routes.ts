// @session independent: Lists and changes the plugins of the one project the daemon serves. No editor is involved.
/**
 * Managing a project's ue-mcp plugins over the daemon's HTTP API, so a client
 * such as an in-editor panel can browse the registry and install, update or
 * remove plugins without a terminal: the plugins.* entries of DAEMON_ROUTES.
 *
 * Install, uninstall and update call extensions/plugin-install.ts, the library
 * `ue-mcp plugin` runs, in this process, one at a time; `output` is the lines
 * that command would have printed. A change takes effect when the server
 * restarts, which the result says.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { loadManifest } from "../extensions/manifest.js";
import { findInstalledPackage } from "../extensions/resolver.js";
import { readPluginsList } from "../extensions/plugins-list.js";
import { loadRegistryCatalog, type RegistryPlugin } from "../extensions/registry-catalog.js";
import {
  captureLog,
  findPluginProject,
  installPlugin,
  uninstallPlugin,
  updatePlugins,
  type BuiltInCategories,
  type PluginOpLog,
} from "../extensions/plugin-install.js";
import type { RouteHandlers, RouteResult } from "./router.js";

export interface InstalledPlugin {
  name: string;
  version: string | null;
  /** The version ue-mcp.yml pins, when it pins one. */
  pinned: string | null;
  status: "ok" | "missing" | "invalid";
  /** Categories the plugin adds actions to. */
  categories: string[];
  /** Why the manifest failed, or which of its parts were dropped. */
  problems: string[];
}

/** A row of GET /v1/plugins/catalog: the registry's own row. */
export type CatalogPlugin = RegistryPlugin;

export interface PluginCommandResult {
  ok: boolean;
  exitCode: number | null;
  /** What the command printed, for the client to show. */
  output: string;
  /** Plugin changes take effect when the server restarts. */
  restartRequired: boolean;
}

/** The plugin changes the routes make, each for the project in `projectDir`. Tests hand in their own. */
export interface PluginOps {
  install(projectDir: string, name: string, version: string | undefined, log: PluginOpLog): Promise<unknown>;
  uninstall(projectDir: string, name: string, log: PluginOpLog): Promise<unknown>;
  update(projectDir: string, name: string | undefined, log: PluginOpLog): Promise<unknown>;
}

/** ALL_TOOLS, loaded the first time a change needs it rather than with the daemon. */
const builtIns = async (): Promise<BuiltInCategories> => (await import("../tools.js")).ALL_TOOLS;

export const pluginOps: PluginOps = {
  install: async (projectDir, name, version, log) =>
    installPlugin(findPluginProject(projectDir), name, { version, builtIns: await builtIns(), log }),
  uninstall: (projectDir, name, log) => uninstallPlugin(findPluginProject(projectDir), name, { log }),
  update: async (projectDir, name, log) => updatePlugins(findPluginProject(projectDir), name, { builtIns: await builtIns(), log }),
};

const NAME = /^(@[a-z0-9][\w.-]*\/)?[a-z0-9][\w.-]*$/i;
const VERSION = /^[\w.^~<>=*+-]+$/;
const CATALOG_TIMEOUT_MS = 8000;

/** The plugins a project's ue-mcp.yml declares, each read from node_modules. */
export function listInstalledPlugins(projectDir: string): InstalledPlugin[] {
  return readPluginsList(path.join(projectDir, "ue-mcp.yml")).map((entry) => {
    const base = { name: entry.name, pinned: entry.version ?? null };
    const pkgDir = findInstalledPackage(entry.name, projectDir);
    if (!pkgDir) return { ...base, version: null, status: "missing", categories: [], problems: ["not installed in node_modules"] };
    let version: string | null = null;
    try {
      version = (JSON.parse(fs.readFileSync(path.join(pkgDir, "package.json"), "utf-8")) as { version?: string }).version ?? null;
    } catch {
      // A package.json that cannot be read leaves the version unknown.
    }
    try {
      const parsed = loadManifest(pkgDir);
      return {
        ...base,
        version,
        status: "ok",
        categories: Object.keys(parsed.manifest.inject),
        problems: parsed.dropped.map((d) => `${d.path}: ${d.reason}`),
      };
    } catch (e) {
      return { ...base, version, status: "invalid", categories: [], problems: [(e as Error).message] };
    }
  });
}

export interface PluginRouteDeps {
  projectDir: string;
  ops?: PluginOps;
  fetchImpl?: typeof fetch;
  /** Told after a command changed the project's plugins. */
  onChanged?: () => void;
}

type PluginRouteName = "plugins.list" | "plugins.catalog" | "plugins.install" | "plugins.uninstall" | "plugins.update";

/** The /v1/plugins routes for one project. Changes run one at a time. */
export function pluginRoutes(deps: PluginRouteDeps): Pick<RouteHandlers, PluginRouteName> {
  const ops = deps.ops ?? pluginOps;
  let busy = false;

  const change = async (op: (log: PluginOpLog) => Promise<unknown>): Promise<RouteResult> => {
    if (busy) return { status: 409, body: { error: "another plugin change is running; wait for it to finish" } };
    busy = true;
    const { log, error, text } = captureLog();
    let ok = true;
    try {
      await op(log);
    } catch (e) {
      ok = false;
      error(e instanceof Error ? e.message : String(e));
    } finally {
      busy = false;
    }
    if (ok) deps.onChanged?.();
    const result: PluginCommandResult = { ok, exitCode: ok ? 0 : 1, output: text().trim(), restartRequired: ok };
    return { status: ok ? 200 : 422, body: result };
  };

  const fields = async (r: { body(): Promise<unknown> }): Promise<{ name?: unknown; version?: unknown }> =>
    ((await r.body()) ?? {}) as { name?: unknown; version?: unknown };

  return {
    "plugins.list": () => ({ status: 200, body: { plugins: listInstalledPlugins(deps.projectDir) } }),
    "plugins.catalog": async () => {
      const catalog = await loadRegistryCatalog({ fetchImpl: deps.fetchImpl, timeoutMs: CATALOG_TIMEOUT_MS });
      if (catalog.source === "none") {
        return {
          status: 502,
          body: {
            error:
              `the plugin registry could not be reached: ${catalog.error}. ` +
              "Installing by npm package name still works (POST /v1/plugins/install with { name: \"<package>\" }), " +
              "and UE_MCP_REGISTRY points ue-mcp at another registry.",
          },
        };
      }
      return { status: 200, body: { plugins: catalog.plugins } };
    },
    "plugins.install": async (r) => {
      const b = await fields(r);
      if (typeof b.name !== "string" || !NAME.test(b.name)) return { status: 400, body: { error: "body must be { name, version? } with a package or registry name" } };
      if (b.version !== undefined && (typeof b.version !== "string" || !VERSION.test(b.version))) return { status: 400, body: { error: "version must be a semver version or range" } };
      const name = b.name;
      const version = typeof b.version === "string" ? b.version : undefined;
      return change((log) => ops.install(deps.projectDir, name, version, log));
    },
    "plugins.uninstall": async (r) => {
      const b = await fields(r);
      if (typeof b.name !== "string" || !NAME.test(b.name)) return { status: 400, body: { error: "body must be { name }" } };
      const name = b.name;
      return change((log) => ops.uninstall(deps.projectDir, name, log));
    },
    "plugins.update": async (r) => {
      const b = await fields(r);
      if (b.name !== undefined && (typeof b.name !== "string" || !NAME.test(b.name))) return { status: 400, body: { error: "name must be a package or registry name" } };
      const name = typeof b.name === "string" ? b.name : undefined;
      return change((log) => ops.update(deps.projectDir, name, log));
    },
  };
}
