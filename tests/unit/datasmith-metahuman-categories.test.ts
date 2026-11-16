import { describe, expect, it } from "vitest";
import { readFileSync } from "node:fs";
import { datasmithTool } from "../../src/tools/datasmith.js";
import { metahumanTool } from "../../src/tools/metahuman.js";
import { ALL_TOOLS } from "../../src/tools.js";
import { classifyAction } from "../../src/dispatch/written-paths.js";
import { REGISTERED_HANDLER_TIMEOUT_SECONDS as BRIDGE_TIMEOUTS } from "../../src/bridge/bridge-timeouts.js";
import { readHandlerFile } from "../../scripts/lib/cpp-registrations.mjs";

const buildCs = readFileSync(
  new URL("../../plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/UE_MCP_Bridge.Build.cs", import.meta.url),
  "utf8",
);

describe("datasmith category (#1383)", () => {
  const source = readHandlerFile("DatasmithHandlers.cpp");

  it("is a registered category with the five actions", () => {
    expect(ALL_TOOLS.map((t) => t.name)).toContain("datasmith");
    expect(Object.keys(datasmithTool.actions).sort()).toEqual(["import", "inspect", "query_actors", "reimport", "status"]);
    expect(datasmithTool.actions.import.description).toContain("Params: filePath, destinationPath, options?");
    expect(datasmithTool.actions.reimport.description).toContain("sceneAssetPath, filePath?, syncActors?, respawnDeleted?, options?");
    expect(datasmithTool.actions.query_actors.description).toContain("tagPrefix?, tags?, metadataKey?, metadataValue?");
  });

  it("classifies reads and mutations", () => {
    expect(classifyAction("datasmith.inspect", { filePath: "C:/a.udatasmith" }).mutates).toBe(false);
    expect(classifyAction("datasmith.query_actors", { tagPrefix: "Revit." }).mutates).toBe(false);
    expect(classifyAction("datasmith.import", { filePath: "C:/a.udatasmith", destinationPath: "/Game/DS" }).mutates).toBe(true);
    expect(classifyAction("datasmith.reimport", { sceneAssetPath: "/Game/DS/Scene" }).mutates).toBe(true);
  });

  it("gives the long calls the plugin's timeouts", () => {
    expect(BRIDGE_TIMEOUTS.datasmith_import).toBe(600);
    expect(BRIDGE_TIMEOUTS.datasmith_reimport).toBe(600);
    expect(datasmithTool.actions.import.timeoutMs).toBe(600_000);
  });

  it("reaches Datasmith by reflection only, and says when the plugin is off", () => {
    expect(buildCs).not.toMatch(/"Datasmith/);
    expect(source).not.toMatch(/#include\s+"Datasmith(?!Handlers)/);
    expect(source).toContain("Datasmith plugin not available");
    expect(source).toContain("bUpdateActors");
    expect(source).toContain("bRespawnDeletedActors");
    expect(source).toContain("GetDatasmithUserData");
  });
});

describe("metahuman category (#1354)", () => {
  const source = readHandlerFile("MetaHumanHandlers.cpp");

  it("is a registered category with create, read, body, wardrobe, rig, textures and assemble", () => {
    expect(ALL_TOOLS.map((t) => t.name)).toContain("metahuman");
    expect(Object.keys(metahumanTool.actions).sort()).toEqual([
      "assemble", "create_character", "read_character", "request_rig", "request_textures", "set_body", "set_wardrobe", "status",
    ]);
    expect(metahumanTool.actions.set_body.description).toContain("Params: assetPath (or characterPath), constraints");
    expect(metahumanTool.actions.assemble.timeoutMs).toBe(900_000);
    expect(BRIDGE_TIMEOUTS.metahuman_assemble).toBe(900);
  });

  it("classifies reads and mutations", () => {
    expect(classifyAction("metahuman.read_character", { assetPath: "/Game/MH" }).mutates).toBe(false);
    expect(classifyAction("metahuman.set_body", { assetPath: "/Game/MH", constraints: { Height: 180 } }).mutates).toBe(true);
  });

  it("reaches the subsystem by reflection only, and says when the plugin is off", () => {
    expect(buildCs).not.toMatch(/"MetaHuman/);
    expect(source).not.toMatch(/#include\s+"MetaHuman(?!Handlers)/);
    expect(source).toContain("MetaHuman plugin not available");
  });

  it("writes body constraints back as a whole array, not through a copy", () => {
    const start = source.indexOf("FMetaHumanHandlers::SetBody(");
    const body = source.slice(start, source.indexOf("FMetaHumanHandlers::SetWardrobe(", start));
    expect(body).toContain("CopyCompleteValue");
    expect(body.indexOf("Unknown body constraint")).toBeLessThan(body.indexOf("Set.Invoke()"));
    expect(body).toContain("CommitBodyState");
    expect(body).toContain('MCPSetRollback(Result, TEXT("metahuman_set_body")');
  });

  it("reports a cloud refusal for want of a login as loginState not-logged-in", () => {
    for (const fn of ["RequestRig", "RequestTextures"]) {
      const start = source.indexOf(`FMetaHumanHandlers::${fn}(`);
      const body = source.slice(start, start + 4000);
      expect(body).toContain("SaysNotLoggedIn(Log)");
      expect(body).toContain("not-logged-in");
    }
  });
});
