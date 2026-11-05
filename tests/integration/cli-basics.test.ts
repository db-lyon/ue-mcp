/**
 * The `ue-mcp` binary's own routing, spawned: help, version, refused flags,
 * and every command's --help, which must print usage and touch nothing.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { CLI_COMMANDS } from "../../src/cli/cli-commands.js";
import { covers } from "../helpers/covers.js";
import { PACKAGE_VERSION, plain, REPO_ROOT, runCli, Sandbox, snapshotTree, type CliResult } from "./harness.js";

let sb: Sandbox;
let project: string;
beforeAll(() => {
  sb = new Sandbox();
  project = sb.makeProject("Game", { config: "ue-mcp:\n  version: 1\n  context:\n    strategy: lean\n" });
});
afterAll(() => sb.cleanup());

/** Run several command lines with bounded concurrency. */
async function many(lines: string[][], cwd?: string): Promise<CliResult[]> {
  const out: CliResult[] = new Array(lines.length);
  let next = 0;
  const worker = async () => {
    while (next < lines.length) {
      const i = next++;
      out[i] = await runCli(sb, lines[i], { cwd });
    }
  };
  await Promise.all(Array.from({ length: 6 }, worker));
  return out;
}

describe("top level", () => {
  it("prints the command list for every help spelling and starts no server", async () => {
    covers("cli.help");
    const results = await many([["help"], ["--help"], ["-h"], [project, "--help"]]);
    for (const r of results) {
      expect(r.code).toBe(0);
      expect(r.stdout).toMatch(/^Usage:/);
      for (const c of CLI_COMMANDS) expect(r.stdout).toContain(`  ${c.name}`);
    }
  });

  it("refuses a stray flag with exit 2 instead of serving", async () => {
    covers("cli.unknown-flag");
    const r = await runCli(sb, ["--porject", project]);
    expect(r.code).toBe(2);
    expect(r.stderr).toContain("Unknown flag: --porject");
    expect(r.stderr).toContain("Usage:");
  });

  it("prints the package version for every spelling", async () => {
    covers("cli.version");
    for (const r of await many([["version"], ["--version"], ["-v"]])) {
      expect(r.code).toBe(0);
      expect(r.stdout.trim()).toBe(PACKAGE_VERSION);
    }
  });
});

describe("every command's --help", () => {
  it("prints its usage, exits 0 and writes nothing into the project or the home directory", async () => {
    covers("cli.command-help");
    const cwd = path.dirname(project);
    const before = { project: snapshotTree(cwd), home: snapshotTree(sb.home) };
    const results = await many(CLI_COMMANDS.map((c) => [c.name, "--help"]), cwd);
    CLI_COMMANDS.forEach((c, i) => {
      const r = results[i];
      expect(r.code, `${c.name} --help: ${r.stderr}`).toBe(0);
      expect(plain(r.stdout), `${c.name} --help`).toContain(`ue-mcp ${c.name}`);
    });
    expect(snapshotTree(cwd)).toEqual(before.project);
    expect(snapshotTree(sb.home)).toEqual(before.home);
  });

  it("is what each package bin prints too", async () => {
    covers("cli.init.bin", "cli.update.bin", "cli.deploy.bin");
    const bins = JSON.parse(fs.readFileSync(path.join(REPO_ROOT, "package.json"), "utf8")).bin as Record<string, string>;
    const pairs: Array<[string, string]> = [
      ["ue-mcp-init", "init"],
      ["ue-mcp-update", "update"],
      ["ue-mcp-deploy", "deploy"],
    ];
    for (const [bin, command] of pairs) {
      const src = path.join(REPO_ROOT, bins[bin].replace(/^dist\//, "src/").replace(/\.js$/, ".ts"));
      const viaBin = await runCli(sb, ["--help"], { entry: src });
      const viaCommand = await runCli(sb, [command, "--help"]);
      expect(viaBin.code, `${bin}: ${viaBin.stderr}`).toBe(0);
      expect(viaBin.stdout).toBe(viaCommand.stdout);
    }
  });
});

describe("strict commands", () => {
  it("refuse a mistyped flag with exit 2 and do no work", async () => {
    covers("cli.command-unknown-flag");
    const cwd = path.dirname(project);
    const before = snapshotTree(cwd);
    const strict = ["init", "update", "deploy", "build", "doctor", "status", "daemon", "extension", "uninstall-hooks"];
    const results = await many(strict.map((c) => [c, "--definitely-not-a-flag"]), cwd);
    strict.forEach((c, i) => {
      expect(results[i].code, `${c}: ${results[i].stdout}${results[i].stderr}`).toBe(2);
      expect(results[i].stderr).toContain("Unknown flag: --definitely-not-a-flag");
    });
    expect(snapshotTree(cwd)).toEqual(before);
  });
});
