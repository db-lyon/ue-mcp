#!/usr/bin/env node
/**
 * `ue-mcp login` / `ue-mcp logout` - authorize this machine to publish plugins
 * to the registry.
 *
 * Runs the GitHub device flow and exchanges the result for a per-author
 * registry token, cached at ~/.ue-mcp/registry.json. After this,
 * `ue-mcp plugin publish` needs no token, no env var, and no site secret.
 */
import {
  clearRegistryAuth,
  loginToRegistry,
  readRegistryAuth,
  registryBase,
} from "./registry-auth.js";

const args = process.argv.slice(2);

function log(msg: string): void {
  console.log(msg ? `[ue-mcp] ${msg}` : "");
}

const HELP = `Usage: ue-mcp login [--force]
       ue-mcp logout

Authorizes this machine to publish plugins to the registry. Runs the GitHub
device flow, then mints your own publish token and caches it in
~/.ue-mcp/registry.json. After this, \`ue-mcp plugin publish\` needs no token.

  --force   Re-authorize and mint a fresh token, replacing the cached one.

CI: set UE_MCP_PUBLISH_TOKEN instead (mint a token at the registry /account page).
Override the registry with UE_MCP_REGISTRY.`;

export async function runLogin(): Promise<void> {
  if (args.includes("--help") || args.includes("-h")) {
    console.log(HELP);
    return;
  }
  const force = args.includes("--force");
  try {
    const auth = await loginToRegistry(log, { force });
    log(`Logged in to ${auth.registry} as @${auth.login}.`);
    log(`Publish with: ue-mcp plugin publish <dir>`);
  } catch (e) {
    console.error(`[ue-mcp] login failed: ${e instanceof Error ? e.message : e}`);
    process.exit(1);
  }
}

export async function runLogout(): Promise<void> {
  if (args.includes("--help") || args.includes("-h")) {
    console.log(HELP);
    return;
  }
  const existing = await readRegistryAuth();
  await clearRegistryAuth();
  if (existing) {
    log(`Logged out of ${existing.registry} (was @${existing.login}).`);
    log(`The token remains valid until you revoke it at ${registryBase()}/account.`);
  } else {
    log("Not logged in.");
  }
}
