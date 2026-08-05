import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import {
  assertTestProject,
  createTestBuildPlan,
  engineExecutables,
  engineRootFromEnginePath,
  getProjectPaths,
  isSameOrUnder,
  protectedEngineRoots,
  resolveTestEngine,
  unrealTargetPlatform,
} from "../../scripts/build-utils.js";

let temporaryRoot: string;

beforeEach(() => {
  temporaryRoot = fs.realpathSync.native(fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-test-engine-")));
});

afterEach(() => {
  fs.rmSync(temporaryRoot, { recursive: true, force: true });
});

function createFakeEngine(name: string): string {
  const engineRoot = path.join(temporaryRoot, name);
  const { buildTool } = engineExecutables(engineRoot);
  fs.mkdirSync(path.dirname(buildTool), { recursive: true });
  fs.writeFileSync(buildTool, "test build tool");
  return engineRoot;
}

describe("UE-MCP test engine build guard", () => {
  it("blocks engine writes by default on the fixed test target", () => {
    const engineRoot = createFakeEngine("dedicated");
    const plan = createTestBuildPlan({ UE_MCP_TEST_ENGINE_ROOT: engineRoot });

    expect(plan.buildArgs).toEqual([
      "ue_mcpEditor",
      unrealTargetPlatform(),
      "Development",
      `-Project="${plan.projectFile}"`,
      "-WaitMutex",
      "-FromMsBuild",
      "-NoEngineChanges",
    ]);
    expect(plan.buildArgs.filter((argument: string) => argument === "-NoEngineChanges")).toHaveLength(1);
  });

  it("blocks engine writes even when no engine settings are configured at all", () => {
    // A missing config must not soften the guard. Discovery may fail, but if it
    // succeeds the build still carries -NoEngineChanges.
    const engineRoot = createFakeEngine("discovered");
    const plan = createTestBuildPlan({ UE_BUILD_TOOL_PATH: engineExecutables(engineRoot).buildTool });

    expect(plan.engineRootSource).toBe("UE_BUILD_TOOL_PATH");
    expect(plan.allowEngineChanges).toBe(false);
    expect(plan.buildArgs).toContain("-NoEngineChanges");
  });

  it("always builds the bundled test project", () => {
    const engineRoot = createFakeEngine("project-pin");
    const plan = createTestBuildPlan({ UE_MCP_TEST_ENGINE_ROOT: engineRoot });

    expect(plan.projectFile).toBe(getProjectPaths().projectFile);
    expect(() => assertTestProject(plan.projectFile)).not.toThrow();
  });

  it("refuses to build any project other than the bundled test project", () => {
    expect(() => assertTestProject(path.join(temporaryRoot, "Vale", "Vale.uproject")))
      .toThrow("only build the bundled test project");
  });

  it("fails loudly when an explicitly configured engine root has no build tool", () => {
    expect(() => resolveTestEngine({ UE_MCP_TEST_ENGINE_ROOT: path.join(temporaryRoot, "missing") }))
      .toThrow("UE_MCP_TEST_ENGINE_ROOT does not point at a usable Unreal engine");
  });

  it("fails loudly rather than silently skipping the build when nothing is found", () => {
    expect(() => resolveTestEngine({}, "linux")).toThrow("Unreal Engine build tool not found");
  });

  it.each([
    ["win32", "Win64"],
    ["darwin", "Mac"],
    ["linux", "Linux"],
  ])("maps %s to Unreal target platform %s", (platform, expected) => {
    expect(unrealTargetPlatform(platform)).toBe(expected);
  });

  it("compares protected Windows paths without case sensitivity, from any host", () => {
    expect(isSameOrUnder("D:\\UE-PRIMARY\\Engine", "d:\\ue-primary", "win32")).toBe(true);
    expect(isSameOrUnder("D:\\UE-PRIMARY-TEST", "d:\\ue-primary", "win32")).toBe(false);
  });

  it("compares protected POSIX paths case sensitively", () => {
    expect(isSameOrUnder("/opt/ue/Engine", "/opt/ue", "linux")).toBe(true);
    expect(isSameOrUnder("/opt/UE/Engine", "/opt/ue", "linux")).toBe(false);
  });

  it("derives the engine root from a build tool path on every layout", () => {
    expect(engineRootFromEnginePath("D:\\UE\\Engine\\Build\\BatchFiles\\Build.bat", "win32")).toBe("D:\\UE");
    expect(engineRootFromEnginePath("/opt/ue/Engine/Build/BatchFiles/Linux/Build.sh", "linux")).toBe("/opt/ue");
    expect(engineRootFromEnginePath("/opt/ue/Engine/Binaries/Linux/UnrealEditor", "linux")).toBe("/opt/ue");
    expect(engineRootFromEnginePath("/somewhere/else/tool.sh", "linux")).toBeNull();
  });

  it("rejects a selected engine inside a protected root", () => {
    const protectedRoot = path.join(temporaryRoot, "protected");
    fs.mkdirSync(protectedRoot);
    const engineRoot = createFakeEngine(path.join("protected", "engine"));

    expect(() => resolveTestEngine({
      UE_MCP_TEST_ENGINE_ROOT: engineRoot,
      UE_MCP_PROTECTED_ENGINE_ROOTS: protectedRoot,
    })).toThrow("UE_MCP_PROTECTED_ENGINE_ROOTS");
  });

  it("rejects a protected root even when engine changes are explicitly allowed", () => {
    const protectedRoot = path.join(temporaryRoot, "protected-optin");
    fs.mkdirSync(protectedRoot);
    const engineRoot = createFakeEngine(path.join("protected-optin", "engine"));

    expect(() => createTestBuildPlan({
      UE_MCP_TEST_ENGINE_ROOT: engineRoot,
      UE_MCP_PROTECTED_ENGINE_ROOTS: protectedRoot,
      UE_MCP_ALLOW_TEST_ENGINE_CHANGES: "true",
    })).toThrow("refuse to run against a protected engine");
  });

  it("does not confuse a sibling path prefix with a protected child", () => {
    const protectedRoot = path.join(temporaryRoot, "engine");
    fs.mkdirSync(protectedRoot);
    const engineRoot = createFakeEngine("engine-test");

    expect(resolveTestEngine({
      UE_MCP_TEST_ENGINE_ROOT: engineRoot,
      UE_MCP_PROTECTED_ENGINE_ROOTS: protectedRoot,
    }).engineRoot).toBe(engineRoot);
  });

  it("rejects a relative protected root instead of protecting nothing", () => {
    expect(() => protectedEngineRoots({ UE_MCP_PROTECTED_ENGINE_ROOTS: "Epic Games" }))
      .toThrow("is not an absolute path");
  });

  it("parses the protected root list with the platform delimiter", () => {
    const first = path.join(temporaryRoot, "one");
    const second = path.join(temporaryRoot, "two");
    fs.mkdirSync(first);
    fs.mkdirSync(second);

    expect(protectedEngineRoots({
      UE_MCP_PROTECTED_ENGINE_ROOTS: ` ${first} ${path.delimiter}${second}${path.delimiter}`,
    })).toEqual([first, second]);
  });

  it("allows engine output creation only through an explicit bootstrap opt-in", () => {
    const engineRoot = createFakeEngine("bootstrap");
    const plan = createTestBuildPlan({
      UE_MCP_TEST_ENGINE_ROOT: engineRoot,
      UE_MCP_ALLOW_TEST_ENGINE_CHANGES: "yes",
    });

    expect(plan.allowEngineChanges).toBe(true);
    expect(plan.buildArgs).not.toContain("-NoEngineChanges");
  });

  it("treats an unrecognised opt-in value as no opt-in", () => {
    const engineRoot = createFakeEngine("garbage-optin");
    const plan = createTestBuildPlan({
      UE_MCP_TEST_ENGINE_ROOT: engineRoot,
      UE_MCP_ALLOW_TEST_ENGINE_CHANGES: "maybe",
    });

    expect(plan.allowEngineChanges).toBe(false);
    expect(plan.buildArgs).toContain("-NoEngineChanges");
  });
});
