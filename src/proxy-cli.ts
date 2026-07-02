#!/usr/bin/env node
import { ProjectContext } from "./project.js";
import { startProxyDaemon } from "./proxy.js";

/**
 * `ue-mcp-proxy <path-to-.uproject-or-project-dir>` - run the relay daemon for
 * one project. Normally spawned automatically by the stdio server (see
 * proxy-client.ts), but it can be launched by hand to keep a warm editor
 * connection alive across client restarts.
 */
async function main() {
  const projectArg = process.argv.find(
    (a) => !a.startsWith("-") && a !== process.argv[0] && a !== process.argv[1],
  );
  if (!projectArg) {
    console.error("Usage: ue-mcp-proxy <path-to-.uproject-or-project-dir>");
    process.exit(2);
  }

  const project = new ProjectContext();
  try {
    project.setProject(projectArg);
  } catch (e) {
    console.error(`[ue-mcp-proxy] Failed to load project: ${e instanceof Error ? e.message : e}`);
    process.exit(2);
  }

  await startProxyDaemon(project);
}

main().catch((e) => {
  console.error(`[ue-mcp-proxy] Fatal: ${e instanceof Error ? e.message : e}`);
  process.exit(1);
});
