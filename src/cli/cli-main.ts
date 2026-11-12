/**
 * The contract every `ue-mcp <command>` module implements, and the two helpers
 * that run one: from the `ue-mcp` dispatcher, or as its own package bin.
 */
import { realpathSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { setFlagsFromString } from "node:v8";

/**
 * A command's entry point. `argv` is everything after the command name. A
 * number is the exit code; nothing means the process ends on its own.
 */
export type CliRun = (argv: string[]) => Promise<number | void>;

/** One flag a command accepts. `value` names its argument, when it takes one. */
export interface CliFlag {
  name: string;
  value?: string;
  description: string;
}

/**
 * What `--help` prints for a command, and the flags it accepts. A strict
 * command refuses any other flag before it runs, so a typo cannot fall
 * through to a destructive default.
 */
export interface CliUsage {
  usage: string;
  summary?: string;
  flags: readonly CliFlag[];
  strict?: boolean;
}

/** `--help` or `-h` anywhere before a `--` separator. */
export function isHelpRequest(argv: readonly string[]): boolean {
  for (const a of argv) {
    if (a === "--") return false;
    if (a === "--help" || a === "-h") return true;
  }
  return false;
}

/** The flags in `argv` that `usage` does not declare. Values of value flags are skipped. */
export function unknownFlags(argv: readonly string[], usage: CliUsage): string[] {
  const byName = new Map(usage.flags.map((f) => [f.name, f]));
  const unknown: string[] = [];
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === "--") break;
    if (!a.startsWith("-") || a === "-") continue;
    const name = a.includes("=") ? a.slice(0, a.indexOf("=")) : a;
    const flag = byName.get(name);
    if (!flag) {
      unknown.push(name);
      continue;
    }
    if (flag.value && !a.includes("=")) i++;
  }
  return unknown;
}

export function formatUsage(usage: CliUsage): string {
  const lines = [`Usage: ${usage.usage}`];
  if (usage.summary) lines.push("", usage.summary);
  const flags = [...usage.flags, { name: "--help, -h", description: "Print this help and exit" }];
  const label = (f: CliFlag): string => (f.value ? `${f.name} ${f.value}` : f.name);
  const width = Math.max(...flags.map((f) => label(f).length));
  lines.push("", "Flags:");
  for (const f of flags) lines.push(`  ${label(f).padEnd(width)}  ${f.description}`);
  return lines.join("\n");
}

/**
 * Handle `--help` and unknown flags before a command runs. Returns the exit
 * code when the command must not run, else undefined.
 */
export function preflight(argv: readonly string[], usage: CliUsage | undefined): number | undefined {
  if (!usage) return undefined;
  if (isHelpRequest(argv)) {
    console.log(formatUsage(usage));
    return 0;
  }
  if (usage.strict) {
    const unknown = unknownFlags(argv, usage);
    if (unknown.length > 0) {
      console.error(`Unknown flag${unknown.length > 1 ? "s" : ""}: ${unknown.join(", ")}\n\n${formatUsage(usage)}`);
      return 2;
    }
  }
  return undefined;
}

/**
 * Node builds where `process.exit()` on Windows can abort with 0xC0000409 once
 * `fetch` has run: V8 recompiles undici's WebAssembly parser in the background
 * and posts to a platform exit has already closed (nodejs/node#56645). Fixed in
 * 24.20.0 and 26.7.0; 23 and 25 never got the fix.
 */
export function hasFetchExitAbort(version: string = process.versions.node, platform: string = process.platform): boolean {
  if (platform !== "win32") return false;
  const [major, minor] = version.replace(/^v/, "").split(".").map(Number);
  if (major === 23 || major === 25) return true;
  if (major === 24) return minor < 20;
  if (major === 26) return minor < 7;
  return false;
}

let fetchExitGuarded = false;

/**
 * Turn off WebAssembly dynamic tiering on the affected builds, so no background
 * recompile is in flight when the process exits. Must run before the first fetch.
 */
export function guardFetchExitAbort(): void {
  if (fetchExitGuarded || !hasFetchExitAbort()) return;
  fetchExitGuarded = true;
  setFlagsFromString("--no-wasm-dynamic-tiering");
}

/** Run a command and exit with the code it returned, if it returned one. */
export async function runCli(run: CliRun, argv: string[], usage?: CliUsage): Promise<void> {
  guardFetchExitAbort();
  const early = preflight(argv, usage);
  const code = early ?? (await run(argv));
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
