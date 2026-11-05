/**
 * Plugin packages built on disk for the plugin integration tests, and a fake
 * npm that installs them the way the real one would: a package directory under
 * node_modules/<name> and a dependency line in package.json.
 *
 * Task modules are plain ESM JavaScript, so a fixture needs no build step.
 * They extend flowkit's BaseTask through an absolute URL into this repo's
 * node_modules, because a package in a temp folder has no ue-mcp beside it and
 * the self-resolve hook only registers for the built server.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { pathToFileURL } from "node:url";
import { WebSocketServer } from "ws";
import { REPO_ROOT, type Sandbox } from "./harness.js";

export const FLOWKIT_URL = pathToFileURL(path.join(REPO_ROOT, "node_modules", "@db-lyon", "flowkit", "dist", "index.js")).href;

export interface PluginPackage {
  name: string;
  version: string;
  /** ue-mcp.plugin.yml text. */
  manifest: string;
  /** Extra files, package-relative path to content. */
  files?: Record<string, string>;
  /** skills/<name>/SKILL.md content, by skill name. */
  skills?: Record<string, string>;
  pkg?: Record<string, unknown>;
}

/** Write a plugin package into `dir` and return `dir`. */
export function writePluginPackage(dir: string, p: PluginPackage): string {
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(
    path.join(dir, "package.json"),
    JSON.stringify({ name: p.name, version: p.version, type: "module", keywords: ["ue-mcp-plugin"], ...p.pkg }, null, 2),
  );
  fs.writeFileSync(path.join(dir, "ue-mcp.plugin.yml"), p.manifest);
  for (const [rel, text] of Object.entries(p.files ?? {})) {
    const abs = path.join(dir, rel);
    fs.mkdirSync(path.dirname(abs), { recursive: true });
    fs.writeFileSync(abs, text);
  }
  for (const [skill, text] of Object.entries(p.skills ?? {})) {
    const abs = path.join(dir, "skills", skill, "SKILL.md");
    fs.mkdirSync(path.dirname(abs), { recursive: true });
    fs.writeFileSync(abs, text);
  }
  return dir;
}

/** A task that answers with a greeting naming the package that served it. */
export function helloTask(served: string): string {
  return `import { BaseTask } from ${JSON.stringify(FLOWKIT_URL)};
export default class Hello extends BaseTask {
  get taskName() { return "fixture.hello"; }
  async execute() {
    const who = this.options && this.options.who !== undefined ? this.options.who : "world";
    return { success: true, data: { greeting: "hello, " + who, servedBy: ${JSON.stringify(served)} } };
  }
}
`;
}

export function skillText(name: string, description: string, body = ""): string {
  return `---\nname: ${name}\ndescription: ${description}\n---\n\n${body}\n`;
}

/** Copy a package into `<projectDir>/node_modules/<name>`, as an install leaves it. */
export function placeInstalled(projectDir: string, pkgDir: string): string {
  const name = (JSON.parse(fs.readFileSync(path.join(pkgDir, "package.json"), "utf8")) as { name: string }).name;
  const dest = path.join(projectDir, "node_modules", ...name.split("/"));
  fs.rmSync(dest, { recursive: true, force: true });
  fs.cpSync(pkgDir, dest, { recursive: true });
  return dest;
}

export interface FakeRegistryPackages {
  /** Package name to version to the directory holding that version. */
  versions: Record<string, Record<string, string>>;
  /** Tarball file name to the directory it unpacks to. */
  tarballs?: Record<string, string>;
}

/**
 * Write an NPM_FAKE_SCRIPT for the sandbox's fake npm and return its path.
 * It answers `init -y`, `install --save <spec>`, `uninstall <name>` and
 * `view <name> version` from `packages`; NPM_FAKE_FAIL=<verb> makes that verb
 * exit 1 without touching anything.
 */
export function writeFakeNpmScript(sb: Sandbox, packages: FakeRegistryPackages): string {
  const configPath = sb.path("fake-npm-packages.json");
  fs.writeFileSync(configPath, JSON.stringify(packages));
  const script = sb.path("fake-npm-script.mjs");
  fs.writeFileSync(
    script,
    `import fs from "node:fs";
import path from "node:path";
const cfg = JSON.parse(fs.readFileSync(${JSON.stringify(configPath)}, "utf8"));
const semverKey = (v) => v.split(".").map((n) => n.padStart(6, "0")).join(".");
const latest = (name) => Object.keys(cfg.versions[name] ?? {}).sort((a, b) => semverKey(a) < semverKey(b) ? -1 : 1).pop();
const readPkg = (cwd) => JSON.parse(fs.readFileSync(path.join(cwd, "package.json"), "utf8"));
const writePkg = (cwd, pj) => fs.writeFileSync(path.join(cwd, "package.json"), JSON.stringify(pj, null, 2) + "\\n");
function place(cwd, srcDir, depSpec) {
  const pj = JSON.parse(fs.readFileSync(path.join(srcDir, "package.json"), "utf8"));
  const dest = path.join(cwd, "node_modules", ...pj.name.split("/"));
  fs.rmSync(dest, { recursive: true, force: true });
  fs.cpSync(srcDir, dest, { recursive: true });
  const root = readPkg(cwd);
  root.dependencies = { ...(root.dependencies ?? {}), [pj.name]: depSpec ?? "^" + pj.version };
  writePkg(cwd, root);
}
export default function (args, cwd) {
  const verb = args[0];
  if (process.env.NPM_FAKE_FAIL === verb) { console.error("npm ERR! simulated " + verb + " failure"); return 1; }
  if (verb === "init") {
    if (!fs.existsSync(path.join(cwd, "package.json"))) writePkg(cwd, { name: path.basename(cwd).toLowerCase(), version: "1.0.0" });
    return 0;
  }
  if (verb === "install") {
    const spec = args.filter((a) => !a.startsWith("-")).slice(1)[0];
    const local = path.resolve(cwd, spec);
    if (fs.existsSync(path.join(local, "package.json"))) { place(cwd, local, "file:" + spec); return 0; }
    const tarball = (cfg.tarballs ?? {})[path.basename(spec)];
    if (tarball) { place(cwd, tarball, "file:" + spec); return 0; }
    const at = spec.lastIndexOf("@");
    const name = at > 0 ? spec.slice(0, at) : spec;
    const version = at > 0 ? spec.slice(at + 1) : latest(name);
    const dir = (cfg.versions[name] ?? {})[version];
    if (!dir) { console.error("npm ERR! 404 " + spec); return 1; }
    place(cwd, dir);
    return 0;
  }
  if (verb === "uninstall") {
    const name = args[1];
    fs.rmSync(path.join(cwd, "node_modules", ...name.split("/")), { recursive: true, force: true });
    if (fs.existsSync(path.join(cwd, "package.json"))) {
      const root = readPkg(cwd);
      if (root.dependencies) delete root.dependencies[name];
      writePkg(cwd, root);
    }
    return 0;
  }
  if (verb === "view") {
    const v = latest(args[1]);
    if (!v) { console.error("npm ERR! 404 " + args[1]); return 1; }
    console.log(v);
    return 0;
  }
  return 0;
}
`,
  );
  return script;
}

/** `ue-mcp.yml` text declaring `plugins`. */
export function pluginsYml(plugins: string[], extra = ""): string {
  return `ue-mcp:\n  version: 1\n${extra}\nplugins:\n${plugins.map((p) => `  - name: ${p}\n`).join("")}`;
}

/**
 * A bridge stand-in that answers only the capabilities handshake, publishing
 * `pluginHandlerSpecs` the way a bridge with a loaded native module does.
 * FakeBridge answers the handshake itself without them.
 */
export async function startSpecsBridge(projectDir: string, pluginHandlerSpecs: Record<string, unknown>): Promise<{ handshakes: () => number; stop: () => Promise<void> }> {
  const server = new WebSocketServer({ host: "127.0.0.1", port: 0 });
  await new Promise<void>((resolve, reject) => {
    server.once("listening", () => resolve());
    server.once("error", reject);
  });
  const port = (server.address() as { port: number }).port;
  let handshakes = 0;
  server.on("connection", (ws) => {
    ws.on("message", (data) => {
      const msg = JSON.parse(data.toString()) as { id?: string; method?: string };
      if (!msg.id) return;
      if (msg.method === "get_bridge_capabilities") {
        handshakes++;
        ws.send(JSON.stringify({ id: msg.id, result: { protocolVersion: 2, projectName: path.basename(projectDir), port, pid: process.pid, pluginHandlerSpecs } }));
      } else {
        ws.send(JSON.stringify({ id: msg.id, error: { code: -32601, message: `Unknown method: ${msg.method}` } }));
      }
    });
  });
  const dir = path.join(projectDir, "Saved", "UE_MCP_Bridge");
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(path.join(dir, "port.json"), JSON.stringify({ port, pid: process.pid, startedAt: new Date().toISOString(), apiVersion: 1 }));
  return {
    handshakes: () => handshakes,
    stop: async () => {
      for (const c of server.clients) c.terminate();
      await new Promise<void>((r) => server.close(() => r()));
    },
  };
}
