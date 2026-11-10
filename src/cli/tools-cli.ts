/**
 * `ue-mcp tools list|enable|disable` - turn categories and Unreal toolsets on
 * and off from a terminal (#1827).
 *
 * The work is the project actions themselves (list_categories,
 * set_category_enabled, set_toolset_enabled), reached one of two ways:
 *
 *   a daemon answers for the project   its HTTP routes, so every client it
 *                                      serves is re-advertised at once
 *   no daemon                          the same runtime a server builds, in
 *                                      this process, with its bridge connected
 *                                      when the editor answers
 *
 * Either way the refusals are the actions' own: an editor that is open but
 * not answering refuses an engine change rather than having its setting file
 * written underneath it.
 */
import * as http from "node:http";
import * as path from "node:path";
import { findUProject } from "../config/uproject-path.js";
import { setDefaultLogLevel } from "../core/log.js";
import { readAnsweringDiscovery, type DaemonDiscovery } from "../daemon/discovery.js";
import { BOLD, DIM, GREEN, RED, RESET, YELLOW } from "./ui/ansi.js";
import type { CliUsage } from "./cli-main.js";

export const usage: CliUsage = {
  usage: "ue-mcp tools <list|enable|disable> [category|toolset ...] [flags]",
  summary:
    "List, turn on or turn off categories and Unreal toolsets for the project in the working directory (or --project).\n" +
    "  list                         Every category, what is on in it, and the Unreal toolsets it maps to\n" +
    "  enable <name>...             Turn categories or toolsets on\n" +
    "  disable <name>...            Turn categories or toolsets off\n" +
    "A name that is a category switches the category (all of it, or --native / --epic); any other\n" +
    "name is an Unreal toolset (as epic(list_toolsets) gives it) and switches only the editor's registry.",
  strict: true,
  flags: [
    { name: "--native", description: "Categories: only ue-mcp's own actions" },
    { name: "--epic", description: "Categories: only the actions wrapping Unreal's toolsets, and those toolsets in the editor" },
    { name: "--project", value: "<uproject>", description: "The project, instead of the one in the working directory" },
    { name: "--json", description: "Print the actions' JSON results only" },
  ],
};

export class ToolsArgError extends Error {}

export interface ToolsCliArgs {
  sub?: string;
  names: string[];
  scope: "all" | "native" | "epic";
  project?: string;
  json: boolean;
}

export function parseToolsArgs(argv: readonly string[]): ToolsCliArgs {
  const out: ToolsCliArgs = { names: [], scope: "all", json: false };
  let native = false;
  let epic = false;
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === "--native") native = true;
    else if (a === "--epic") epic = true;
    else if (a === "--json") out.json = true;
    else if (a === "--project" || a.startsWith("--project=")) {
      const v = a.includes("=") ? a.slice(a.indexOf("=") + 1) : argv[++i];
      if (!v) throw new ToolsArgError("--project needs a .uproject path.");
      out.project = v;
    } else if (a.startsWith("-")) throw new ToolsArgError(`Unknown flag ${a}.`);
    else if (out.sub === undefined) out.sub = a;
    else out.names.push(a);
  }
  if (native && epic) throw new ToolsArgError("--native and --epic together are the whole category: leave both off.");
  out.scope = native ? "native" : epic ? "epic" : "all";
  return out;
}

/** What the commands need from wherever the actions run. */
interface Backend {
  list(): Promise<Outcome>;
  setCategory(category: string, enabled: boolean, scope: string): Promise<Outcome>;
  setToolsets(toolsets: string[], enabled: boolean): Promise<Outcome>;
  close(): Promise<void>;
}

type Outcome = { ok: true; data: Record<string, unknown> } | { ok: false; error: string };

function request(d: DaemonDiscovery, method: string, route: string, body?: unknown): Promise<Outcome> {
  return new Promise((resolve) => {
    const payload = body === undefined ? undefined : JSON.stringify(body);
    const req = http.request({
      host: "127.0.0.1",
      port: d.port,
      path: route,
      method,
      headers: { authorization: `Bearer ${d.token}`, ...(payload ? { "content-type": "application/json" } : {}) },
    }, (res) => {
      const chunks: Buffer[] = [];
      res.on("data", (c: Buffer) => chunks.push(c));
      res.on("end", () => {
        const text = Buffer.concat(chunks).toString("utf-8");
        let parsed: Record<string, unknown> = {};
        try {
          parsed = JSON.parse(text) as Record<string, unknown>;
        } catch {
          parsed = { error: text };
        }
        if ((res.statusCode ?? 500) >= 400) resolve({ ok: false, error: String(parsed.error ?? `HTTP ${res.statusCode}`) });
        else resolve({ ok: true, data: parsed });
      });
    });
    req.on("error", (e) => resolve({ ok: false, error: e.message }));
    if (payload) req.write(payload);
    req.end();
  });
}

function daemonBackend(d: DaemonDiscovery): Backend {
  return {
    list: () => request(d, "GET", "/v1/tools"),
    setCategory: (category, enabled, scope) => request(d, "POST", "/v1/tools/categories", { category, enabled, scope }),
    setToolsets: (toolsets, enabled) => request(d, "POST", "/v1/tools/toolsets", { toolsets, enabled }),
    close: async () => undefined,
  };
}

async function inProcessBackend(uproject: string): Promise<Backend> {
  setDefaultLogLevel("warn");
  const { createRuntime } = await import("../server/runtime.js");
  const rt = await createRuntime([uproject]);
  // One attempt: an editor that answers is changed live, one that does not is the actions' to judge.
  for (const s of rt.sessions.list()) {
    try {
      await s.bridge.connect();
    } catch {
      /* not reachable: the editor-down path decides */
    }
  }
  const run = async (action: string, args: Record<string, unknown> = {}): Promise<Outcome> => {
    const result = await rt.callAction("project", action, args);
    const text = result.content.map((c) => (c.type === "text" ? c.text : "")).join("\n");
    if (result.isError) return { ok: false, error: text.replace(/^Error \[[A-Z_]+\]: /, "") };
    try {
      return { ok: true, data: JSON.parse(text) as Record<string, unknown> };
    } catch {
      return { ok: true, data: { result: text } };
    }
  };
  return {
    list: () => run("list_categories"),
    setCategory: (category, enabled, scope) => run("set_category_enabled", { category, enabled, scope }),
    setToolsets: (toolsets, enabled) => run("set_toolset_enabled", { toolsets, enabled }),
    close: async () => undefined,
  };
}

interface CategoryRow {
  name: string;
  enabled: boolean;
  native: { enabled: boolean; actions: number };
  epic: { enabled: boolean; actions: number; toolsets: Array<{ name: string; enabled?: boolean | null; blockedInSettings?: boolean }> };
}

function onOff(on: boolean): string {
  return on ? `${GREEN}on${RESET}` : `${RED}off${RESET}`;
}

function printList(data: Record<string, unknown>): void {
  const rows = (data.categories ?? []) as CategoryRow[];
  const width = Math.max(8, ...rows.map((r) => r.name.length));
  console.log(`  ${BOLD}${"category".padEnd(width)}  native        epic${RESET}`);
  for (const r of rows) {
    const native = `${onOff(r.native.enabled)} ${DIM}${String(r.native.actions).padStart(4)}${RESET}`;
    const epic = r.epic.actions > 0 || r.epic.toolsets.length > 0
      ? `${onOff(r.epic.enabled)} ${DIM}${String(r.epic.actions).padStart(4)} actions, ${r.epic.toolsets.length} toolsets${RESET}`
      : `${DIM}-${RESET}`;
    console.log(`  ${r.name.padEnd(width)}  ${native}     ${epic}`);
  }
  const engine = data.engine as { connected?: boolean; settingsFile?: string; blocked?: string[] } | undefined;
  console.log("");
  if (engine?.connected) console.log(`  ${DIM}Editor connected: toolset states are the running registry's.${RESET}`);
  else if (engine?.settingsFile) {
    console.log(`  ${DIM}No editor: toolsets blocked in ${engine.settingsFile}: ${(engine.blocked ?? []).join(", ") || "none"}${RESET}`);
  }
  if (typeof data.deprecated === "string") console.log(`  ${YELLOW}${data.deprecated}${RESET}`);
}

/** Entry point for `ue-mcp tools <list|enable|disable>`. */
export async function run(argv: string[]): Promise<number> {
  let args: ToolsCliArgs;
  try {
    args = parseToolsArgs(argv);
  } catch (e) {
    console.error(`  ${RED}${e instanceof Error ? e.message : String(e)}${RESET}`);
    return 2;
  }
  const fail = (message: string, code = 1): number => {
    if (args.json) console.log(JSON.stringify({ ok: false, error: message }));
    else console.error(`  ${RED}${message}${RESET}`);
    return code;
  };
  const sub = args.sub;
  if (sub !== "list" && sub !== "enable" && sub !== "disable") {
    return fail(sub ? `Unknown tools command '${sub}'. Use list, enable or disable.` : "Name a tools command: list, enable or disable.", 2);
  }
  if (sub === "list" && (args.names.length > 0 || args.scope !== "all")) return fail("tools list takes no names and no --native / --epic.", 2);
  if (sub !== "list" && args.names.length === 0) return fail(`tools ${sub} needs at least one category or toolset name.`, 2);

  const uproject = findUProject(args.project ?? process.cwd());
  if (!uproject) return fail("No .uproject found. Run from your project directory or pass --project <uproject>.", 2);

  const daemon = await readAnsweringDiscovery(path.dirname(uproject));
  const backend = daemon ? daemonBackend(daemon) : await inProcessBackend(uproject);
  try {
    const listed = await backend.list();
    if (!listed.ok) return fail(listed.error);
    if (sub === "list") {
      if (args.json) console.log(JSON.stringify(listed.data, null, 2));
      else printList(listed.data);
      return 0;
    }

    const enabled = sub === "enable";
    const categories = new Set(((listed.data.categories ?? []) as CategoryRow[]).map((c) => c.name));
    const catNames = args.names.filter((n) => categories.has(n.toLowerCase()));
    const toolsets = args.names.filter((n) => !categories.has(n.toLowerCase()));
    const notToolsets = toolsets.filter((n) => !n.includes("."));
    if (notToolsets.length > 0) {
      return fail(`${notToolsets.map((n) => `'${n}'`).join(", ")} is not a category or a toolset. Categories: ${[...categories].sort().join(", ")}. A toolset name is qualified, e.g. EditorToolset.MaterialToolset.`, 2);
    }
    if (toolsets.length > 0 && args.scope !== "all") return fail("--native and --epic apply to categories; a toolset is switched whole.", 2);

    const results: Array<Record<string, unknown>> = [];
    let failed = false;
    for (const name of catNames) {
      const r = await backend.setCategory(name.toLowerCase(), enabled, args.scope);
      if (!r.ok) {
        failed = true;
        results.push({ category: name, ok: false, error: r.error });
        if (!args.json) console.error(`  ${RED}${name}: ${r.error}${RESET}`);
        continue;
      }
      results.push({ ok: true, ...r.data });
      if (!args.json) {
        const engine = r.data.engine as { route?: string; file?: string; note?: string } | undefined;
        const where = engine?.route === "editor" ? " (live in the editor too)" : engine?.route === "settings-file" ? ` (toolsets written to ${engine.file})` : "";
        console.log(`  ${name}${args.scope === "all" ? "" : ` ${args.scope}`}: ${onOff(enabled)}${where}`);
        if (engine?.note) console.log(`    ${DIM}${engine.note}${RESET}`);
        if (typeof r.data.warning === "string") console.log(`    ${YELLOW}${r.data.warning}${RESET}`);
      }
    }
    if (toolsets.length > 0) {
      const r = await backend.setToolsets(toolsets, enabled);
      if (!r.ok) {
        failed = true;
        results.push({ toolsets, ok: false, error: r.error });
        if (!args.json) console.error(`  ${RED}${toolsets.join(", ")}: ${r.error}${RESET}`);
      } else {
        results.push({ ok: true, ...r.data });
        if (!args.json) {
          const where = r.data.route === "editor" ? "in the running editor" : `in ${String(r.data.file)}`;
          console.log(`  ${toolsets.join(", ")}: ${onOff(enabled)} ${where}`);
          if (typeof r.data.note === "string") console.log(`    ${DIM}${r.data.note}${RESET}`);
        }
      }
    }
    if (args.json) console.log(JSON.stringify({ ok: !failed, results }, null, 2));
    else if (!daemon) console.log(`\n  ${DIM}No daemon is serving this project; running MCP clients pick the change up when they restart.${RESET}`);
    return failed ? 1 : 0;
  } finally {
    await backend.close();
  }
}
