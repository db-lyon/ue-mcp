/**
 * A process the server starts for the long run (an editor, a daemon) must not
 * be in the server's process tree, which a client ends on reconnect (#1274).
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { describe, expect, it } from "vitest";
import { spawnOutsideTree } from "../../src/core/spawn-outside-tree.js";

const alive = (pid: number): boolean => {
  try {
    process.kill(pid, 0);
    return true;
  } catch {
    return false;
  }
};

describe("spawnOutsideTree (#1274)", () => {
  it.runIf(process.platform === "win32")("starts the process with a parent that has already exited", async () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-outside-tree-"));
    const out = path.join(dir, "ppid.txt");
    const log = fs.openSync(path.join(dir, "out.log"), "a");
    spawnOutsideTree(process.execPath, ["-e", `require("fs").writeFileSync(${JSON.stringify(out)}, String(process.ppid))`], { output: log });
    fs.closeSync(log);
    for (let i = 0; i < 100 && !fs.existsSync(out); i++) await new Promise((r) => setTimeout(r, 50));
    const ppid = Number(fs.readFileSync(out, "utf-8"));
    expect(ppid).not.toBe(process.pid);
    for (let i = 0; i < 40 && alive(ppid); i++) await new Promise((r) => setTimeout(r, 50));
    expect(alive(ppid)).toBe(false);
    fs.rmSync(dir, { recursive: true, force: true });
  }, 15_000);

  it("passes the arguments through untouched", async () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-outside-tree-"));
    const out = path.join(dir, "args.json");
    const tricky = ["C:\\Program Files\\Epic Games\\x.uproject", "a \"quoted\" & ^ arg", ""];
    spawnOutsideTree(process.execPath, ["-e", `require("fs").writeFileSync(${JSON.stringify(out)}, JSON.stringify(process.argv.slice(1)))`, ...tricky]);
    for (let i = 0; i < 100 && !fs.existsSync(out); i++) await new Promise((r) => setTimeout(r, 50));
    expect(JSON.parse(fs.readFileSync(out, "utf-8"))).toEqual(tricky);
    fs.rmSync(dir, { recursive: true, force: true });
  }, 15_000);
});
