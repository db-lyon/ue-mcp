/**
 * `ue-mcp extension install`: an extension zip is verified before it is
 * unpacked, and lands where the daemon loads extensions.
 */
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { execFileSync } from "node:child_process";
import { installExtension, removeExtension } from "../../src/daemon/extension-install.js";

let dir: string;
const tar = process.platform === "win32" ? path.join(process.env.SystemRoot ?? "C:\\Windows", "System32", "tar.exe") : "tar";

beforeEach(() => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-extinst-"));
});
afterEach(() => {
  fs.rmSync(dir, { recursive: true, force: true });
});

/** A zip of an extension folder; `pkg` overrides package.json fields. */
function zipExtension(version: string, pkg: Record<string, unknown> = {}): { zip: string; sha256: string } {
  const src = path.join(dir, `src-${version}`, "studio");
  fs.mkdirSync(path.join(src, "dist"), { recursive: true });
  fs.writeFileSync(path.join(src, "dist", "index.js"), `export function activate() {} // ${version}`);
  fs.writeFileSync(path.join(src, "package.json"), JSON.stringify({
    name: "studio-ext", version, type: "module", main: "dist/index.js",
    ueMcpExtension: { name: "studio", api: { min: 1, max: 1 } }, ...pkg,
  }));
  const zip = path.join(dir, `studio-${version}.zip`);
  execFileSync(tar, ["-a", "-c", "-f", zip, "-C", path.dirname(src), "studio"]);
  return { zip, sha256: crypto.createHash("sha256").update(fs.readFileSync(zip)).digest("hex") };
}

describe("installExtension", () => {
  it("installs a zip under its extension name and replaces an older copy", async () => {
    const root = path.join(dir, "extensions");
    const v1 = zipExtension("0.1.0");
    await installExtension(v1.zip, { sha256: v1.sha256, root });
    const v2 = zipExtension("0.2.0");
    const installed = await installExtension(v2.zip, { root });
    expect(installed).toMatchObject({ name: "studio", version: "0.2.0", sha256: v2.sha256 });
    expect(fs.readFileSync(path.join(root, "studio", "dist", "index.js"), "utf-8")).toContain("0.2.0");
    expect(fs.readdirSync(root)).toEqual(["studio"]);
    expect(removeExtension("studio", root)).toBe(true);
    expect(fs.existsSync(path.join(root, "studio"))).toBe(false);
  });

  it("refuses a zip whose hash does not match and leaves nothing behind", async () => {
    const root = path.join(dir, "extensions");
    const v1 = zipExtension("0.1.0");
    await expect(installExtension(v1.zip, { sha256: "0".repeat(64), root })).rejects.toThrow(/does not match the expected SHA-256/);
    expect(fs.existsSync(path.join(root, "studio"))).toBe(false);
  });

  it("refuses a URL without a hash", async () => {
    await expect(installExtension("https://example.invalid/studio.zip", { root: dir })).rejects.toThrow(/needs --sha256/);
  });

  it("refuses an archive that is not an extension, or targets another extension API", async () => {
    const root = path.join(dir, "extensions");
    const none = zipExtension("0.1.0", { ueMcpExtension: undefined });
    await expect(installExtension(none.zip, { root })).rejects.toThrow(/not a ue-mcp extension/);
    const future = zipExtension("0.3.0", { ueMcpExtension: { name: "studio", api: { min: 99, max: 99 } } });
    await expect(installExtension(future.zip, { root })).rejects.toThrow(/needs extension API 99\.\.99/);
  });
});

describe("installing from a release manifest", () => {
  it("follows the manifest to the archive and checks its hash", async () => {
    const http = await import("node:http");
    const v = zipExtension("0.4.0");
    let good = true;
    const server = http.createServer((req, res) => {
      if (req.url === "/latest.json") {
        res.writeHead(200, { "content-type": "application/json" }).end(JSON.stringify({ url: "studio.zip", sha256: good ? v.sha256 : "f".repeat(64) }));
      } else if (req.url === "/studio.zip") {
        res.writeHead(200).end(fs.readFileSync(v.zip));
      } else res.writeHead(404).end();
    });
    await new Promise<void>((r) => server.listen(0, "127.0.0.1", () => r()));
    const base = `http://127.0.0.1:${(server.address() as { port: number }).port}`;
    try {
      const root = path.join(dir, "extensions");
      expect(await installExtension(`${base}/latest.json`, { root })).toMatchObject({ name: "studio", version: "0.4.0" });
      good = false;
      await expect(installExtension(`${base}/latest.json`, { root })).rejects.toThrow(/does not match the expected SHA-256/);
    } finally {
      await new Promise<void>((r) => server.close(() => r()));
    }
  });
});
