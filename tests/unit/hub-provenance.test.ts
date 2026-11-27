/**
 * What a run tested: the workspace from source control.
 */
import { describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { workspaceNow } from "../../src/hub/provenance.js";
import type { Run } from "../../src/hub/vcs.js";

describe("the workspace", () => {
  const gitRepo = () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "hub-prov-"));
    fs.mkdirSync(path.join(dir, ".git"));
    return dir;
  };

  it("reads Git's commit and its changed files", async () => {
    const dir = gitRepo();
    const r: Run = async (cmd, args) => {
      if (cmd === "git" && args[0] === "status") return "# branch.oid abc\n# branch.head main\n1 .M N... 100644 100644 100644 a b Content/BP_Door.uasset\n";
      if (cmd === "git" && args[0] === "rev-parse") return "0123456789abcdef0123456789abcdef01234567\n";
      return null;
    };
    try {
      expect(await workspaceNow(dir, r)).toEqual({ vcs: "git", revision: "0123456789abcdef0123456789abcdef01234567", dirty: 1 });
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });

  it("says when the project is under no source control, and when the revision cannot be read", async () => {
    const plain = fs.mkdtempSync(path.join(os.tmpdir(), "hub-prov-"));
    const dir = gitRepo();
    try {
      expect(await workspaceNow(plain, async () => null)).toEqual({ vcs: "none", revision: null, dirty: null });
      const r: Run = async (_cmd, args) => (args[0] === "status" ? "# branch.head main\n" : null);
      expect(await workspaceNow(dir, r)).toEqual({ vcs: "git", revision: null, dirty: 0 });
    } finally {
      fs.rmSync(plain, { recursive: true, force: true });
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });
});
