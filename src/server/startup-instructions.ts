// @session per-session: The initialize instructions a server started on one project sends.
/**
 * The instructions a server started on one project answers `initialize` with,
 * built without starting that server: its config, its context strategy and
 * its plugins' knowledge and load warnings, and no editor connection.
 *
 * The stdio shim answers `initialize` itself with these, so a client is not
 * kept waiting while the project's daemon starts. The daemon composes the
 * same text from the same inputs at the same version.
 */
import { ProjectContext } from "../config/project.js";
import { resolveContextStrategy } from "../config/context-strategy.js";
import { packageVersion } from "../core/package-root.js";
import { buildProjectLoad } from "../sessions/session-load.js";
import { composeServerInstructions } from "../surface/context/instructions.js";

export async function startupInstructions(uproject: string): Promise<string> {
  const project = new ProjectContext();
  project.setProject(uproject);
  const strategy = resolveContextStrategy(project.config.context?.strategy);
  const { surface } = await buildProjectLoad(project, packageVersion());
  const name = project.projectName ?? "default";
  return composeServerInstructions(strategy, [surface], [name], name);
}
