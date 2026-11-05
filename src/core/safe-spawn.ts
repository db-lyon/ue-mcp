/**
 * Starting child processes without a shell.
 *
 * Every command this package runs goes through here as an argv array. The one
 * case that still needs cmd.exe is a Windows batch file (Build.bat, npm.cmd),
 * which Node refuses to start without a shell; that line is built from tokens
 * checked against the characters cmd acts on, and anything else is refused
 * with a message naming the value.
 */
import { spawn, spawnSync, type ChildProcess, type SpawnOptions, type SpawnSyncOptions, type SpawnSyncReturns } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";

/** A value that cannot be passed to a child process as given. */
export class UnsafeArgumentError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "UnsafeArgumentError";
  }
}

/** A command and the argv it runs with. */
export interface Invocation {
  command: string;
  args: string[];
  /** Set for a cmd.exe line, which is already quoted and must reach cmd unchanged. */
  windowsVerbatimArguments?: boolean;
}

/** Unreal module, target and project names: UnrealBuildTool accepts nothing else. */
export const UE_IDENTIFIER_RE = /^[A-Za-z_][A-Za-z0-9_]*$/;
export const BUILD_CONFIGURATIONS = ["Debug", "DebugGame", "Development", "Shipping", "Test"] as const;
const PLATFORM_RE = /^[A-Za-z0-9]+$/;

/** Characters cmd.exe acts on even inside double quotes (`^` is doubled by CALL), and line breaks. */
const CMD_ACTIVE_RE = /["%!^\x00-\x1f\x7f]/;
/** What a token may hold outside quotes. */
const CMD_BARE_RE = /^[A-Za-z0-9_\-.=:\\/,+@~]+$/;

export function assertUeIdentifier(value: string, what: string): string {
  if (!UE_IDENTIFIER_RE.test(value)) {
    throw new UnsafeArgumentError(
      `${what} '${value}' is not a valid Unreal name: it must start with a letter or underscore and hold only letters, digits and underscores.`,
    );
  }
  return value;
}

export function assertBuildConfiguration(value: string): string {
  if (!(BUILD_CONFIGURATIONS as readonly string[]).includes(value)) {
    throw new UnsafeArgumentError(`Unknown build configuration '${value}'. Use one of: ${BUILD_CONFIGURATIONS.join(", ")}.`);
  }
  return value;
}

export function assertPlatform(value: string): string {
  if (!PLATFORM_RE.test(value)) {
    throw new UnsafeArgumentError(`Platform '${value}' is not a platform name (letters and digits only, e.g. Win64, Mac, Linux).`);
  }
  return value;
}

/** Refuse a value cmd.exe would act on even when it is quoted. */
export function assertCmdSafe(value: string, what: string): string {
  if (CMD_ACTIVE_RE.test(value)) {
    throw new UnsafeArgumentError(
      `${what} contains a character the Windows command processor acts on (" % ! ^ or a control character): ` +
      `${JSON.stringify(value)}. Rename or move it so the path holds none of them.`,
    );
  }
  return value;
}

/** One token for a cmd.exe line: bare when it is plain, else quoted. */
export function cmdQuote(value: string, what: string): string {
  assertCmdSafe(value, what);
  return CMD_BARE_RE.test(value) ? value : `"${value}"`;
}

/**
 * Check a token that is already shaped for a cmd.exe line, such as
 * `-Project="C:\My Game\Game.uproject"`: quoted segments may hold anything
 * but the characters cmd acts on inside quotes, the rest only plain characters.
 */
export function assertCmdToken(token: string, what: string): string {
  let bad = token === "" || (token.split('"').length - 1) % 2 !== 0;
  const outside = token.replace(/"([^"]*)"/g, (_m, inner: string) => {
    if (CMD_ACTIVE_RE.test(inner)) bad = true;
    return "";
  });
  if (bad || (outside !== "" && !CMD_BARE_RE.test(outside))) {
    throw new UnsafeArgumentError(`${what} cannot be passed to the Windows command processor safely: ${JSON.stringify(token)}.`);
  }
  return token;
}

/**
 * Run a Windows batch file with cmd.exe, as an argv array. `/s` makes cmd
 * strip exactly the outer pair of quotes, so the script path keeps its own.
 */
export function windowsBatchInvocation(script: string, tokens: string[]): Invocation {
  assertCmdSafe(script, "Batch file path");
  tokens.forEach((t, i) => assertCmdToken(t, `Argument ${i + 1}`));
  const line = [`"${script}"`, ...tokens].join(" ");
  return {
    command: process.env.ComSpec || "cmd.exe",
    args: ["/d", "/s", "/c", `"${line}"`],
    windowsVerbatimArguments: true,
  };
}

/** npm's own entry script, so npm runs under node with no shell in between. */
export function findNpmCli(env: NodeJS.ProcessEnv = process.env, execPath: string = process.execPath): string | null {
  const candidates: string[] = [];
  const fromEnv = env.npm_execpath;
  if (fromEnv) {
    if (path.basename(fromEnv) === "npm-cli.js") candidates.push(fromEnv);
    else candidates.push(path.join(path.dirname(fromEnv), "npm-cli.js"));
  }
  const nodeDir = path.dirname(execPath);
  candidates.push(path.join(nodeDir, "node_modules", "npm", "bin", "npm-cli.js"));
  candidates.push(path.join(nodeDir, "..", "lib", "node_modules", "npm", "bin", "npm-cli.js"));
  return candidates.find((c) => fs.existsSync(c)) ?? null;
}

/**
 * How to run `npm <args>` without a shell. Arguments starting with `-` are the
 * caller's own flags; a package name or spec from elsewhere goes through
 * `assertPackageSpec` first.
 */
export function npmInvocation(
  args: string[],
  opts: { platform?: NodeJS.Platform; npmCli?: string | null } = {},
): Invocation {
  const npmCli = opts.npmCli === undefined ? findNpmCli() : opts.npmCli;
  if (npmCli) return { command: process.execPath, args: [npmCli, ...args] };
  if ((opts.platform ?? process.platform) !== "win32") return { command: "npm", args };
  return windowsBatchInvocation("npm.cmd", args.map((a, i) => cmdQuote(a, `npm argument ${i + 1}`)));
}

/**
 * A package name, `name@range`, folder, tarball or URL handed to npm. It must
 * not read as an option, and must hold no control characters.
 */
export function assertPackageSpec(spec: string): string {
  if (spec.trim() === "" || spec.startsWith("-") || /[\x00-\x1f\x7f]/.test(spec)) {
    throw new UnsafeArgumentError(`'${spec}' is not a package name or spec npm can be given.`);
  }
  return spec;
}

/** Plain semver, the only shape a version taken from npm is used in. */
export const PLAIN_SEMVER_RE = /^\d{1,8}\.\d{1,8}\.\d{1,8}(?:-[0-9A-Za-z][0-9A-Za-z.-]{0,63})?$/;

export function assertSemver(value: string, what: string): string {
  if (!PLAIN_SEMVER_RE.test(value)) throw new UnsafeArgumentError(`${what} '${value}' is not a version number.`);
  return value;
}

export function spawnInvocation(inv: Invocation, options: SpawnOptions = {}): ChildProcess {
  return spawn(inv.command, inv.args, { windowsHide: true, ...options, shell: false, windowsVerbatimArguments: inv.windowsVerbatimArguments });
}

export function spawnInvocationSync(inv: Invocation, options: SpawnSyncOptions = {}): SpawnSyncReturns<string | Buffer> {
  return spawnSync(inv.command, inv.args, { windowsHide: true, ...options, shell: false, windowsVerbatimArguments: inv.windowsVerbatimArguments });
}

/** Run npm without a shell. */
export function runNpmSync(args: string[], options: SpawnSyncOptions = {}): SpawnSyncReturns<string | Buffer> {
  return spawnInvocationSync(npmInvocation(args), options);
}
