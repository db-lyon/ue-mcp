// @session independent: The short names install resolves, read from the published index. No editor is involved.
/**
 * The short names `ue-mcp install <name>` accepts. Each maps to the release
 * manifest it installs from and the name output shows. The list is data:
 * install-index.json at the root of the ue-mcp repository, read from its main
 * branch when a name is installed, so adding or retiring a name needs no
 * release and core names none of them. UE_MCP_INSTALL_INDEX names another
 * index of the same shape (a file or an http(s) URL) that replaces it.
 *
 * `{version}` in a manifest URL is this ue-mcp's version, for an extension
 * released beside core.
 */
import * as fs from "node:fs";
import { readEnv } from "../core/env.js";
import { packageVersion } from "../core/package-root.js";
import { CORE_REPO } from "../github/repos.js";
import { isExtensionName } from "./extension-manifest.js";

export interface InstallName {
  /** What output calls it. */
  title: string;
  /** Release manifest URL, see readExtensionManifest. */
  manifest: string;
}

export const OFFICIAL_INSTALL_INDEX = `https://raw.githubusercontent.com/${CORE_REPO.owner}/${CORE_REPO.repo}/main/install-index.json`;

const INDEX_TIMEOUT_MS = 10_000;

async function readIndex(source: string, fromEnv: boolean): Promise<Record<string, InstallName>> {
  const where = fromEnv ? `UE_MCP_INSTALL_INDEX (${source})` : `The install index (${source})`;
  let text: string;
  try {
    if (/^https?:\/\//i.test(source)) {
      const res = await fetch(source, { signal: AbortSignal.timeout(INDEX_TIMEOUT_MS), headers: { accept: "application/json", "user-agent": "ue-mcp" } });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      text = await res.text();
    } else {
      text = fs.readFileSync(source, "utf-8");
    }
  } catch (e) {
    const reason = e instanceof Error ? e.message : String(e);
    throw new Error(`${where} could not be read: ${reason}. Install from the release manifest URL or zip instead (ue-mcp install <url|zip>), or point UE_MCP_INSTALL_INDEX at a copy of the index.`);
  }
  try {
    return JSON.parse(text) as Record<string, InstallName>;
  } catch (e) {
    throw new Error(`${where} is not readable JSON: ${e instanceof Error ? e.message : String(e)}`);
  }
}

/** True when `target` is written as a short name rather than a path or URL. Short names follow the extension name rule. */
export function isInstallName(target: string): boolean {
  return isExtensionName(target);
}

/** The entry for a short name. Throws, listing the known names, for one that is not in the index. */
export async function resolveInstallName(name: string): Promise<InstallName> {
  const fromEnv = readEnv("installIndex");
  const all = await readIndex(fromEnv ?? OFFICIAL_INSTALL_INDEX, fromEnv !== undefined);
  const hit = Object.prototype.hasOwnProperty.call(all, name) ? all[name] : undefined;
  if (!hit?.manifest) {
    throw new Error(`'${name}' is not something ue-mcp can install by name (known: ${Object.keys(all).sort().join(", ") || "none"})`);
  }
  return { title: hit.title || name, manifest: hit.manifest.replaceAll("{version}", packageVersion()) };
}
