/**
 * A loaded project follows its .uproject's EngineAssociation on disk.
 *
 * The daemon loads a project once and keeps it for hours. A user who moves the
 * project from one engine to another while it runs edits EngineAssociation;
 * the rebuild flow read the file again and compiled against the new engine,
 * and start_editor then launched the old one from the value parsed at load.
 * Both lookups are asserted here against two real engine trees in a temp dir,
 * named by path associations, so the answer depends on nothing installed.
 */
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { ProjectContext } from "../../src/config/project.js";
import { findEditorExecutable } from "../../src/editor/editor-control.js";
import { buildEngineLookup } from "../../src/editor/editor-build.js";
import { engineBuildTool, engineEditorBinaries, projectEngineVersion, selectEngine } from "../../src/editor/engine-root.js";

function touch(file: string): void {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, "");
}

/** An engine tree with a build tool and an editor binary, and nothing else. */
function fakeEngine(root: string): void {
  touch(engineBuildTool(root));
  touch(engineEditorBinaries(root)[0]);
}

function writeUProject(uproject: string, association: string): void {
  fs.writeFileSync(uproject, JSON.stringify({ FileVersion: 3, EngineAssociation: association, Plugins: [] }, null, 2));
}

describe("EngineAssociation changed on disk after load", () => {
  let dir: string;
  let uproject: string;
  let engineA: string;
  let engineB: string;

  beforeEach(() => {
    // Nothing configured may outrank the association under test.
    vi.stubEnv("UE_MCP_TEST_ENGINE_ROOT", "");
    vi.stubEnv("UE_BUILD_TOOL_PATH", "");
    vi.stubEnv("UE_EDITOR_PATH", "");
    dir = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-engine-assoc-")));
    vi.stubEnv("UE_MCP_GLOBAL_CONFIG", path.join(dir, "global-config.yml"));
    engineA = path.join(dir, "UE_A");
    engineB = path.join(dir, "UE_B");
    fakeEngine(engineA);
    fakeEngine(engineB);
    uproject = path.join(dir, "Game", "Game.uproject");
    fs.mkdirSync(path.join(dir, "Game", "Content"), { recursive: true });
    writeUProject(uproject, "../UE_A");
  });

  afterEach(() => {
    vi.unstubAllEnvs();
    fs.rmSync(dir, { recursive: true, force: true });
  });

  it("the editor launch and the build both resolve the engine the file names now", () => {
    const project = new ProjectContext();
    project.setProject(uproject);

    expect(project.engineAssociation).toBe("../UE_A");
    expect(findEditorExecutable(project)).toBe(engineEditorBinaries(engineA)[0]);
    expect(selectEngine(buildEngineLookup(uproject), "buildTool").buildTool).toBe(engineBuildTool(engineA));

    // Same length on purpose: the edit that broke was 5.8 to 5.9, which leaves
    // the size alone, and it lands well inside one mtime tick of the load.
    writeUProject(uproject, "../UE_B");

    expect(project.engineAssociation).toBe("../UE_B");
    expect(project.engineLookup().engineAssociation).toBe("../UE_B");
    expect(findEditorExecutable(project)).toBe(engineEditorBinaries(engineB)[0]);
    expect(selectEngine(buildEngineLookup(uproject), "buildTool").buildTool).toBe(engineBuildTool(engineB));
  });

  it("projectEngineVersion reads the version the file names now", () => {
    writeUProject(uproject, "5.8");
    const project = new ProjectContext();
    project.setProject(uproject);
    expect(projectEngineVersion(project)).toBe("5.8");

    writeUProject(uproject, "5.9");
    expect(projectEngineVersion(project)).toBe("5.9");
  });

  it("an aged file whose size and mtime did not move is not read again", () => {
    const project = new ProjectContext();
    project.setProject(uproject);
    // Age the file past the window in which a same-tick edit could hide, then
    // let the next read record it as settled.
    const old = new Date(Date.now() - 60_000);
    fs.utimesSync(uproject, old, old);
    expect(project.engineAssociation).toBe("../UE_A");

    // Rewrite it behind the stat: same size, mtime put back. Only a re-read
    // could see this, so the old answer proves the stat is what is consulted.
    writeUProject(uproject, "../UE_C");
    fs.utimesSync(uproject, old, old);
    expect(project.engineAssociation).toBe("../UE_A");
  });
});
