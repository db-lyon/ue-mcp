import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { findUProject, projectDirOf } from "../../src/config/uproject-path.js";

let root: string;

beforeEach(() => {
  root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-uproject-path-"));
});

afterEach(() => {
  fs.rmSync(root, { recursive: true, force: true });
});

describe("findUProject", () => {
  it("finds a .uproject in the directory whatever the extension's case", () => {
    fs.writeFileSync(path.join(root, "Game.UPROJECT"), "{}");
    expect(findUProject(root)).toBe(path.join(root, "Game.UPROJECT"));
  });

  it("returns a named .uproject only when it exists", () => {
    const file = path.join(root, "Game.uproject");
    expect(findUProject(file)).toBeNull();
    fs.writeFileSync(file, "{}");
    expect(findUProject(file)).toBe(file);
  });

  it("searches parents only as far as walkUp allows", () => {
    fs.writeFileSync(path.join(root, "Game.uproject"), "{}");
    const deep = path.join(root, "Source", "Game");
    fs.mkdirSync(deep, { recursive: true });
    expect(findUProject(deep)).toBeNull();
    expect(findUProject(deep, { walkUp: 1 })).toBeNull();
    expect(findUProject(deep, { walkUp: 2 })).toBe(path.join(root, "Game.uproject"));
  });
});

describe("projectDirOf", () => {
  it("is the parent of a .uproject and the path itself otherwise", () => {
    expect(projectDirOf(path.join(root, "Game.UProject"))).toBe(root);
    expect(projectDirOf(root)).toBe(root);
  });
});
