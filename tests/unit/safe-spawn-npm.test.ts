import { describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { UnsafeArgumentError, assertPackageSpec, findWindowsNpm, npmInvocation, runNpmSync } from "../../src/core/safe-spawn.js";
import { latestFromNpmView } from "../../src/cli/update.js";

/**
 * `plugin install`, `plugin remove`, `plugin update` and `update` hand npm a
 * package name the user or an agent typed, or a version npm reported. npm runs
 * under node with no shell, so each stays exactly one argument.
 */
describe("npm without a shell", () => {
  it("on Windows runs the npm-cli.js beside the first npm.cmd on PATH under node", () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-npmcli-"));
    try {
      const empty = path.join(dir, "empty");
      fs.mkdirSync(empty);
      fs.writeFileSync(path.join(dir, "npm.cmd"), "");
      const cli = path.join(dir, "node_modules", "npm", "bin", "npm-cli.js");
      fs.mkdirSync(path.dirname(cli), { recursive: true });
      fs.writeFileSync(cli, "");
      const env = { PATH: `${empty};${dir}` };
      expect(findWindowsNpm(env)).toEqual({ npmCli: cli });
      const inv = npmInvocation(["install", "--save", "a&calc"], { platform: "win32", env });
      expect(inv).toEqual({ command: process.execPath, args: [cli, "install", "--save", "a&calc"] });
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });

  it("runs the npm binary with argv off Windows", () => {
    expect(npmInvocation(["view", "a&calc"], { platform: "linux" })).toEqual({ command: "npm", args: ["view", "a&calc"] });
  });

  it("on Windows, an npm.cmd with no npm-cli.js beside it gets a quoted cmd line, refusing what quoting cannot hold", () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-npmcmd-"));
    try {
      const cmd = path.join(dir, "npm.cmd");
      fs.writeFileSync(cmd, "");
      const env = { PATH: dir };
      const inv = npmInvocation(["install", "a&calc"], { platform: "win32", env });
      expect(inv.windowsVerbatimArguments).toBe(true);
      expect(inv.args.slice(0, 3)).toEqual(["/d", "/s", "/c"]);
      expect(inv.args[3]).toBe(`""${cmd}" install "a&calc""`);
      for (const bad of ["a%PATH%", 'a"b', "a!b", "a^b", "a\nb"]) {
        expect(() => npmInvocation(["install", bad], { platform: "win32", env })).toThrow(UnsafeArgumentError);
      }
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });

  it("runs a real npm on this machine without a shell", () => {
    const r = runNpmSync(["--version"], { encoding: "utf8" });
    expect(r.status).toBe(0);
    expect(String(r.stdout).trim()).toMatch(/^\d+\.\d+\.\d+/);
  });

  it("refuses a package spec that npm would read as an option", () => {
    for (const bad of ["-g", "--registry=http://x", "", " ", "a\nb"]) {
      expect(() => assertPackageSpec(bad)).toThrow(UnsafeArgumentError);
    }
    for (const ok of ["ue-mcp-plugin-x", "@scope/pkg@^1.2.0", "./local/dir", "https://x.test/p.tgz"]) {
      expect(assertPackageSpec(ok)).toBe(ok);
    }
  });
});

describe("the version `ue-mcp update` installs", () => {
  it("is taken from npm view only when it is a version number", () => {
    expect(latestFromNpmView("1.4.12\n")).toBe("1.4.12");
    expect(latestFromNpmView("npm notice x\n1.5.0-beta.2")).toBe("1.5.0-beta.2");
    expect(latestFromNpmView("1.4.12 & calc")).toBeNull();
    expect(latestFromNpmView("1.4.12;rm -rf ~")).toBeNull();
    expect(latestFromNpmView(null)).toBeNull();
  });
});
