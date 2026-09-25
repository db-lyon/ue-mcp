import * as path from "node:path";
import { describe, expect, it } from "vitest";
import {
  editorOwnsProject,
  extractProjectPath,
  selectEditorsForProject,
  type EditorProcess,
} from "../../src/engine-observer.js";

const GAME_A = path.resolve(path.join("C:", "work", "GameA", "GameA.uproject"));
const GAME_B = path.resolve(path.join("C:", "work", "GameB", "GameB.uproject"));

function proc(overrides: Partial<EditorProcess> & { pid: number }): EditorProcess {
  return {
    commandLine: "",
    projectPath: null,
    headless: false,
    responding: true,
    windowTitle: null,
    ...overrides,
  };
}

describe("extractProjectPath", () => {
  const EXE = "C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor.exe";

  it("takes the .uproject argument, not the whole unquoted command line", () => {
    // #967/#970/#965: the old lazy regex started at the EXECUTABLE's drive
    // letter and ran to the end, because `[^"]` matches the space between the
    // two arguments. Every process reported a "project path" that was the exe
    // and the project glued together, so the editor holding a project open
    // never matched that project. stop_editor then refused to stop a healthy
    // editor while printing that concatenation back as its evidence.
    const parsed = extractProjectPath(`${EXE} ${GAME_A}`);
    expect(parsed).toBe(GAME_A);
    expect(parsed).not.toContain("UnrealEditor.exe");
  });

  it("matches the project it was launched with", () => {
    const cmd = `${EXE} ${GAME_A}`;
    expect(editorOwnsProject(proc({ pid: 1, projectPath: extractProjectPath(cmd) }), GAME_A)).toBe(true);
  });

  it("reads a quoted executable and a quoted project", () => {
    expect(extractProjectPath(`"${EXE}" "${GAME_A}"`)).toBe(GAME_A);
  });

  it("reads an unquoted project path containing spaces", () => {
    const spaced = path.resolve(path.join("C:", "My Projects", "Game A", "Game A.uproject"));
    expect(extractProjectPath(`${EXE} ${spaced}`)).toBe(spaced);
  });

  it("reads the -Project= form a commandlet is launched with", () => {
    const cmd = `C:\\UE\\UnrealEditor-Cmd.exe -Project="${GAME_A}" -run=WorldPartitionBuilderCommandlet`;
    expect(extractProjectPath(cmd)).toBe(GAME_A);
    const unquoted = `C:\\UE\\UnrealEditor-Cmd.exe -Project=${GAME_A} -run=X`;
    expect(extractProjectPath(unquoted)).toBe(GAME_A);
  });

  it("reads a POSIX command line", () => {
    const posix = "/opt/UE/Engine/Binaries/Linux/UnrealEditor /home/dev/Demo/Demo.uproject -log";
    expect(extractProjectPath(posix)).toBe(path.resolve("/home/dev/Demo/Demo.uproject"));
  });

  it("reports nothing when there is no .uproject on the line", () => {
    expect(extractProjectPath(EXE)).toBeNull();
    expect(extractProjectPath("")).toBeNull();
  });

  it("never returns a candidate that still contains the executable", () => {
    // The relative-argument case, where there is no drive root to walk back to.
    expect(extractProjectPath(`${EXE} Demo.uproject`)).toBe(path.resolve("Demo.uproject"));
  });
});

describe("editorOwnsProject", () => {
  it("matches the editor holding this .uproject open", () => {
    expect(editorOwnsProject(proc({ pid: 1, projectPath: GAME_A }), GAME_A)).toBe(true);
  });

  it("rejects an editor holding a different project open", () => {
    expect(editorOwnsProject(proc({ pid: 1, projectPath: GAME_B }), GAME_A)).toBe(false);
  });

  it("ignores separator and case spelling of the same file", () => {
    const spelled = GAME_A.replace(/\\/g, "/").toUpperCase();
    expect(editorOwnsProject(proc({ pid: 1, projectPath: spelled }), GAME_A)).toBe(true);
  });

  it("never matches a process whose command line could not be read", () => {
    // "Might be ours" is the wrong answer for anything that can stop an editor.
    expect(editorOwnsProject(proc({ pid: 1, projectPath: null }), GAME_A)).toBe(false);
  });

  it("does not match a project whose path is a prefix of another", () => {
    const nested = path.resolve(path.join("C:", "work", "GameA2", "GameA2.uproject"));
    expect(editorOwnsProject(proc({ pid: 1, projectPath: nested }), GAME_A)).toBe(false);
  });
});

describe("selectEditorsForProject", () => {
  it("keeps only the editors for the requested project", () => {
    const selected = selectEditorsForProject(
      [
        proc({ pid: 1, projectPath: GAME_A }),
        proc({ pid: 2, projectPath: GAME_B }),
        proc({ pid: 3, projectPath: GAME_A }),
      ],
      GAME_A,
    );
    expect(selected.map((p) => p.pid)).toEqual([1, 3]);
  });

  it("excludes headless shards of the same project", () => {
    const selected = selectEditorsForProject(
      [
        proc({ pid: 1, projectPath: GAME_A, headless: true }),
        proc({ pid: 2, projectPath: GAME_A }),
      ],
      GAME_A,
    );
    expect(selected.map((p) => p.pid)).toEqual([2]);
  });

  it("keeps headless editors of the project when asked to, for stop and ownership (#1150)", () => {
    const selected = selectEditorsForProject(
      [
        proc({ pid: 1, projectPath: GAME_A, headless: true }),
        proc({ pid: 2, projectPath: GAME_B, headless: true }),
      ],
      GAME_A,
      { includeHeadless: true },
    );
    expect(selected.map((p) => p.pid)).toEqual([1]);
  });

  it("reports nothing when only other projects are running", () => {
    const selected = selectEditorsForProject([proc({ pid: 2, projectPath: GAME_B })], GAME_A);
    expect(selected).toEqual([]);
  });

  it("falls back to unreadable processes only when nothing matched positively", () => {
    const unknown = proc({ pid: 9, projectPath: null });
    expect(selectEditorsForProject([unknown, proc({ pid: 2, projectPath: GAME_B })], GAME_A)).toEqual([unknown]);
    expect(
      selectEditorsForProject([unknown, proc({ pid: 1, projectPath: GAME_A })], GAME_A).map((p) => p.pid),
    ).toEqual([1]);
  });

  it("returns every interactive editor when no project is named", () => {
    const selected = selectEditorsForProject(
      [
        proc({ pid: 1, projectPath: GAME_A }),
        proc({ pid: 2, projectPath: GAME_B }),
        proc({ pid: 3, projectPath: GAME_B, headless: true }),
      ],
      null,
    );
    expect(selected.map((p) => p.pid)).toEqual([1, 2]);
  });
});
