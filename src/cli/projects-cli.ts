/**
 * `ue-mcp projects [list|add <uproject>|remove <uproject>] [--json]`
 *
 * list    this computer's Unreal projects, their ue-mcp state and the daemons running (the machine report)
 * add     put a project on the list
 * remove  take one off: an added project is forgotten, a found one is hidden
 */
import * as path from "node:path";
import { addProject, machineReport, removeProject, type MachineProject } from "../machine/projects.js";
import { BOLD, DIM, RESET, fail, ok } from "./ui/ansi.js";
import type { CliUsage } from "./cli-main.js";

const USAGE = "usage: ue-mcp projects [list | add <uproject> | remove <uproject>] [--json]";

export const usage: CliUsage = {
  usage: "ue-mcp projects [list | add <uproject> | remove <uproject>] [flags]",
  summary: "List this computer's Unreal projects and the daemons running, or add and remove projects.",
  strict: true,
  flags: [{ name: "--json", description: "Print one JSON document" }],
};

function line(p: MachineProject): string {
  const parts = [
    p.engine.version ? `UE ${p.engine.version}` : "UE ?",
    p.ueMcp.setUp ? `ue-mcp ${p.ueMcp.bridge?.version ?? "?"}` : "ue-mcp not set up",
    ...Object.entries(p.extensions).map(([name, v]) => `${name} ${v}`),
    p.daemon.running ? `daemon pid ${p.daemon.running.pid}` : "",
  ].filter(Boolean);
  return `${BOLD}${p.name}${RESET}  ${parts.join("  ")}  ${DIM}${p.dir}${RESET}`;
}

export async function run(argv: string[]): Promise<number | void> {
  const json = argv.includes("--json");
  const out = (v: unknown): void => {
    process.stdout.write(JSON.stringify(v, null, 2) + "\n");
  };
  const [sub = "list", target, ...extra] = argv.filter((a) => a !== "--json");
  const valid = (sub === "list" && !target) || ((sub === "add" || sub === "remove") && target && extra.length === 0);
  if (!valid) {
    if (json) out({ ok: false, error: USAGE });
    else fail(USAGE);
    return 2;
  }
  try {
    if (sub === "list") {
      const report = machineReport();
      if (json) return out(report);
      if (report.projects.length === 0) ok("no Unreal projects found; add one with `ue-mcp projects add <uproject>`");
      for (const p of report.projects) console.log(`  ${line(p)}`);
      const listed = new Set(report.projects.map((p) => p.key));
      for (const d of report.daemons.filter((x) => !listed.has(x.key))) {
        console.log(`  ${DIM}daemon pid ${d.pid} for ${d.uproject ? path.basename(d.uproject, ".uproject") : d.projectRoot} (not listed)${RESET}`);
      }
      return 0;
    }
    const file = sub === "add" ? addProject(target!) : removeProject(target!);
    if (json) out({ ok: true, [sub === "add" ? "added" : "removed"]: file });
    else ok(`${sub === "add" ? "added" : "removed"} ${file}`);
    return 0;
  } catch (e) {
    const message = e instanceof Error ? e.message : String(e);
    if (json) out({ ok: false, error: message });
    else fail(message);
    return 1;
  }
}
