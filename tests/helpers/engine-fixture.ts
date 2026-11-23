/**
 * A stand-in engine tree with just what the BuildId checks read: a version
 * file, an editor binary for engine selection, and the editor module manifest
 * that carries the engine's BuildId.
 */
import * as fs from "node:fs";
import * as path from "node:path";

export function fakeEngine(root: string, version: string, buildId: string, platform = "Win64"): string {
  const [major, minor] = version.split(".").map(Number);
  fs.mkdirSync(path.join(root, "Engine", "Build"), { recursive: true });
  fs.writeFileSync(path.join(root, "Engine", "Build", "Build.version"), JSON.stringify({ MajorVersion: major, MinorVersion: minor, PatchVersion: 0 }));
  // Engine selection looks for the editor of the host OS, whatever platform the manifest is written for.
  for (const [dir, exe] of [["Win64", "UnrealEditor.exe"], ["Linux", "UnrealEditor"], ["Mac", "UnrealEditor"]]) {
    fs.mkdirSync(path.join(root, "Engine", "Binaries", dir), { recursive: true });
    fs.writeFileSync(path.join(root, "Engine", "Binaries", dir, exe), "");
  }
  const bin = path.join(root, "Engine", "Binaries", platform);
  fs.mkdirSync(bin, { recursive: true });
  fs.writeFileSync(path.join(bin, "UnrealEditor.modules"), JSON.stringify({ BuildId: buildId, Modules: {} }));
  return root;
}
