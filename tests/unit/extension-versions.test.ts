/**
 * Extension versions side by side (#1619): which version a project loads, the
 * migration of the single unversioned install, and pruning versions no project
 * records.
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import {
  chooseVersion,
  copyFor,
  installedCopies,
  migrateLegacy,
  pruneVersions,
  recordedVersion,
  recordVersion,
} from "../../src/daemon/extension-versions.js";
import { useExtensionVersion, removeExtension } from "../../src/daemon/extension-install.js";
import { recordedExtensionVersions } from "../../src/config/user-state.js";
import { restoreEnv } from "../helpers/env.js";

describe("chooseVersion", () => {
  const loadable = ["0.1.23", "0.1.24-beta.3", "0.1.24-beta.19"];

  it("loads the recorded version", () => {
    expect(chooseVersion(loadable, "0.1.23", null)).toEqual({ version: "0.1.23", record: false });
  });

  it("falls back to the newest older version when the recorded one does not load, and keeps the record", () => {
    expect(chooseVersion(loadable, "0.1.24-beta.10", null)).toEqual({ version: "0.1.24-beta.3", record: false });
    expect(chooseVersion(loadable, "0.1.0", null)).toEqual({ version: "0.1.24-beta.19", record: false });
  });

  it("records the unversioned copy's version for a project that ran it before", () => {
    expect(chooseVersion(loadable, null, { version: "0.1.23", usedBefore: true })).toEqual({ version: "0.1.23", record: true });
  });

  it("records the newest for a project that never ran it", () => {
    expect(chooseVersion(loadable, null, { version: "0.1.23", usedBefore: false })).toEqual({ version: "0.1.24-beta.19", record: true });
    expect(chooseVersion(loadable, null, null)).toEqual({ version: "0.1.24-beta.19", record: true });
  });

  it("records the newest when the unversioned copy's version does not load here", () => {
    expect(chooseVersion(["0.2.0"], null, { version: "0.1.0", usedBefore: true })).toEqual({ version: "0.2.0", record: true });
  });

  it("loads nothing when nothing loads", () => {
    expect(chooseVersion([], "1.0.0", null)).toBeNull();
  });
});

describe("the versions on disk", () => {
  let root: string;
  let home: string;
  const saved = { ...process.env };

  const write = (folder: string, name: string, version: string) => {
    const dir = path.join(root, folder);
    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(path.join(dir, "package.json"), JSON.stringify({ version, main: "index.js", ueMcpExtension: { name } }));
    fs.writeFileSync(path.join(dir, "index.js"), "export function activate() {}");
    return dir;
  };

  beforeAll(() => {
    home = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-extver-"));
    process.env.UE_MCP_USER_STATE = path.join(home, "state.json");
    root = path.join(home, "extensions");
  });
  afterAll(() => {
    restoreEnv(saved);
    fs.rmSync(home, { recursive: true, force: true });
  });

  it("copies the unversioned install to its version's folder and leaves it for older daemons", () => {
    write("studio", "studio", "0.1.23");
    write("studio.staging-123", "studio", "0.1.24");
    write("misnamed", "studio", "0.0.1");
    migrateLegacy(root);
    expect(fs.existsSync(path.join(root, "studio", "package.json"))).toBe(true);
    expect(fs.existsSync(path.join(root, "studio@0.1.23", "package.json"))).toBe(true);
    const copies = installedCopies(root).get("studio")!;
    expect(copies.map((c) => [c.version, c.legacy])).toEqual([["0.1.23", true], ["0.1.23", false]]);
    expect(copyFor(copies, "0.1.23")!.dir).toBe(path.join(root, "studio@0.1.23"));
    // Run again, nothing changes.
    migrateLegacy(root);
    expect(fs.readdirSync(root).sort()).toEqual(["misnamed", "studio", "studio.staging-123", "studio@0.1.23"]);
  });

  it("records a version per project and moves one project without touching another", () => {
    write("studio@0.1.24", "studio", "0.1.24");
    recordVersion(path.join(home, "A"), "studio", "0.1.23");
    recordVersion(path.join(home, "B"), "studio", "0.1.23");
    useExtensionVersion("studio", "0.1.24", path.join(home, "A"), root);
    expect(recordedVersion(path.join(home, "A"), "studio")).toBe("0.1.24");
    expect(recordedVersion(path.join(home, "B"), "studio")).toBe("0.1.23");
    // The record is keyed the way every project setting is: case and slashes fold.
    expect(recordedVersion(path.join(home, "a").toUpperCase(), "studio")).toBe("0.1.24");
    expect(() => useExtensionVersion("studio", "9.9.9", path.join(home, "A"), root)).toThrow(/studio 9\.9\.9 is not installed; installed: 0\.1\.23, 0\.1\.24/);
  });

  it("prunes only versions no project records, keeping the newest", () => {
    write("studio@0.1.20", "studio", "0.1.20");
    write("studio@0.1.25-beta.1", "studio", "0.1.25-beta.1");
    expect(recordedExtensionVersions("studio")).toEqual(new Set(["0.1.23", "0.1.24"]));
    expect(pruneVersions(root, "studio")).toEqual(["0.1.20"]);
    expect(fs.readdirSync(root).filter((f) => f.startsWith("studio@")).sort()).toEqual(["studio@0.1.23", "studio@0.1.24", "studio@0.1.25-beta.1"]);
    // The unversioned folder is never pruned.
    expect(fs.existsSync(path.join(root, "studio"))).toBe(true);
  });

  it("removes every version and every project's record", () => {
    expect(removeExtension("studio", root)).toBe(true);
    expect(fs.readdirSync(root).filter((f) => f === "studio" || f.startsWith("studio@"))).toEqual([]);
    expect(recordedVersion(path.join(home, "B"), "studio")).toBeNull();
    expect(() => removeExtension("studio@0.1.23", root)).toThrow(/not an extension name/);
  });
});
