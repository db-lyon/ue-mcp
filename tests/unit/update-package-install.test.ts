import * as path from "node:path";
import { describe, expect, it } from "vitest";

import { resolvePackageInstall } from "../../src/cli/update.js";

const root = (...parts: string[]) => path.resolve(path.sep, ...parts);
const project = root("Games", "MyGame");

describe("resolvePackageInstall (#1630)", () => {
  it("updates a global install globally", () => {
    const plan = resolvePackageInstall({
      packageRoot: root("npm-global", "node_modules", "ue-mcp"),
      globalRoot: root("npm-global", "node_modules"),
      version: "2.0.0",
      protectedDirs: [project],
    });
    expect(plan).toEqual({ kind: "global", command: "npm install -g ue-mcp@2.0.0" });
  });

  it("updates an npx cache in its own prefix, never the working directory", () => {
    const cache = root("Users", "me", "npm-cache", "_npx", "abc123");
    const plan = resolvePackageInstall({
      packageRoot: path.join(cache, "node_modules", "ue-mcp"),
      globalRoot: root("npm-global", "node_modules"),
      version: "2.0.0",
      protectedDirs: [project, path.join(project, "Source")],
    });
    expect(plan).toEqual({ kind: "prefix", command: "npm install ue-mcp@2.0.0", cwd: cache });
  });

  it("refuses to install into the project a local copy lives in", () => {
    for (const dir of [project, path.join(project, "Content")]) {
      const plan = resolvePackageInstall({
        packageRoot: path.join(project, "node_modules", "ue-mcp"),
        globalRoot: root("npm-global", "node_modules"),
        version: "2.0.0",
        protectedDirs: [dir],
      });
      expect(plan.kind).toBe("refuse");
      if (plan.kind === "refuse") expect(plan.reason).toContain("npm uninstall ue-mcp");
    }
  });

  it("refuses a copy in a folder above the project", () => {
    const plan = resolvePackageInstall({
      packageRoot: root("Games", "node_modules", "ue-mcp"),
      globalRoot: null,
      version: "2.0.0",
      protectedDirs: [project],
    });
    expect(plan.kind).toBe("refuse");
  });

  it("refuses a checkout npm did not install", () => {
    const plan = resolvePackageInstall({
      packageRoot: root("src", "ue-mcp"),
      globalRoot: root("npm-global", "node_modules"),
      version: "2.0.0",
      protectedDirs: [project],
    });
    expect(plan.kind).toBe("refuse");
  });
});
