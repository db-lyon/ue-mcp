// @session per-session: Scopes a mode with --editor, one editor at a time.
/**
 * `ue-mcp <feedback|dialog|playtest> mode [<value>|clear] [--editor <name>]`:
 * print, set or clear one scoped mode (config/modes.ts). Every mode prints the
 * same shape and accepts the same words, from its definition.
 */
import { CLEAR_WORDS, type ScopedMode } from "../config/modes.js";
import { getUserStatePath } from "../config/state-store.js";
import { BOLD, CYAN, DIM, RESET, fail, info, ok, warn } from "../terminal/ansi.js";
import { selectProjectOrExit, type ProjectSelection } from "./editor-flag.js";

const STATE_FILE = "~/.ue-mcp/state.json";

/** The help lines for `mode`, which a command with other subcommands prints among its own. */
export function modeHelpLines<M extends string>(mode: ScopedMode<M>): string[] {
  const { cli, values } = mode.def;
  return [
    `  ${BOLD}mode${RESET}              Print the current ${cli.title.toLowerCase()}`,
    `  ${BOLD}mode${RESET} <mode>       Set the mode (${values.join(" | ")})`,
    `  ${BOLD}mode ${CLEAR_WORDS[0]}${RESET}        Clear it (also: ${CLEAR_WORDS.slice(1).join(", ")})`,
  ];
}

/** What each value means, and what applies with nothing set. */
export function modeValueLines<M extends string>(mode: ScopedMode<M>): string[] {
  const { cli, values } = mode.def;
  const width = Math.max(...values.map((v) => v.length));
  return [
    ...values.map((v) => `  ${DIM}${v.padEnd(width)}  ${cli.describe[v]}${RESET}`),
    "",
    `  ${DIM}Unset: ${cli.unsetEffective}. --editor <name> sets it for one project only.${RESET}`,
  ];
}

function printHelp<M extends string>(mode: ScopedMode<M>): void {
  const lines = ["", `  ${BOLD}${CYAN}ue-mcp ${mode.def.name}${RESET}`, "", `  ${mode.def.cli.summary}`, "", ...modeHelpLines(mode), "", ...modeValueLines(mode), ""];
  for (const line of lines) console.log(line);
}

/** Print, set or clear the mode for the selected project (or the user). Returns the exit code. */
export function runModeSubcommand<M extends string>(mode: ScopedMode<M>, arg: string | undefined, target: ProjectSelection): number {
  const { def } = mode;
  const projectRoot = target.projectRoot;
  const scope = target.label ? `editor '${target.label}'` : "per-user";
  const usage = `npx ue-mcp ${def.name} mode <${def.values.join("|")}> [--editor <name>]`;

  if (arg === undefined) {
    const { project, user } = mode.scopes(projectRoot);
    const resolved = mode.resolve(projectRoot);
    console.log("");
    if (resolved.source === "env") {
      console.log(`  ${BOLD}effective:${RESET} ${resolved.value} ${DIM}(${mode.envName})${RESET}`);
    } else {
      console.log(`  ${BOLD}effective:${RESET} ${resolved.source === "default" ? def.cli.unsetEffective : resolved.value}`);
    }
    if (projectRoot) {
      console.log(`  ${DIM}${scope}: ${project ?? "(not set; falls back to the per-user preference)"}${RESET}`);
    }
    console.log(`  ${DIM}preference (${STATE_FILE}): ${user ?? `(not set; ${def.cli.unsetPreference})`}${RESET}`);
    if (resolved.ignoredEnv !== undefined) {
      warn(`${mode.envName}="${resolved.ignoredEnv.toLowerCase()}" names no mode and is ignored. Allowed: ${def.values.join(", ")}.`);
    }
    console.log("");
    console.log(`  ${DIM}Set with: ${usage}${RESET}`);
    console.log(`  ${DIM}Clear: npx ue-mcp ${def.name} mode ${CLEAR_WORDS[0]} [--editor <name>]${RESET}`);
    console.log("");
    return 0;
  }

  const value = arg.trim().toLowerCase();
  if ((CLEAR_WORDS as readonly string[]).includes(value)) {
    mode.set(undefined, projectRoot);
    ok(
      projectRoot
        ? `Cleared the ${def.cli.title.toLowerCase()} for ${scope}. It falls back to the per-user preference.`
        : `Cleared the ${def.cli.title.toLowerCase()} preference. ${def.cli.afterClear}`,
    );
    info(getUserStatePath());
    return 0;
  }

  const named = mode.parse(value);
  if (!named) {
    fail(`Unknown mode "${arg}". Allowed: ${def.values.join(", ")}, or ${CLEAR_WORDS[0]} to clear it. Usage: ${usage}`);
    return 1;
  }
  mode.set(named, projectRoot);
  ok(
    projectRoot
      ? `${def.cli.title} for ${scope} set to "${named}" (stored per project in ${STATE_FILE}).`
      : `${def.cli.title} set to "${named}" (per-user, stored in ${STATE_FILE}).`,
  );
  const caution = def.cli.caution?.[named];
  if (caution) warn(caution);
  const rawEnv = mode.envValue();
  if (rawEnv) {
    warn(
      `${mode.envName}=${rawEnv} is set in your env and will override this preference for processes started from that shell. ` +
        `Unset it there for the stored mode to apply.`,
    );
  }
  return 0;
}

/** The whole `ue-mcp <name>` command for a mode with no other subcommands. */
export function modeCommand<M extends string>(mode: ScopedMode<M>): (argv: string[]) => Promise<number | void> {
  return async (argv) => {
    try {
      const target = selectProjectOrExit(argv);
      const sub = target.rest[0];
      switch (sub) {
        case "mode":
          return runModeSubcommand(mode, target.rest[1], target);
        case undefined:
        case "help":
        case "--help":
        case "-h":
          printHelp(mode);
          return;
        default:
          fail(`Unknown subcommand: ${mode.def.name} ${sub}`);
          printHelp(mode);
          return 1;
      }
    } catch (e) {
      console.error(`[ue-mcp] ${mode.def.name} failed: ${e instanceof Error ? e.message : e}`);
      return 1;
    }
  };
}
