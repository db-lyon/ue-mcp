/**
 * The daemon runs the version the project's bridge came from. A client entry
 * launching `ue-mcp@latest` reaches the stable line, and a shim of that version
 * starting its own daemon put a beta tester's project on the stable server.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as path from "node:path";
import { daemonCommand, daemonVersionFor, npxCliPath } from "../../src/daemon/shim.js";
import { writeInstallMarker } from "../../src/editor/install-marker.js";
import { packageVersion } from "../../src/core/package-root.js";
import { NPX_AGE_GATE_OVERRIDE } from "../../src/integrations/claude-code/mcp-client-config.js";
import { ProjectFixture } from "../helpers/project-fixture.js";

let fixture: ProjectFixture;

beforeEach(() => {
  fixture = new ProjectFixture("ue-mcp-daemon-version-");
});
afterEach(() => {
  fixture.cleanup();
});

function project(kind: "binary" | "source" | null, version: string): string {
  const uproject = fixture.makeProject(`P${Math.random().toString(36).slice(2, 8)}`, { engine: "5.8" });
  const dir = path.dirname(uproject);
  if (kind) {
    const pluginDir = path.join(dir, "Plugins", "UE_MCP_Bridge");
    fs.mkdirSync(path.join(pluginDir, kind === "source" ? "Source" : "Binaries"), { recursive: true });
    fs.writeFileSync(path.join(pluginDir, "UE_MCP_Bridge.uplugin"), JSON.stringify({ VersionName: version }));
    writeInstallMarker(pluginDir, { kind, engine: "5.8", platform: "Win64", version });
  }
  return dir;
}

describe("daemonVersionFor", () => {
  it("follows a binary bridge newer than this package", () => {
    expect(daemonVersionFor(project("binary", "1.4.11-beta.8"), "1.4.10")).toBe("1.4.11-beta.8");
  });

  it("keeps this package for an older or equal bridge", () => {
    expect(daemonVersionFor(project("binary", "1.4.10"), "1.4.11-beta.8")).toBe("1.4.11-beta.8");
    expect(daemonVersionFor(project("binary", "1.4.10"), "1.4.10")).toBe("1.4.10");
  });

  it("keeps this package for a source install or no bridge", () => {
    expect(daemonVersionFor(project("source", "99.0.0"), "1.4.10")).toBe("1.4.10");
    expect(daemonVersionFor(project(null, ""), "1.4.10")).toBe("1.4.10");
  });
});

describe("daemonCommand", () => {
  it("runs this entry for this package's own version", () => {
    const args = daemonCommand("/p/Game.uproject", packageVersion(), null)!;
    expect(args.slice(-3)).toEqual(["daemon", "run", path.resolve("/p/Game.uproject")]);
    expect(args).toContain(process.argv[1]);
  });

  // Never this package's own version, which takes the other branch.
  const other = "99.0.0-other.1";

  it("runs the other version through npx without a shell", () => {
    const args = daemonCommand("/p/Game.uproject", other, "/node/npx-cli.js")!;
    expect(args).toEqual([
      "/node/npx-cli.js", "-y", NPX_AGE_GATE_OVERRIDE, `ue-mcp@${other}`, "daemon", "run", path.resolve("/p/Game.uproject"),
    ]);
  });

  it("gives up on another version when npx is not there", () => {
    expect(daemonCommand("/p/Game.uproject", other, null)).toBeNull();
  });
});

describe("npxCliPath", () => {
  it("finds npm's npx entry where each platform's node install puts it", () => {
    const win = path.join(fixture.root, "nodewin");
    fs.mkdirSync(path.join(win, "node_modules", "npm", "bin"), { recursive: true });
    fs.writeFileSync(path.join(win, "node_modules", "npm", "bin", "npx-cli.js"), "");
    expect(npxCliPath(path.join(win, "node.exe"), "win32")).toBe(path.join(win, "node_modules", "npm", "bin", "npx-cli.js"));

    const posix = path.join(fixture.root, "nodeposix");
    fs.mkdirSync(path.join(posix, "bin"), { recursive: true });
    fs.mkdirSync(path.join(posix, "lib", "node_modules", "npm", "bin"), { recursive: true });
    fs.writeFileSync(path.join(posix, "lib", "node_modules", "npm", "bin", "npx-cli.js"), "");
    expect(npxCliPath(path.join(posix, "bin", "node"), "linux")).toBe(path.join(posix, "bin", "..", "lib", "node_modules", "npm", "bin", "npx-cli.js"));

    expect(npxCliPath(path.join(fixture.root, "nowhere", "node"), "linux")).toBeNull();
  });
});
