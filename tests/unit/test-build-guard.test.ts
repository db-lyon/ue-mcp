import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import {
  createTestBuildPlan,
  engineExecutables,
  isSameOrUnder,
  resolveTestEngine,
  TEST_ENGINE_MARKER,
  unrealTargetPlatform,
} from "../../scripts/build-utils.js";

let temporaryRoot: string;

beforeEach(() => {
  temporaryRoot = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-test-engine-"));
});

afterEach(() => {
  fs.rmSync(temporaryRoot, { recursive: true, force: true });
});

function createFakeEngine(name: string): string {
  const engineRoot = path.join(temporaryRoot, name);
  const { buildTool } = engineExecutables(engineRoot);
  fs.mkdirSync(path.dirname(buildTool), { recursive: true });
  fs.writeFileSync(buildTool, "test build tool");
  fs.writeFileSync(path.join(engineRoot, TEST_ENGINE_MARKER), "Dedicated UE-MCP test engine.\n");
  return engineRoot;
}

describe("UE-MCP test engine build guard", () => {
  it("requires an explicit test engine root", () => {
    expect(() => resolveTestEngine({})).toThrow("UE_MCP_TEST_ENGINE_ROOT is required");
  });

  it("rejects an unmarked engine root", () => {
    const engineRoot = createFakeEngine("unmarked");
    fs.rmSync(path.join(engineRoot, TEST_ENGINE_MARKER));

    expect(() => resolveTestEngine({ UE_MCP_TEST_ENGINE_ROOT: engineRoot }))
      .toThrow("not marked as a dedicated test engine");
  });

  it("builds only the fixed test target with engine changes blocked", () => {
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

  it.each([
    ["win32", "Win64"],
    ["darwin", "Mac"],
    ["linux", "Linux"],
  ])("maps %s to Unreal target platform %s", (platform, expected) => {
    expect(unrealTargetPlatform(platform)).toBe(expected);
  });

  it("compares protected Windows paths without case sensitivity", () => {
    expect(isSameOrUnder("D:\\UE-PRIMARY\\Engine", "d:\\ue-primary", "win32")).toBe(true);
  });

  it("rejects a selected engine inside a protected root", () => {
    const protectedRoot = path.join(temporaryRoot, "protected");
    fs.mkdirSync(protectedRoot);
    const engineRoot = createFakeEngine(path.join("protected", "engine"));

    expect(() => resolveTestEngine({
      UE_MCP_TEST_ENGINE_ROOT: engineRoot,
      UE_MCP_PROTECTED_ENGINE_ROOTS: protectedRoot,
      UE_MCP_ALLOW_TEST_ENGINE_CHANGES: "true",
    })).toThrow("forbidden under protected engine root");
  });

  it("does not confuse a sibling path prefix with a protected child", () => {
    const protectedRoot = path.join(temporaryRoot, "engine");
    fs.mkdirSync(protectedRoot);
    const engineRoot = createFakeEngine("engine-test");

    expect(resolveTestEngine({
      UE_MCP_TEST_ENGINE_ROOT: engineRoot,
      UE_MCP_PROTECTED_ENGINE_ROOTS: protectedRoot,
    }).engineRoot).toBe(fs.realpathSync.native(engineRoot));
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
});
