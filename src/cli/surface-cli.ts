// @session independent: Measures whatever MCP server command it is given; never talks to an editor.
/**
 * `ue-mcp surface [flags] -- <command...>` and `ue-mcp surface --url <url>`:
 * start any MCP server, send initialize and tools/list, and print what it
 * advertises (tool count, action count, schema size). The rules are in
 * surface-measure.ts and docs/verify.md; they apply to every server alike.
 */
import type { ChildProcess } from "node:child_process";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StreamableHTTPClientTransport } from "@modelcontextprotocol/sdk/client/streamableHttp.js";
import { ReadBuffer, serializeMessage } from "@modelcontextprotocol/sdk/shared/stdio.js";
import type { Transport } from "@modelcontextprotocol/sdk/shared/transport.js";
import type { JSONRPCMessage } from "@modelcontextprotocol/sdk/types.js";
import { commandInvocation, spawnInvocation } from "../core/safe-spawn.js";
import { packageVersion } from "../core/package-root.js";
import { flagValue } from "./args.js";
import type { CliUsage } from "./cli-main.js";
import { formatSurface, measureSurface, type ListedTool, type SurfaceReport } from "./surface-measure.js";

export const usage: CliUsage = {
  usage: "ue-mcp surface [flags] -- <command> [args...]   |   ue-mcp surface --url <url> [flags]",
  summary:
    "Start any MCP server, list its tools and print the tool count, action count and input schema size.\n" +
    "Actions: one per enumerated value of a tool's action, operation or command parameter; 1 for any other tool.",
  strict: true,
  flags: [
    { name: "--json", description: "Print one JSON document" },
    { name: "--url", value: "<url>", description: "Measure a streamable HTTP server instead of starting a command" },
    { name: "--env", value: "<KEY=VALUE>", description: "Set a variable for the started server (repeatable)" },
    { name: "--timeout", value: "<seconds>", description: "Give up after this long (default 180, room for a first npx or uvx download)" },
    { name: "--per-tool", description: "Also list each tool's action count and schema size" },
  ],
};

/** The parsed command line. */
export interface SurfaceArgs {
  json: boolean;
  perTool: boolean;
  url?: string;
  env: Record<string, string>;
  timeoutMs: number;
  command: string[];
}

const VALUE_FLAGS = new Set(["--url", "--env", "--timeout"]);

/** Split `ue-mcp surface` argv into its own flags and the server command after `--`. */
export function parseSurfaceArgs(argv: readonly string[]): SurfaceArgs | { error: string } {
  const sep = argv.indexOf("--");
  const own = sep === -1 ? [...argv] : argv.slice(0, sep);
  let command = sep === -1 ? [] : argv.slice(sep + 1);
  if (sep === -1) {
    // Without a separator, the first word that is not a flag or a flag's value starts the command.
    for (let i = 0; i < own.length; i++) {
      const a = own[i];
      if (VALUE_FLAGS.has(a)) {
        i++;
        continue;
      }
      if (!a.startsWith("-")) {
        command = own.splice(i);
        break;
      }
    }
  }
  const env: Record<string, string> = {};
  for (let i = 0; i < own.length; i++) {
    const a = own[i];
    const value = a === "--env" ? own[i + 1] : a.startsWith("--env=") ? a.slice(6) : undefined;
    if (value === undefined) continue;
    const eq = value.indexOf("=");
    if (eq <= 0) return { error: `--env takes KEY=VALUE, got '${value}'.` };
    env[value.slice(0, eq)] = value.slice(eq + 1);
  }
  const timeoutRaw = flagValue(own, "--timeout");
  const timeoutS = timeoutRaw === undefined ? 180 : Number(timeoutRaw);
  if (!Number.isFinite(timeoutS) || timeoutS <= 0) return { error: `--timeout takes a number of seconds, got '${timeoutRaw}'.` };
  const url = flagValue(own, "--url");
  if (url !== undefined && command.length > 0) return { error: "Give either --url or a command after --, not both." };
  if (url === undefined && command.length === 0) return { error: "Name the server to measure: ue-mcp surface -- <command> [args...], or --url <url>." };
  if (url !== undefined) {
    try {
      new URL(url);
    } catch {
      return { error: `--url '${url}' is not a URL.` };
    }
  }
  return { json: own.includes("--json"), perTool: own.includes("--per-tool"), url, env, timeoutMs: timeoutS * 1000, command };
}

/**
 * An MCP client transport over a child's stdio, started hidden and without a
 * shell (a Windows batch shim such as npx.cmd goes through cmd.exe on a line
 * of checked tokens). The child's stderr is kept for the error message.
 */
class ChildStdioTransport implements Transport {
  onclose?: () => void;
  onerror?: (error: Error) => void;
  onmessage?: (message: JSONRPCMessage) => void;
  private child?: ChildProcess;
  private readonly buffer = new ReadBuffer();
  stderrTail = "";
  exitCode: number | null = null;
  protocolVersion?: string;

  setProtocolVersion(version: string): void {
    this.protocolVersion = version;
  }

  constructor(
    private readonly command: string[],
    private readonly env: Record<string, string>,
  ) {}

  start(): Promise<void> {
    const [name, ...args] = this.command;
    const child = spawnInvocation(commandInvocation(name, args), {
      env: { ...process.env, ...this.env },
      stdio: ["pipe", "pipe", "pipe"],
    });
    this.child = child;
    return new Promise((resolve, reject) => {
      child.once("error", (e) => {
        reject(new Error(`could not start '${name}': ${e.message}`));
        this.onerror?.(e);
      });
      child.once("spawn", () => resolve());
      child.stdout?.on("data", (chunk: Buffer) => {
        this.buffer.append(chunk);
        for (;;) {
          let message: JSONRPCMessage | null;
          try {
            message = this.buffer.readMessage();
          } catch {
            // A line that is not JSON-RPC (a stray log line on stdout) is skipped, as most clients do.
            continue;
          }
          if (!message) break;
          this.onmessage?.(message);
        }
      });
      child.stderr?.on("data", (chunk: Buffer) => {
        this.stderrTail = (this.stderrTail + chunk.toString("utf8")).slice(-4000);
      });
      child.stdin?.on("error", (e) => this.onerror?.(e));
      child.on("close", (code) => {
        this.exitCode = code;
        this.child = undefined;
        this.onclose?.();
      });
    });
  }

  send(message: JSONRPCMessage): Promise<void> {
    return new Promise((resolve) => {
      const stdin = this.child?.stdin;
      if (!stdin || !stdin.writable) return resolve();
      if (stdin.write(serializeMessage(message))) resolve();
      else stdin.once("drain", resolve);
    });
  }

  async close(): Promise<void> {
    const child = this.child;
    if (!child) return;
    child.stdin?.end();
    await new Promise<void>((resolve) => {
      const timer = setTimeout(() => {
        // The child this command started, and nothing else: a tree kill on Windows, where npx and uvx wrap the server.
        if (process.platform === "win32" && child.pid) {
          spawnInvocation({ command: "taskkill", args: ["/pid", String(child.pid), "/t", "/f"] }, { stdio: "ignore" }).on("close", () => resolve()); // lint-prose-allow: no-kill
        } else {
          child.kill();
          resolve();
        }
      }, 2000);
      child.once("close", () => {
        clearTimeout(timer);
        resolve();
      });
    });
  }
}

async function listAllTools(client: Client): Promise<ListedTool[]> {
  const tools: ListedTool[] = [];
  let cursor: string | undefined;
  for (let page = 0; page < 1000; page++) {
    const res = await client.listTools(cursor ? { cursor } : undefined);
    tools.push(...res.tools.map((t) => ({ name: t.name, description: t.description, inputSchema: t.inputSchema })));
    cursor = res.nextCursor;
    if (!cursor) break;
  }
  return tools;
}

/** Connect, list and measure. Exported for the tests. */
export async function measureServer(args: SurfaceArgs): Promise<SurfaceReport> {
  const client = new Client({ name: "ue-mcp-surface", version: packageVersion() }, { capabilities: {} });
  const stdio = args.url ? undefined : new ChildStdioTransport(args.command, args.env);
  const transport: Transport = stdio ?? new StreamableHTTPClientTransport(new URL(args.url!));
  let timer: NodeJS.Timeout | undefined;
  const timeout = new Promise<never>((_, reject) => {
    timer = setTimeout(() => reject(new Error(`no answer to initialize and tools/list within ${args.timeoutMs / 1000}s`)), args.timeoutMs);
  });
  try {
    const work = (async () => {
      await client.connect(transport, { timeout: args.timeoutMs });
      const tools = await listAllTools(client);
      const info = client.getServerVersion();
      return measureSurface(tools, {
        server: info ? { name: info.name, version: info.version } : null,
        protocolVersion: (transport as { protocolVersion?: string }).protocolVersion ?? null,
        instructions: client.getInstructions() ?? null,
      });
    })();
    return await Promise.race([work, timeout]);
  } catch (e) {
    const message = e instanceof Error ? e.message : String(e);
    const tail = stdio?.stderrTail.trim();
    const exited = stdio && stdio.exitCode !== null ? ` (the server exited with code ${stdio.exitCode})` : "";
    throw new Error(`${message}${exited}${tail ? `\n--- server stderr (last lines) ---\n${tail.split(/\r?\n/).slice(-15).join("\n")}` : ""}`);
  } finally {
    clearTimeout(timer);
    await client.close().catch(() => undefined);
    await transport.close().catch(() => undefined);
  }
}

/** Entry point for `ue-mcp surface`. */
export async function run(argv: string[]): Promise<number | void> {
  const parsed = parseSurfaceArgs(argv);
  const json = argv.slice(0, argv.indexOf("--") === -1 ? argv.length : argv.indexOf("--")).includes("--json");
  if ("error" in parsed) {
    if (json) process.stdout.write(JSON.stringify({ ok: false, error: parsed.error }, null, 2) + "\n");
    else console.error(parsed.error);
    return 2;
  }
  try {
    const report = await measureServer(parsed);
    const target = parsed.url ? { url: parsed.url } : { command: parsed.command };
    if (parsed.json) process.stdout.write(JSON.stringify({ ok: true, ...target, measuredAt: new Date().toISOString(), ...report }, null, 2) + "\n");
    else console.log(formatSurface(report, { perTool: parsed.perTool }));
    return 0;
  } catch (e) {
    const message = e instanceof Error ? e.message : String(e);
    if (parsed.json) process.stdout.write(JSON.stringify({ ok: false, error: message }, null, 2) + "\n");
    else console.error(`surface: ${message}`);
    return 1;
  }
}
