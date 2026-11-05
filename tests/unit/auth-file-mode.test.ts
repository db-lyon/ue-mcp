import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { writeUserAuth, readUserAuth } from "../../src/feedback/github-auth.js";
import { writePrivateFile } from "../../src/core/private-file.js";

/**
 * auth.json holds a GitHub token. Its mode was set only when the file was
 * created and the directory's never, so a file or folder that already
 * existed with wider bits kept them. Every write now leaves 0700 and 0600.
 */
const posix = process.platform !== "win32";

describe("credential file modes", () => {
  let dir: string;
  const saved = process.env.UE_MCP_AUTH_DIR;
  beforeEach(() => {
    dir = path.join(fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-authmode-")), "auth");
    process.env.UE_MCP_AUTH_DIR = dir;
  });
  afterEach(() => {
    if (saved === undefined) delete process.env.UE_MCP_AUTH_DIR;
    else process.env.UE_MCP_AUTH_DIR = saved;
    fs.rmSync(path.dirname(dir), { recursive: true, force: true });
  });

  it("tightens an existing world-readable auth.json and its directory on rewrite", async () => {
    fs.mkdirSync(dir, { recursive: true, mode: 0o777 });
    fs.chmodSync(dir, 0o777);
    const file = path.join(dir, "auth.json");
    fs.writeFileSync(file, "{}", { mode: 0o666 });
    fs.chmodSync(file, 0o666);

    await writeUserAuth({ token: "t0k", login: "me", authorized_at: "now" });

    expect((await readUserAuth())?.token).toBe("t0k");
    if (posix) {
      expect(fs.statSync(file).mode & 0o777).toBe(0o600);
      expect(fs.statSync(dir).mode & 0o777).toBe(0o700);
    }
    expect(fs.readdirSync(dir)).toEqual(["auth.json"]);
  });

  it("creates the directory owner-only", async () => {
    await writeUserAuth({ token: "t", login: "me", authorized_at: "now" });
    if (posix) expect(fs.statSync(dir).mode & 0o777).toBe(0o700);
    expect(fs.existsSync(path.join(dir, "auth.json"))).toBe(true);
  });

  // Creating a file link needs a privilege Windows does not grant by default.
  it.runIf(posix)("replaces a link at the target instead of writing through it", async () => {
    fs.mkdirSync(dir, { recursive: true });
    const elsewhere = path.join(path.dirname(dir), "elsewhere.json");
    fs.writeFileSync(elsewhere, "keep");
    const file = path.join(dir, "auth.json");
    fs.symlinkSync(elsewhere, file, "file");
    await writePrivateFile(file, "secret");
    expect(fs.readFileSync(elsewhere, "utf8")).toBe("keep");
    expect(fs.lstatSync(file).isSymbolicLink()).toBe(false);
    expect(fs.readFileSync(file, "utf8")).toBe("secret");
  });
});
