/**
 * What a run tested: the script's version from its definition and the
 * workspace from source control.
 */
import { describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { scriptHashOf, workspaceNow } from "../../src/hub/provenance.js";
import { canonical } from "../../src/hub/util.js";
import type { Run } from "../../src/hub/vcs.js";

describe("the script's version", () => {
  const read = { path: "/Game/Tests/Door", name: "Door", description: "Opens", map: "/Game/Maps/Glade", steps: [{ instruction: "Walk", judge: "auto", expect: { value: "x", op: "gt", target: 1 } }], step_count: 1 };

  it("is the same for the same definition, whatever the key order, name or prose", () => {
    const shuffled = { step_count: 1, steps: [{ expect: { target: 1, op: "gt", value: "x" }, judge: "auto", instruction: "Walk" }], map: "/Game/Maps/Glade", name: "Renamed", description: "Other words", path: "/Game/QA/Door" };
    expect(scriptHashOf(read)).toMatch(/^sha256:[0-9a-f]{64}$/);
    expect(scriptHashOf(shuffled)).toBe(scriptHashOf(read));
    expect(canonical({ b: 1, a: [2, { d: 3, c: 4 }] })).toBe('{"a":[2,{"c":4,"d":3}],"b":1}');
  });

  it("changes with a step or the map, and is unknown without steps", () => {
    expect(scriptHashOf({ ...read, map: "/Game/Maps/Other" })).not.toBe(scriptHashOf(read));
    expect(scriptHashOf({ ...read, steps: [...read.steps, { instruction: "Jump", judge: "manual" }] })).not.toBe(scriptHashOf(read));
    expect(scriptHashOf({ success: false })).toBeNull();
    expect(scriptHashOf(null)).toBeNull();
  });
});

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
