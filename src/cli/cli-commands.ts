/**
 * Every `ue-mcp <command>` there is. A first argument naming one of these runs
 * it; anything else starts the server with the arguments as projects. doctor
 * reads the same table to tell a one-shot command line from a running server.
 */
import { isHelpRequest, runCli, type CliRun, type CliUsage } from "./cli-main.js";
import { packageVersion } from "../core/package-root.js";

export interface CliCommand {
  name: string;
  aliases?: readonly string[];
  summary: string;
  /** Loaded only when the command runs, so the server never pays for it. */
  load: () => Promise<{ run: CliRun; usage?: CliUsage }>;
  /** The command prints its own help for a lone `--help`, so that is what it gets. */
  ownHelp?: boolean;
}

export const CLI_COMMANDS: readonly CliCommand[] = [
  { name: "init", summary: "Set up ue-mcp in a project", load: () => import("./init.js") },
  { name: "update", summary: "Update the deployed bridge and the server", load: () => import("./update.js") },
  { name: "doctor", summary: "Report what is installed and running", load: () => import("./doctor.js") },
  { name: "extension", summary: "Install, remove or list daemon extensions", load: () => import("./extension-cli.js") },
  { name: "daemon", summary: "Run, start, stop or inspect a project's daemon", load: () => import("./daemon-cli.js") },
  { name: "status", summary: "Report one project's install and editor state (--json)", load: () => import("./status.js") },
  { name: "deploy", summary: "Deploy the bridge plugin into a project", load: () => import("./deploy-cli.js") },
  { name: "hook", summary: "Claude Code hook handler", load: () => import("./hook-handler.js") },
  { name: "uninstall-hooks", summary: "Remove a project's installed hooks", load: () => import("./uninstall-hooks.js") },
  { name: "auth", summary: "Authorize feedback issues as your GitHub account", load: () => import("./auth-cli.js") },
  {
    name: "login",
    summary: "Authorize this machine to publish plugins",
    load: () => import("./login-cli.js").then((m) => ({ run: m.runLogin })),
    ownHelp: true,
  },
  {
    name: "logout",
    summary: "Forget the cached registry token",
    load: () => import("./login-cli.js").then((m) => ({ run: m.runLogout })),
    ownHelp: true,
  },
  { name: "feedback", summary: "Review and file deferred feedback", load: () => import("./feedback-cli.js"), ownHelp: true },
  { name: "dialog", summary: "Set how blocking editor dialogs are handled", load: () => import("./dialog-cli.js"), ownHelp: true },
  { name: "playtest", summary: "Set who starts playtest runs", load: () => import("./playtest-cli.js"), ownHelp: true },
  { name: "resolve", summary: "Work a GitHub issue into a pull request", load: () => import("./resolve.js") },
  { name: "build", summary: "Build a project's editor target", load: () => import("./build-cli.js") },
  { name: "plugin", summary: "Install, create and publish plugins", load: () => import("./plugin-cli.js") },
  { name: "context", summary: "Read or set the context strategy", load: () => import("./context-cli.js"), ownHelp: true },
  {
    name: "version",
    aliases: ["--version", "-v"],
    summary: "Print the installed version",
    load: async () => ({
      run: async () => {
        console.log(packageVersion());
      },
    }),
  },
];

/** The command a first argument names, by name or alias. */
export function findCliCommand(arg: string | undefined): CliCommand | undefined {
  if (arg === undefined) return undefined;
  return CLI_COMMANDS.find((c) => c.name === arg || c.aliases?.includes(arg));
}

/** Every name and alias a first argument can use to run a command. */
export function cliCommandNames(): string[] {
  return CLI_COMMANDS.flatMap((c) => [c.name, ...(c.aliases ?? [])]);
}

/** First arguments that ask the `ue-mcp` binary itself for help. */
export const HELP_ARGS: readonly string[] = ["help", "--help", "-h"];

/** What `ue-mcp --help` prints: how to start the server, and every command. */
export function formatTopLevelUsage(): string {
  const width = Math.max(...CLI_COMMANDS.map((c) => c.name.length));
  return [
    "Usage:",
    "  ue-mcp [project.uproject ...]   Start the MCP server on stdio for each project",
    "  ue-mcp <command> [args]         Run a command (ue-mcp <command> --help for its flags)",
    "",
    "Commands:",
    ...CLI_COMMANDS.map((c) => `  ${c.name.padEnd(width)}  ${c.summary}`),
  ].join("\n");
}

/** What a `ue-mcp` command line asks for, decided before anything runs. */
export type TopLevelRoute =
  | { kind: "help" }
  | { kind: "command"; command: CliCommand; argv: string[] }
  | { kind: "unknown-flags"; flags: string[] }
  | { kind: "serve"; projects: string[] };

/**
 * Route `ue-mcp <args>`. The server takes projects only, so any flag that is
 * not a command or a help spelling is refused instead of starting it.
 */
export function routeTopLevel(args: readonly string[]): TopLevelRoute {
  const first = args[0];
  if (first !== undefined && HELP_ARGS.includes(first)) return { kind: "help" };
  const command = findCliCommand(first);
  if (command) return { kind: "command", command, argv: args.slice(1) };
  if (isHelpRequest(args)) return { kind: "help" };
  const flags = args.filter((a) => a.startsWith("-"));
  if (flags.length > 0) return { kind: "unknown-flags", flags };
  return { kind: "serve", projects: [...args] };
}

/** The usage a command without its own declares: its name and summary, no flags. */
function fallbackUsage(command: CliCommand): CliUsage {
  return { usage: `ue-mcp ${command.name}`, summary: command.summary, flags: [] };
}

/**
 * Run one command with the arguments after its name, then exit as it asks.
 * `--help` never reaches a command's work: it prints usage and exits 0.
 */
export async function runCliCommand(command: CliCommand, argv: string[]): Promise<void> {
  try {
    const { run, usage } = await command.load();
    if (command.ownHelp && isHelpRequest(argv)) await runCli(run, ["--help"]);
    else await runCli(run, argv, usage ?? fallbackUsage(command));
  } catch (e) {
    console.error(`[ue-mcp] ${command.name} failed: ${e instanceof Error ? e.message : e}`);
    process.exit(1);
  }
}
