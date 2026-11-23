// @session per-session: Reads and decides one project's consent requests through its daemon, chosen with --project.
/**
 * `ue-mcp editor requests|approve <id>|deny <id>`: a person's side of the
 * consent rule (#2075). An agent session cannot override a wait or a block
 * from another session; the person at the terminal can, through the
 * project's daemon, which holds the requests.
 */
import * as path from "node:path";
import { findUProject } from "../config/uproject-path.js";
import type { EditorDisruptionRequest } from "../contract/daemon.js";
import { daemonRequest } from "../daemon/client.js";
import { readAnsweringDiscovery } from "../daemon/discovery.js";
import { BOLD, DIM, GREEN, RED, RESET, YELLOW } from "../terminal/ansi.js";
import type { CliUsage } from "./cli-main.js";
import { selectProject } from "./editor-flag.js";

export const usage: CliUsage = {
  usage: "ue-mcp editor <requests|approve|deny> [request-id] [flags]",
  summary:
    "Read and decide requests to stop, restart, rebuild or patch the editor that other agent sessions are using.\n" +
    "  requests          Every request with each session's answer, and what each session is doing\n" +
    "  approve <id>      Let the request run, whatever the sessions answered\n" +
    "  deny <id>         Refuse it",
  strict: true,
  flags: [
    { name: "--project", value: "<uproject>", description: "The project, instead of the one in the working directory" },
    { name: "--json", description: "Print the daemon's JSON answer only" },
  ],
};

const STATE_COLOR: Record<string, string> = { approved: GREEN, done: GREEN, blocked: RED, denied: RED, pending: YELLOW, confirm: YELLOW, expired: DIM };

function printRequest(r: EditorDisruptionRequest): void {
  console.log(`  ${BOLD}${r.id}${RESET}  ${STATE_COLOR[r.state] ?? ""}${r.state}${RESET}  ${r.op} asked by '${r.requester.label}' (${r.requester.id})${r.reason ? `: ${r.reason}` : ""}`);
  if (r.state === "confirm") console.log(`    ${DIM}waiting for the asking agent to account for its own subagents${RESET}`);
  if (r.self.inflight.length) console.log(`    ${DIM}its own calls in flight: ${r.self.inflight.join(", ")}${RESET}`);
  for (const a of r.answers) {
    console.log(`    ${a.label} (${a.session}${a.parent ? `, started by ${a.parent}` : ""}): ${a.answer.replace(/_/g, " ")}, ${a.reason}`);
  }
}

/** Entry point for `ue-mcp editor <requests|approve|deny>`. */
export async function run(argv: string[]): Promise<number> {
  let rest: string[];
  let project: string | undefined;
  try {
    const target = selectProject(argv, { projectFlag: true });
    rest = target.rest;
    project = target.projectPath;
  } catch (e) {
    console.error(`  ${RED}${e instanceof Error ? e.message : String(e)}${RESET}`);
    return 2;
  }
  const json = rest.includes("--json");
  const args = rest.filter((a) => a !== "--json");
  const fail = (message: string, code = 1): number => {
    if (json) console.log(JSON.stringify({ ok: false, error: message }));
    else console.error(`  ${RED}${message}${RESET}`);
    return code;
  };
  const bad = args.find((a) => a.startsWith("-"));
  if (bad) return fail(`Unknown flag ${bad}.`, 2);
  const [sub, id] = args;
  if (sub !== "requests" && sub !== "approve" && sub !== "deny") {
    return fail(sub ? `Unknown editor command '${sub}'. Use requests, approve or deny.` : "Name an editor command: requests, approve or deny.", 2);
  }
  if (sub !== "requests" && !id) return fail(`editor ${sub} needs the request id, such as r-3f2a1c. ue-mcp editor requests lists them.`, 2);

  const uproject = findUProject(project ?? process.cwd());
  if (!uproject) return fail("No .uproject found. Run from your project directory or pass --project <uproject>.", 2);
  const daemon = await readAnsweringDiscovery(path.dirname(uproject));
  if (!daemon) return fail("No ue-mcp daemon is running for this project, so there are no requests: requests live in the daemon the agents share.");

  try {
    if (sub === "requests") {
      const res = await daemonRequest(daemon, "disruptions.list");
      if (res.status >= 400) return fail(String((res.json as { error?: string } | undefined)?.error ?? `HTTP ${res.status}`));
      const body = res.json as { enforcing: boolean; requests: EditorDisruptionRequest[] };
      if (json) {
        console.log(JSON.stringify(body, null, 2));
        return 0;
      }
      if (!body.enforcing) console.log(`  ${DIM}disruption.mode is off in ue-mcp.yml: nothing is asked.${RESET}`);
      if (body.requests.length === 0) console.log("  No requests.");
      for (const r of body.requests) printRequest(r);
      return 0;
    }
    const res = await daemonRequest(daemon, sub === "approve" ? "disruptions.approve" : "disruptions.deny", {
      method: "POST",
      params: { id: id! },
      body: { by: "ue-mcp editor (terminal)" },
    });
    if (res.status >= 400) return fail(String((res.json as { error?: string } | undefined)?.error ?? `HTTP ${res.status}`));
    const r = res.json as EditorDisruptionRequest;
    if (json) console.log(JSON.stringify(r, null, 2));
    else {
      printRequest(r);
      console.log(sub === "approve"
        ? `  ${GREEN}Approved.${RESET} It runs when '${r.requester.label}' runs the operation again.`
        : `  ${RED}Denied.${RESET}`);
    }
    return 0;
  } catch (e) {
    return fail(`The daemon did not answer: ${e instanceof Error ? e.message : String(e)}`);
  }
}
