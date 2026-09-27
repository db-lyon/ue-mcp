import type { SessionRegistry } from "../sessions/session.js";
import { ueMcpConfigRejections, describeConfigRejections } from "../config/project.js";
import { attach, attachSummary } from "../editor/deployer.js";
import { checkPluginFreshness } from "../editor/bridge-freshness.js";
import { collapsingEnvWarnings } from "../config/session-env.js";
import { info, warn, error } from "../core/log.js";

/**
 * Register one session per project argument, reporting each by name. A
 * positional that fails to load is named rather than silently dropped, and
 * with none left the project-less default session attaches to 9877.
 */
export function registerSessions(sessions: SessionRegistry, projectArgs: string[]): void {
  for (const arg of projectArgs) {
    try {
      const session = sessions.register({ projectPath: arg });
      info(
        "server",
        `Project loaded: ${session.project.projectName} (engine ${session.project.engineAssociation ?? "unknown"})` +
          (projectArgs.length > 1 ? ` as editor '${session.name}' on port ${session.bridge.port}` : ""),
      );

      // Non-destructive attach; deployment is reserved for `ue-mcp init` / `ue-mcp deploy`.
      const result = attach(session.project);
      info("deploy", attachSummary(result));

      // #785: a compiled plugin older than its source otherwise only shows up
      // later as "Unknown method", which reads as "not implemented yet".
      const freshness = checkPluginFreshness(session.project.projectPath);
      if (freshness.stale && freshness.message) {
        warn("deploy", freshness.message);
      }

      // D3: a malformed `ue-mcp:` key is dropped on its own, and named here.
      for (const line of describeConfigRejections(ueMcpConfigRejections(session.project.projectDir))) {
        warn("config", line);
      }
    } catch (e) {
      error("server", `Failed to initialize project '${arg}'`, e);
    }
  }

  // Which environment variables decide for every editor at once. Silent at one.
  for (const line of collapsingEnvWarnings(sessions.list().map((s) => s.name))) {
    warn("env", line);
  }

  if (sessions.size === 0) sessions.register({});
}
