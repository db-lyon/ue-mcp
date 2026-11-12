/**
 * The short names `ue-mcp install <name>` accepts. Each maps to the release
 * manifest it installs from and the name output shows. Data only: what a
 * name installs is decided by its manifest. UE_MCP_INSTALL_INDEX names a
 * JSON file of the same shape that replaces this list.
 */
import * as fs from "node:fs";
import { readEnv } from "../core/env.js";

export interface InstallName {
  /** What output calls it. */
  title: string;
  /** Release manifest URL, see readExtensionManifest. */
  manifest: string;
}

const OFFICIAL: Record<string, InstallName> = {
  studio: {
    title: "Studio",
    manifest: "https://github.com/db-lyon/ue-mcp-studio/releases/latest/download/latest.json",
  },
};

function index(): Record<string, InstallName> {
  const file = readEnv("installIndex");
  if (!file) return OFFICIAL;
  try {
    return JSON.parse(fs.readFileSync(file, "utf-8")) as Record<string, InstallName>;
  } catch (e) {
    throw new Error(`UE_MCP_INSTALL_INDEX (${file}) is not readable JSON: ${e instanceof Error ? e.message : String(e)}`);
  }
}

/** True when `target` is written as a short name rather than a path or URL. */
export function isInstallName(target: string): boolean {
  return /^[a-z0-9][a-z0-9-]*$/.test(target);
}

/** The entry for a short name. Throws, listing the known names, for one that is not in the index. */
export function resolveInstallName(name: string): InstallName {
  const all = index();
  const hit = Object.prototype.hasOwnProperty.call(all, name) ? all[name] : undefined;
  if (!hit?.manifest) {
    throw new Error(`'${name}' is not something ue-mcp can install by name (known: ${Object.keys(all).sort().join(", ") || "none"})`);
  }
  return { title: hit.title || name, manifest: hit.manifest };
}
