import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";

/**
 * Where this ue-mcp package lives on disk. This module sits directly under
 * src/ (tsx) or dist/ (built), so its parent directory is the package root in
 * both layouts.
 */
const MODULE_DIR = path.dirname(fileURLToPath(import.meta.url));

/** The directory holding package.json, plugin/ and skills/. */
export function packageRoot(): string {
  return path.resolve(MODULE_DIR, "..");
}

/** A sibling entry point of the running build (e.g. "deploy-cli.js"), whatever the caller's depth. */
export function packageModulePath(fileName: string): string {
  return path.join(MODULE_DIR, fileName);
}

/** Reported when package.json cannot be read: valid semver, older than any release. */
export const UNKNOWN_PACKAGE_VERSION = "0.0.0";

let cachedVersion: string | undefined;

/** This package's version from its own package.json. */
export function packageVersion(): string {
  if (cachedVersion !== undefined) return cachedVersion;
  try {
    const pkg = JSON.parse(fs.readFileSync(path.join(packageRoot(), "package.json"), "utf-8")) as { version?: unknown };
    cachedVersion = typeof pkg.version === "string" ? pkg.version : UNKNOWN_PACKAGE_VERSION;
  } catch {
    cachedVersion = UNKNOWN_PACKAGE_VERSION;
  }
  return cachedVersion;
}
