import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";

/**
 * Where this ue-mcp package lives on disk. This module sits in src/core/ (tsx)
 * or dist/core/ (built), so src/ or dist/ is its parent and the package root
 * is one above that, in both layouts.
 */
const ENTRY_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

/** The directory holding package.json, plugin/ and skills/. */
export function packageRoot(): string {
  return path.resolve(ENTRY_DIR, "..");
}

/** A top-level entry point of the running build (e.g. "deploy-cli.js"), whatever the caller's depth. */
export function packageModulePath(fileName: string): string {
  return path.join(ENTRY_DIR, fileName);
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
