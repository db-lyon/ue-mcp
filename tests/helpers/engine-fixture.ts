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
  const bin = path.join(root, "Engine", "Binaries", platform);
  fs.mkdirSync(bin, { recursive: true });
  for (const exe of ["UnrealEditor.exe", "UnrealEditor"]) fs.writeFileSync(path.join(bin, exe), "");
  fs.writeFileSync(path.join(bin, "UnrealEditor.modules"), JSON.stringify({ BuildId: buildId, Modules: {} }));
  return root;
}
