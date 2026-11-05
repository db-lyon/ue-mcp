/**
 * A bridge plugin tree as a release ships it: a descriptor listing the
 * bridge module, the module manifest naming its library, and the library.
 * The updater refuses a tree without all three (verifyBridgeTree), so every
 * fixture standing in for a release builds one here.
 */
import * as fs from "node:fs";
import * as path from "node:path";

export function writeBridgeTree(root: string, version: string, platform: string, library = "dll"): string {
  const bin = path.join(root, "Binaries", platform);
  fs.mkdirSync(bin, { recursive: true });
  fs.writeFileSync(path.join(root, "UE_MCP_Bridge.uplugin"), JSON.stringify({
    FileVersion: 3, VersionName: version, Modules: [{ Name: "UE_MCP_Bridge", Type: "Editor", LoadingPhase: "PostEngineInit" }],
  }));
  fs.writeFileSync(path.join(bin, "UnrealEditor.modules"), JSON.stringify({ BuildId: "1", Modules: { UE_MCP_Bridge: "UnrealEditor-UE_MCP_Bridge.dll" } }));
  fs.writeFileSync(path.join(bin, "UnrealEditor-UE_MCP_Bridge.dll"), library);
  return root;
}
