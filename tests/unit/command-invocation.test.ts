import { describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { UnsafeArgumentError, commandInvocation, spawnInvocationSync } from "../../src/core/safe-spawn.js";
import { parseIssueNumber } from "../../src/cli/resolve.js";

/**
 * `ue-mcp resolve` runs git, gh and claude with a branch name, a repo and an
 * issue number in their arguments. Each runs from an argv array, never a
 * shell line, so none of them is parsed as a command.
 */
describe("commands found on PATH", () => {
  function withBin(files: string[], body: (dir: string) => void): void {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-cmdinv-"));
    try {
      for (const f of files) fs.writeFileSync(path.join(dir, f), "");
      body(dir);
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  }
  const env = (dir: string) => ({ PATH: dir, PATHEXT: ".COM;.EXE;.BAT;.CMD" });

  it("passes argv straight through off Windows", () => {
    expect(commandInvocation("git", ["checkout", "-b", "a;b&c"], { platform: "linux" })).toEqual({ command: "git", args: ["checkout", "-b", "a;b&c"] });
  });

  it("runs a Windows executable directly with argv", () => {
    withBin(["gh.exe"], (dir) => {
      const inv = commandInvocation("gh", ["issue", "view", "1&calc"], { platform: "win32", env: env(dir) });
      expect(inv).toEqual({ command: path.join(dir, "gh.exe"), args: ["issue", "view", "1&calc"] });
    });
  });

  it("runs a Windows batch shim through cmd.exe with every argument quoted and checked", () => {
    withBin(["claude.cmd"], (dir) => {
      const inv = commandInvocation("claude", ["--print", "Bash(git commit:*)", "a&b"], { platform: "win32", env: env(dir) });
      expect(inv.windowsVerbatimArguments).toBe(true);
      expect(inv.args[3]).toBe(`""${path.join(dir, "claude.cmd")}" --print "Bash(git commit:*)" "a&b""`);
      expect(() => commandInvocation("claude", ["%PATH%"], { platform: "win32", env: env(dir) })).toThrow(UnsafeArgumentError);
    });
  });

  it("keeps a shell operator as one argument in a real child process", () => {
    const r = spawnInvocationSync(
      commandInvocation(process.execPath, ["-e", "process.stdout.write(JSON.stringify(process.argv.slice(1)))", "x & echo y", "a|b"]),
      { encoding: "utf8" },
    );
    expect(JSON.parse(String(r.stdout))).toEqual(["x & echo y", "a|b"]);
  });
});

describe("the issue number resolve takes", () => {
  it("is digits only", () => {
    expect(parseIssueNumber("16")).toBe(16);
    for (const bad of [undefined, "", "0", "1e3", "0x10", "16 && calc", "-1", "16;x", " 16"]) {
      expect(parseIssueNumber(bad)).toBeNull();
    }
  });
});
