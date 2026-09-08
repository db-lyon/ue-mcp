/**
 * attach() on server startup, engine-free (#817).
 *
 * attach is the one thing the server does to a project before anybody asks it
 * to do anything, so what it writes matters more than what it reports. A
 * project that does not have the bridge installed has to come back out of it
 * byte for byte: enabling a plugin that is not on disk turns that project's
 * next launch in Unreal into a missing-plugin prompt, on a project the user
 * only pointed at.
 *
 * The live tests asserts the same property against the real test project
 * (tests/live/single-editor.test.ts); this is the half that needs no editor.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { attach } from "../../src/deployer.js";
import { ProjectContext } from "../../src/project.js";

let root: string;

function makeProject(name: string): string {
  const dir = path.join(root, name);
  fs.mkdirSync(dir, { recursive: true });
  const uproject = path.join(dir, `${name}.uproject`);
  fs.writeFileSync(
    uproject,
    JSON.stringify({ FileVersion: 3, EngineAssociation: "5.6" }, null, 2),
    "utf-8",
  );
  return uproject;
}

/** Put a plugin descriptor where attach looks for the installed bridge. */
function installBridge(uproject: string, version: string): void {
  const dir = path.join(path.dirname(uproject), "Plugins", "UE_MCP_Bridge");
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(
    path.join(dir, "UE_MCP_Bridge.uplugin"),
    JSON.stringify({ FileVersion: 3, VersionName: version, FriendlyName: "UE MCP Bridge" }, null, 2),
    "utf-8",
  );
}

function contextFor(uproject: string): ProjectContext {
  const context = new ProjectContext();
  context.setProject(uproject);
  return context;
}

beforeEach(() => {
  root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-attach-"));
});

afterEach(() => {
  fs.rmSync(root, { recursive: true, force: true });
});

describe("attach", () => {
  it("writes nothing into a project that has no bridge installed", () => {
    const uproject = makeProject("NoBridge");
    const before = fs.readFileSync(uproject, "utf-8");

    const result = attach(contextFor(uproject));

    expect(result.cppPluginPresent).toBe(false);
    expect(result.cppPluginEnabled).toBe(false);
    expect(result.pythonPluginEnabled).toBe(false);
    expect(fs.readFileSync(uproject, "utf-8")).toBe(before);
    expect(fs.readFileSync(uproject, "utf-8")).not.toContain("UE_MCP_Bridge");
    // Still reports what it found, which is what the startup warning reads.
    expect(result.packagedVersion).toBeTruthy();
    expect(result.installedVersion).toBeNull();
  });

  it("enables the plugins once the bridge is really there", () => {
    const uproject = makeProject("WithBridge");
    installBridge(uproject, "9.9.9");

    const result = attach(contextFor(uproject));

    expect(result.cppPluginPresent).toBe(true);
    expect(result.cppPluginEnabled).toBe(true);
    expect(result.pythonPluginEnabled).toBe(true);
    const written = JSON.parse(fs.readFileSync(uproject, "utf-8")) as {
      Plugins: Array<{ Name: string; Enabled: boolean }>;
    };
    expect(written.Plugins.map((p) => p.Name)).toContain("UE_MCP_Bridge");
    expect(written.Plugins.map((p) => p.Name)).toContain("PythonScriptPlugin");
    expect(result.installedVersion).toBe("9.9.9");
    expect(result.versionMatch).toBe(false);
  });

  it("leaves an already-configured project byte-identical", () => {
    const uproject = makeProject("Configured");
    installBridge(uproject, "9.9.9");
    attach(contextFor(uproject));
    const before = fs.readFileSync(uproject, "utf-8");

    const again = attach(contextFor(uproject));

    expect(again.cppPluginEnabled).toBe(false);
    expect(again.pythonPluginEnabled).toBe(false);
    expect(fs.readFileSync(uproject, "utf-8")).toBe(before);
  });
});
