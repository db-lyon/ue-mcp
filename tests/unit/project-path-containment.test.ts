/**
 * Project file actions stay inside the folders they are documented to use.
 *
 * Each case drives the real handler against a throwaway project in the temp
 * folder, with a file beside the project that no action may read or write.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { ProjectContext } from "../../src/config/project.js";
import { sourceActions } from "../../src/tools/project/source.js";
import { configActions } from "../../src/tools/project/config.js";
import { engineActions } from "../../src/tools/project/engine.js";
import { resolveInside, requireModuleName, PathOutsideRootsError } from "../../src/core/path-guard.js";
import type { ActionSpec } from "../../src/core/types.js";

let sandbox: string;
let projectDir: string;
let engineRoot: string;
let outsideDir: string;
let ctx: { project: ProjectContext; session: undefined };

function handler(spec: ActionSpec | undefined): (c: unknown, p: Record<string, unknown>) => Promise<unknown> {
  if (!spec || spec.kind !== "handler") throw new Error("not a handler action");
  return spec.handler as (c: unknown, p: Record<string, unknown>) => Promise<unknown>;
}

const call = (spec: ActionSpec | undefined, params: Record<string, unknown>) => handler(spec)(ctx, params);

beforeAll(() => {
  sandbox = fs.realpathSync.native(fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-contain-")));
  projectDir = path.join(sandbox, "Probe");
  engineRoot = path.join(sandbox, "Engine58");
  outsideDir = path.join(sandbox, "outside");

  fs.mkdirSync(path.join(projectDir, "Source", "Probe", "Public"), { recursive: true });
  fs.mkdirSync(path.join(projectDir, "Source", "Probe", "Private"), { recursive: true });
  fs.mkdirSync(path.join(projectDir, "Config"), { recursive: true });
  fs.mkdirSync(path.join(projectDir, "Content"), { recursive: true });
  fs.mkdirSync(path.join(engineRoot, "Engine", "Source", "Runtime", "Core", "Public"), { recursive: true });
  fs.mkdirSync(outsideDir, { recursive: true });

  fs.writeFileSync(path.join(projectDir, "Source", "Probe", "Probe.Build.cs"),
    "public class Probe : ModuleRules\n{\n\tpublic Probe(ReadOnlyTargetRules Target) : base(Target)\n\t{\n\t}\n}\n");
  fs.writeFileSync(path.join(projectDir, "Source", "Probe", "Public", "Inside.h"), "#pragma once\nclass FInside {};\n");
  fs.writeFileSync(path.join(projectDir, "Config", "DefaultGame.ini"), "[Section]\nKey=Inside\n");
  fs.writeFileSync(path.join(projectDir, "secret.txt"), "project root, outside the module");
  fs.writeFileSync(path.join(engineRoot, "Engine", "Source", "Runtime", "Core", "Public", "CoreTypes.h"), "#pragma once\n");
  fs.writeFileSync(path.join(sandbox, "secret.txt"), "outside the project");
  fs.writeFileSync(path.join(sandbox, "x.ini"), "[Outside]\nKey=Leaked\n");
  fs.writeFileSync(path.join(outsideDir, "Outside.h"), "#pragma once\nclass FOutside {};\n");
  fs.writeFileSync(path.join(outsideDir, "Outside.cpp"), "// outside\n");

  // A link inside the module that points out of the project. A junction needs no privilege on Windows.
  fs.symlinkSync(outsideDir, path.join(projectDir, "Source", "Probe", "Public", "link"), process.platform === "win32" ? "junction" : "dir");

  const uproject = path.join(projectDir, "Probe.uproject");
  fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, EngineAssociation: engineRoot, Modules: [{ Name: "Probe" }] }));
  const project = new ProjectContext();
  project.setProject(uproject);
  // Only the engine this test made, never one from the developer's environment.
  const lookup = project.engineLookup.bind(project);
  project.engineLookup = () => ({ ...lookup(), env: {} });
  ctx = { project, session: undefined };
});

afterAll(() => {
  fs.rmSync(sandbox, { recursive: true, force: true });
});

const outsideFiles = (): string[] => fs.readdirSync(sandbox).sort();

describe("write_source_file", () => {
  it("refuses a fileName that climbs out of the module", async () => {
    const before = outsideFiles();
    await expect(call(sourceActions.write_source_file, { module: "Probe", fileName: "../../../outside.h", content: "x" }))
      .rejects.toThrow(/outside the folders/);
    expect(outsideFiles()).toEqual(before);
  });

  it("refuses an absolute fileName outside the project", async () => {
    const target = path.join(outsideDir, "Absolute.h");
    await expect(call(sourceActions.write_source_file, { module: "Probe", fileName: target, content: "x" }))
      .rejects.toThrow(/outside the folders/);
    expect(fs.existsSync(target)).toBe(false);
  });

  it("refuses to write through a link that leaves the project", async () => {
    await expect(call(sourceActions.write_source_file, { module: "Probe", visibility: "Public", fileName: "link/Through.h", content: "x" }))
      .rejects.toThrow(/outside the folders/);
    expect(fs.existsSync(path.join(outsideDir, "Through.h"))).toBe(false);
  });

  it("refuses a module name that is a path", async () => {
    await expect(call(sourceActions.write_source_file, { module: "../..", fileName: "A.h", content: "x" }))
      .rejects.toThrow(/module name/);
  });

  it("still writes inside the module, subfolders included", async () => {
    const result = await call(sourceActions.write_source_file, { module: "Probe", visibility: "Public", fileName: "Sub/New.h", content: "#pragma once\n" }) as { path: string };
    expect(result.path).toBe(path.join(projectDir, "Source", "Probe", "Public", "Sub", "New.h"));
    expect(fs.readFileSync(result.path, "utf-8")).toBe("#pragma once\n");
  });
});

describe("read_source_file", () => {
  it("refuses '../../secret.txt', which leaves the module", async () => {
    await expect(call(sourceActions.read_source_file, { module: "Probe", fileName: "../../secret.txt" }))
      .rejects.toThrow(/outside the folders/);
  });

  it("refuses a path out of the project", async () => {
    await expect(call(sourceActions.read_source_file, { module: "Probe", visibility: "Public", fileName: "../../../../secret.txt" }))
      .rejects.toThrow(/outside the folders/);
  });

  it("refuses to read through a link that leaves the project", async () => {
    await expect(call(sourceActions.read_source_file, { module: "Probe", visibility: "Public", fileName: "link/Outside.h" }))
      .rejects.toThrow(/outside the folders/);
  });

  it("still reads a file in the module", async () => {
    const result = await call(sourceActions.read_source_file, { module: "Probe", fileName: "Inside.h" }) as { content: string };
    expect(result.content).toContain("FInside");
  });
});

describe("read_cpp_source and read_cpp_header", () => {
  it("refuse an absolute path outside the project", async () => {
    await expect(call(sourceActions.read_cpp_source, { sourcePath: path.join(outsideDir, "Outside.cpp") }))
      .rejects.toThrow(/outside the folders/);
    await expect(call(sourceActions.read_cpp_header, { headerPath: path.join(outsideDir, "Outside.h") }))
      .rejects.toThrow(/outside the folders/);
  });

  it("refuse a relative path that climbs out", async () => {
    await expect(call(sourceActions.read_cpp_header, { headerPath: "../../outside/Outside.h" }))
      .rejects.toThrow(/outside the folders/);
  });

  it("still read a header in the project", async () => {
    const parsed = await call(sourceActions.read_cpp_header, { headerPath: "Probe/Public/Inside.h" }) as { classes?: unknown[] };
    expect(parsed).toBeTruthy();
  });
});

describe("read_config", () => {
  it("refuses a configName that climbs out of Config", async () => {
    await expect(call(configActions.read_config, { configName: "../../x" })).rejects.toThrow(/outside the folders/);
  });

  it("refuses an absolute configName outside Config", async () => {
    await expect(call(configActions.read_config, { configName: path.join(sandbox, "x.ini") })).rejects.toThrow(/outside the folders/);
  });

  it("still reads Game as DefaultGame.ini", async () => {
    const result = await call(configActions.read_config, { configName: "Game" }) as { sections: Record<string, Record<string, string>> };
    expect(result.sections.Section.Key).toBe("Inside");
  });
});

describe("module names", () => {
  it("add_module_dependency refuses a moduleName with '..' and a dependency that is not an identifier", async () => {
    await expect(call(sourceActions.add_module_dependency, { moduleName: "../Probe", dependency: "UMG" })).rejects.toThrow(/module name/);
    await expect(call(sourceActions.add_module_dependency, { moduleName: "Probe", dependency: "UMG\" });//" })).rejects.toThrow(/module name/);
  });

  it("read_module refuses a moduleName with '..'", async () => {
    await expect(call(sourceActions.read_module, { moduleName: "../Probe" })).rejects.toThrow(/module name/);
  });
});

describe("write_cpp_file and add_cpp_member", () => {
  it("refuse a path out of Source", async () => {
    await expect(call(sourceActions.write_cpp_file, { path: "../../outside/W.h", content: "x" })).rejects.toThrow(/outside the folders/);
    await expect(call(sourceActions.add_cpp_member, { headerPath: path.join(outsideDir, "Outside.h"), declaration: "int32 X;", memberName: "X" }))
      .rejects.toThrow(/outside the folders/);
  });

  it("refuse a link out of the project", async () => {
    await expect(call(sourceActions.write_cpp_file, { path: "Probe/Public/link/W.h", content: "x" })).rejects.toThrow(/outside the folders/);
  });

  it.runIf(process.platform === "win32")("accept a case variant of the project path on Windows", async () => {
    const upper = path.join(projectDir.toUpperCase(), "Source", "Probe", "Public", "Case.h");
    const result = await call(sourceActions.write_cpp_file, { path: upper, content: "#pragma once\n" }) as { path: string };
    expect(fs.existsSync(path.join(projectDir, "Source", "Probe", "Public", "Case.h"))).toBe(true);
    expect(result.path.toLowerCase()).toBe(upper.toLowerCase());
  });

  it.runIf(process.platform === "win32")("refuse a case variant that climbs out on Windows", async () => {
    const upper = path.join(projectDir.toUpperCase(), "Source", "..", "..", "outside", "W.h");
    await expect(call(sourceActions.write_cpp_file, { path: upper, content: "x" })).rejects.toThrow(/outside the folders/);
  });
});

describe("directory parameters", () => {
  it("list_files refuses a directory outside the project", async () => {
    await expect(call(sourceActions.list_files, { directory: ".." })).rejects.toThrow(/outside the folders/);
    await expect(call(sourceActions.list_files, { directory: outsideDir })).rejects.toThrow(/outside the folders/);
  });

  it("search_cpp refuses an absolute directory outside the project", async () => {
    await expect(call(sourceActions.search_cpp, { query: "class", directory: outsideDir })).rejects.toThrow(/outside the folders/);
  });

  it("list_content_assets refuses a mount path that climbs out of Content", async () => {
    await expect(call(sourceActions.list_content_assets, { contentPath: "/Game/../.." })).rejects.toThrow(/'\.\.'/);
  });
});

describe("read_engine_header", () => {
  it("refuses an absolute path outside the engine and the project", async () => {
    await expect(call(engineActions.read_engine_header, { headerPath: path.join(outsideDir, "Outside.h") }))
      .rejects.toThrow(/outside the folders/);
  });

  it("refuses a relative path that climbs out of the engine", async () => {
    await expect(call(engineActions.read_engine_header, { headerPath: "../../../outside/Outside.h" }))
      .rejects.toThrow(/outside the folders/);
  });

  it("still reads an engine header", async () => {
    const result = await call(engineActions.read_engine_header, { headerPath: "Runtime/Core/Public/CoreTypes.h" }) as { engineRoot: string };
    expect(path.resolve(result.engineRoot)).toBe(path.resolve(engineRoot));
  });
});

describe("resolveInside", () => {
  it("accepts the root itself and paths below it", () => {
    expect(resolveInside([projectDir], ".")).toBe(projectDir);
    expect(resolveInside([projectDir], "Source/x.h")).toBe(path.join(projectDir, "Source", "x.h"));
  });

  it("refuses a sibling that shares the root's prefix", () => {
    expect(() => resolveInside([projectDir], `${projectDir}Sibling/x.h`)).toThrow(PathOutsideRootsError);
  });

  it("names every allowed root in the error", () => {
    expect(() => resolveInside([projectDir, engineRoot], outsideDir)).toThrow(new RegExp(engineRoot.replace(/[\\^$.*+?()[\]{}|]/g, "\\$&")));
  });

  it("requireModuleName accepts identifiers only", () => {
    expect(requireModuleName("Probe_2")).toBe("Probe_2");
    for (const bad of ["", "..", "a/b", "a\\b", "1abc", "a.b"]) expect(() => requireModuleName(bad)).toThrow();
  });
});
