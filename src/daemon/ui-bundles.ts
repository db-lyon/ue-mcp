/**
 * UI bundles the daemon serves at /ui/. A bundle is a directory with a
 * `manifest.json` naming every file and its SHA-256, and `manifest.sig`, an
 * Ed25519 signature over the manifest bytes. A bundle is installed and served
 * only when the signature verifies against a key an extension trusted and every
 * file matches its hash, because the page it serves can reach native editor
 * bindings.
 *
 * Bundles are versioned apart from ue-mcp. Each declares the daemon API range
 * it works with, and the daemon serves the newest compatible one.
 */
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as path from "node:path";
import { userDir } from "../core/user-dir.js";

export interface UiManifest {
  name: string;
  version: string;
  /** Daemon API versions this bundle works with, inclusive. */
  api: { min: number; max: number };
  /** Relative path (forward slashes) to lowercase hex SHA-256. */
  files: Record<string, string>;
}

export interface UiBundle {
  manifest: UiManifest;
  dir: string;
}

export function uiStoreDir(): string {
  return path.join(userDir(), "ui");
}

function sha256(file: string): string {
  return crypto.createHash("sha256").update(fs.readFileSync(file)).digest("hex");
}

/** Every file under `dir`, as forward-slash relative paths. */
function listFiles(dir: string, base = dir): string[] {
  const out: string[] = [];
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, e.name);
    if (e.isDirectory()) out.push(...listFiles(full, base));
    else out.push(path.relative(base, full).split(path.sep).join("/"));
  }
  return out;
}

/**
 * Verify a bundle directory. Throws with the reason when the signature does not
 * verify against any trusted key, a listed file is missing or altered, or a
 * file is present that the manifest does not list.
 */
export function verifyBundle(dir: string, trustedKeys: readonly crypto.KeyObject[]): UiManifest {
  const manifestPath = path.join(dir, "manifest.json");
  const sigPath = path.join(dir, "manifest.sig");
  if (!fs.existsSync(manifestPath) || !fs.existsSync(sigPath)) throw new Error(`${dir} has no manifest.json and manifest.sig`);
  if (trustedKeys.length === 0) throw new Error("no trusted UI signing key is registered");
  const bytes = fs.readFileSync(manifestPath);
  const sig = Buffer.from(fs.readFileSync(sigPath, "utf-8").trim(), "base64");
  if (!trustedKeys.some((k) => crypto.verify(null, bytes, k, sig))) throw new Error(`${dir}: manifest signature does not verify`);

  const manifest = JSON.parse(bytes.toString("utf-8")) as UiManifest;
  if (typeof manifest.version !== "string" || !manifest.api || typeof manifest.files !== "object") {
    throw new Error(`${dir}: malformed manifest`);
  }
  for (const [rel, hash] of Object.entries(manifest.files)) {
    if (rel.includes("..") || path.isAbsolute(rel)) throw new Error(`${dir}: unsafe path ${rel}`);
    const file = path.join(dir, rel);
    if (!fs.existsSync(file)) throw new Error(`${dir}: ${rel} is missing`);
    if (sha256(file) !== hash.toLowerCase()) throw new Error(`${dir}: ${rel} does not match its hash`);
  }
  const listed = new Set(Object.keys(manifest.files));
  for (const rel of listFiles(dir)) {
    if (rel === "manifest.json" || rel === "manifest.sig") continue;
    if (!listed.has(rel)) throw new Error(`${dir}: ${rel} is not in the manifest`);
  }
  return manifest;
}

export class UiBundleStore {
  private readonly keys: crypto.KeyObject[] = [];
  private current: UiBundle | null = null;

  constructor(private readonly apiVersion: number, private readonly root: string = uiStoreDir()) {}

  /** Trust a public key (PEM, Ed25519) for verifying bundles. */
  trustKey(pem: string): void {
    const key = crypto.createPublicKey(pem);
    if (key.asymmetricKeyType !== "ed25519") throw new Error("UI signing keys must be Ed25519");
    this.keys.push(key);
    this.current = null;
  }

  /** Verify a bundle and copy it into the store. Returns its manifest. */
  install(sourceDir: string): UiManifest {
    const manifest = verifyBundle(sourceDir, this.keys);
    const target = path.join(this.root, `${manifest.name}-${manifest.version}`.replace(/[^A-Za-z0-9._-]/g, "_"));
    const staging = `${target}.staging-${process.pid}`;
    fs.rmSync(staging, { recursive: true, force: true });
    fs.cpSync(sourceDir, staging, { recursive: true });
    verifyBundle(staging, this.keys);
    fs.rmSync(target, { recursive: true, force: true });
    fs.renameSync(staging, target);
    this.current = null;
    return manifest;
  }

  /** The newest installed bundle that verifies and supports this daemon's API, or null. */
  active(): UiBundle | null {
    if (this.current) return this.current;
    if (!fs.existsSync(this.root) || this.keys.length === 0) return null;
    const candidates: UiBundle[] = [];
    for (const e of fs.readdirSync(this.root, { withFileTypes: true })) {
      if (!e.isDirectory() || e.name.includes(".staging-")) continue;
      const dir = path.join(this.root, e.name);
      try {
        const manifest = verifyBundle(dir, this.keys);
        if (manifest.api.min <= this.apiVersion && this.apiVersion <= manifest.api.max) candidates.push({ manifest, dir });
      } catch {
        // Not ours, tampered, or for a key no extension trusts: never served.
      }
    }
    candidates.sort((a, b) => a.manifest.version.localeCompare(b.manifest.version, undefined, { numeric: true }));
    this.current = candidates.at(-1) ?? null;
    return this.current;
  }

  /** The file a /ui/ path names inside the active bundle, or null. */
  resolve(urlPath: string): string | null {
    const bundle = this.active();
    if (!bundle) return null;
    let rel = decodeURIComponent(urlPath.replace(/^\/ui\/?/, ""));
    if (rel === "" || rel.endsWith("/")) rel += "index.html";
    if (!(rel in bundle.manifest.files)) return null;
    return path.join(bundle.dir, rel);
  }
}

const TYPES: Record<string, string> = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".mjs": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json",
  ".svg": "image/svg+xml",
  ".png": "image/png",
  ".jpg": "image/jpeg",
  ".woff2": "font/woff2",
  ".ico": "image/x-icon",
  ".map": "application/json",
};

export function contentType(file: string): string {
  return TYPES[path.extname(file).toLowerCase()] ?? "application/octet-stream";
}

/**
 * Write `manifest.json` and `manifest.sig` into a built bundle directory,
 * hashing every file in it. For the release step that publishes a bundle; the
 * private key never reaches a user's machine.
 */
export function signBundle(
  dir: string,
  meta: { name: string; version: string; api: { min: number; max: number } },
  privateKeyPem: string,
): UiManifest {
  const files: Record<string, string> = {};
  for (const rel of listFiles(dir)) {
    if (rel === "manifest.json" || rel === "manifest.sig") continue;
    files[rel] = sha256(path.join(dir, ...rel.split("/")));
  }
  const manifest: UiManifest = { ...meta, files };
  const bytes = Buffer.from(JSON.stringify(manifest, null, 2));
  fs.writeFileSync(path.join(dir, "manifest.json"), bytes);
  const sig = crypto.sign(null, bytes, crypto.createPrivateKey(privateKeyPem));
  fs.writeFileSync(path.join(dir, "manifest.sig"), sig.toString("base64"));
  return manifest;
}
