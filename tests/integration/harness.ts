/**
 * The integration harness: the shipped entry point, spawned the way a user or
 * an MCP client runs it, inside a sandbox that keeps every user-scoped file
 * and every network endpoint local.
 *
 * Isolation follows the golden recorder (tests/golden/capture.ts): every
 * inherited UE_MCP_* variable is dropped, HOME, USERPROFILE and APPDATA point
 * into the sandbox (so ~/.ue-mcp, ~/.claude and the Claude Desktop config are
 * the sandbox's), each user file the server reads has its own override set
 * anyway, the update check is off, and every service origin the code can be
 * pointed at is a closed local port unless a test starts a fake there.
 * `npm` resolves to a fake on PATH that records its arguments, so nothing a
 * test runs can install from, or publish to, a real registry.
 */
import { spawn } from "node:child_process";
import * as fs from "node:fs";
import * as http from "node:http";
import * as os from "node:os";
import * as path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";

const here = path.dirname(fileURLToPath(import.meta.url));
export const REPO_ROOT = path.resolve(here, "..", "..");
export const ENTRY = path.join(REPO_ROOT, "src", "index.ts");
/** tsx by absolute URL, so a child whose cwd is a sandbox still loads it. Daemons inherit it through execArgv. */
export const TSX = pathToFileURL(path.join(REPO_ROOT, "node_modules", "tsx", "dist", "loader.mjs")).href;
export const PACKAGE_VERSION = (JSON.parse(fs.readFileSync(path.join(REPO_ROOT, "package.json"), "utf8")) as { version: string }).version;

/** An origin nothing listens on: port 1 is privileged everywhere we run, so a connect is refused at once. */
export const DEAD_ORIGIN = "http://127.0.0.1:1";

export interface ProjectOptions {
  /** EngineAssociation written into the descriptor. Defaults to 5.6. */
  engine?: string;
  /** Text for ue-mcp.yml beside the descriptor. */
  config?: string;
  content?: boolean;
}

/** One temp directory holding a fake home, projects and anything a test writes. */
export class Sandbox {
  readonly root: string;
  readonly home: string;
  readonly bin: string;
  readonly npmLog: string;

  constructor(prefix = "ue-mcp-integ-") {
    this.root = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), prefix)));
    this.home = path.join(this.root, "home");
    this.bin = path.join(this.root, "bin");
    this.npmLog = path.join(this.root, "npm-calls.jsonl");
    fs.mkdirSync(this.home, { recursive: true });
    fs.mkdirSync(path.join(this.home, "AppData", "Roaming"), { recursive: true });
    fs.mkdirSync(this.bin, { recursive: true });
    this.installFakeNpm();
  }

  path(...parts: string[]): string {
    return path.join(this.root, ...parts);
  }

  /** `<root>/<name>/<name>.uproject`, returned as that file's path. */
  makeProject(name: string, opts: ProjectOptions = {}): string {
    const dir = path.join(this.root, name);
    fs.mkdirSync(opts.content ? path.join(dir, "Content") : dir, { recursive: true });
    const uproject = path.join(dir, `${name}.uproject`);
    fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, EngineAssociation: opts.engine ?? "5.6" }, null, 2));
    if (opts.config !== undefined) fs.writeFileSync(path.join(dir, "ue-mcp.yml"), opts.config);
    return uproject;
  }

  /** The environment every spawned process gets. `extra` wins; undefined deletes. */
  env(extra: Record<string, string | undefined> = {}): Record<string, string> {
    const env: Record<string, string> = {};
    for (const [k, v] of Object.entries(process.env)) {
      // A developer's model key would let a flow agent step reach the real API.
      if (v === undefined || k.startsWith("UE_MCP_") || k.startsWith("ANTHROPIC_")) continue;
      // A developer's npm config would otherwise reach the fake's children.
      if (/^npm_/i.test(k)) continue;
      env[k] = v;
    }
    const pathKey = Object.keys(env).find((k) => k.toUpperCase() === "PATH") ?? "PATH";
    env[pathKey] = `${this.bin}${path.delimiter}${env[pathKey] ?? ""}`;
    Object.assign(env, {
      HOME: this.home,
      USERPROFILE: this.home,
      APPDATA: path.join(this.home, "AppData", "Roaming"),
      XDG_CONFIG_HOME: path.join(this.home, ".config"),
      UE_MCP_GLOBAL_CONFIG: path.join(this.home, ".ue-mcp", "config.yml"),
      UE_MCP_USER_STATE: path.join(this.home, ".ue-mcp", "state.json"),
      UE_MCP_AUTH_DIR: path.join(this.home, ".ue-mcp"),
      UE_MCP_DAEMON_DIR: path.join(this.home, ".ue-mcp", "daemons"),
      UE_MCP_EXTENSIONS_DIR: path.join(this.home, ".ue-mcp", "extensions"),
      UE_MCP_PENDING_DIR: path.join(this.home, ".ue-mcp", "pending-feedback"),
      UE_MCP_REGISTRY_CACHE: path.join(this.home, ".ue-mcp", "registry-catalog.json"),
      UE_MCP_VERSION_CACHE: path.join(this.home, ".ue-mcp", "version-check.json"),
      UE_MCP_DISABLE_UPDATE_CHECK: "1",
      UE_MCP_LOG_LEVEL: "error",
      UE_MCP_REGISTRY: DEAD_ORIGIN,
      UE_MCP_FEEDBACK: DEAD_ORIGIN,
      UE_MCP_FEEDBACK_ENDPOINT: `${DEAD_ORIGIN}/`,
      UE_MCP_GITHUB_OAUTH: DEAD_ORIGIN,
      UE_MCP_GITHUB_API: DEAD_ORIGIN,
      UE_MCP_NPM_REGISTRY: DEAD_ORIGIN,
      ANTHROPIC_BASE_URL: DEAD_ORIGIN,
      UE_MCP_BINARIES: path.join(this.root, "no-binaries"),
      UE_MCP_PROTECTED_ENGINE_ROOTS: "",
      NPM_FAKE_LOG: this.npmLog,
      NO_COLOR: "1",
    });
    for (const [k, v] of Object.entries(extra)) {
      if (v === undefined) delete env[k];
      else env[k] = v;
    }
    return env;
  }

  /** Every npm invocation the fake saw, in order. */
  npmCalls(): Array<{ args: string[]; cwd: string }> {
    if (!fs.existsSync(this.npmLog)) return [];
    return fs.readFileSync(this.npmLog, "utf8").split("\n").filter(Boolean).map((l) => JSON.parse(l));
  }

  /**
   * `npm` on PATH, as a recording fake. `NPM_FAKE_SCRIPT` names a module that
   * decides what each call does; without one every call succeeds silently.
   */
  private installFakeNpm(): void {
    const js = path.join(this.bin, "fake-npm.mjs");
    fs.writeFileSync(
      js,
      `import fs from "node:fs";
const args = process.argv.slice(2);
if (process.env.NPM_FAKE_LOG) fs.appendFileSync(process.env.NPM_FAKE_LOG, JSON.stringify({ args, cwd: process.cwd() }) + "\\n");
let code = 0;
if (process.env.NPM_FAKE_SCRIPT) {
  const mod = await import(new URL("file:///" + process.env.NPM_FAKE_SCRIPT.replace(/\\\\/g, "/")).href);
  code = (await mod.default(args, process.cwd())) ?? 0;
}
process.exit(code);
`,
    );
    fs.writeFileSync(path.join(this.bin, "npm.cmd"), `@"${process.execPath}" "${js}" %*\r\n`);
    const sh = path.join(this.bin, "npm");
    fs.writeFileSync(sh, `#!/bin/sh\nexec "${process.execPath}" "${js}" "$@"\n`);
    fs.chmodSync(sh, 0o755);
  }

  cleanup(): void {
    try {
      fs.rmSync(this.root, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
    } catch {
      // A detached child can still hold a log open on Windows; it is the OS temp folder.
    }
  }
}

export interface CliResult {
  code: number | null;
  stdout: string;
  stderr: string;
}

export interface CliOptions {
  cwd?: string;
  env?: Record<string, string | undefined>;
  input?: string;
  timeoutMs?: number;
  /** Run another entry under tsx instead of src/index.ts (a package bin). */
  entry?: string;
}

/** `ue-mcp <args>`, spawned, run to exit. */
export function runCli(sb: Sandbox, args: string[], opts: CliOptions = {}): Promise<CliResult> {
  return new Promise((resolve, reject) => {
    const child = spawn(process.execPath, ["--import", TSX, opts.entry ?? ENTRY, ...args], {
      cwd: opts.cwd ?? sb.root,
      env: sb.env(opts.env),
      stdio: ["pipe", "pipe", "pipe"],
      windowsHide: true,
    });
    let stdout = "";
    let stderr = "";
    child.stdout.on("data", (d) => (stdout += d));
    child.stderr.on("data", (d) => (stderr += d));
    const timer = setTimeout(() => {
      child.kill();
      reject(new Error(`ue-mcp ${args.join(" ")} did not exit in ${opts.timeoutMs ?? 60_000}ms\nstdout:\n${stdout}\nstderr:\n${stderr}`));
    }, opts.timeoutMs ?? 60_000);
    child.on("error", (e) => {
      clearTimeout(timer);
      reject(e);
    });
    child.on("close", (code) => {
      clearTimeout(timer);
      resolve({ code, stdout, stderr });
    });
    child.stdin.end(opts.input ?? "");
  });
}

export interface ServerOptions {
  projects: string[];
  strategy?: "micro" | "lean" | "full";
  mode?: "daemon" | "in-process";
  /** UE_MCP_PORT for every session. 1 (editor down) unless a fake bridge is found by lockfile. */
  port?: number | null;
  env?: Record<string, string | undefined>;
  cwd?: string;
  /** Node arguments before the projects. Defaults to the source entry under tsx; a test of the built package names its dist/index.js. */
  entry?: string[];
}

export interface CallResult {
  isError: boolean;
  blocks: string[];
  text: string;
}

/** A real ue-mcp server on stdio with an MCP client attached. */
export class McpServer {
  private constructor(
    readonly client: Client,
    private readonly transport: StdioClientTransport,
    readonly sb: Sandbox,
    private readonly logPath: string,
  ) {}

  static async start(sb: Sandbox, opts: ServerOptions): Promise<McpServer> {
    const logPath = sb.path(`server-${Date.now()}-${Math.random().toString(36).slice(2, 7)}.log`);
    const logFd = fs.openSync(logPath, "a");
    const extra: Record<string, string | undefined> = {
      UE_MCP_SERVER_MODE: opts.mode ?? "in-process",
      UE_MCP_PORT: opts.port === null ? undefined : String(opts.port ?? 1),
      UE_MCP_CONTEXT_STRATEGY: opts.strategy,
      UE_MCP_DAEMON_IDLE_MS: "60000",
      ...opts.env,
    };
    const client = new Client({ name: "ue-mcp-integration", version: "1.0.0" }, { capabilities: {} });
    const transport = new StdioClientTransport({
      command: process.execPath,
      args: [...(opts.entry ?? ["--import", TSX, ENTRY]), ...opts.projects],
      cwd: opts.cwd ?? sb.root,
      env: sb.env(extra),
      stderr: logFd,
    });
    try {
      await client.connect(transport);
    } finally {
      try { fs.closeSync(logFd); } catch { /* the child holds its own handle */ }
    }
    return new McpServer(client, transport, sb, logPath);
  }

  get instructions(): string {
    return this.client.getInstructions() ?? "";
  }

  get log(): string {
    return fs.existsSync(this.logPath) ? fs.readFileSync(this.logPath, "utf8") : "";
  }

  async listTools(): Promise<Array<{ name: string; description: string; inputSchema: unknown }>> {
    const listed = await this.client.listTools();
    return listed.tools.map((t) => ({ name: t.name, description: t.description ?? "", inputSchema: t.inputSchema as unknown }));
  }

  async toolNames(): Promise<string[]> {
    return (await this.listTools()).map((t) => t.name).sort();
  }

  async call(name: string, args: Record<string, unknown>, timeoutMs = 60_000): Promise<CallResult> {
    const raw = await this.client.callTool({ name, arguments: args }, undefined, { timeout: timeoutMs });
    const blocks = ((raw.content ?? []) as Array<{ type: string; text?: string }>)
      .filter((b) => b.type === "text")
      .map((b) => b.text ?? "");
    return { isError: raw.isError === true, blocks, text: blocks.join("\n") };
  }

  async close(): Promise<void> {
    await this.client.close().catch(() => undefined);
    await this.transport.close().catch(() => undefined);
    await stopSandboxDaemons(this.sb);
  }
}

/** Shut down every daemon a sandbox's discovery directory knows about. */
export async function stopSandboxDaemons(sb: Sandbox): Promise<void> {
  const dir = path.join(sb.home, ".ue-mcp", "daemons");
  if (!fs.existsSync(dir)) return;
  let any = false;
  for (const f of fs.readdirSync(dir).filter((n) => n.endsWith(".json"))) {
    try {
      const d = JSON.parse(fs.readFileSync(path.join(dir, f), "utf8")) as { port: number; token: string; pid?: number };
      any = true;
      await fetch(`http://127.0.0.1:${d.port}/v1/shutdown`, { method: "POST", headers: { authorization: `Bearer ${d.token}` } }).catch(() => undefined);
    } catch {
      // half-written or already gone
    }
  }
  if (any) await new Promise((r) => setTimeout(r, 700));
}

/** The first non-machine text block, parsed as JSON. */
export function json<T = Record<string, unknown>>(r: CallResult): T {
  const body = r.blocks.find((b) => !b.startsWith("MACHINE_") && !b.startsWith("UE_MCP_")) ?? r.text;
  return JSON.parse(body) as T;
}

/** Text with ANSI colour codes removed; some commands colour even into a pipe. */
export function plain(s: string): string {
  return s.replace(new RegExp(`${String.fromCharCode(27)}\\[[0-9;]*m`, "g"), "");
}

/** Recursive snapshot of a tree as relative path -> content, for "wrote nothing" checks. */
export function snapshotTree(dir: string): Record<string, string> {
  const out: Record<string, string> = {};
  const walk = (d: string) => {
    for (const e of fs.readdirSync(d, { withFileTypes: true })) {
      const full = path.join(d, e.name);
      if (e.isDirectory()) walk(full);
      else out[path.relative(dir, full).split(path.sep).join("/")] = fs.readFileSync(full, "utf8");
    }
  };
  if (fs.existsSync(dir)) walk(dir);
  return out;
}

export interface FakeRequest {
  method: string;
  url: string;
  headers: http.IncomingHttpHeaders;
  body: string;
}

/** A local HTTP server standing in for a remote service. The handler answers each request. */
export class FakeHttp {
  readonly requests: FakeRequest[] = [];
  private constructor(private readonly server: http.Server) {}

  static async start(
    handler: (req: FakeRequest) => { status?: number; json?: unknown; body?: string | Buffer; headers?: Record<string, string> },
  ): Promise<FakeHttp> {
    let self: FakeHttp | undefined;
    const server = http.createServer((req, res) => {
      const chunks: Buffer[] = [];
      req.on("data", (c) => chunks.push(c as Buffer));
      req.on("end", () => {
        const r: FakeRequest = { method: req.method ?? "GET", url: req.url ?? "/", headers: req.headers, body: Buffer.concat(chunks).toString("utf8") };
        self?.requests.push(r);
        let out: ReturnType<typeof handler>;
        try {
          out = handler(r);
        } catch (e) {
          out = { status: 500, body: String(e) };
        }
        const headers: Record<string, string> = { ...(out.headers ?? {}) };
        let payload: string | Buffer = out.body ?? "";
        if (out.json !== undefined) {
          payload = JSON.stringify(out.json);
          headers["content-type"] ??= "application/json";
        }
        res.writeHead(out.status ?? 200, headers);
        res.end(payload);
      });
    });
    await new Promise<void>((resolve) => server.listen(0, "127.0.0.1", () => resolve()));
    self = new FakeHttp(server);
    return self;
  }

  get origin(): string {
    const a = this.server.address();
    if (a === null || typeof a === "string") throw new Error("not listening");
    return `http://127.0.0.1:${a.port}`;
  }

  async stop(): Promise<void> {
    this.server.closeAllConnections?.();
    await new Promise<void>((r) => this.server.close(() => r()));
  }
}

/** Poll until `check` returns a truthy value, or fail after `ms`. */
export async function waitFor<T>(check: () => T | Promise<T>, ms = 20_000, what = "condition"): Promise<T> {
  const until = Date.now() + ms;
  let last: unknown;
  while (Date.now() < until) {
    try {
      const v = await check();
      if (v) return v;
    } catch (e) {
      last = e;
    }
    await new Promise((r) => setTimeout(r, 150));
  }
  throw new Error(`timed out waiting for ${what}${last ? `: ${String(last)}` : ""}`);
}
