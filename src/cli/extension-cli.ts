/**
 * `ue-mcp extension <install|remove|list>`
 *
 *   install <zip|url> [--sha256 <hex>] [project] [--json]
 *   remove <name> [project] [--json]
 *   list [--json]
 *
 * Extensions load when a daemon starts, so install and remove restart the
 * project's daemon when one is running.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { findUProject } from "../config/uproject-path.js";
import { readLiveDiscovery } from "../daemon/discovery.js";
import { extensionsDir } from "../daemon/extensions.js";
import { installExtension, removeExtension } from "../daemon/extension-install.js";
import { ensureDaemon } from "../daemon/shim.js";
import { fail, ok } from "./ui/ansi.js";

const USAGE = "usage: ue-mcp extension <install <zip|url> [--sha256 <hex>] | remove <name> | list> [project] [--json]";

function flag(argv: string[], name: string): string | undefined {
  const i = argv.indexOf(name);
  return i >= 0 ? argv[i + 1] : undefined;
}

/** Stop the project's daemon and start a new one, so it loads the current extensions. */
async function restartDaemon(uproject: string): Promise<boolean> {
  const dir = path.dirname(path.resolve(uproject));
  const d = readLiveDiscovery(dir);
  if (!d) return false;
  await fetch(`http://127.0.0.1:${d.port}/v1/shutdown`, { method: "POST", headers: { authorization: `Bearer ${d.token}` } }).catch(() => undefined);
  const deadline = Date.now() + 15_000;
  while (readLiveDiscovery(dir)?.pid === d.pid && Date.now() < deadline) await new Promise((r) => setTimeout(r, 200));
  await ensureDaemon(uproject);
  return true;
}

export async function run(argv: string[]): Promise<number | void> {
  const json = argv.includes("--json");
  const out = (v: unknown): void => {
    process.stdout.write(JSON.stringify(v, null, 2) + "\n");
  };
  const sha256 = flag(argv, "--sha256");
  const positional = argv.filter((a, i) => !a.startsWith("--") && argv[i - 1] !== "--sha256");
  const [sub, target, projectArg] = positional;
  const uproject = projectArg ?? findUProject(process.cwd()) ?? undefined;

  try {
    if (sub === "list") {
      const root = extensionsDir();
      const names = fs.existsSync(root)
        ? fs.readdirSync(root, { withFileTypes: true }).filter((e) => e.isDirectory() && !/\.(staging|old)-\d+$/.test(e.name)).map((e) => e.name)
        : [];
      const rows = names.map((n) => {
        try {
          const pkg = JSON.parse(fs.readFileSync(path.join(root, n, "package.json"), "utf-8")) as { version?: string };
          return { name: n, version: pkg.version ?? "?" };
        } catch {
          return { name: n, version: "?" };
        }
      });
      if (json) out({ ok: true, dir: root, extensions: rows });
      else if (rows.length === 0) ok(`no extensions in ${root}`);
      else for (const r of rows) ok(`${r.name} ${r.version}`);
      return 0;
    }
    if (sub === "install" && target) {
      const installed = await installExtension(target, { sha256 });
      const restarted = uproject ? await restartDaemon(uproject) : false;
      if (json) out({ ok: true, ...installed, daemonRestarted: restarted });
      else ok(`installed ${installed.name} ${installed.version}${restarted ? "; the project's daemon restarted to load it" : ""}`);
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
