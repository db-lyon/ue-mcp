/**
 * The engine resolution order (#959, #961, #962, #974).
 *
 * Two suites, on purpose. The first drives the resolver with injected probes so
 * a Windows layout and a Linux layout can both be asserted on any host, and the
 * ordering is checked as ordering rather than as "what this machine happens to
 * have installed". The second builds real directories in a temp dir, because
 * the sibling-engine walk-up is the fix these issues asked for and it has to
 * work against a real filesystem.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import {
  EngineResolutionError,
  engineBuildTool,
  engineCandidates,
  protectedEngineRoots,
  resolveEngineRoot,
  selectEngine,
  trySelectEngine,
  type EngineLookup,
} from "../../src/editor/engine-root.js";
import { readEngineRootFromLog } from "../../src/editor/engine-observer.js";

/** A resolver that knows only about the paths listed, on the platform given. */
function lookupOver(
  present: string[],
  overrides: Partial<EngineLookup> = {},
  platform: NodeJS.Platform = "win32",
): EngineLookup {
  const normalize = (value: string) => value.replace(/\\/g, "/").toLowerCase();
  const set = new Set(present.map(normalize));
  return {
    platform,
    env: {},
    ...overrides,
    hooks: {
      exists: (candidate) => set.has(normalize(candidate)),
      associationInstall: () => null,
      lastEngineRoot: () => null,
      ...overrides.hooks,
    },
  };
}

/**
 * Separator-insensitive comparison. The resolver joins with the target
 * platform's rules, so a Windows answer comes back with backslashes while the
 * fixtures above are written with forward slashes; neither is the thing under
 * test.
 */
function slashes(value: string | null): string | null {
  return value === null ? null : value.replace(/\\/g, "/");
}

const WIN_SIBLING_ENGINE = "C:/stream/Engine/Build/BatchFiles/Build.bat";
const WIN_PROJECT = "C:/stream/MyProject/MyProject.uproject";

describe("engine resolution order", () => {
  it("finds a source build that is a sibling of the project", () => {
    const selected = selectEngine(
      lookupOver([WIN_SIBLING_ENGINE], { projectPath: WIN_PROJECT }),
      "buildTool",
    );
    expect(slashes(selected.engineRoot)).toBe("C:/stream");
    expect(selected.source).toBe("project-relative engine tree");
    expect(slashes(selected.buildTool)).toBe(WIN_SIBLING_ENGINE);
  });

  it("finds an engine several directories above the project", () => {
    const selected = selectEngine(
      lookupOver([WIN_SIBLING_ENGINE], { projectPath: "C:/stream/games/deep/MyProject/MyProject.uproject" }),
      "buildTool",
    );
    expect(slashes(selected.engineRoot)).toBe("C:/stream");
  });

  it("reads an EngineAssociation that is a relative path to the engine", () => {
    const selected = selectEngine(
      lookupOver([WIN_SIBLING_ENGINE], { projectPath: WIN_PROJECT, engineAssociation: "../" }),
      "buildTool",
    );
    expect(selected.source).toBe("EngineAssociation path");
    expect(slashes(selected.engineRoot)).toBe("C:/stream");
  });

  it("resolves a GUID association through the registered-builds lookup", () => {
    const selected = selectEngine(
      lookupOver(["D:/registered/Engine/Build/BatchFiles/Build.bat"], {
        projectPath: WIN_PROJECT,
        engineAssociation: "{2a5c4e8f-9b31-4a6d-8f21-77c0d9e4b105}",
        hooks: { associationInstall: () => "D:/registered" },
      }),
      "buildTool",
    );
    expect(selected.source).toBe("EngineAssociation");
    expect(slashes(selected.engineRoot)).toBe("D:/registered");
  });

  it("resolves a version association through the launcher install", () => {
    const selected = selectEngine(
      lookupOver(["C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat"], {
        projectPath: WIN_PROJECT,
        engineAssociation: "5.8",
        hooks: { associationInstall: (association) => `C:/Program Files/Epic Games/UE_${association}` },
      }),
      "buildTool",
    );
    expect(selected.source).toBe("EngineAssociation");
    expect(slashes(selected.engineRoot)).toBe("C:/Program Files/Epic Games/UE_5.8");
  });

  it("prefers a sibling engine tree over the association's launcher install", () => {
    const selected = selectEngine(
      lookupOver([WIN_SIBLING_ENGINE, "C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat"], {
        projectPath: WIN_PROJECT,
        engineAssociation: "5.8",
        hooks: { associationInstall: () => "C:/Program Files/Epic Games/UE_5.8" },
      }),
      "buildTool",
    );
    expect(slashes(selected.engineRoot)).toBe("C:/stream");
  });

  it("lets UE_MCP_TEST_ENGINE_ROOT outrank the project's own engine", () => {
    const selected = selectEngine(
      lookupOver([WIN_SIBLING_ENGINE, "D:/pinned/Engine/Build/BatchFiles/Build.bat"], {
        projectPath: WIN_PROJECT,
        env: { UE_MCP_TEST_ENGINE_ROOT: "D:/pinned" },
      }),
      "buildTool",
    );
    expect(selected.source).toBe("UE_MCP_TEST_ENGINE_ROOT");
    expect(slashes(selected.engineRoot)).toBe("D:/pinned");
  });

  it("lets UE_BUILD_TOOL_PATH name the build script directly", () => {
    const selected = selectEngine(
      lookupOver([WIN_SIBLING_ENGINE, "D:/other/Engine/Build/BatchFiles/Build.bat"], {
        projectPath: WIN_PROJECT,
        env: { UE_BUILD_TOOL_PATH: "D:/other/Engine/Build/BatchFiles/Build.bat" },
      }),
      "buildTool",
    );
    expect(selected.source).toBe("UE_BUILD_TOOL_PATH");
    expect(slashes(selected.engineRoot)).toBe("D:/other");
  });

  it("uses editor.buildToolPath when no environment variable is set", () => {
    const selected = selectEngine(
      lookupOver([WIN_SIBLING_ENGINE, "D:/configured/Engine/Build/BatchFiles/Build.bat"], {
        projectPath: WIN_PROJECT,
        configBuildToolPath: "D:/configured/Engine/Build/BatchFiles/Build.bat",
      }),
      "buildTool",
    );
    expect(selected.source).toBe("editor.buildToolPath");
  });

  it("falls back to the engine the project was last opened with", () => {
    const selected = selectEngine(
      lookupOver(["E:/last/Engine/Build/BatchFiles/Build.bat"], {
        projectPath: WIN_PROJECT,
        hooks: { lastEngineRoot: () => "E:/last" },
      }),
      "buildTool",
    );
    expect(selected.source).toBe("last editor launch");
  });

  it("falls back to a default install last", () => {
    const selected = selectEngine(
      lookupOver([
        "C:/Program Files/Epic Games",
        "C:/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat",
      ], { projectPath: WIN_PROJECT }),
      "buildTool",
    );
    expect(selected.source).toBe("default engine install");
    expect(slashes(selected.engineRoot)).toBe("C:/Program Files/Epic Games/UE_5.7");
  });

  it("resolves the Linux build script layout", () => {
    const selected = selectEngine(
      lookupOver(["/stream/Engine/Build/BatchFiles/Linux/Build.sh"], {
        projectPath: "/stream/MyProject/MyProject.uproject",
      }, "linux"),
      "buildTool",
    );
    expect(slashes(selected.engineRoot)).toBe("/stream");
  });

  it("resolves the macOS build script layout", () => {
    const selected = selectEngine(
      lookupOver(["/stream/Engine/Build/BatchFiles/Mac/Build.sh"], {
        projectPath: "/stream/MyProject/MyProject.uproject",
      }, "darwin"),
      "buildTool",
    );
    expect(slashes(selected.engineRoot)).toBe("/stream");
  });

  it("lists every path it probed when nothing resolves", () => {
    let thrown: unknown;
    try {
      selectEngine(lookupOver([], { projectPath: WIN_PROJECT }), "buildTool");
    } catch (err) {
      thrown = err;
    }
    expect(thrown).toBeInstanceOf(EngineResolutionError);
    const error = thrown as EngineResolutionError;
    expect(error.message).toContain("Tried:");
    // The whole point of #974: the message names the paths, so a missing tool,
    // a wrong root and an unsupported layout do not all read the same.
    expect(slashes(error.message)).toContain(WIN_SIBLING_ENGINE);
    expect(error.message).toContain("walking up from the project");
    expect(error.tried.length).toBeGreaterThan(0);
  });

  it("names the setting when an explicit pin points nowhere", () => {
    expect(() =>
      selectEngine(
        lookupOver([WIN_SIBLING_ENGINE], {
          projectPath: WIN_PROJECT,
          env: { UE_MCP_TEST_ENGINE_ROOT: "D:/typo" },
        }),
        "buildTool",
      ),
    ).toThrow(/UE_MCP_TEST_ENGINE_ROOT does not point at a usable Unreal engine/);
  });

  it("separates the engines that can build from the ones that can be read", () => {
    // A binary-only distribution builds but ships no Engine/Source, so the
    // engine-source readers have to keep looking. One resolver, two questions.
    const present = [
      WIN_SIBLING_ENGINE,
      "C:/Program Files/Epic Games",
      "C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat",
      "C:/Program Files/Epic Games/UE_5.8/Engine/Source",
    ];
    expect(slashes(selectEngine(lookupOver(present, { projectPath: WIN_PROJECT }), "buildTool").engineRoot)).toBe(
      "C:/stream",
    );
    expect(slashes(selectEngine(lookupOver(present, { projectPath: WIN_PROJECT }), "engineRoot").engineRoot)).toBe(
      "C:/Program Files/Epic Games/UE_5.8",
    );
  });

  it("finds the editor binary in the tree it selected", () => {
    const selected = selectEngine(
      lookupOver([WIN_SIBLING_ENGINE, "C:/stream/Engine/Binaries/Win64/UnrealEditor.exe"], {
        projectPath: WIN_PROJECT,
      }),
      "editor",
    );
    expect(slashes(selected.editorExecutable)).toBe("C:/stream/Engine/Binaries/Win64/UnrealEditor.exe");
  });

  it("offers each candidate once, most specific first", () => {
    const candidates = engineCandidates(
      lookupOver([WIN_SIBLING_ENGINE], {
        projectPath: WIN_PROJECT,
        engineAssociation: "5.8",
        env: { UE_MCP_TEST_ENGINE_ROOT: "D:/pinned" },
        hooks: { associationInstall: () => "C:/stream" },
      }),
    );
    expect(candidates.map((candidate) => candidate.source)).toEqual([
      "UE_MCP_TEST_ENGINE_ROOT",
      "project-relative engine tree",
    ]);
  });

  it("returns null rather than throwing for the optional lookup", () => {
    expect(trySelectEngine(lookupOver([], { projectPath: WIN_PROJECT }), "buildTool")).toBeNull();
    expect(resolveEngineRoot(lookupOver([], { projectPath: WIN_PROJECT }))).toBeNull();
  });
});

describe("protected engine roots", () => {
  it("refuses an engine on the deny list even when it is explicitly pinned", () => {
    expect(() =>
      selectEngine(
        lookupOver(["D:/pinned/Engine/Build/BatchFiles/Build.bat"], {
          projectPath: WIN_PROJECT,
          env: {
            UE_MCP_TEST_ENGINE_ROOT: "D:/pinned",
            UE_MCP_PROTECTED_ENGINE_ROOTS: "D:\\pinned",
          },
        }),
        "buildTool",
      ),
    ).toThrow(/UE_MCP_PROTECTED_ENGINE_ROOTS/);
  });

  it("protects everything beneath a denied root", () => {
    expect(() =>
      selectEngine(
        lookupOver(["D:/protected/inner/Engine/Build/BatchFiles/Build.bat"], {
          projectPath: WIN_PROJECT,
          env: {
            UE_MCP_TEST_ENGINE_ROOT: "D:/protected/inner",
            UE_MCP_PROTECTED_ENGINE_ROOTS: "D:\\protected",
          },
        }),
        "buildTool",
      ),
    ).toThrow(/UE_MCP_PROTECTED_ENGINE_ROOTS/);
  });

  it("does not treat a sibling with a shared prefix as protected", () => {
    const selected = selectEngine(
      lookupOver(["D:/protected-test/Engine/Build/BatchFiles/Build.bat"], {
        projectPath: WIN_PROJECT,
        env: {
          UE_MCP_TEST_ENGINE_ROOT: "D:/protected-test",
          UE_MCP_PROTECTED_ENGINE_ROOTS: "D:\\protected",
        },
      }),
      "buildTool",
    );
    expect(slashes(selected.engineRoot)).toBe("D:/protected-test");
  });

  it("rejects a relative deny entry rather than protecting nothing", () => {
    expect(() => protectedEngineRoots({ UE_MCP_PROTECTED_ENGINE_ROOTS: "engines/ue" })).toThrow(
      /is not an absolute path/,
    );
  });
});

describe("engine resolution against real directories", () => {
  let temporaryRoot: string;

  beforeEach(() => {
    temporaryRoot = fs.realpathSync.native(fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-engine-root-")));
  });

  afterEach(() => {
    fs.rmSync(temporaryRoot, { recursive: true, force: true });
  });

  function createEngine(name: string): string {
    const engineRoot = path.join(temporaryRoot, name);
    const buildTool = engineBuildTool(engineRoot);
    fs.mkdirSync(path.dirname(buildTool), { recursive: true });
    fs.writeFileSync(buildTool, "");
    fs.mkdirSync(path.join(engineRoot, "Engine", "Source"), { recursive: true });
    return engineRoot;
  }

  function createProject(relative: string, engineAssociation: string): string {
    const projectDir = path.join(temporaryRoot, relative);
    fs.mkdirSync(projectDir, { recursive: true });
    const projectPath = path.join(projectDir, `${path.basename(projectDir)}.uproject`);
    fs.writeFileSync(projectPath, JSON.stringify({ EngineAssociation: engineAssociation }));
    return projectPath;
  }

  it("resolves the sibling engine of a real project directory", () => {
    // The Perforce stream layout: <stream>/Engine beside <stream>/MyProject.
    const engineRoot = createEngine("stream/Engine/..");
    const projectPath = createProject("stream/MyProject", "");
    const selected = selectEngine({ projectPath, env: {} }, "buildTool");
    expect(slashes(selected.engineRoot)).toBe(slashes(path.resolve(engineRoot)));
    expect(selected.source).toBe("project-relative engine tree");
    expect(fs.existsSync(selected.buildTool!)).toBe(true);
  });

  it("answers engine-source reads from that same tree", () => {
    createEngine("stream/Engine/..");
    const projectPath = createProject("stream/MyProject", "");
    expect(resolveEngineRoot({ projectPath, env: {} })).toBe(path.join(temporaryRoot, "stream"));
  });

  it("reports the paths it probed when a real project has no engine anywhere", () => {
    const projectPath = createProject("orphan", "");
    // Confined to the temp tree, so whatever this machine has installed cannot
    // answer and the failure path is the one under test.
    const confined = {
      projectPath,
      env: {},
      hooks: {
        exists: (candidate: string) => candidate.startsWith(temporaryRoot) && fs.existsSync(candidate),
        lastEngineRoot: () => null,
      },
    };
    expect(() => selectEngine(confined, "buildTool")).toThrow(/Unreal Engine build tool not found/);
    let message = "";
    try {
      selectEngine(confined, "buildTool");
    } catch (err) {
      message = (err as Error).message;
    }
    expect(message).toContain(engineBuildTool(path.join(temporaryRoot, "orphan")));
    expect(message).toContain("walking up from the project");
  });
});

describe("the engine a project was last opened with", () => {
  let temporaryRoot: string;

  beforeEach(() => {
    temporaryRoot = fs.realpathSync.native(fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-engine-log-")));
  });

  afterEach(() => {
    fs.rmSync(temporaryRoot, { recursive: true, force: true });
  });

  function writeLog(body: string): string {
    const projectDir = path.join(temporaryRoot, "MyProject");
    fs.mkdirSync(path.join(projectDir, "Saved", "Logs"), { recursive: true });
    fs.writeFileSync(path.join(projectDir, "Saved", "Logs", "MyProject.log"), body);
    return path.join(projectDir, "MyProject.uproject");
  }

  it("reads the engine root out of the log's Base Directory line", () => {
    const projectPath = writeLog(
      [
        "Log file open, 08/28/26 09:14:02",
        "LogInit: Display: RandInit(0)",
        `LogInit: Base Directory: ${path.join(temporaryRoot, "stream", "Engine", "Binaries", "Win64")}/`,
      ].join("\n"),
    );
    expect(readEngineRootFromLog(projectPath)).toBe(path.join(temporaryRoot, "stream"));
  });

  it("returns null when the project has never been opened", () => {
    expect(readEngineRootFromLog(path.join(temporaryRoot, "Absent", "Absent.uproject"))).toBeNull();
    expect(readEngineRootFromLog(null)).toBeNull();
  });

  it("returns null when the log has no engine line", () => {
    expect(readEngineRootFromLog(writeLog("Log file open\nLogInit: Display: nothing useful here"))).toBeNull();
  });
});

describe("deny-list parsing is platform independent", () => {
  // This suite exists because the deny list used path.delimiter, which is ":"
  // on POSIX and therefore cut "D:\protected" into "D" and "\protected". CI
  // runs on ubuntu and the developer machine is Windows, so the break only
  // ever appeared after a push. It is a SAFETY list: a mangled entry does not
  // fail loudly, it silently protects nothing.
  const roots = (value: string) =>
    protectedEngineRoots({ UE_MCP_PROTECTED_ENGINE_ROOTS: value } as NodeJS.ProcessEnv);

  it("keeps a Windows drive-letter path whole", () => {
    expect(roots(String.raw`D:\protected`)).toEqual([String.raw`D:\protected`]);
    expect(roots("C:/Program Files/Epic Games/UE_5.8")).toEqual([
      "C:/Program Files/Epic Games/UE_5.8",
    ]);
  });

  it("separates several Windows paths on semicolons", () => {
    expect(roots(String.raw`D:\one;E:\two`)).toEqual([String.raw`D:\one`, String.raw`E:\two`]);
  });

  it("still separates POSIX paths on colons", () => {
    expect(roots("/opt/ue:/srv/ue")).toEqual(["/opt/ue", "/srv/ue"]);
  });

  it("tolerates whitespace around a separator without losing the drive letter", () => {
    expect(roots(String.raw` D:\one ; E:\two `)).toEqual([String.raw`D:\one`, String.raw`E:\two`]);
  });

  it("still rejects a relative entry rather than protecting nothing", () => {
    expect(() => roots("Epic Games")).toThrow("is not an absolute path");
    expect(() => roots("./relative")).toThrow("is not an absolute path");
  });
});
