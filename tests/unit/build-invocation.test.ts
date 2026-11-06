import { describe, expect, it } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { buildInvocation, buildProject } from "../../src/editor/editor-build.js";
import { UnsafeArgumentError, assertCmdToken, windowsBatchInvocation } from "../../src/core/safe-spawn.js";

/**
 * project(build) takes the configuration and platform from the caller and the
 * project name from a file name, so none of them may become part of a shell
 * line. The build runs from an argv array, with each name checked first.
 */
describe("the process project(build) starts", () => {
  const bat = "C:\\Program Files\\Epic Games\\UE_5.7\\Engine\\Build\\BatchFiles\\Build.bat";
  const base = { buildTool: bat, projectPath: "C:\\Games\\My Game (R&D)\\Game.uproject", platform: "Win64", configuration: "Development", windows: true };

  it("runs Build.bat through cmd.exe as an argv array with one checked line", () => {
    const inv = buildInvocation(base);
    expect(inv.windowsVerbatimArguments).toBe(true);
    expect(inv.args.slice(0, 3)).toEqual(["/d", "/s", "/c"]);
    expect(inv.args).toHaveLength(4);
    expect(inv.args[3].startsWith(`""${bat}" GameEditor Win64 Development -Project="${base.projectPath}" -WaitMutex -FromMsBuild -NoHotReloadFromIDE`)).toBe(true);
    expect(inv.args[3].endsWith('"')).toBe(true);
  });

  it("passes the project path straight into argv off Windows (#740)", () => {
    const inv = buildInvocation({ ...base, buildTool: "/ue/Engine/Build/BatchFiles/Linux/Build.sh", projectPath: "/g/My Game/Game.uproject", platform: "Linux", windows: false });
    expect(inv.command).toBe("/ue/Engine/Build/BatchFiles/Linux/Build.sh");
    expect(inv.windowsVerbatimArguments).toBeUndefined();
    expect(inv.args).toContain("-Project=/g/My Game/Game.uproject");
  });

  it("refuses a project name that is not an Unreal identifier", () => {
    for (const name of ["Victim&calc&", "My Game", "1Game", 'a"b', "x%PATH%"]) {
      expect(() => buildInvocation({ ...base, projectPath: `C:\\G\\${name}.uproject` })).toThrow(UnsafeArgumentError);
      expect(() => buildInvocation({ ...base, projectPath: `/g/${name}.uproject`, windows: false })).toThrow(UnsafeArgumentError);
    }
  });

  it("refuses a configuration outside the five Unreal knows", () => {
    for (const configuration of ["Development&calc", "development", "", "Shipping Test"]) {
      expect(() => buildInvocation({ ...base, configuration })).toThrow(/configuration/);
    }
    for (const configuration of ["Debug", "DebugGame", "Development", "Shipping", "Test"]) {
      expect(() => buildInvocation({ ...base, configuration })).not.toThrow();
    }
  });

  it("refuses a platform that is not a plain name", () => {
    for (const platform of ["Win64&calc", "Win 64", "Win64|x", "../x"]) {
      expect(() => buildInvocation({ ...base, platform })).toThrow(/Platform/);
    }
  });

  it("refuses paths holding characters cmd acts on inside quotes", () => {
    for (const bad of ['C:\\a"b', "C:\\%TEMP%", "C:\\a!b", "C:\\a^b", "C:\\a\nb"]) {
      expect(() => buildInvocation({ ...base, projectPath: `${bad}\\Game.uproject` })).toThrow(UnsafeArgumentError);
      expect(() => buildInvocation({ ...base, buildTool: `${bad}\\Build.bat` })).toThrow(UnsafeArgumentError);
    }
  });

  it("runs a build tool that is not a batch file directly, even on Windows", () => {
    const inv = buildInvocation({ ...base, buildTool: "C:\\UE\\UnrealBuildTool.exe" });
    expect(inv.command).toBe("C:\\UE\\UnrealBuildTool.exe");
    expect(inv.args).toContain(`-Project=${base.projectPath}`);
  });

  it("reports a refused project as a failed build, without starting anything", async () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-build-inv-"));
    try {
      const uproject = path.join(dir, "Victim&calc&.uproject");
      fs.writeFileSync(uproject, "{}");
      fs.writeFileSync(path.join(dir, "ue-mcp.yml"), "editor:\n  buildToolPath: " + JSON.stringify(path.join(dir, "Build.bat")) + "\n");
      fs.writeFileSync(path.join(dir, "Build.bat"), "");
      const r = await buildProject(uproject, { configuration: "Development" });
      expect(r.success).toBe(false);
      expect(r.exitCode).toBeNull();
      expect(r.message).toMatch(/^Build refused: Project name 'Victim&calc&'/);
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });
});

describe("cmd.exe tokens", () => {
  it("accepts plain and quoted tokens", () => {
    expect(() => assertCmdToken("-WaitMutex", "t")).not.toThrow();
    expect(() => assertCmdToken('-Project="C:\\A & B (x)\\G.uproject"', "t")).not.toThrow();
  });

  it("refuses operators outside quotes and unbalanced quotes", () => {
    for (const t of ["a&b", "a|b", "a>b", "a<b", '-P="x', "a b", "(x)", ""]) {
      expect(() => assertCmdToken(t, "t")).toThrow(UnsafeArgumentError);
    }
    expect(() => windowsBatchInvocation("C:\\x.bat", ["ok", "x&y"])).toThrow(UnsafeArgumentError);
  });
});
