/**
 * `ue-mcp flow list|plan|run` - the flow engine from a terminal (#1477).
 *
 * Builds the same runtime the server does for one project, in this process,
 * and sends each request through the flow tool's own dispatch. No server, no
 * daemon and no HTTP surface is involved, so a flow behaves here exactly as it
 * does for an MCP client.
 */
import * as path from "node:path";
import type { FlowEvent } from "../flow/events.js";
import { subscribeFlowEvents } from "../flow/events.js";
import { findUProject, isUProjectPath } from "../config/uproject-path.js";
import { takeEditorTarget, EditorFlagError } from "./editor-flag.js";
import { setDefaultLogLevel } from "../core/log.js";
import { BOLD, DIM, GREEN, RED, RESET, YELLOW } from "./ui/ansi.js";
import type { CliUsage } from "./cli-main.js";

export const usage: CliUsage = {
  usage: "ue-mcp flow <list|plan|run> [name] [flags]",
  summary:
    "List, plan or run the flows of the project in the working directory (or --editor).\n" +
    "  list         Every flow from ue-mcp.yml, the user-global config, plugins and the built-ins\n" +
    "  plan <name>  The resolved steps and the params a run may pass, without running anything\n" +
    "  run <name>   Run the flow, printing each step as it finishes. Exits 1 when the flow fails.",
  strict: true,
  flags: [
    { name: "--param", value: "<key=value>", description: "run: a runtime option for every step (repeatable). JSON values are parsed" },
    { name: "--skip", value: "<step>", description: "run: a step name or number to skip (repeatable)" },
    { name: "--rollback-on-failure", description: "run: invoke the collected inverses in reverse order when a step fails" },
    { name: "--editor", value: "<name-or-path>", description: "Target one of the editors this server drives, or a project path" },
    { name: "--json", description: "Print the flow tool's JSON result only" },
  ],
};

export interface FlowCliArgs {
  sub?: string;
  name?: string;
  params: Record<string, unknown>;
  skip: string[];
  rollback: boolean;
  json: boolean;
  extra: string[];
}

/** A `--param` value: JSON when it parses (numbers, booleans, objects), the raw string otherwise. */
function paramValue(raw: string): unknown {
  try {
    return JSON.parse(raw);
  } catch {
    return raw;
  }
}

export class FlowArgError extends Error {}

/** Parse what follows `ue-mcp flow`, with --editor already taken out. */
export function parseFlowArgs(argv: readonly string[]): FlowCliArgs {
  const out: FlowCliArgs = { params: {}, skip: [], rollback: false, json: false, extra: [] };
  const positionals: string[] = [];
  const valueOf = (i: number, flag: string): string => {
    const arg = argv[i];
    if (arg.includes("=") && arg.startsWith(`${flag}=`)) return arg.slice(flag.length + 1);
    const next = argv[i + 1];
    if (next === undefined) throw new FlowArgError(`${flag} needs a value.`);
    return next;
  };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === "--json") out.json = true;
    else if (arg === "--rollback-on-failure") out.rollback = true;
    else if (arg === "--param" || arg.startsWith("--param=")) {
      const pair = valueOf(i, "--param");
      if (arg === "--param") i++;
      const eq = pair.indexOf("=");
      if (eq <= 0) throw new FlowArgError(`--param takes key=value, got '${pair}'.`);
      out.params[pair.slice(0, eq)] = paramValue(pair.slice(eq + 1));
    } else if (arg === "--skip" || arg.startsWith("--skip=")) {
      out.skip.push(valueOf(i, "--skip"));
      if (arg === "--skip") i++;
    } else positionals.push(arg);
  }
  [out.sub, out.name, ...out.extra] = positionals;
  return out;
}

/** The project a flow command addresses: --editor first, then the working directory. */
function targetProject(argv: string[]): { project: string | null; rest: string[] } {
  const target = takeEditorTarget(argv);
  if (target.projectPath) {
    const p = target.projectPath;
    return { project: isUProjectPath(p) ? path.resolve(p) : findUProject(p), rest: target.rest };
  }
  return { project: findUProject(process.cwd()), rest: target.rest };
}

interface CliFailure {
  code: number;
  message: string;
}

function report(json: boolean, failure: CliFailure): number {
  if (json) console.log(JSON.stringify({ ok: false, error: failure.message }));
  else console.error(`  ${RED}${failure.message}${RESET}`);
  return failure.code;
}

/** A line when a step starts and one when it finishes, while the run is still going. */
export function progressLine(event: FlowEvent): string | undefined {
  if (event.type === "step_started") return `  ${DIM}${event.step.stepNumber}. ${event.step.name} started${RESET}`;
  if (event.type !== "step_completed") return undefined;
  const r = event.result;
  const mark = r.skipped ? `${DIM}-${RESET}` : r.success ? `${GREEN}ok${RESET}` : `${RED}FAILED${RESET}`;
  const err = r.error ? ` ${r.error.message}` : "";
  return `  ${event.step.stepNumber}. ${event.step.name} ${mark} ${DIM}${r.duration}ms${RESET}${err}`;
}

interface ListResult {
  flowCount: number;
  flows: Array<{ name: string; description?: string; stepCount: number }>;
}

function printList(list: ListResult): void {
  if (list.flows.length === 0) {
    console.log("  No flows.");
    return;
  }
  const width = Math.max(...list.flows.map((f) => f.name.length));
  for (const f of list.flows) {
    const steps = `${f.stepCount} step${f.stepCount === 1 ? "" : "s"}`;
    console.log(`  ${BOLD}${f.name.padEnd(width)}${RESET}  ${DIM}${steps.padEnd(8)}${RESET}  ${f.description ?? ""}`);
  }
  console.log(`\n  ${list.flowCount} flow${list.flowCount === 1 ? "" : "s"}`);
}

interface PlanResult {
  steps?: Array<{ stepNumber: number; name: string; type: string; skipped: boolean; when?: unknown }>;
  inputs?: Array<{ name: string; type: string | string[]; required: boolean; description?: string }>;
  definition?: { description?: string };
}

function printPlan(name: string, plan: PlanResult): void {
  console.log(`  ${BOLD}${name}${RESET}${plan.definition?.description ? `  ${plan.definition.description}` : ""}`);
  console.log("");
  for (const s of plan.steps ?? []) {
    const kind = s.type === "flow" ? " (flow)" : "";
    const when = s.when !== undefined ? ` ${DIM}when ${String(s.when)}${RESET}` : "";
    console.log(`  ${s.stepNumber}. ${s.name}${kind}${when}`);
  }
  const inputs = plan.inputs ?? [];
  if (inputs.length > 0) {
    console.log("\n  Params:");
    for (const i of inputs) {
      const type = Array.isArray(i.type) ? i.type.join("|") : i.type;
      const req = i.required ? `${YELLOW}required${RESET}` : `${DIM}optional${RESET}`;
      console.log(`    ${i.name} (${type}) ${req}${i.description ? `  ${i.description}` : ""}`);
    }
  }
}

/** Entry point for `ue-mcp flow <list|plan|run> [name] [flags]`. */
export async function run(argv: string[]): Promise<number> {
  const wantsJson = argv.includes("--json");
  let args: FlowCliArgs;
  let project: string | null;
  try {
    const target = targetProject(argv);
    project = target.project;
    args = parseFlowArgs(target.rest);
  } catch (e) {
    if (e instanceof EditorFlagError || e instanceof FlowArgError) return report(wantsJson, { code: 2, message: e.message });
    throw e;
  }
  const json = args.json;

  const sub = args.sub;
  if (sub !== "list" && sub !== "plan" && sub !== "run") {
    return report(json, { code: 2, message: sub ? `Unknown flow command '${sub}'. Use list, plan or run.` : "Name a flow command: list, plan or run." });
  }
  if (sub !== "list" && !args.name) return report(json, { code: 2, message: `flow ${sub} needs a flow name.` });
  if (args.extra.length > 0) return report(json, { code: 2, message: `Unexpected argument${args.extra.length > 1 ? "s" : ""}: ${args.extra.join(" ")}` });
  if (sub !== "run" && (args.skip.length > 0 || args.rollback || Object.keys(args.params).length > 0)) {
    return report(json, { code: 2, message: "--param, --skip and --rollback-on-failure apply to flow run only." });
  }
  if (!project) return report(json, { code: 2, message: "No .uproject found. Run from your project directory or pass --editor." });

  // The runtime logs its startup the way a server does; a terminal command wants warnings only.
  setDefaultLogLevel("warn");
  const { createRuntime } = await import("../server/runtime.js");
  const rt = await createRuntime([project]);

  const input: Record<string, unknown> = { action: sub };
  if (args.name) input.flowName = args.name;
  if (sub === "run") {
    if (Object.keys(args.params).length > 0) input.params = args.params;
    if (args.skip.length > 0) input.skip = args.skip;
    if (args.rollback) input.rollback_on_failure = true;
    // Only a run reaches the editor; list and plan read config.
    await rt.connectBridges();
  }

  const unsubscribe = sub === "run" && !json
    ? subscribeFlowEvents((event) => {
        const line = progressLine(event);
        if (line) console.log(line);
      })
    : () => undefined;
  let result;
  try {
    result = await rt.callTool(rt.flowTool, input);
  } finally {
    unsubscribe();
  }

  // A flow answers with one text block; a media block (an image a step returned) carries no JSON.
  const first = result.content[0];
  const text = first?.type === "text" ? first.text : "";
  if (result.isError) {
    if (json) console.log(text);
    else console.error(`  ${RED}${text}${RESET}`);
    return 1;
  }
  let parsed: Record<string, unknown>;
  try {
    parsed = JSON.parse(text) as Record<string, unknown>;
  } catch {
    console.log(text);
    return 0;
  }
  if (json) {
    console.log(JSON.stringify(parsed, null, 2));
  } else if (sub === "list") {
    printList(parsed as unknown as ListResult);
  } else if (sub === "plan") {
    printPlan(args.name!, parsed as PlanResult);
  } else {
    console.log("");
    console.log(String(parsed.summary ?? ""));
  }
  return sub === "run" && parsed.success !== true ? 1 : 0;
}
