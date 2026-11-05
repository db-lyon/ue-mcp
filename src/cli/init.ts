import * as fs from "node:fs";
import * as path from "node:path";
import * as readline from "node:readline";
import { ProjectContext } from "../config/project.js";
import { BOLD, CYAN, DIM, GREEN, RED, RESET, fail, info, ok, warn } from "./ui/ansi.js";
import { checkboxSelect, singleSelect, type CheckboxItem } from "./ui/select.js";
import { runFeedbackAuthStep } from "./auth-cli.js";
import type { CliUsage } from "./cli-main.js";
import { getInstalledHooks, type FeedbackMode } from "../config/user-state.js";
import { coreSkillsInstalled } from "../extensions/skills.js";
import { detectMcpClients, isProjectScopedClient, ueMcpServerArgs } from "../integrations/claude-code/mcp-client-config.js";
import { takeEditorTarget, EditorFlagError } from "./editor-flag.js";
import { findUProject } from "../config/uproject-path.js";
import {
  CATEGORIES,
  ENRICHABLE_CATEGORIES,
  FEEDBACK_MODES,
  apply,
  bridgePort,
  plan,
  type ContextStrategy,
  type InitChoices,
  type InitResult,
} from "./init-core.js";

/* ------------------------------------------------------------------ */
/*  Ask for project path with readline                                 */
/* ------------------------------------------------------------------ */

function askPath(): Promise<string> {
  const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout,
  });
  return new Promise((resolve) => {
    rl.question(
      `  ${BOLD}?${RESET} UE project path (.uproject or directory): `,
      (answer) => {
        rl.close();
        resolve(answer.trim().replace(/^["']|["']$/g, ""));
      },
    );
  });
}

/* ------------------------------------------------------------------ */
/*  Main init flow                                                     */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/*  Machine interface: init --yes [--json]                             */
/* ------------------------------------------------------------------ */

/** `--name=value` or `--name value`; undefined when absent. */
function flagValue(argv: string[], name: string): string | undefined {
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === name) return argv[i + 1];
    if (a.startsWith(`${name}=`)) return a.slice(name.length + 1);
  }
  return undefined;
}

/** true for --name, false for --no-name, undefined when neither. */
function flagBool(argv: string[], name: string): boolean | undefined {
  if (argv.includes(`--no-${name}`)) return false;
  if (argv.includes(`--${name}`)) return true;
  return undefined;
}

const list = (v: string | undefined): string[] | undefined =>
  v === undefined ? undefined : v.split(",").map((x) => x.trim()).filter(Boolean);

/** The choices `init --yes` reads off its flags. Unset flags keep the project's current state. */
export function choicesFromFlags(argv: string[], projectPath: string): InitChoices {
  const install = flagValue(argv, "--install");
  if (install !== undefined && install !== "auto" && install !== "source" && install !== "binary") {
    throw new Error(`--install must be auto, source or binary, not '${install}'`);
  }
  const context = flagValue(argv, "--context");
  if (context !== undefined && context !== "micro" && context !== "lean" && context !== "full") {
    throw new Error(`--context must be micro, lean or full, not '${context}'`);
  }
  const feedbackMode = flagValue(argv, "--feedback-mode");
  if (feedbackMode !== undefined && !(FEEDBACK_MODES as readonly string[]).includes(feedbackMode)) {
    throw new Error(`--feedback-mode must be interactive, auto-approve or defer, not '${feedbackMode}'`);
  }
  const native = flagBool(argv, "native-tools");
  const exclude = list(flagValue(argv, "--native-exclude"));
  return {
    project: projectPath,
    disable: list(flagValue(argv, "--disable")),
    nativeTools: native === undefined && exclude === undefined ? undefined : { enabled: native !== false, exclude: exclude ?? [] },
    contextStrategy: context as ContextStrategy | undefined,
    clients: list(flagValue(argv, "--clients")),
    feedback: flagBool(argv, "feedback"),
    feedbackMode: feedbackMode as FeedbackMode | undefined,
    promptHook: flagBool(argv, "hook"),
    skills: flagBool(argv, "skills"),
    install: install as InitChoices["install"],
    binaries: flagValue(argv, "--binaries") ?? null,
    // The machine path pins by default, so the server and bridge move together.
    pin: flagBool(argv, "pin") ?? true,
    command: flagValue(argv, "--command") ?? null,
  };
}

function printResult(r: InitResult): void {
  for (const step of r.steps) {
    const line = `${step.detail}  ${DIM}${step.target}${RESET}`;
    if (step.status === "failed") fail(line);
    else ok(line);
  }
  for (const w of r.warnings) warn(w);
}

async function initNonInteractive(argv: string[], projectPath: string): Promise<number> {
  const json = argv.includes("--json");
  const emit = (value: unknown): void => {
    process.stdout.write(JSON.stringify(value, null, 2) + "\n");
  };
  let choices: InitChoices;
  try {
    choices = choicesFromFlags(argv, projectPath);
  } catch (e) {
    const message = e instanceof Error ? e.message : String(e);
    if (json) emit({ ok: false, error: message });
    else fail(message);
    return 2;
  }
  // Anything a library prints goes to stderr, so stdout stays one JSON document.
  const restore = json ? redirectConsoleToStderr() : () => {};
  try {
    const planned = plan(choices);
    if (argv.includes("--dry-run")) {
      if (json) emit({ ok: planned.blockers.length === 0, dryRun: true, plan: planned });
      else console.log(JSON.stringify(planned, null, 2));
      return planned.blockers.length === 0 ? 0 : 1;
    }
    const result = await apply(planned);
    if (json) emit(result);
    else {
      printResult(result);
      if (!result.ok) fail(result.error ?? "init failed");
    }
    return result.ok ? 0 : 1;
  } catch (e) {
    const message = e instanceof Error ? e.message : String(e);
    if (json) emit({ ok: false, error: message });
    else fail(message);
    return 1;
  } finally {
    restore();
  }
}

function redirectConsoleToStderr(): () => void {
  const original = console.log;
  console.log = (...args: unknown[]) => console.error(...args);
  return () => {
    console.log = original;
  };
}

/* ------------------------------------------------------------------ */
/*  Interactive init                                                   */
/* ------------------------------------------------------------------ */


async function init(argv: string[]): Promise<number | void> {
  // 1. Project: --editor, then --project, then the first positional, then cwd, then ask.
  let initTarget: { projectPath?: string; rest: string[] };
  try {
    initTarget = takeEditorTarget(argv);
  } catch (e) {
    fail(e instanceof EditorFlagError ? e.message : String(e));
    return 1;
  }
  const rest = initTarget.rest;
  let uprojectPath = initTarget.projectPath || flagValue(rest, "--project") || firstPositional(rest) || "";
  const machine = rest.includes("--yes") || rest.includes("-y") || rest.includes("--json");
  if (!machine && !process.stdin.isTTY) {
    fail(
      "ue-mcp init asks its questions on a terminal, and stdin is not one. Pass --yes (or --json) to run "
      + "without prompts; --project, --clients, --context, --disable, --native-exclude and "
      + "--[no-]feedback, --[no-]hook, --[no-]skills answer them.",
    );
    return 2;
  }

  if (!uprojectPath) {
    const found = findUProject(process.cwd());
    if (found) {
      uprojectPath = found;
      if (!machine) info(`Found ${path.basename(found)} in current directory`);
    } else if (machine) {
      const message = "No .uproject found. Pass --project=<path>.";
      if (rest.includes("--json")) process.stdout.write(JSON.stringify({ ok: false, error: message }) + "\n");
      else fail(message);
      return 2;
    } else {
      uprojectPath = await askPath();
    }
  }

  if (machine) return initNonInteractive(rest, uprojectPath);

  console.log("");
  console.log(`  ${BOLD}${CYAN}UE-MCP Setup${RESET}`);
  console.log("");

  const project = new ProjectContext();
  try {
    project.setProject(uprojectPath);
  } catch (e) {
    fail(e instanceof Error ? e.message : String(e));
    return 1;
  }

  ok(`Found UE ${project.engineAssociation ?? "?"} project "${project.projectName}"`);
  // The port this worktree will use, so checkouts on one machine are visibly distinct.
  const port = bridgePort(project);
  info(
    port.pinnedBy
      ? `Bridge port for this worktree: ${DIM}${port.port}${RESET} (${port.pinnedBy === "env" ? "pinned by UE_MCP_PORT" : "pinned by ue-mcp.bridge.port"}; ${port.derivedPort} is what the project path derives)`
      : `Bridge port for this worktree: ${DIM}${port.port}${RESET} (derived from the project path; pin with ue-mcp.bridge.port if needed)`,
  );
  console.log("");

  // Re-init keeps prior opt-outs.
  const existingDisabled = new Set(project.config.disable ?? []);

  // 2. Tool categories.
  const optional = CATEGORIES.filter((c) => !c.alwaysOn);
  const states = await checkboxSelect("Tool categories", optional.map((c): CheckboxItem => {
    const parts: string[] = [];
    if (c.description) parts.push(c.description);
    if (c.requiredPlugins) parts.push(`requires ${c.requiredPlugins.join(", ")}`);
    return { label: c.label, checked: !existingDisabled.has(c.name), suffix: parts.length > 0 ? parts.join(" - ") : undefined };
  }), "Pass --yes with --disable <categories>.");
  const disabled = optional.filter((_, i) => !states[i]).map((c) => c.name);
  console.log("");

  // 2b. Native (Epic 5.8) tools, on by default.
  const existingNative = (project.config.nativeTools ?? {}) as { enabled?: boolean; exclude?: string[] };
  const nativeEnabled = (await checkboxSelect("Native Unreal tools (Epic 5.8)", [{
    label: "Enable native Epic 5.8 MCP tools",
    checked: existingNative.enabled !== false,
    suffix: "Wraps Unreal's ToolsetRegistry (GAS, Niagara, PCG, UMG, ...) as first-class actions in the matching categories",
  }], "Pass --yes with --native-tools or --no-native-tools."))[0];
  let nativeExclude: string[] = [];
  if (nativeEnabled) {
    const existingExclude = new Set(existingNative.exclude ?? []);
    const offer = ENRICHABLE_CATEGORIES.filter((c) => !disabled.includes(c));
    const includeStates = await checkboxSelect(
      "Include native tools in these categories (uncheck to skip)",
      offer.map((c) => ({ label: c, checked: !existingExclude.has(c) })),
      "Pass --yes with --native-exclude <categories>.",
    );
    nativeExclude = offer.filter((_, i) => !includeStates[i]);
    console.log("");
  }
  console.log("");

  // 2c. Context strategy.
  const CONTEXT_TIERS = ["micro", "lean", "full"] as const;
  const existingStrategy = project.config.context?.strategy ?? "micro";
  const tierLabels = [
    "micro - one gateway tool fronts everything (smallest seed, default)",
    "lean  - category tools and action names visible, signatures on demand",
    "full  - every action's signature listed inline (largest seed, no discovery calls)",
  ].map((l, i) => (CONTEXT_TIERS[i] === existingStrategy ? `${l}   [current]` : l));
  const contextStrategy = CONTEXT_TIERS[await singleSelect("Context strategy", tierLabels, "Pass --yes with --context micro|lean|full.")] ?? "micro";
  console.log("");

  // 3. MCP clients. Global and Desktop configs affect every project, so they start unchecked.
  const detected = detectMcpClients(project.projectDir!).filter((c) => c.detected);
  let selectedClients: string[] = [];
  if (detected.length > 0) {
    const clientStates = await checkboxSelect("Configure MCP clients", detected.map((c) => ({
      label: c.name,
      checked: isProjectScopedClient(c.name),
      suffix: c.configPath,
    })), "Pass --yes with --clients <ids>.");
    selectedClients = detected.filter((_, i) => clientStates[i]).map((c) => c.name);
  } else {
    warn("No MCP clients detected. Add this to your MCP client config:");
    console.log("");
    console.log(`    ${DIM}{`);
    console.log(`      "mcpServers": {`);
    console.log(`        "ue-mcp": {`);
    console.log(`          "command": "npx",`);
    console.log(`          "args": ${JSON.stringify(ueMcpServerArgs(project.projectPath!)).replace(/,/g, ", ")}`);
    console.log(`        }`);
    console.log(`      }`);
    console.log(`    }${RESET}`);
    console.log("");
  }
  console.log("");

  // 4. Agent behavior. Everything here is off on a fresh install; re-init keeps prior choices.
  const configuredClaudeCode = selectedClients.some((n) => n.startsWith("Claude Code"));
  const isReInit = fs.existsSync(path.join(project.projectDir!, "ue-mcp.yml"));
  const behaviorItems: CheckboxItem[] = [{
    label: "Enable feedback(submit) tool for filing tool-gap issues",
    checked: isReInit && !existingDisabled.has("feedback"),
    suffix: "Recommended. Calls block on a user-approval prompt before anything is posted to a public tracker.",
  }];
  if (configuredClaudeCode) {
    const settingsPath = path.join(project.projectDir!, ".claude", "settings.json");
    const hookInstalled = new Set(getInstalledHooks(project.projectDir!).map((p) => path.resolve(p))).has(path.resolve(settingsPath));
    behaviorItems.push({
      label: "Auto-nudge agent to offer feedback after execute_python",
      checked: hookInstalled,
      suffix: "Opt-in. Installs a PostToolUse hook in .claude/settings.json; ignored if feedback is off.",
    });
    behaviorItems.push({
      label: "Install bundled Claude Code skills (workflow guides)",
      checked: coreSkillsInstalled(project.projectDir!),
      suffix: "Recommended for Claude Code. Copies skill markdown into .claude/skills/.",
    });
  }
  const behaviorStates = await checkboxSelect("Agent behavior", behaviorItems, "Pass --yes with --[no-]feedback, --[no-]hook and --[no-]skills.");
  const feedback = behaviorStates[0];
  const promptHook = configuredClaudeCode && (behaviorStates[1] ?? false) && feedback;

  // 5. Plan, then apply.
  const choices: InitChoices = {
    project: project.projectPath!,
    disable: disabled,
    nativeTools: { enabled: nativeEnabled, exclude: nativeExclude },
    contextStrategy,
    clients: selectedClients,
    feedback,
    promptHook,
    skills: configuredClaudeCode && (behaviorStates[2] ?? false),
    install: (flagValue(rest, "--install") as InitChoices["install"]) ?? "auto",
    binaries: flagValue(rest, "--binaries") ?? null,
    pin: flagBool(rest, "pin") ?? false,
  };
  const planned = plan(choices);
  console.log("");
  info(`Bridge install: ${BOLD}${planned.install.kind}${RESET} (${planned.install.reason})`);
  const result = await apply(planned);
  printResult(result);
  if (!result.ok) {
    fail(result.error ?? "init failed");
    return 1;
  }

  // OAuth only with the prompt hook on, where the agent will routinely ask to file feedback.
  if (promptHook) await runFeedbackAuthStep();

  console.log("");
  console.log(`  ${BOLD}${GREEN}Setup complete!${RESET}`);
  console.log("");
  console.log(`  ${DIM}Open (or restart) your editor to load the bridge plugin.`);
  console.log(`  Then ask your AI: project(action="get_status")${RESET}`);
  console.log("");
}

/** Flags that take a value, so the argument after one is not a positional. */
const VALUE_FLAGS = new Set([
  "--project", "--clients", "--install", "--binaries", "--disable", "--context", "--native-exclude", "--command",
]);

function firstPositional(argv: string[]): string | undefined {
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (VALUE_FLAGS.has(a)) {
      i++;
      continue;
    }
    if (!a.startsWith("-")) return a;
  }
  return undefined;
}

const toggle = (name: string, description: string) => [
  { name: `--${name}`, description },
  { name: `--no-${name}`, description: `Opposite of --${name}` },
];

export const usage: CliUsage = {
  usage: "ue-mcp init [project] [flags]",
  summary: "Set up ue-mcp in a project. Interactive unless --yes or --json is given.",
  strict: true,
  flags: [
    { name: "--project", value: "<path>", description: "The .uproject (or its directory)" },
    { name: "--yes", description: "Run without prompts; unset flags keep the project's current state" },
    { name: "-y", description: "Same as --yes" },
    { name: "--json", description: "Run without prompts and print one JSON result" },
    { name: "--dry-run", description: "With --yes: print the plan, write nothing" },
    { name: "--clients", value: "<ids>", description: "MCP clients to configure, comma separated" },
    { name: "--install", value: "<auto|source|binary>", description: "How the bridge is installed" },
    { name: "--binaries", value: "<dir-or-url>", description: "Prebuilt bridge binaries to install" },
    { name: "--disable", value: "<categories>", description: "Tool categories to turn off, comma separated" },
    { name: "--context", value: "<micro|lean|full>", description: "Context strategy" },
    { name: "--native-exclude", value: "<categories>", description: "Categories that skip native Epic tools" },
    { name: "--command", value: "<cmd>", description: "Launcher the client config uses instead of npx" },
    ...toggle("feedback", "Enable the feedback(submit) tool"),
    { name: "--feedback-mode", value: "<interactive|auto-approve|defer>", description: "Feedback approval mode for this project" },
    ...toggle("hook", "Install the feedback nudge hook"),
    ...toggle("skills", "Install the bundled Claude Code skills"),
    ...toggle("native-tools", "Wrap native Epic tools"),
    ...toggle("pin", "Pin the client config to this exact version"),
    { name: "--editor", value: "<name-or-path>", description: "Target one of the editors this server drives" },
  ],
};

/** Entry point for `ue-mcp init [project] [--editor <name-or-path>]`. */
export async function run(argv: string[]): Promise<number | void> {
  try {
    return await init(argv);
  } catch (e) {
    console.error(`\n  ${RED}Fatal error: ${e instanceof Error ? e.message : e}${RESET}\n`);
    return 1;
  }
}
