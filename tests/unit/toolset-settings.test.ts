import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import {
  TOOLSET_SETTINGS_SECTION,
  readBlockedNames,
  setBlockedNames,
  toolsetSettingsFile,
} from "../../src/integrations/epic/toolset-settings.js";

let dir: string;
beforeEach(() => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), "toolset-settings-"));
});
afterEach(() => fs.rmSync(dir, { recursive: true, force: true }));

describe("which file the Toolset Registry reads", () => {
  it("is the project's DefaultGame.ini on 5.9 and the per-user ini on 5.8", () => {
    expect(toolsetSettingsFile(dir, "5.9", "win32")).toEqual({ file: path.join(dir, "Config", "DefaultGame.ini"), scope: "project", engineVersion: "5.9" });
    expect(toolsetSettingsFile(dir, "5.8", "win32")).toEqual({
      file: path.join(dir, "Saved", "Config", "WindowsEditor", "EditorPerProjectUserSettings.ini"),
      scope: "user",
      engineVersion: "5.8",
    });
    expect(toolsetSettingsFile(dir, "5.10", "linux")).toMatchObject({ scope: "project" });
  });

  it("is none before 5.8, or when the version cannot be read", () => {
    expect(toolsetSettingsFile(dir, "5.7")).toMatchObject({ reason: expect.stringContaining("arrived in UE 5.8") });
    expect(toolsetSettingsFile(dir, "{1234-ABCD}")).toMatchObject({ reason: expect.stringContaining("could not be read") });
    expect(toolsetSettingsFile(dir, null)).toHaveProperty("reason");
  });
});

describe("BlockedNames in one ini file", () => {
  it("adds entries under the section, keeping every other line and the line endings", () => {
    const file = path.join(dir, "DefaultGame.ini");
    fs.writeFileSync(file, "[/Script/EngineSettings.GeneralProjectSettings]\r\nProjectID=ABC\r\n");
    const w = setBlockedNames(file, ["EditorToolset.MaterialToolset", "GasToolset.Abilities"], true);
    expect(w.changed).toBe(true);
    const text = fs.readFileSync(file, "utf-8");
    expect(text).toContain("ProjectID=ABC\r\n");
    expect(text).toContain(`[${TOOLSET_SETTINGS_SECTION}]\r\n+BlockedNames=EditorToolset.MaterialToolset\r\n+BlockedNames=GasToolset.Abilities\r\n`);
    expect(readBlockedNames(file)).toEqual(["EditorToolset.MaterialToolset", "GasToolset.Abilities"]);
    expect(setBlockedNames(file, ["editortoolset.materialtoolset"], true).changed).toBe(false);
  });

  it("removes only the named entries when unblocking, and reads every operator UE writes", () => {
    const file = path.join(dir, "EditorPerProjectUserSettings.ini");
    fs.writeFileSync(file, [
      `[${TOOLSET_SETTINGS_SECTION}]`,
      "BlockedNames=A.One",
      "+BlockedNames=\"B.Two\"",
      ".BlockedNames=C.Three",
      "-BlockedNames=Z.Gone",
      "AllowedNames=Keep.Me",
      "",
      "[Other]",
      "BlockedNames=Not.Ours",
      "",
    ].join("\n"));
    expect(readBlockedNames(file)).toEqual(["A.One", "B.Two", "C.Three"]);
    const w = setBlockedNames(file, ["B.Two", "A.One"], false);
    expect(w.blocked).toEqual(["C.Three"]);
    const text = fs.readFileSync(file, "utf-8");
    expect(text).toContain("AllowedNames=Keep.Me");
    expect(text).toContain("[Other]\nBlockedNames=Not.Ours");
    expect(text).toContain("-BlockedNames=Z.Gone");
  });

  it("creates the file and its folder on first block, and leaves a missing file alone on unblock", () => {
    const file = path.join(dir, "Saved", "Config", "WindowsEditor", "EditorPerProjectUserSettings.ini");
    expect(setBlockedNames(file, ["X.Y"], false).changed).toBe(false);
    expect(fs.existsSync(file)).toBe(false);
    setBlockedNames(file, ["X.Y"], true);
    expect(fs.readFileSync(file, "utf-8")).toBe(`[${TOOLSET_SETTINGS_SECTION}]\n+BlockedNames=X.Y\n`);
  });
});
