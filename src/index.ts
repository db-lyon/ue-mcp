#!/usr/bin/env node
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { error, info, warn } from "./core/log.js";
import { resolveServerMode } from "./daemon/server-mode.js";
import { startVersionCheck } from "./core/version-check.js";
import { startFlowHttpServer } from "./flow/http-server.js";
import { packageVersion } from "./core/package-root.js";
import { formatTopLevelUsage, routeTopLevel, runCliCommand } from "./cli/cli-commands.js";
import { createRuntime } from "./server/runtime.js";
import { createMcpServer } from "./server/mcp-server.js";

const projectArgs = (): string[] => process.argv.slice(2).filter((a) => !a.startsWith("-"));

async function main() {
  // The next tool response carries the notice if a newer version is published.
  startVersionCheck(packageVersion());

  // #817: every positional argument is a project with its own session.
  const serving = resolveServerMode(projectArgs());
  if (serving.mode === "in-process") info("server", `serving in process (${serving.reason})`);
  const rt = await createRuntime(projectArgs());
  const { server } = createMcpServer(rt);

  // ── Optional HTTP surface for flow.run (#144) ───────────────────
  // Opt-in via `ue-mcp.http`; binds to 127.0.0.1 only.
  if (rt.project.config.http?.enabled) {
    try {
      startFlowHttpServer(rt.flowTool, rt.baseCtx, {
        port: rt.project.config.http.port,
        host: rt.project.config.http.host,
      });
    } catch (e) {
      error("http", "Failed to start HTTP server", e);
    }
  }

  await rt.connectBridges();
  rt.logSummary();

  await server.connect(new StdioServerTransport());
  // The client closing stdin ends the session; without this it waits out its
  // kill timeout. A server also serving flows over HTTP stays up for those.
  if (!rt.project.config.http?.enabled) process.stdin.once("end", () => process.exit(0));
}

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
  // outlives editor restarts. A daemon that cannot be started falls back to
  // in process, so a client is never left without a server.
  void import("./daemon/shim.js").then(async (m) => {
    try {
      await m.ensureDaemon(projectArgs()[0]);
    } catch (e) {
      warn("shim", "daemon unavailable; serving in process", e);
      await main();
      return;
    }
    await m.runShim(projectArgs()[0]);
  }).catch((e) => {
    error("shim", "Fatal error", e);
    process.exit(1);
  });
} else {
  main().catch((e) => {
    error("server", "Fatal error", e);
    process.exit(1);
  });
}
