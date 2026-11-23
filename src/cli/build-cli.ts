#!/usr/bin/env node
// @session per-session: Builds one project, chosen with --editor.
import * as path from "node:path";
import { buildProject } from "../editor/editor-build.js";
import { selectProjectOrExit } from "./editor-flag.js";
import { findUProject, isUProjectPath } from "../config/uproject-path.js";
import { RESET, BOLD, GREEN, RED, CYAN } from "../terminal/ansi.js";
import type { CliUsage } from "./cli-main.js";
import type { EditorDisruptionRequest } from "../contract/daemon.js";
import { daemonRequest } from "../daemon/client.js";
import { readAnsweringDiscovery } from "../daemon/discovery.js";

function targetUProject(argv: string[]): string | null {
  const arg = selectProjectOrExit(argv, { positional: true }).projectPath;
  // A named .uproject is taken as given, so a missing one is reported rather
  // than silently replaced by whatever project cwd holds.
  if (arg && isUProjectPath(arg)) return path.resolve(arg);
  return (arg && findUProject(arg)) || findUProject(process.cwd());
}

/**
 * Why the build must not run yet, or null. Only a running daemon with an
 * editor attached is asked: with no daemon there are no sessions to ask, and
 * with no editor up a build takes nothing away.
 */
async function buildConsentRefusal(uprojectPath: string): Promise<string | null> {
  const daemon = await readAnsweringDiscovery(path.dirname(uprojectPath));
  if (!daemon) return null;
  try {
    const health = await daemonRequest(daemon, "health");
    const editors = (health.json as { editors?: Array<{ connected?: boolean }> } | undefined)?.editors ?? [];
    if (!editors.some((e) => e.connected)) return null;
    const res = await daemonRequest(daemon, "disruptions.request", { method: "POST", body: { op: "build", requester: "ue-mcp build" } });
    if (res.status === 404) return null; // an older daemon without the consent routes
    const r = res.json as EditorDisruptionRequest | undefined;
    if (!r || r.state === "approved") return null;
    const answers = r.answers.map((a) => `    ${a.label} (${a.session}): ${a.answer.replace(/_/g, " ")}, ${a.reason}`).join("\n");
    return `Not built: other sessions are using the editor (request ${r.id}, ${r.state}).\n${answers}\n`
      + `  Approve it with \`ue-mcp editor approve ${r.id}\` and run ue-mcp build again, or wait for them to answer go.`;
  } catch {
    return null;
  }
}

async function main(argv: string[]) {
  console.log("");
  console.log(`  ${BOLD}${CYAN}UE-MCP Build${RESET}`);
  console.log("");

  const uprojectPath = targetUProject(argv);
  if (!uprojectPath) {
    console.log(`  ${RED}No .uproject found. Run from your project directory or pass the path.${RESET}`);
    process.exit(1);
  }

  const projectName = path.basename(uprojectPath, path.extname(uprojectPath));
  console.log(`  Project: ${GREEN}${projectName}${RESET}`);
  console.log(`  Path:    ${uprojectPath}`);
  console.log("");

  // Building under an editor other agent sessions are using takes it away from
  // them, so the project's daemon is asked first (#2075).
  const refusal = await buildConsentRefusal(uprojectPath);
  if (refusal) {
    console.log(`  ${RED}${refusal}${RESET}`);
    process.exit(1);
  }

  const result = await buildProject(uprojectPath, { onOutput: (text) => process.stdout.write(text) });

  console.log("");
  if (result.success) {
    console.log(`  ${GREEN}${BOLD}Build succeeded${RESET}`);
  } else {
    console.log(`  ${RED}${BOLD}${result.message}${RESET}`);
    process.exit(1);
  }
}

export const usage: CliUsage = {
  usage: "ue-mcp build [project] [flags]",
  summary: "Build the project's editor target.",
  strict: true,
  flags: [{ name: "--editor", value: "<name-or-path>", description: "Target one of the editors this server drives" }],
};

/** Entry point for `ue-mcp build [project] [--editor <name-or-path>]`. */
export async function run(argv: string[]): Promise<number | void> {
  try {
    await main(argv);
  } catch (e) {
    console.error(`\n  ${RED}Unexpected error: ${e instanceof Error ? e.message : String(e)}${RESET}`);
    return 1;
  }
}
