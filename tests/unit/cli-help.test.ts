import { describe, it, expect, vi, beforeEach, afterEach, type MockInstance } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { CLI_COMMANDS, formatTopLevelUsage, routeTopLevel, runCliCommand } from "../../src/cli/cli-commands.js";
import { formatUsage, hasFetchExitAbort, isHelpRequest, preflight, runCli, unknownFlags, type CliUsage } from "../../src/cli/cli-main.js";

// #1226: --help must print usage and exit 0 without touching anything, and a
// mistyped flag must be refused before a command reaches its default work.

let dir: string;
let cwd: string;
let exit: MockInstance<typeof process.exit>;
let log: MockInstance<typeof console.log>;
let error: MockInstance<typeof console.error>;

/** Every file under `root` with its contents, to prove nothing was written. */
function snapshot(root: string): Record<string, string> {
  const out: Record<string, string> = {};
  const walk = (d: string): void => {
    for (const e of fs.readdirSync(d, { withFileTypes: true })) {
      const p = path.join(d, e.name);
      if (e.isDirectory()) walk(p);
      else out[path.relative(root, p)] = fs.readFileSync(p, "utf-8");
    }
  };
  walk(root);
  return out;
}

beforeEach(() => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-help-"));
  fs.writeFileSync(path.join(dir, "Game.uproject"), JSON.stringify({ EngineAssociation: "5.8", Plugins: [] }));
  fs.writeFileSync(path.join(dir, "ue-mcp.yml"), "version: 1\n");
  cwd = process.cwd();
  process.chdir(dir);
  exit = vi.spyOn(process, "exit").mockImplementation((() => undefined) as never);
  log = vi.spyOn(console, "log").mockImplementation(() => undefined);
  error = vi.spyOn(console, "error").mockImplementation(() => undefined);
});

afterEach(() => {
  process.chdir(cwd);
  vi.restoreAllMocks();
  fs.rmSync(dir, { recursive: true, force: true });
});

const printed = (): string => log.mock.calls.map((c) => c.join(" ")).join("\n");

describe("--help on every ue-mcp command", () => {
  it.each(CLI_COMMANDS.map((c) => [c.name, c] as const))("%s prints usage, exits 0 and writes nothing", async (_n, command) => {
    const before = snapshot(dir);
    await runCliCommand(command, ["--help"]);
    for (const [code] of exit.mock.calls) expect(code).toBe(0);
    expect(error).not.toHaveBeenCalled();
    expect(printed().length).toBeGreaterThan(0);
    expect(snapshot(dir)).toEqual(before);
  });

  it.each(["update", "deploy", "init", "plugin"])("%s --help wins over a project argument and other flags", async (name) => {
    const command = CLI_COMMANDS.find((c) => c.name === name)!;
    const before = snapshot(dir);
    await runCliCommand(command, [path.join(dir, "Game.uproject"), "--yes", "-h"]);
    expect(exit).toHaveBeenCalledWith(0);
    expect(printed()).toContain(`ue-mcp ${name}`);
    expect(snapshot(dir)).toEqual(before);
  });

  it("plugin install --help does not install a package named --help", async () => {
    const before = snapshot(dir);
    await runCliCommand(CLI_COMMANDS.find((c) => c.name === "plugin")!, ["install", "--help"]);
    expect(exit).toHaveBeenCalledWith(0);
    expect(snapshot(dir)).toEqual(before);
  });
});

describe("the package bins", () => {
  const bins = {
    init: () => import("../../src/init.js"),
    update: () => import("../../src/update.js"),
    deploy: () => import("../../src/deploy-cli.js"),
  };
  it.each(Object.entries(bins))("ue-mcp-%s --help prints usage, exits 0 and writes nothing", async (_bin, load) => {
    const mod = await load();
    const before = snapshot(dir);
    await runCli(mod.run, ["--help"], mod.usage);
    expect(exit).toHaveBeenCalledWith(0);
    expect(printed()).toContain("Usage:");
    expect(snapshot(dir)).toEqual(before);
  });
});

describe("unknown flags", () => {
  it.each(["update", "deploy", "init", "build", "doctor", "status"])("%s refuses a mistyped flag before doing anything", async (name) => {
    const before = snapshot(dir);
    await runCliCommand(CLI_COMMANDS.find((c) => c.name === name)!, ["--no-deplyo"]);
    expect(exit).toHaveBeenCalledWith(2);
    expect(error.mock.calls.flat().join(" ")).toContain("--no-deplyo");
    expect(snapshot(dir)).toEqual(before);
  });

  it("accepts declared flags, their values and --flag=value", () => {
    const usage: CliUsage = {
      usage: "x",
      strict: true,
      flags: [{ name: "--a", description: "" }, { name: "--b", value: "<v>", description: "" }],
    };
    expect(unknownFlags(["proj", "--a", "--b", "-weird-value", "--b=1"], usage)).toEqual([]);
    expect(unknownFlags(["--c", "--a=1", "-z"], usage)).toEqual(["--c", "-z"]);
    expect(unknownFlags(["--", "--c"], usage)).toEqual([]);
    expect(preflight(["--c"], { ...usage, strict: false })).toBeUndefined();
  });

  it("treats -h and --help as help only before a -- separator", () => {
    expect(isHelpRequest(["x", "-h"])).toBe(true);
    expect(isHelpRequest(["--", "--help"])).toBe(false);
  });

  it("lists every flag and the help flag in the usage text", () => {
    const text = formatUsage({ usage: "ue-mcp x", flags: [{ name: "--b", value: "<v>", description: "bee" }] });
    expect(text).toContain("--b <v>");
    expect(text).toContain("--help, -h");
  });
});

describe("ue-mcp with no command", () => {
  it("routes help spellings to the top-level usage instead of the server", () => {
    for (const a of ["--help", "-h", "help"]) expect(routeTopLevel([a]).kind).toBe("help");
    expect(routeTopLevel(["C:/p/Game.uproject", "--help"]).kind).toBe("help");
  });

  it("refuses a stray flag instead of starting the server", () => {
    expect(routeTopLevel(["--porject", "C:/p/Game.uproject"])).toEqual({ kind: "unknown-flags", flags: ["--porject"] });
  });

  it("serves projects and runs commands", () => {
    expect(routeTopLevel(["C:/p/Game.uproject"])).toEqual({ kind: "serve", projects: ["C:/p/Game.uproject"] });
    expect(routeTopLevel([]).kind).toBe("serve");
    const r = routeTopLevel(["update", "--help"]);
    expect(r.kind === "command" && r.command.name).toBe("update");
    expect(routeTopLevel(["--version"]).kind).toBe("command");
  });

  it("names every command in the top-level usage", () => {
    const text = formatTopLevelUsage();
    for (const c of CLI_COMMANDS) expect(text).toContain(c.name);
  });
});

// #1442: Windows builds whose exit aborts after fetch get the exit guard.
describe("hasFetchExitAbort", () => {
  it("matches the Windows builds without the upstream fix", () => {
    for (const v of ["23.0.0", "24.0.0", "24.19.0", "25.2.1", "26.6.0"]) expect(hasFetchExitAbort(v, "win32"), v).toBe(true);
    for (const v of ["22.23.4", "24.20.0", "24.22.0", "26.7.0", "27.0.0"]) expect(hasFetchExitAbort(v, "win32"), v).toBe(false);
  });

  it("never applies off Windows", () => {
    expect(hasFetchExitAbort("24.19.0", "linux")).toBe(false);
    expect(hasFetchExitAbort("24.19.0", "darwin")).toBe(false);
  });
});
