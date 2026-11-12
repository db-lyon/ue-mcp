/**
 * Fetching and unpacking the zips ue-mcp installs: extensions, bridge
 * binaries and editor plugins. A download is checked against its SHA-256
 * before anything reads it.
 */
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as path from "node:path";
import { execFileSync } from "node:child_process";

export function sha256Of(file: string): string {
  return crypto.createHash("sha256").update(fs.readFileSync(file)).digest("hex");
}

/** Download `url` to `dest` and refuse it unless it hashes to `sha256`. */
export async function downloadVerified(url: string, dest: string, sha256: string): Promise<void> {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`download failed (${res.status} ${res.statusText}): ${url}`);
  fs.writeFileSync(dest, Buffer.from(await res.arrayBuffer()));
  const actual = sha256Of(dest);
  if (actual !== sha256.toLowerCase()) {
    throw new Error(`${url} does not match the expected SHA-256 (expected ${sha256}, got ${actual})`);
  }
}

/**
 * bsdtar reads zip and ships with Windows 10+ and macOS. On Windows it is named
 * by path, because a Git Bash PATH puts GNU tar first and GNU tar cannot.
 */
export function extractZip(zip: string, into: string): void {
  fs.mkdirSync(into, { recursive: true });
  const tar = process.platform === "win32"
    ? path.join(process.env.SystemRoot ?? process.env.SYSTEMROOT ?? "C:\\Windows", "System32", "tar.exe")
    : "tar";
  try {
    execFileSync(tar, ["-xf", zip, "-C", into], { windowsHide: true, stdio: ["ignore", "ignore", "pipe"] });
  } catch {
    execFileSync("unzip", ["-q", "-o", zip, "-d", into], { windowsHide: true, stdio: ["ignore", "ignore", "pipe"] });
  }
}
