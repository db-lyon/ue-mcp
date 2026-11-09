/**
 * `ue-mcp extension <install|use|remove|list>`, and `ue-mcp install`, which
 * is `extension install` under the name users are given.
 *
 *   install <name|zip|url> [--sha256 <hex>] [project] [--json]
 *   use <name> <version> [project] [--json]
 *   remove <name> [project] [--json]
 *   list [project] [--json]
 *
 * Versions install side by side and each project loads the one it records:
 * install records the project at the version it installed, use moves the
 * project to one already installed. Extensions load when a daemon starts, so
 * install, use and remove restart the project's daemon when one is running.
 *
 * A short name resolves through install-names.ts to a release manifest. A
 * manifest that declares an editor plugin also sets the project up for
 * ue-mcp when it is not, and installs that plugin's build for its engine.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { findUProject } from "../config/uproject-path.js";
import { readAnsweringDiscovery } from "../daemon/discovery.js";
import { extensionsDir } from "../daemon/extensions.js";
import { installExtension, isManifestUrl, readExtensionManifest, removeExtension, useExtensionVersion } from "../daemon/extension-install.js";
import { isInstallName, resolveInstallName } from "../daemon/install-names.js";
import { installEditorPlugin } from "../editor/editor-plugin.js";
import { ensurePluginsEnabled, ensureSetUp } from "./init-core.js";
import { installedCopies, migrateLegacy, recordedVersion, versionsOf } from "../daemon/extension-versions.js";
import { ensureDaemon } from "../daemon/shim.js";
import { waitForExit } from "../daemon/supersede-watch.js";
import { fail, ok, redirectConsoleToStderr, warn } from "./ui/ansi.js";
import type { CliUsage } from "./cli-main.js";

const USAGE = "usage: ue-mcp extension <install <name|zip|url> [--sha256 <hex>] | use <name> <version> | remove <name> | list> [project] [--json]";

/** A value flag's value, written `--name value` or `--name=value`. */
function flag(argv: string[], name: string): string | undefined {
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === name) return argv[i + 1];
    if (argv[i].startsWith(`${name}=`)) return argv[i].slice(name.length + 1);
  }
  return undefined;
}

/** Stop the project's daemon and start a new one, so it loads the current extensions. */
async function restartDaemon(uproject: string): Promise<boolean> {
  const dir = path.dirname(path.resolve(uproject));
  const d = await readAnsweringDiscovery(dir);
  if (!d) return false;
  await fetch(`http://127.0.0.1:${d.port}/v1/shutdown`, { method: "POST", headers: { authorization: `Bearer ${d.token}` } }).catch(() => undefined);
  // Wait for the process to end, not just its record: a new daemon started beside a live old one leaves two attached to the editor.
  await waitForExit(d.pid, 30_000);
  await ensureDaemon(uproject);
  return true;
}

/**
 * Install `target` (a short name, a zip, a URL or a release manifest) for
 * `uproject`, saying each step as it lands. Returns what --json prints.
 */
async function install(target: string, sha256: string | undefined, uproject: string | undefined, say: (line: string) => void): Promise<Record<string, unknown>> {
  const named = isInstallName(target) ? resolveInstallName(target) : null;
  const source = named?.manifest ?? target;
  const manifest = isManifestUrl(source) ? await readExtensionManifest(source) : null;
  const plugin = manifest?.pluginChannel;
  const projectDir = uproject ? path.dirname(path.resolve(uproject)) : undefined;
  const result: Record<string, unknown> = {};

  // An editor plugin needs the bridge beside it, so the project is set up first.
  if (plugin && uproject) {
    const setup = await ensureSetUp(uproject);
    if (setup && !setup.ok) throw new Error(`setting up ue-mcp in ${uproject} failed: ${setup.error}`);
    if (setup) {
      result.setup = setup.steps;
      result.warnings = setup.warnings;
      say(`set up ue-mcp in ${path.basename(uproject, ".uproject")}`);
      for (const w of setup.warnings) warn(w);
    }
  }

  const installed = await installExtension(manifest?.url ?? source, { sha256: manifest?.sha256 ?? sha256, ...(projectDir ? { projectDir } : {}) });
  Object.assign(result, installed);
  say(`installed ${named?.title ?? installed.name} ${installed.version}`);

  if (plugin && uproject) {
    const p = await installEditorPlugin(plugin, uproject, manifest.pluginMin);
    ensurePluginsEnabled(uproject, [p.name]);
    result.editorPlugin = p;
    say(p.changed ? `installed the ${p.title} editor plugin ${p.version}` : `the ${p.title} editor plugin ${p.version} is already installed`);
  } else if (plugin) {
    warn("no .uproject here, so no project was set up: run this again in the project folder or name its .uproject");
  }

  const restarted = uproject ? await restartDaemon(uproject) : false;
  result.daemonRestarted = restarted;
  if (restarted) say("the project's daemon restarted to load it");
  if (plugin && uproject) say(`Open ${path.basename(uproject, ".uproject")} in Unreal to finish.`);
  return result;
}

const FLAGS = [
  { name: "--json", description: "Print one JSON document" },
  { name: "--sha256", value: "<hex>", description: "install: expected archive digest" },
];

export const usage: CliUsage = {
  usage: "ue-mcp extension <install <name|zip|url> | use <name> <version> | remove <name> | list> [project] [flags]",
  summary: "Install, choose, remove or list daemon extensions and the version each project loads.",
  strict: true,
  flags: FLAGS,
};

/** `ue-mcp install`: the same install, as the command users are shown. */
export const installUsage: CliUsage = {
  usage: "ue-mcp install <name|zip|url> [project] [flags]",
  summary: "Install by name into a project, e.g. `ue-mcp install studio`, setting the project up for ue-mcp when it is not.",
  strict: true,
  flags: FLAGS,
};

export function runInstall(argv: string[]): Promise<number | void> {
  return run(["install", ...argv]);
}

export async function run(argv: string[]): Promise<number | void> {
  const json = argv.includes("--json");
  const out = (v: unknown): void => {
    process.stdout.write(JSON.stringify(v, null, 2) + "\n");
  };
  const sha256 = flag(argv, "--sha256");
  const positional = argv.filter((a, i) => !a.startsWith("--") && argv[i - 1] !== "--sha256");
  const [sub, target] = positional;
  // use takes a version before the project; list takes no target.
  const projectArg = sub === "use" ? positional[3] : sub === "list" ? positional[1] : positional[2];
  const uproject = projectArg ?? findUProject(process.cwd()) ?? undefined;
  const projectDir = uproject ? path.dirname(path.resolve(uproject)) : undefined;

  try {
    if (sub === "list") {
      const root = extensionsDir();
      if (fs.existsSync(root)) migrateLegacy(root);
      // version is what the project records, else the newest; installed is every version on the machine.
      const rows = [...installedCopies(root)].map(([name, copies]) => {
        const installed = versionsOf(copies);
        const recorded = projectDir ? recordedVersion(projectDir, name) : null;
        return { name, version: recorded ?? installed[installed.length - 1], installed, ...(projectDir ? { recorded } : {}) };
      }).sort((a, b) => a.name.localeCompare(b.name));
      if (json) out({ ok: true, dir: root, extensions: rows });
      else if (rows.length === 0) ok(`no extensions in ${root}`);
      else for (const r of rows) ok(`${r.name} ${r.version}${r.installed.length > 1 ? ` (installed: ${r.installed.join(", ")})` : ""}`);
      return 0;
    }
    if (sub === "install" && target) {
      // Setup and warnings print as they go; with --json that goes to stderr.
      const restore = json ? redirectConsoleToStderr() : () => {};
      try {
        const result = await install(target, sha256, uproject, json ? () => undefined : ok);
        if (json) out({ ok: true, ...result });
      } finally {
        restore();
      }
      return 0;
    }
    if (sub === "use" && target && positional[2]) {
      if (!uproject || !projectDir) throw new Error("use needs a project: name its .uproject or run it inside the project");
      const used = useExtensionVersion(target, positional[2], projectDir);
      const restarted = await restartDaemon(uproject);
      if (json) out({ ok: true, ...used, daemonRestarted: restarted });
      else ok(`this project now loads ${used.name} ${used.version}${restarted ? "; the project's daemon restarted to load it" : ""}`);
      return 0;
    }
    if (sub === "remove" && target) {
      const removed = removeExtension(target);
      const restarted = removed && uproject ? await restartDaemon(uproject) : false;
      if (json) out({ ok: true, removed, daemonRestarted: restarted });
      else ok(removed ? `removed ${target}` : `${target} is not installed`);
      return 0;
    }
    fail(USAGE);
    return 2;
  } catch (e) {
    const message = e instanceof Error ? e.message : String(e);
    if (json) out({ ok: false, error: message });
    else fail(message);
    return 1;
  }
}
