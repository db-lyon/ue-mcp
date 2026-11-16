// @session independent: The rules an extension package must meet to install and load. Pure logic.
/**
 * What an extension's package.json has to say for this ue-mcp to install and
 * load it. The extension contract itself (DaemonExtensionApi, the manifest
 * field, the API versions) is in contract/daemon.ts.
 */
import { isNewer } from "../core/version-check.js";
import { EXTENSION_API_MIN_VERSION, EXTENSION_API_VERSION, extensionApiSupported, type ExtensionManifest } from "../contract/daemon.js";

/** The rule for an extension's name (`ueMcpExtension.name`), which is also its folder name and its install short name. */
export function isExtensionName(name: string): boolean {
  return /^[a-z0-9][a-z0-9-]*$/.test(name);
}

/**
 * Why an extension cannot run on this ue-mcp, or null when it can. Install and
 * load both ask this, so they accept the same set. An extension that needs a
 * newer ue-mcp is refused whole, never loaded with parts missing.
 */
export function extensionRefusal(name: string, ext: ExtensionManifest | undefined, installed: string): { reason: string; needsUeMcp?: string } | null {
  const min = ext?.api?.min ?? 1;
  const max = ext?.api?.max ?? min;
  if (!extensionApiSupported(min, max)) {
    const fix = min > EXTENSION_API_VERSION
      ? "update ue-mcp (`npx ue-mcp update`) to run it"
      : `install a newer build of ${name}`;
    return { reason: `${name} needs extension API ${min}..${max}; this ue-mcp serves ${EXTENSION_API_MIN_VERSION}..${EXTENSION_API_VERSION}. To fix it, ${fix}` };
  }
  if (ext?.ueMcp && isNewer(ext.ueMcp, installed)) {
    return {
      reason: `${name} needs ue-mcp ${ext.ueMcp} or later; this is ${installed}. Update ue-mcp with \`npx ue-mcp update\``,
      needsUeMcp: ext.ueMcp,
    };
  }
  return null;
}
