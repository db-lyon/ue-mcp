import { spawn } from "node:child_process";

/**
 * Runs in a short-lived node process: start the target detached, then exit as
 * soon as it has started, so the target's parent is a process that is gone.
 */
const TRAMPOLINE = [
  "const { spawn } = require('node:child_process');",
  "const [cmd, args, opts] = [process.argv[1], JSON.parse(process.argv[2]), JSON.parse(process.argv[3])];",
  "const verbatim = opts.windowsVerbatimArguments === true;",
  // Verbatim means verbatim for argv[0] too, so a program path with a space
  // has to be quoted by hand or the target reads it as two arguments.
  "const argv0 = verbatim ? { argv0: '\"' + cmd + '\"' } : {};",
  "const c = spawn(cmd, args, { detached: true, stdio: ['ignore', 'inherit', 'inherit'], windowsHide: true, windowsVerbatimArguments: verbatim, ...argv0 });",
  "c.once('error', (e) => { process.stderr.write(String(e) + '\\n'); process.exit(1); });",
  "c.once('spawn', () => { c.unref(); process.exit(0); });",
].join("\n");

export interface OutsideTreeOptions {
  env?: NodeJS.ProcessEnv;
  /** A file descriptor for the process's stdout and stderr; omitted discards them. */
  output?: number;
  /**
   * Windows only: `args` are joined into the command line as they are, with no
   * quoting or escaping. For a program that reads its raw command line, such
   * as Unreal, when the caller has built the pieces itself.
   */
  windowsVerbatimArguments?: boolean;
}

/**
 * Start a long-lived process (an editor, a daemon) that outlives this one
 * whatever ends it (#1274).
 *
 * An MCP client on Windows that closes a stdio server ends the server's whole
 * process tree when it has not exited 300 ms after stdin closed. The tree is
 * walked by parent PID, and `detached` does not change a child's parent, so an
 * editor the server launched went down on every client reconnect, with no
 * shutdown and no crash report. Starting it through a trampoline that exits at
 * once leaves it with a parent that no longer exists, so no tree walk from the
 * server reaches it. Elsewhere `detached` already gives the child its own session.
 */
export function spawnOutsideTree(command: string, args: string[], options: OutsideTreeOptions = {}): void {
  const stdio = options.output === undefined
    ? ("ignore" as const)
    : (["ignore", options.output, options.output] as const);
  const verbatim = options.windowsVerbatimArguments === true;
  if (process.platform !== "win32") {
    spawn(command, args, {
      detached: true,
      windowsHide: true,
      windowsVerbatimArguments: verbatim,
      stdio: stdio as never,
      env: options.env,
    }).unref();
    return;
  }
  const inner = JSON.stringify({ windowsVerbatimArguments: verbatim });
  spawn(process.execPath, ["-e", TRAMPOLINE, command, JSON.stringify(args), inner], {
    detached: true,
    windowsHide: true,
    stdio: stdio as never,
    env: options.env,
  }).unref();
}
