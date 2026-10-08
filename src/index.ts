#!/usr/bin/env node
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { error } from "./core/log.js";
import { readEnv } from "./core/env.js";
import { startVersionCheck } from "./core/version-check.js";
import { startFlowHttpServer } from "./flow/http-server.js";
import { packageVersion } from "./core/package-root.js";
import { findCliCommand, runCliCommand } from "./cli/cli-commands.js";
import { createRuntime } from "./server/runtime.js";
import { createMcpServer } from "./server/mcp-server.js";

const projectArgs = (): string[] => process.argv.slice(2).filter((a) => !a.startsWith("-"));

async function main() {
  // The next tool response carries the notice if a newer version is published.
  startVersionCheck(packageVersion());

  // #817: every positional argument is a project with its own session.
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
}

// A first argument naming a command runs it; anything else starts the server.
const command = findCliCommand(process.argv[2]);
if (command) {
  void runCliCommand(command, process.argv.slice(3));
} else if (readEnv("daemon") === "1" && projectArgs().length === 1) {
  // Opt-in: relay to the project's daemon instead of serving in process.
  void import("./daemon/shim.js").then((m) => m.runShim(projectArgs()[0])).catch((e) => {
    error("shim", "Fatal error", e);
    process.exit(1);
  });
} else {
  main().catch((e) => {
    error("server", "Fatal error", e);
    process.exit(1);
  });
}
