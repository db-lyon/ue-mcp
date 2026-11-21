#!/usr/bin/env node
// @session per-session: Wires the registry, the per-session loads and the MCP server together.
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { InMemoryTransport } from "@modelcontextprotocol/sdk/inMemory.js";
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { error, info } from "./core/log.js";
import type { InProcessServer } from "./daemon/shim.js";
import { startupInstructions } from "./server/startup-instructions.js";
import { resolveServerMode } from "./daemon/server-mode.js";
import { startVersionCheck } from "./core/version-check.js";
import { startFlowHttpServer } from "./flow/http-server.js";
import { packageVersion } from "./core/package-root.js";
import { formatTopLevelUsage, routeTopLevel, runCliCommand } from "./cli/cli-commands.js";
import { guardFetchExitAbort } from "./cli/cli-main.js";
import { createRuntime } from "./server/runtime.js";
import { createMcpServer } from "./server/mcp-server.js";

const projectArgs = (): string[] => process.argv.slice(2).filter((a) => !a.startsWith("-"));

/** The whole server in this process, connected to its editors but to no client yet. */
async function startInProcess(): Promise<{ server: McpServer; outlivesClient: boolean }> {
  // The next tool response carries the notice if a newer version is published.
  startVersionCheck(packageVersion());

  // #817: every positional argument is a project with its own session.
  const rt = await createRuntime(projectArgs());
  const { server } = createMcpServer(rt);

  // ── Optional HTTP surface for flow.run (#144) ───────────────────
  // Opt-in via `ue-mcp.http`; binds to 127.0.0.1 only.
  if (rt.project.config.http?.enabled) {
    try {
      startFlowHttpServer(rt.flowRequest, {
        port: rt.project.config.http.port,
        host: rt.project.config.http.host,
      });
    } catch (e) {
      error("http", "Failed to start HTTP server", e);
    }
  }

  await rt.connectBridges();
  rt.logSummary();
  // A server also serving flows over HTTP stays up for those after the client goes.
  return { server, outlivesClient: !!rt.project.config.http?.enabled };
}

async function main() {
  const serving = resolveServerMode(projectArgs());
  if (serving.mode === "in-process") info("server", `serving in process (${serving.reason})`);
  const { server, outlivesClient } = await startInProcess();
  await server.connect(new StdioServerTransport());
  // The client closing stdin ends the session; without this it waits out its
  // kill timeout.
  if (!outlivesClient) process.stdin.once("end", () => process.exit(0));
}

/** The in-process server behind the shim's stdio, for when the daemon cannot start. */
async function inProcessBehindShim(): Promise<InProcessServer> {
  const { server, outlivesClient } = await startInProcess();
  const [client, serverSide] = InMemoryTransport.createLinkedPair();
  await server.connect(serverSide);
  return { transport: client, outlivesClient };
}

guardFetchExitAbort();

// A first argument naming a command runs it; help and stray flags print usage;
// anything else starts the server.
const route = routeTopLevel(process.argv.slice(2));
if (route.kind === "help") {
  console.log(formatTopLevelUsage());
  process.exit(0);
} else if (route.kind === "unknown-flags") {
  console.error(`Unknown flag${route.flags.length > 1 ? "s" : ""}: ${route.flags.join(", ")}\n\n${formatTopLevelUsage()}`);
  process.exit(2);
} else if (route.kind === "command") {
  void runCliCommand(route.command, route.argv);
} else if (resolveServerMode(projectArgs()).mode === "daemon") {
  // Relay to the project's daemon, which owns the editor connection and
  // outlives editor restarts. The shim answers initialize at once, and a
  // daemon that cannot be started falls back to in process behind it, so a
  // client is never left without a server.
  const uproject = projectArgs()[0];
  void import("./daemon/shim.js").then((m) =>
    m.runShim(uproject, { instructions: () => startupInstructions(uproject), inProcess: inProcessBehindShim }),
  ).catch((e) => {
    error("shim", "Fatal error", e);
    process.exit(1);
  });
} else {
  main().catch((e) => {
    error("server", "Fatal error", e);
    process.exit(1);
  });
}
