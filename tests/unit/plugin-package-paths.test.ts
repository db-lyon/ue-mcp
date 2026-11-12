/**
 * A plugin's manifest names paths inside its own package and a folder under
 * the project's Plugins/. None of them may reach outside, by `..`, an
 * absolute path or a link.
 */
import { afterEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { deployNativeModule, pruneStaleNativeFiles } from "../../src/extensions/native-deploy.js";
import { PluginManifestSchema } from "../../src/extensions/manifest.js";
import { loadPlugins } from "../../src/extensions/loader.js";
import { pathInPackage } from "../../src/extensions/package-paths.js";

const temps: string[] = [];
function tempDir(): string {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-pkgpath-"));
  temps.push(dir);
  return dir;
}
function write(file: string, content: string): void {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, content);
}
afterEach(() => {
  for (const dir of temps.splice(0)) fs.rmSync(dir, { recursive: true, force: true });
});

describe("native module deploy", () => {
  it("refuses a uePluginName that is a path, writing nothing outside Plugins/", () => {
    const root = tempDir();
    const project = path.join(root, "project");
    const pkg = path.join(root, "pkg");
    write(path.join(pkg, "ue/x.txt"), "x");
    fs.mkdirSync(project);
    for (const name of ["../../escaped", "..", "a/b", "C:/abs", "1Plug", ""]) {
      expect(() => deployNativeModule(pkg, "ue", name, project)).toThrow(/not an Unreal plugin name/);
    }
    expect(fs.existsSync(path.join(root, "escaped"))).toBe(false);
    expect(fs.existsSync(path.join(project, "Plugins"))).toBe(false);
  });

  it("refuses a source outside the package", () => {
    const root = tempDir();
    const pkg = path.join(root, "pkg");
    write(path.join(root, "secret/key.txt"), "s");
    fs.mkdirSync(pkg);
    const project = tempDir();
    expect(() => deployNativeModule(pkg, "../secret", "Plug", project)).toThrow(/outside the plugin package/);
    expect(() => deployNativeModule(pkg, path.join(root, "secret"), "Plug", project)).toThrow(/outside the plugin package/);
    expect(fs.existsSync(path.join(project, "Plugins", "Plug", "key.txt"))).toBe(false);
  });

  it("does not follow a link inside the source", () => {
    const root = tempDir();
    const pkg = path.join(root, "pkg");
    write(path.join(root, "outside/secret.txt"), "s");
    write(path.join(pkg, "ue/ok.txt"), "ok");
    fs.symlinkSync(path.join(root, "outside"), path.join(pkg, "ue", "linked"), "junction");
    const project = tempDir();
    expect(() => deployNativeModule(pkg, "ue", "Plug", project)).toThrow(/links are not copied/);
    expect(fs.existsSync(path.join(project, "Plugins", "Plug", "linked", "secret.txt"))).toBe(false);
  });

  it("refuses a source that is itself a link out of the package", () => {
    const root = tempDir();
    const pkg = path.join(root, "pkg");
    write(path.join(root, "outside/secret.txt"), "s");
    fs.mkdirSync(pkg);
    fs.symlinkSync(path.join(root, "outside"), path.join(pkg, "ue"), "junction");
    expect(() => deployNativeModule(pkg, "ue", "Plug", tempDir())).toThrow(/through a link/);
  });

  it("never deletes a recorded path outside the project", () => {
    const root = tempDir();
    const project = path.join(root, "project");
    fs.mkdirSync(project);
    write(path.join(root, "victim.txt"), "keep");
    const removed = pruneStaleNativeFiles(project, ["../victim.txt", path.join(root, "victim.txt")], []);
    expect(removed).toBe(0);
    expect(fs.existsSync(path.join(root, "victim.txt"))).toBe(true);
  });
});

describe("plugin manifest paths", () => {
  it("accepts only an Unreal name as uePluginName", () => {
    const native = (uePluginName: string) => ({ actionPrefix: "kn", nativeModule: { uePluginName, minBridgeApi: 1, source: "ue", handlers: {} } });
    expect(PluginManifestSchema.safeParse(native("Sample_Recorder")).success).toBe(true);
    for (const bad of ["../../x", "a/b", "a\\b", "1x", "a b"]) {
      expect(PluginManifestSchema.safeParse(native(bad)).success).toBe(false);
    }
  });

  it("resolves package paths inside the package only", () => {
    const pkg = tempDir();
    expect(pathInPackage(pkg, "knowledge/pcg.md", "knowledge file")).toBe(path.join(pkg, "knowledge", "pcg.md"));
    for (const bad of ["../x.md", "../../etc/passwd", path.join(os.tmpdir(), "x.md")]) {
      expect(() => pathInPackage(pkg, bad, "knowledge file")).toThrow(/outside the plugin package/);
    }
  });

  it("skips a knowledge file outside the package instead of reading it", async () => {
    const project = tempDir();
    write(path.join(project, "secret.md"), "TOP SECRET");
    const pkg = path.join(project, "node_modules", "kn-plugin");
    write(path.join(pkg, "package.json"), JSON.stringify({ name: "kn-plugin", version: "1.0.0" }));
    write(path.join(pkg, "ok.md"), "fine");
    write(path.join(pkg, "ue-mcp.plugin.yml"), "actionPrefix: kn\nknowledge:\n  pcg: ok.md\n  level: ../../secret.md\n");
    const load = await loadPlugins([], [{ name: "kn-plugin" }], project, "1.2.3");
    expect(load.records[0].status).toBe("active");
    const all = Object.values(load.knowledgeByCategory).flat().join("\n");
    expect(all).toContain("fine");
    expect(all).not.toContain("TOP SECRET");
  });
});
