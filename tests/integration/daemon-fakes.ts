/**
 * Helpers for the daemon and extension integration tests: a daemon spawned the
 * way the CLI spawns one, its discovery record, authorized requests to it, its
 * event stream, and the extension archives and signed UI bundles it loads.
 */
import { spawn, execFileSync, type ChildProcess } from "node:child_process";
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as path from "node:path";
import { projectKey } from "../../src/daemon/discovery.js";
import { signBundle } from "../../src/daemon/ui-bundles.js";
import { ENTRY, runCli, TSX, waitFor, type Sandbox } from "./harness.js";

export interface Discovery {
  pid: number;
  port: number;
  token: string;
  version: string;
  apiVersion: number;
  projectRoot: string;
  startedAt: string;
}

/** The sandbox's discovery directory, as the harness environment names it. */
export function daemonDirOf(sb: Sandbox): string {
  return path.join(sb.home, ".ue-mcp", "daemons");
}

export function discoveryFile(sb: Sandbox, uproject: string): string {
  return path.join(daemonDirOf(sb), `${projectKey(path.dirname(uproject))}.json`);
}

export function readDiscovery(sb: Sandbox, uproject: string): Discovery | null {
  try {
    return JSON.parse(fs.readFileSync(discoveryFile(sb, uproject), "utf8")) as Discovery;
  } catch {
    return null;
  }
}

export function pidAlive(pid: number): boolean {
  try {
    process.kill(pid, 0);
    return true;
  } catch (e) {
    return (e as NodeJS.ErrnoException).code === "EPERM";
  }
}

/** Environment for a daemon in a sandbox: no editor, and no registry outside it. */
export function daemonEnv(extra: Record<string, string | undefined> = {}): Record<string, string | undefined> {
  return { UE_MCP_PORT: "1", UE_MCP_DAEMON_IDLE_MS: "120000", ...extra };
}

/** `ue-mcp daemon start --json`, then the discovery record it published. */
export async function startDaemonCli(sb: Sandbox, uproject: string, env: Record<string, string | undefined> = {}): Promise<Discovery> {
  const r = await runCli(sb, ["daemon", "start", uproject, "--json"], { env: daemonEnv(env) });
  if (r.code !== 0) throw new Error(`daemon start exited ${r.code}\n${r.stdout}\n${r.stderr}`);
  const d = readDiscovery(sb, uproject);
  if (!d) throw new Error(`daemon start printed ${r.stdout} but published nothing`);
  return d;
}

/** A long-running `ue-mcp <args>` child, with its output collected. */
export interface Spawned {
  child: ChildProcess;
  output(): string;
  exited: Promise<number | null>;
}

export function spawnCli(sb: Sandbox, args: string[], env: Record<string, string | undefined> = {}): Spawned {
  const child = spawn(process.execPath, ["--import", TSX, ENTRY, ...args], {
    cwd: sb.root,
    env: sb.env(env),
    stdio: ["ignore", "pipe", "pipe"],
    windowsHide: true,
  });
  let out = "";
  child.stdout!.on("data", (d) => (out += d));
  child.stderr!.on("data", (d) => (out += d));
  const exited = new Promise<number | null>((resolve) => child.on("close", (code) => resolve(code)));
  return { child, output: () => out, exited };
}

export function base(d: Discovery): string {
  return `http://127.0.0.1:${d.port}`;
}

export function bearer(d: Discovery): Record<string, string> {
  return { authorization: `Bearer ${d.token}` };
}

/** An authorized request to a daemon, answered as status and parsed body. */
export async function call(
  d: Discovery,
  method: string,
  route: string,
  body?: unknown,
): Promise<{ status: number; body: any; text: string }> {
  const res = await fetch(`${base(d)}${route}`, {
    method,
    headers: { ...bearer(d), ...(body !== undefined ? { "content-type": "application/json" } : {}) },
    body: body !== undefined ? JSON.stringify(body) : undefined,
  });
  const text = await res.text();
  let parsed: unknown = undefined;
  try {
    parsed = text ? JSON.parse(text) : undefined;
  } catch {
    // not JSON
  }
  return { status: res.status, body: parsed, text };
}

export interface SseEvent {
  id: string | null;
  type: string;
  data: any;
}

/**
 * Read a server-sent event stream until `done` holds for the events so far,
 * then close it. Fails after `ms`.
 */
export async function readSse(url: string, headers: Record<string, string>, done: (events: SseEvent[]) => boolean, ms = 30_000): Promise<SseEvent[]> {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), ms);
  const events: SseEvent[] = [];
  try {
    const res = await fetch(url, { headers, signal: ctrl.signal });
    if (res.status !== 200) throw new Error(`${url} answered ${res.status}`);
    const reader = res.body!.getReader();
    const decoder = new TextDecoder();
    let buf = "";
    for (;;) {
      const { value, done: ended } = await reader.read();
      if (ended) break;
      buf += decoder.decode(value, { stream: true });
      let cut: number;
      while ((cut = buf.indexOf("\n\n")) >= 0) {
        const frame = buf.slice(0, cut);
        buf = buf.slice(cut + 2);
        let id: string | null = null;
        let type = "message";
        const data: string[] = [];
        for (const line of frame.split("\n")) {
          if (line.startsWith("id: ")) id = line.slice(4);
          else if (line.startsWith("event: ")) type = line.slice(7);
          else if (line.startsWith("data: ")) data.push(line.slice(6));
        }
        if (data.length === 0) continue;
        let parsed: unknown = data.join("\n");
        try {
          parsed = JSON.parse(parsed as string);
        } catch {
          // plain text
        }
        events.push({ id, type, data: parsed });
      }
      if (done(events)) break;
    }
    return events;
  } catch (e) {
    if (ctrl.signal.aborted) throw new Error(`timed out reading ${url}; saw ${events.map((e) => e.type).join(", ") || "nothing"}`);
    throw e;
  } finally {
    clearTimeout(timer);
    ctrl.abort();
  }
}

/** Wait for a daemon process to be gone and its discovery record removed. */
export async function waitGone(sb: Sandbox, uproject: string, pid: number, ms = 20_000): Promise<void> {
  await waitFor(() => !pidAlive(pid) && readDiscovery(sb, uproject)?.pid !== pid, ms, `daemon pid ${pid} to exit`);
}

// ── Extensions and UI bundles ──────────────────────────────────────────

const TAR = process.platform === "win32" ? path.join(process.env.SystemRoot ?? "C:\\Windows", "System32", "tar.exe") : "tar";

export const UI_KEYS = crypto.generateKeyPairSync("ed25519");
export const UI_PUBLIC_PEM = UI_KEYS.publicKey.export({ type: "spki", format: "pem" }).toString();
const UI_PRIVATE_PEM = UI_KEYS.privateKey.export({ type: "pkcs8", format: "pem" }).toString();

/** A signed UI bundle directory whose index.html carries `marker`. */
export function signedBundle(dir: string, version: string, marker: string, pem = UI_PRIVATE_PEM): string {
  fs.mkdirSync(path.join(dir, "assets"), { recursive: true });
  fs.writeFileSync(path.join(dir, "index.html"), `<!doctype html><title>${marker}</title>`);
  fs.writeFileSync(path.join(dir, "assets", "app.js"), `console.log(${JSON.stringify(marker)})`);
  signBundle(dir, { name: "integ-ui", version, api: { min: 1, max: 1 } }, pem);
  return dir;
}

export interface ExtensionSpec {
  name: string;
  version: string;
  /** Body of `activate(api)`. */
  activate?: string;
  /** Extra package.json fields. */
  pkg?: Record<string, unknown>;
  /** A signed UI bundle shipped in the package under ui/, installed at activation. */
  bundle?: { version: string; marker: string };
}

/** The default activate body: a route that proves the extension runs inside the daemon. */
export function probeActivate(name: string): string {
  return `
    api.route("GET", "/whoami", () => ({ body: { name: ${JSON.stringify(name)}, pid: process.pid, projectDir: api.projectDir, dataDir: api.dataDir } }));
    api.route("POST", "/echo/:id", (req) => ({ status: 201, body: { id: req.params.id, got: req.body ?? null } }));
    api.events.publish("activated", { name: ${JSON.stringify(name)} });
  `;
}

/** Write an extension package folder named for the extension under `parent`. */
export function writeExtension(parent: string, spec: ExtensionSpec): string {
  const dir = path.join(parent, spec.name);
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(path.join(dir, "package.json"), JSON.stringify({
    name: `${spec.name}-ext`,
    version: spec.version,
    type: "module",
    main: "index.js",
    ueMcpExtension: { name: spec.name, api: { min: 1, max: 1 } },
    ...spec.pkg,
  }));
  let body = spec.activate ?? probeActivate(spec.name);
  if (spec.bundle) {
    signedBundle(path.join(dir, "ui"), spec.bundle.version, spec.bundle.marker);
    body = `
      api.ui.trustKey(${JSON.stringify(UI_PUBLIC_PEM)});
      api.ui.install(fileURLToPath(new URL("./ui", import.meta.url)));
      ${body}`;
  }
  fs.writeFileSync(path.join(dir, "index.js"), `import { fileURLToPath } from "node:url";\nexport function activate(api) {${body}}\n`);
  return dir;
}

/** Zip an extension package the way a release ships one: the folder at the archive root. */
export function zipExtension(work: string, spec: ExtensionSpec): { zip: string; sha256: string } {
  const src = path.join(work, `src-${spec.name}-${spec.version}-${crypto.randomBytes(3).toString("hex")}`);
  writeExtension(src, spec);
  const zip = path.join(work, `${spec.name}-${spec.version}-${crypto.randomBytes(3).toString("hex")}.zip`);
  execFileSync(TAR, ["-a", "-c", "-f", zip, "-C", src, spec.name], { windowsHide: true });
  return { zip, sha256: crypto.createHash("sha256").update(fs.readFileSync(zip)).digest("hex") };
}
