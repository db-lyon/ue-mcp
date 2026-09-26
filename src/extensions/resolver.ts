import * as fs from "node:fs";
import * as path from "node:path";
import { compareVersions } from "./version.js";

/**
 * Resolve an npm package by name (and optional version pin) inside the given
 * project's `node_modules`. Walks up parent directories so a server launched
 * from a subfolder still finds the project root install.
 */
export interface ResolvedPackage {
  name: string;
  version: string;
  pkgDir: string;
  pkgJsonPath: string;
}

interface PackageJsonShape {
  name?: string;
  version?: string;
  keywords?: string[];
}

function readPackageJson(pkgDir: string): PackageJsonShape | null {
  const pj = path.join(pkgDir, "package.json");
  if (!fs.existsSync(pj)) return null;
  try {
    return JSON.parse(fs.readFileSync(pj, "utf-8")) as PackageJsonShape;
  } catch {
    return null;
  }
}

/**
 * Walk up from `startDir` looking for a `node_modules/<name>` directory.
 * Returns the matching package directory or null.
 */
export function findInstalledPackage(name: string, startDir: string): string | null {
  let dir = path.resolve(startDir);
  const root = path.parse(dir).root;
  while (true) {
    const candidate = path.join(dir, "node_modules", ...name.split("/"));
    if (fs.existsSync(path.join(candidate, "package.json"))) return candidate;
    if (dir === root) return null;
    const parent = path.dirname(dir);
    if (parent === dir) return null;
    dir = parent;
  }
}

export function resolvePackage(
  name: string,
  version: string | undefined,
  projectDir: string,
): ResolvedPackage {
  const pkgDir = findInstalledPackage(name, projectDir);
  if (!pkgDir) {
    throw new Error(`Plugin package not installed: ${name}. Run \`ue-mcp plugin install ${name}\` from the project directory.`);
  }
  const pj = readPackageJson(pkgDir);
  if (!pj || !pj.version) {
    throw new Error(`Plugin package at ${pkgDir} has no readable package.json`);
  }
  if (version && compareVersions(pj.version, version) !== 0) {
    throw new Error(
      `Plugin ${name} version mismatch: ue-mcp.yml pins ${version}, installed ${pj.version}. ` +
      `Run \`ue-mcp plugin install ${name} --version ${version}\` or remove the pin.`,
    );
  }
  return { name, version: pj.version, pkgDir, pkgJsonPath: path.join(pkgDir, "package.json") };
}
