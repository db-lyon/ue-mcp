/**
 * Every `ue-mcp <command>` there is. A first argument naming one of these runs
 * it; anything else starts the server with the arguments as projects. doctor
 * reads the same table to tell a one-shot command line from a running server.
 */
import { runCli, type CliRun } from "./cli-main.js";
import { packageVersion } from "../core/package-root.js";

export interface CliCommand {
  name: string;
  aliases?: readonly string[];
  summary: string;
  /** Loaded only when the command runs, so the server never pays for it. */
  load: () => Promise<{ run: CliRun }>;
}

export const CLI_COMMANDS: readonly CliCommand[] = [
  { name: "init", summary: "Set up ue-mcp in a project", load: () => import("./init.js") },
  { name: "update", summary: "Update the deployed bridge and the server", load: () => import("./update.js") },
  { name: "doctor", summary: "Report what is installed and running", load: () => import("./doctor.js") },
  { name: "deploy", summary: "Deploy the bridge plugin into a project", load: () => import("./deploy-cli.js") },
  { name: "hook", summary: "Claude Code hook handler", load: () => import("./hook-handler.js") },
  { name: "uninstall-hooks", summary: "Remove a project's installed hooks", load: () => import("./uninstall-hooks.js") },
  { name: "auth", summary: "Authorize feedback issues as your GitHub account", load: () => import("./auth-cli.js") },
  {
    name: "login",
    summary: "Authorize this machine to publish plugins",
    load: () => import("./login-cli.js").then((m) => ({ run: m.runLogin })),
  },
  {
    name: "logout",
    summary: "Forget the cached registry token",
    load: () => import("./login-cli.js").then((m) => ({ run: m.runLogout })),
  },
  { name: "feedback", summary: "Review and file deferred feedback", load: () => import("./feedback-cli.js") },
  { name: "dialog", summary: "Set how blocking editor dialogs are handled", load: () => import("./dialog-cli.js") },
  { name: "resolve", summary: "Work a GitHub issue into a pull request", load: () => import("./resolve.js") },
  { name: "build", summary: "Build a project's editor target", load: () => import("./build-cli.js") },
  { name: "plugin", summary: "Install, create and publish plugins", load: () => import("./plugin-cli.js") },
  { name: "context", summary: "Read or set the context strategy", load: () => import("./context-cli.js") },
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

/** Run one command with the arguments after its name, then exit as it asks. */
export async function runCliCommand(command: CliCommand, argv: string[]): Promise<void> {
  try {
    const { run } = await command.load();
    await runCli(run, argv);
  } catch (e) {
    console.error(`[ue-mcp] ${command.name} failed: ${e instanceof Error ? e.message : e}`);
    process.exit(1);
  }
}
