/**
 * The contract every `ue-mcp <command>` module implements, and the two helpers
 * that run one: from the `ue-mcp` dispatcher, or as its own package bin.
 */
import { realpathSync } from "node:fs";
import { fileURLToPath } from "node:url";

/**
 * A command's entry point. `argv` is everything after the command name. A
 * number is the exit code; nothing means the process ends on its own.
 */
export type CliRun = (argv: string[]) => Promise<number | void>;

/** Run a command and exit with the code it returned, if it returned one. */
export async function runCli(run: CliRun, argv: string[]): Promise<void> {
  const code = await run(argv);
  if (typeof code === "number") process.exit(code);
}

/**
 * Is the module at `moduleUrl` the script node was started with? Compared by
 * real path, so an npm bin symlink or shim still counts as the module itself.
 */
export function isMainModule(moduleUrl: string): boolean {
  const entry = process.argv[1];
  if (!entry) return false;
  try {
    return realpathSync(fileURLToPath(moduleUrl)) === realpathSync(entry);
  } catch {
    return false;
  }
}
