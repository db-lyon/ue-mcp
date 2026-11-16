// @session per-session: Reports the install and editor state of the one project it is handed.
/**
 * `ue-mcp status [project] [--json]` - the install and editor state of one
 * project, read-only. The in-editor UI and the installer read the JSON form to
 * decide what is missing; nothing here writes.
 */
import { findUProject } from "../config/uproject-path.js";
import { collectStatus, type StatusReport } from "../integrations/install-status.js";
import { selectProject } from "./editor-flag.js";
import { BOLD, DIM, RESET, fail, ok, warn } from "../terminal/ansi.js";
import type { CliUsage } from "./cli-main.js";

function printStatus(s: StatusReport): void {
  console.log("");
  console.log(`  ${BOLD}${s.project.name}${RESET}  ${DIM}UE ${s.project.engine ?? "?"}  ue-mcp ${s.version}${RESET}`);
  const p = s.plugin;
  if (p.deployed) ok(`bridge ${p.installedVersion ?? "?"} (${p.installKind ?? "unknown"})  ${DIM}${p.pluginDir}${RESET}`);
  else warn("bridge not installed");
  if (s.editor.running) ok(`editor running (${s.editor.instances.filter((i) => i.alive).map((i) => `pid ${i.pid} port ${i.port}`).join(", ")})`);
  else console.log(`  ${DIM}editor not running${RESET}`);
  for (const c of s.clients.filter((x) => x.configured)) {
    ok(`${c.name}: ${c.pinned ? `ue-mcp@${c.pinned}` : "ue-mcp@latest"}  ${DIM}${c.configPath}${RESET}`);
  }
  for (const problem of s.problems) fail(`${problem.what} ${problem.fix}`);
  console.log("");
}

export const usage: CliUsage = {
  usage: "ue-mcp status [project] [flags]",
  summary: "Report one project's install and editor state.",
  strict: true,
  flags: [
    { name: "--json", description: "Print one JSON document" },
    { name: "--editor", value: "<name-or-path>", description: "Target one of the editors this server drives" },
  ],
};

/** Entry point for `ue-mcp status [project] [--json] [--editor <name-or-path>]`. */
export async function run(argv: string[]): Promise<number | void> {
  const json = argv.includes("--json");
  const out = (value: unknown): void => {
    process.stdout.write(JSON.stringify(value, null, 2) + "\n");
  };
  try {
    const uproject = selectProject(argv, { positional: true }).projectPath ?? findUProject(process.cwd());
    if (!uproject) {
      const message = "No .uproject found. Run from your project directory or pass the path.";
      if (json) out({ ok: false, error: message });
      else fail(message);
      return 2;
    }
    const status = collectStatus(uproject);
    if (json) out(status);
    else printStatus(status);
    return 0;
  } catch (e) {
    const message = e instanceof Error ? e.message : String(e);
    if (json) out({ ok: false, error: message });
    else fail(message);
    return 1;
  }
}
