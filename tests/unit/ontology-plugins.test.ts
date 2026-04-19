import { describe, it, expect, beforeEach, afterEach } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { categoryTool, bp, type ToolDef } from "../../src/types.js";
import {
  OntologyRegistry,
  createHandlerRegistryProjector,
  createPluginProjector,
  parse,
  compose,
  select,
} from "../../src/ontology/index.js";
import { serializeFragment } from "../../src/ontology/index.js";

function writePlugin(dir: string, name: string, descriptor: Record<string, unknown>): void {
  const sub = path.join(dir, name);
  fs.mkdirSync(sub, { recursive: true });
  fs.writeFileSync(path.join(sub, `${name}.uplugin`), JSON.stringify(descriptor), "utf-8");
}

describe("ontology: PluginProjector", () => {
  let tmpDir: string;
  let engineRoot: string;
  let projectDir: string;

  beforeEach(() => {
    tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-plug-"));
    engineRoot = path.join(tmpDir, "UE_5.7");
    projectDir = path.join(tmpDir, "MyProject");
    fs.mkdirSync(path.join(engineRoot, "Engine", "Plugins"), { recursive: true });
    fs.mkdirSync(path.join(projectDir, "Plugins"), { recursive: true });
  });

  afterEach(() => {
    fs.rmSync(tmpDir, { recursive: true, force: true });
  });

  it("discovers engine plugins and project plugins into distinct sources", () => {
    writePlugin(path.join(engineRoot, "Engine", "Plugins"), "GameplayAbilities", {
      FriendlyName: "Gameplay Abilities",
      EnabledByDefault: true,
      VersionName: "1.0",
      Modules: [{ Name: "GameplayAbilities", Type: "Runtime", LoadingPhase: "Default" }],
    });
    writePlugin(path.join(engineRoot, "Engine", "Plugins"), "Niagara", {
      FriendlyName: "Niagara",
      EnabledByDefault: true,
    });
    writePlugin(path.join(projectDir, "Plugins"), "MyCustom", {
      FriendlyName: "My Custom Plugin",
      EnabledByDefault: false,
    });

    const proj = createPluginProjector();
    const frag = proj.project({ engineRoot, projectDir });
    const parsed = parse(serializeFragment(frag), "projected");
    const view = compose([{ priority: 1, fragment: parsed }]);

    const catalog = view.root.children!.Plugins.children!.Catalog;
    expect(catalog.fields!.engineCount).toBe(2);
    expect(catalog.fields!.projectCount).toBe(1);

    expect(Object.keys(catalog.children ?? {}).sort()).toEqual(["GameplayAbilities", "MyCustom", "Niagara"]);
    const gas = catalog.children!.GameplayAbilities;
    expect(gas.fields!.source).toBe("engine");
    expect((gas.fields!.enabled as { marker?: string }).marker).toBe("enabled");
    expect(gas.children!.Modules.children!.GameplayAbilities.fields!.type).toBe("Runtime");

    const mine = catalog.children!.MyCustom;
    expect(mine.fields!.source).toBe("project");
    expect((mine.fields!.enabled as { marker?: string }).marker).toBe("disabled");
  });

  it("selector can find disabled plugins across engine + project", () => {
    writePlugin(path.join(engineRoot, "Engine", "Plugins"), "A", { EnabledByDefault: true });
    writePlugin(path.join(engineRoot, "Engine", "Plugins"), "B", { EnabledByDefault: false });
    writePlugin(path.join(projectDir, "Plugins"), "C", { EnabledByDefault: false });

    const proj = createPluginProjector();
    const frag = proj.project({ engineRoot, projectDir });
    const view = compose([{ priority: 1, fragment: parse(serializeFragment(frag), "x") }]);

    const disabled = select(view.root, "/UE/Plugins/Catalog/*@enabled=disabled");
    expect(disabled.map((m) => m.path.split("/").pop()).sort()).toEqual(["B", "C"]);
  });
});

describe("ontology registry: checkRequires preflight", () => {
  let tmpDir: string;
  let engineRoot: string;
  let projectDir: string;
  let registry: OntologyRegistry;

  function buildTools(): ToolDef[] {
    return [
      categoryTool(
        "gas",
        "GAS.",
        { create_ability: bp("Create ability", "create_ability") },
        undefined,
        undefined,
        { requires: ["GameplayAbilities"] },
      ),
      categoryTool("editor", "Editor.", {
        get_status: bp("Read", "editor_get_status"),
        execute_python: {
          bridge: "execute_python",
          classification: "destructive",
          approval: "explicit",
          risk: "catastrophic",
          requires: ["PythonScriptPlugin"],
        },
      }),
    ];
  }

  beforeEach(() => {
    tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-pre-"));
    engineRoot = path.join(tmpDir, "UE_5.7");
    projectDir = path.join(tmpDir, "MyProject");
    fs.mkdirSync(path.join(engineRoot, "Engine", "Plugins"), { recursive: true });
    fs.mkdirSync(path.join(projectDir, "Plugins"), { recursive: true });

    const projectedDir = path.join(tmpDir, "projected");
    fs.mkdirSync(projectedDir, { recursive: true });
    registry = new OntologyRegistry(
      () => projectedDir,
      () => ({
        kernel: { priority: 0, paths: [] },
        projected: { priority: 1, paths: [projectedDir] },
        repoLocal: { priority: 2, paths: [] },
      }),
    );
    registry.register(createHandlerRegistryProjector(buildTools()), () => undefined);
    registry.register(createPluginProjector(), () => ({ engineRoot, projectDir }));
  });

  afterEach(() => {
    fs.rmSync(tmpDir, { recursive: true, force: true });
  });

  it("returns ok=false with missing list when a required plugin is absent", () => {
    registry.projectAll();
    const r = registry.checkRequires("gas", "create_ability");
    expect(r.ok).toBe(false);
    expect(r.missing).toEqual(["GameplayAbilities"]);
    expect(r.disabled).toEqual([]);
  });

  it("returns ok=false with disabled list when a required plugin is present but disabled", () => {
    writePlugin(path.join(engineRoot, "Engine", "Plugins"), "GameplayAbilities", {
      EnabledByDefault: false,
    });
    registry.projectAll();
    const r = registry.checkRequires("gas", "create_ability");
    expect(r.ok).toBe(false);
    expect(r.missing).toEqual([]);
    expect(r.disabled).toEqual(["GameplayAbilities"]);
  });

  it("returns ok=true when every required plugin is present and enabled", () => {
    writePlugin(path.join(engineRoot, "Engine", "Plugins"), "GameplayAbilities", {
      EnabledByDefault: true,
    });
    registry.projectAll();
    const r = registry.checkRequires("gas", "create_ability");
    expect(r.ok).toBe(true);
    expect(r.declared).toEqual(["GameplayAbilities"]);
    expect(r.missing).toEqual([]);
    expect(r.disabled).toEqual([]);
  });

  it("returns ok=true for actions with no declared requires", () => {
    registry.projectAll();
    const r = registry.checkRequires("editor", "get_status");
    expect(r.ok).toBe(true);
    expect(r.declared).toEqual([]);
  });

  it("returns ok=true (benign) for unknown tool+action pairs so new actions don't block", () => {
    registry.projectAll();
    const r = registry.checkRequires("unknown", "nothing");
    expect(r.ok).toBe(true);
  });
});
