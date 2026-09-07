/**
 * A real ue-mcp server, driven over stdio, for the live tests (#817, plan 7.3).
 *
 * The engine-free multi-editor tests asserts routing by calling the routing
 * functions. That is the right level for a suite with no engine, and it leaves
 * one thing unproven: that the SERVER wires those functions together the way
 * the tests assume. A live case that matters (a leaked routing parameter, a
 * refusal that never fires, a union that advertises what dispatch cannot
 * serve) is a wiring failure, so the cases here go through the shipped entry
 * point, `initialize`, `tools/list` and `tools/call`, exactly as a client does.
 *
 * Two editors, with one engine: the live editor is one session, and a
 * throwaway project whose editor is not running is the other. Startup
 * registers every argv project regardless of editor state (plan item 4.4), and
 * the session COUNT is what arms addressing, gating, attribution and the union
 * refusal. So every "beyond one editor" case is reachable on a machine with a
 * single editor, and the editor that does exist is a real one.
 *
 * Nothing here launches or stops an editor.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";

const here = path.dirname(fileURLToPath(import.meta.url));
export const REPO_ROOT = path.resolve(here, "..", "..");

export interface LiveServerOptions {
  /** `.uproject` paths, in argv order. The first one is the active session. */
  projects: string[];
  /** Pins the bridge port for every session. Only the no-project case wants it. */
  port?: number;
  host?: string;
  /** Extra environment for the server process. */
  env?: Record<string, string>;
}

/** One running server plus the client attached to it. */
export class LiveServer {
  private constructor(
    readonly client: Client,
    private readonly transport: StdioClientTransport,
    private readonly sandbox: string,
    private readonly logPath: string,
  ) {}

  static async start(options: LiveServerOptions): Promise<LiveServer> {
    const sandbox = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-live-server-"));
    const logPath = path.join(sandbox, "server.log");
    const logFd = fs.openSync(logPath, "a");

    const env: NodeJS.ProcessEnv = {};
    for (const [key, value] of Object.entries(process.env)) {
      // A live tests that inherited the shell's UE_MCP_* settings would be
      // testing this machine's configuration rather than the server.
      if (key.startsWith("UE_MCP_")) continue;
      env[key] = value;
    }
    env.HOME = sandbox;
    env.USERPROFILE = sandbox;
    env.UE_MCP_HOST = options.host ?? "127.0.0.1";
    if (options.port !== undefined) env.UE_MCP_PORT = String(options.port);
    env.UE_MCP_GLOBAL_CONFIG = path.join(sandbox, "global-config.yml");
    env.UE_MCP_USER_STATE = path.join(sandbox, "state.json");
    env.UE_MCP_AUTH_DIR = path.join(sandbox, "auth");
    env.UE_MCP_DISABLE_UPDATE_CHECK = "1";
    env.UE_MCP_LOG_LEVEL = "error";
    Object.assign(env, options.env ?? {});

    const client = new Client({ name: "ue-mcp-live-tests", version: "1.0.0" }, { capabilities: {} });
    const transport = new StdioClientTransport({
      command: process.execPath,
      args: ["--import", "tsx", path.join(REPO_ROOT, "src", "index.ts"), ...options.projects],
      cwd: REPO_ROOT,
      env: env as Record<string, string>,
      stderr: logFd,
    });

    try {
      await client.connect(transport);
    } finally {
      try { fs.closeSync(logFd); } catch { /* the child holds its own handle */ }
    }
    return new LiveServer(client, transport, sandbox, logPath);
  }

  /** The `initialize` instructions this server handed the client. */
  get instructions(): string {
    return this.client.getInstructions() ?? "";
  }

  /** Everything the server wrote to stderr, which is where startup narrates. */
  get log(): string {
    return fs.existsSync(this.logPath) ? fs.readFileSync(this.logPath, "utf-8") : "";
  }

  async listToolNames(): Promise<string[]> {
    const listed = await this.client.listTools();
    return listed.tools.map((t) => t.name).sort();
  }

  async listTools(): Promise<Array<{ name: string; description: string; inputSchema: unknown }>> {
    const listed = await this.client.listTools();
    return listed.tools.map((t) => ({
      name: t.name,
      description: t.description ?? "",
      inputSchema: t.inputSchema as unknown,
    }));
  }

  /** One `tools/call`, flattened to the text blocks and the error flag. */
  async call(name: string, args: Record<string, unknown>): Promise<LiveCallResult> {
    const raw = await this.client.callTool({ name, arguments: args });
    const blocks = (raw.content ?? []) as Array<{ type: string; text?: string }>;
    const texts = blocks.filter((b) => b.type === "text").map((b) => b.text ?? "");
    return {
      isError: raw.isError === true,
      blocks: texts,
      text: texts.join("\n"),
    };
  }

  async close(): Promise<void> {
    await this.client.close().catch(() => undefined);
    await this.transport.close().catch(() => undefined);
    fs.rmSync(this.sandbox, { recursive: true, force: true });
  }
}

export interface LiveCallResult {
  isError: boolean;
  /** Every text block, in order. Attribution and machine blocks are their own. */
  blocks: string[];
  text: string;
}

/** The editor a response says served it, or null when it carried no block. */
export function servingEditor(result: LiveCallResult): string | null {
  const block = result.blocks.find((b) => b.startsWith("MACHINE_EDITOR="));
  if (!block) return null;
  const parsed = JSON.parse(block.slice("MACHINE_EDITOR=".length)) as { editor?: string };
  return parsed.editor ?? null;
}

/** The first text block parsed as JSON, which is how tool results arrive. */
export function resultJson<T = Record<string, unknown>>(result: LiveCallResult): T {
  const body = result.blocks.find((b) => !b.startsWith("MACHINE_")) ?? "";
  return JSON.parse(body) as T;
}
