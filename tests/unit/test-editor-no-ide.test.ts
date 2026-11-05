/**
 * Every editor the test tooling launches carries the argument that keeps it
 * from opening an IDE. create_cpp_class adds its files to the solution through
 * the editor's source code accessor, which starts Visual Studio when none is
 * running; a test editor must never do that on a developer's desktop.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { describe, expect, it } from "vitest";
import { NO_BACKGROUND_THROTTLE_ARG, NO_IDE_ARG, TEST_EDITOR_ARGS } from "../../scripts/test-editor-args.mjs";
import { readUeMcpConfig } from "../../src/config/project.js";

const ROOT = path.resolve(__dirname, "..", "..");
const read = (rel: string) => fs.readFileSync(path.join(ROOT, rel), "utf8");

describe("test editors open no IDE", () => {
  it("the argument points the source code accessor at the null one", () => {
    expect(NO_IDE_ARG).toBe(
      "-ini:EditorSettings:[/Script/SourceCodeAccess.SourceCodeAccessSettings]:PreferredAccessor=NullSourceCodeAccessor",
    );
    expect(TEST_EDITOR_ARGS).toContain(NO_IDE_ARG);
  });

  it("test editors are not throttled in the background, where they always are", () => {
    expect(TEST_EDITOR_ARGS).toContain(NO_BACKGROUND_THROTTLE_ARG);
    expect(readUeMcpConfig(path.join(ROOT, "tests", "ue_mcp")).editor?.launchArgs).toContain(NO_BACKGROUND_THROTTLE_ARG);
  });

  it("every script that spawns an editor passes it", () => {
    const scripts = fs.readdirSync(path.join(ROOT, "scripts")).filter((n) => /\.(m?js)$/.test(n));
    const spawning = scripts.filter((n) => /spawn\(\s*editorExe\b/.test(read(`scripts/${n}`)));
    expect(spawning).toContain("run.js");
    for (const name of spawning) {
      const calls = read(`scripts/${name}`).match(/spawn\(\s*editorExe\b[^)]*\)/g) ?? [];
      expect(calls.length, name).toBeGreaterThan(0);
      for (const call of calls) expect(call, `${name}: ${call}`).toContain("TEST_EDITOR_ARGS");
    }
  });

  it("start_editor and restart_editor pass the project's launchArgs, and tests/ue_mcp sets it", () => {
    expect(read("src/editor/editor-control.ts")).toMatch(
      /spawnOutsideTree\(editorExe, \[project\.projectPath, \.\.\.\(project\.config\.editor\?\.launchArgs \?\? \[\]\)\]/,
    );
    expect(readUeMcpConfig(path.join(ROOT, "tests", "ue_mcp")).editor?.launchArgs).toContain(NO_IDE_ARG);
  });
});
