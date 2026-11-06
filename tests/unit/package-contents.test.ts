import { describe, expect, it } from "vitest";
import { execFileSync } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { npmInvocation } from "../../src/core/safe-spawn.js";

/**
 * What `npm publish` would upload, read from `npm pack --dry-run` so the
 * package.json `files` list and every ignore rule are applied exactly as npm
 * applies them. No key file, credential blob or PEM private key may be in it,
 * nor the constant the removed embedded credential was unwrapped with.
 */
const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const BINARY = /\.(dll|pdb|lib|exp|exe|so|dylib|a|o|obj|png|jpg|jpeg|ico|uasset|umap|zip|tgz|gz|woff2?)$/i;
const OLD_KEY_CONSTANT = ["ue-mcp-feedback", "installation-key-v1"].join("/");
const PEM_PRIVATE_KEY = /-----BEGIN [A-Z ]*PRIVATE KEY-----[\s\S]*?-----END [A-Z ]*PRIVATE KEY-----/;

function packedFiles(): string[] {
  const inv = npmInvocation(["pack", "--dry-run", "--json", "--ignore-scripts"]);
  // execFileSync's option types omit windowsVerbatimArguments, which spawnSync honours.
  const verbatim = { windowsVerbatimArguments: inv.windowsVerbatimArguments };
  const out = execFileSync(inv.command, inv.args, {
    ...verbatim,
    cwd: repoRoot,
    encoding: "utf8",
    windowsHide: true,
    stdio: ["ignore", "pipe", "pipe"],
    maxBuffer: 64 * 1024 * 1024,
  });
  const parsed = JSON.parse(out) as unknown;
  const entries = Array.isArray(parsed) ? parsed : Object.values(parsed as Record<string, unknown>);
  const first = entries[0] as { files: Array<{ path: string }> };
  return first.files.map((f) => f.path);
}

describe("the published package", () => {
  const files = packedFiles();

  it("is the package npm would publish", () => {
    expect(files).toContain("package.json");
    expect(files.some((f) => f.startsWith("plugin/"))).toBe(true);
  });

  it("holds no key file or credential blob", () => {
    const offenders = files.filter((f) => /\.(pem|key|p12|pfx)$/i.test(f) || f === "assets/installation.bin" || /(^|\/)installation\.bin$/.test(f));
    expect(offenders).toEqual([]);
  });

  it("holds no PEM private key and not the old credential's unwrap constant in any text file", () => {
    const offenders: string[] = [];
    for (const rel of files) {
      if (BINARY.test(rel)) continue;
      const abs = path.join(repoRoot, rel);
      if (fs.statSync(abs).size > 4 * 1024 * 1024) continue;
      const text = fs.readFileSync(abs, "utf8");
      if (PEM_PRIVATE_KEY.test(text) || text.includes(OLD_KEY_CONSTANT)) offenders.push(rel);
    }
    expect(offenders).toEqual([]);
  });
});
