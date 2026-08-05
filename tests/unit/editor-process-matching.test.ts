import * as path from "node:path";
import { describe, expect, it } from "vitest";
import { editorOwnsProject, selectEditorsForProject, type EditorProcess } from "../../src/engine-observer.js";

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
