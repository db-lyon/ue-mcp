/**
 * Managing a project's ue-mcp plugins over the daemon's HTTP API, so a client
 * such as an in-editor panel can browse the registry and install, update or
 * remove plugins without a terminal.
 *
 *   GET  /v1/plugins            the project's plugins: version, status, categories
 *   GET  /v1/plugins/catalog    the registry's published plugins
 *   POST /v1/plugins/install    { name, version? }
 *   POST /v1/plugins/uninstall  { name }
 *   POST /v1/plugins/update     { name? }
 *
 * Install, uninstall and update run `ue-mcp plugin <command>` in the project,
 * the same code path as the CLI, one at a time. A change takes effect when the
 * server restarts, which the result says.
 */
import { spawn } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { loadManifest } from "../extensions/manifest.js";
import { findInstalledPackage } from "../extensions/resolver.js";
import { readPluginsList } from "../extensions/plugins-list.js";
import { registryBase } from "../extensions/registry-catalog.js";
import { isSuperseded } from "../extensions/superseded.js";

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

export interface CatalogPlugin {
  slug: string;
  name: string;
  packageName?: string;
  tagline?: string;
  description?: string;
  readme?: string;
  category?: string;
  tags?: string[];
  repoUrl?: string;
  homepageUrl?: string;
  status?: string;
}

export interface PluginCommandResult {
  ok: boolean;
  exitCode: number | null;
  /** What the command printed, for the client to show. */
  output: string;
  /** Plugin changes take effect when the server restarts. */
  restartRequired: boolean;
}

/** Runs `ue-mcp plugin <args>` in a project folder. */
export type PluginCommandRunner = (args: string[], cwd: string) => Promise<{ exitCode: number | null; output: string }>;

const COMMAND_TIMEOUT_MS = 10 * 60_000;
const CATALOG_TTL_MS = 10 * 60_000;
const NAME = /^(@[a-z0-9][\w.-]*\/)?[a-z0-9][\w.-]*$/i;
const VERSION = /^[\w.^~<>=*+-]+$/;

/** The CLI this daemon was started from, with the same node flags, so a source checkout under tsx runs its own source. */
const cliArgs = (): string[] => [...process.execArgv, process.argv[1]];

export const runPluginCommand: PluginCommandRunner = (args, cwd) =>
  new Promise((resolve) => {
    const child = spawn(process.execPath, [...cliArgs(), "plugin", ...args], { cwd, windowsHide: true, env: process.env });
    let output = "";
    const take = (b: Buffer) => {
      output = (output + b.toString()).slice(-64 * 1024);
    };
    child.stdout.on("data", take);
    child.stderr.on("data", take);
    const timer = setTimeout(() => child.kill(), COMMAND_TIMEOUT_MS);
    child.on("close", (code) => {
      clearTimeout(timer);
      resolve({ exitCode: code, output });
    });
    child.on("error", (e) => {
      clearTimeout(timer);
      resolve({ exitCode: null, output: e.message });
    });
  });

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
  run?: PluginCommandRunner;
  fetchImpl?: typeof fetch;
  /** Told after a command changed the project's plugins. */
  onChanged?: () => void;
}

export type PluginRouteResult = { status: number; body: unknown } | null;

/**
 * Handle one /v1/plugins request; null when the path is not one of these
 * routes. `sub` is the path after /v1/plugins.
 */
export function pluginRoutes(deps: PluginRouteDeps) {
  const run = deps.run ?? runPluginCommand;
  const fetchImpl = deps.fetchImpl ?? fetch;
  let catalog: { at: number; plugins: CatalogPlugin[] } | null = null;
  let busy = false;

  const command = async (args: string[]): Promise<PluginRouteResult> => {
    if (busy) return { status: 409, body: { error: "another plugin change is running; wait for it to finish" } };
    busy = true;
    try {
      const r = await run(args, deps.projectDir);
      const ok = r.exitCode === 0;
      if (ok) deps.onChanged?.();
      const result: PluginCommandResult = { ok, exitCode: r.exitCode, output: r.output.trim(), restartRequired: ok };
      return { status: ok ? 200 : 422, body: result };
    } finally {
      busy = false;
    }
  };

  return async (method: string, sub: string, body: unknown): Promise<PluginRouteResult> => {
    const b = (body ?? {}) as { name?: unknown; version?: unknown };
    if (method === "GET" && (sub === "" || sub === "/")) {
      return { status: 200, body: { plugins: listInstalledPlugins(deps.projectDir) } };
    }
    if (method === "GET" && sub === "/catalog") {
      if (!catalog || Date.now() - catalog.at > CATALOG_TTL_MS) {
        try {
          const res = await fetchImpl(`${registryBase()}/api/plugins`, { signal: AbortSignal.timeout(8000) });
          if (!res.ok) return { status: 502, body: { error: `the plugin registry answered ${res.status}` } };
          const j = (await res.json()) as { plugins?: CatalogPlugin[] };
          // A plugin now built into core is not offered for install.
          const plugins = (j.plugins ?? []).filter((p) => !isSuperseded(p.slug) && !(p.packageName && isSuperseded(p.packageName)));
          catalog = { at: Date.now(), plugins };
        } catch (e) {
          if (!catalog) return { status: 502, body: { error: `the plugin registry could not be reached: ${(e as Error).message}` } };
        }
      }
      return { status: 200, body: { plugins: catalog.plugins } };
    }
    if (method === "POST" && sub === "/install") {
      if (typeof b.name !== "string" || !NAME.test(b.name)) return { status: 400, body: { error: "body must be { name, version? } with a package or registry name" } };
      if (b.version !== undefined && (typeof b.version !== "string" || !VERSION.test(b.version))) return { status: 400, body: { error: "version must be a semver version or range" } };
      return command(["install", b.name, ...(typeof b.version === "string" ? ["--version", b.version] : [])]);
    }
    if (method === "POST" && sub === "/uninstall") {
      if (typeof b.name !== "string" || !NAME.test(b.name)) return { status: 400, body: { error: "body must be { name }" } };
      return command(["uninstall", b.name]);
    }
    if (method === "POST" && sub === "/update") {
      if (b.name !== undefined && (typeof b.name !== "string" || !NAME.test(b.name))) return { status: 400, body: { error: "name must be a package or registry name" } };
      return command(["update", ...(typeof b.name === "string" ? [b.name] : [])]);
    }
    return null;
  };
}
