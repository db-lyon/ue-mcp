// @session per-session: Reads module-manifest BuildIds from the engine one project launches with and the plugin directories it is handed.
/**
 * Unreal loads a module only when its manifest's BuildId matches the engine's.
 * Engine major and minor are not enough: two previews or hotfixes of one
 * version have different BuildIds, and a plugin built against one is skipped
 * by the other ("Skipping out-of-date modules in manifest ... (BuildId A != B)"),
 * which the editor then offers to rebuild on every launch (#2072).
 *
 * The engine's BuildId is in Engine/Binaries/<Platform>/UnrealEditor.modules,
 * and in UnrealEditor.version beside it. A plugin's is in its own
 * Binaries/<Platform>/UnrealEditor.modules.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { ProjectContext } from "../config/project.js";
import { engineLookupFor, projectEngineVersion, trySelectEngine } from "./engine-root.js";
import { engineMajorMinor, unrealPlatform } from "./install-marker.js";

/** The BuildId a `.modules` or `.version` file names, or null when it cannot be read. */
export function readBuildId(file: string): string | null {
  try {
    const parsed = JSON.parse(fs.readFileSync(file, "utf-8").replace(/^﻿/, "")) as { BuildId?: unknown };
    return typeof parsed.BuildId === "string" && parsed.BuildId.length > 0 ? parsed.BuildId : null;
  } catch {
    return null;
  }
}

/** The BuildId of the engine at `engineRoot` for `platform`, or null when it cannot be told. */
export function engineBuildId(engineRoot: string | null | undefined, platform: string): string | null {
  if (!engineRoot) return null;
  const bin = path.join(engineRoot, "Engine", "Binaries", platform);
  return readBuildId(path.join(bin, "UnrealEditor.modules")) ?? readBuildId(path.join(bin, "UnrealEditor.version"));
}

/**
 * The BuildId of the engine `uprojectPath` launches with, or null when it
 * cannot be told. The editor's tree is selected, not the source tree, since a
 * launcher install has no Engine/Source and is still what loads the bridge. An
 * engine of another major.minor than the project names is not the one the
 * binaries were picked for, so its BuildId is not reported.
 */
export function projectEngineBuildId(uprojectPath: string, platform: string = unrealPlatform()): string | null {
  try {
    const project = new ProjectContext();
    project.setProject(uprojectPath);
    const lookup = engineLookupFor(project.projectPath, project.engineAssociation, project.config.editor);
    const root = trySelectEngine(lookup, "editor")?.engineRoot ?? null;
    const version = projectEngineVersion(project);
    if (!root || !version || engineMajorMinor(root) !== version) return null;
    return engineBuildId(root, platform);
  } catch {
    return null;
  }
}

/** The BuildId of a plugin's editor module manifest, or null when it has none. */
export function pluginBuildId(pluginDir: string, platform: string): string | null {
  return readBuildId(path.join(pluginDir, "Binaries", platform, "UnrealEditor.modules"));
}

/** Every `.modules` manifest under a plugin's Binaries/<platform>, with its BuildId (null when unreadable). */
export function pluginManifestBuildIds(pluginDir: string, platform: string): Array<{ file: string; buildId: string | null }> {
  const bin = path.join(pluginDir, "Binaries", platform);
  let names: string[];
  try {
    names = fs.readdirSync(bin).filter((n) => n.toLowerCase().endsWith(".modules")).sort();
  } catch {
    return [];
  }
  return names.map((n) => ({ file: `Binaries/${platform}/${n}`, buildId: readBuildId(path.join(bin, n)) }));
}

/** True only when both BuildIds are known and differ. An unknown side is never a mismatch. */
export function buildIdsDiffer(engine: string | null | undefined, plugin: string | null | undefined): boolean {
  return !!engine && !!plugin && engine !== plugin;
}

/**
 * Prebuilt binaries built for another build of the same engine version. Carries
 * both BuildIds so a caller can fall back to a source build or say which engine
 * build the binaries need.
 */
export class EngineBuildMismatchError extends Error {
  readonly code = "bridge_engine_mismatch";
  constructor(
    readonly engineBuildId: string,
    readonly binaryBuildId: string,
    readonly engine: string,
    readonly from: string,
  ) {
    super(
      `the prebuilt bridge in ${from} was built for UE ${engine} build ${binaryBuildId}, and this engine is build ${engineBuildId}. ` +
        "Unreal would skip its modules and ask to rebuild on every launch, so it was not installed. " +
        "Install the C++ toolchain Unreal uses and run the same command again, which then builds the bridge from source for this engine, " +
        `or update the engine to build ${binaryBuildId} and run it again.`,
    );
    this.name = "EngineBuildMismatchError";
  }
}
