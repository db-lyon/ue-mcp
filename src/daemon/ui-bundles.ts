/**
 * UI bundles the daemon serves at /ui/ and /ui/ext/<name>/. A bundle is a directory with a
 * `manifest.json` naming every file and its SHA-256, and `manifest.sig`, an
 * Ed25519 signature over the manifest bytes. A bundle is installed and served
 * only when the signature verifies against a key an extension trusted and every
 * file matches its hash, because the page it serves can reach native editor
 * bindings.
 *
 * Bundles are versioned apart from ue-mcp. Each declares the daemon API range
 * it works with. A bundle named for a loaded extension is served only at that
 * extension's version or older (see selectBundle), so an older agent never gets
 * a newer UI. Each loaded extension's own bundle is also served at
 * /ui/ext/<name>/, and /ui/ext/ is never looked up in the root bundle.
 */
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as path from "node:path";
import { userDir } from "../core/user-dir.js";
import { compareVersions } from "../extensions/version.js";

export interface UiManifest {
  name: string;
  version: string;
  /** Daemon API versions this bundle works with, inclusive. */
  api: { min: number; max: number };
  /** Relative path (forward slashes) to lowercase hex SHA-256. */
  files: Record<string, string>;
  /**
   * A module another page loads from /ui/ext/<name>/: `entry` is a file in
   * `files`, `host` names the page meant to load it. Opaque to the daemon.
   */
  module?: { entry: string; host: string };
}

/** A loaded extension's own bundle, as /v1/health lists it. */
export interface UiBundleInfo {
  name: string;
  version: string;
  /** Where it is served: /ui/ext/<name>/. */
  base: string;
  module?: { entry: string; host: string };
}

export interface UiBundle {
  manifest: UiManifest;
  dir: string;
}

/** A loaded extension, as far as choosing its UI bundle goes. */
export interface UiBundleOwner {
  /** The extension name, which its bundles carry as manifest.name. */
  name: string;
  /** The extension package version. */
  version: string;
  /** The oldest bundle version it works with (`ueMcpExtension.ui.min`). */
  uiMin?: string;
  /** false keeps its bundles out of /ui/ (`ueMcpExtension.ui.root`); they are served only at /ui/ext/<name>/. */
  root?: boolean;
}

/** Whether a bundle falls in its owner's window: from `uiMin` up to the owner's own version. */
function inWindow(m: UiManifest, owner: UiBundleOwner): boolean {
  return compareVersions(m.version, owner.version) <= 0 && (!owner.uiMin || compareVersions(m.version, owner.uiMin) >= 0);
}

// Semver order: a release ranks above its own prereleases, so 1.2.0 outranks 1.2.0-beta.3.
// Equal precedence (build metadata differs) prefers the version string the owner names exactly.
function newest<T extends { manifest: UiManifest }>(pool: readonly T[], ownerOf: Map<string, UiBundleOwner>): T | null {
  const exact = (c: T) => (ownerOf.get(c.manifest.name)?.version === c.manifest.version ? 1 : 0);
  return [...pool].sort((a, b) => compareVersions(a.manifest.version, b.manifest.version) || exact(a) - exact(b)).at(-1) ?? null;
}

/**
 * The bundle to serve at /ui/. A bundle named for a loaded extension is a
 * candidate only from that extension's `uiMin` up to its own version, so the
 * exact match wins and nothing newer is ever served, and never when the
 * extension sets `root: false`. Bundles no loaded extension names are used only
 * when no owned one qualifies. Newest wins in semver order.
 */
export function selectBundle<T extends { manifest: UiManifest }>(candidates: readonly T[], owners: readonly UiBundleOwner[]): T | null {
  const ownerOf = new Map(owners.map((o) => [o.name, o]));
  const owned: T[] = [];
  const free: T[] = [];
  for (const c of candidates) {
    const owner = ownerOf.get(c.manifest.name);
    if (!owner) free.push(c);
    else if (owner.root !== false && inWindow(c.manifest, owner)) owned.push(c);
  }
  return newest(owned.length > 0 ? owned : free, ownerOf);
}

/** The bundle one loaded extension serves at /ui/ext/<name>/: its own, newest in its window. */
export function selectOwnerBundle<T extends { manifest: UiManifest }>(candidates: readonly T[], owner: UiBundleOwner): T | null {
  const own = candidates.filter((c) => c.manifest.name === owner.name && inWindow(c.manifest, owner));
  return newest(own, new Map([[owner.name, owner]]));
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
 * file is present that the manifest does not list. A function for
 * `trustedKeys` picks the keys from the manifest's name.
 */
export function verifyBundle(dir: string, trustedKeys: readonly crypto.KeyObject[] | ((name: string) => readonly crypto.KeyObject[])): UiManifest {
  const manifestPath = path.join(dir, "manifest.json");
  const sigPath = path.join(dir, "manifest.sig");
  if (!fs.existsSync(manifestPath) || !fs.existsSync(sigPath)) throw new Error(`${dir} has no manifest.json and manifest.sig`);
  const bytes = fs.readFileSync(manifestPath);
  let manifest: UiManifest;
  try {
    manifest = JSON.parse(bytes.toString("utf-8")) as UiManifest;
  } catch {
    throw new Error(`${dir}: malformed manifest`);
  }
  const keys = typeof trustedKeys === "function" ? trustedKeys(String(manifest?.name)) : trustedKeys;
  if (keys.length === 0) throw new Error(`no trusted UI signing key is registered for ${String(manifest?.name)}`);
  const sig = Buffer.from(fs.readFileSync(sigPath, "utf-8").trim(), "base64");
  if (!keys.some((k) => crypto.verify(null, bytes, k, sig))) throw new Error(`${dir}: manifest signature does not verify`);

  if (typeof manifest.version !== "string" || !manifest.api || typeof manifest.files !== "object") {
    throw new Error(`${dir}: malformed manifest`);
  }
  const mod = manifest.module;
  if (mod !== undefined && (typeof mod?.entry !== "string" || typeof mod.host !== "string" || !(mod.entry in manifest.files))) {
    throw new Error(`${dir}: module must name a host and an entry listed in files`);
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
  private readonly keys: Array<{ key: crypto.KeyObject; owner?: string }> = [];
  private candidates: UiBundle[] | null = null;
  private owners: UiBundleOwner[] = [];

  constructor(private readonly apiVersion: number, private readonly root: string = uiStoreDir()) {}

  /**
   * Trust a public key (PEM, Ed25519) for verifying bundles. A bundle named for
   * a loaded extension verifies only against that extension's keys and keys
   * with no owner; any other bundle verifies against every key.
   */
  trustKey(pem: string, owner?: string): void {
    const key = crypto.createPublicKey(pem);
    if (key.asymmetricKeyType !== "ed25519") throw new Error("UI signing keys must be Ed25519");
    this.keys.push(owner ? { key, owner } : { key });
    this.candidates = null;
  }

  /** The keys a bundle with this manifest name verifies against. */
  private keysFor = (name: string): crypto.KeyObject[] => {
    const owned = this.owners.some((o) => o.name === name);
    return this.keys.filter((k) => !owned || !k.owner || k.owner === name).map((k) => k.key);
  };

  /** The loaded extensions whose bundles are held to their versions. Replaces the previous set. */
  setOwners(owners: readonly UiBundleOwner[]): void {
    this.owners = [...owners];
    this.candidates = null;
  }

  /** Verify a bundle and copy it into the store. Returns its manifest. */
  install(sourceDir: string): UiManifest {
    const manifest = verifyBundle(sourceDir, this.keysFor);
    const target = path.join(this.root, `${manifest.name}-${manifest.version}`.replace(/[^A-Za-z0-9._-]/g, "_"));
    const staging = `${target}.staging-${process.pid}`;
    fs.rmSync(staging, { recursive: true, force: true });
    fs.cpSync(sourceDir, staging, { recursive: true });
    verifyBundle(staging, this.keysFor);
    fs.rmSync(target, { recursive: true, force: true });
    fs.renameSync(staging, target);
    this.candidates = null;
    return manifest;
  }

  /** Installed bundles that verify and support this daemon's API. */
  private verified(): UiBundle[] {
    if (this.candidates) return this.candidates;
    const candidates: UiBundle[] = [];
    if (fs.existsSync(this.root) && this.keys.length > 0) {
      for (const e of fs.readdirSync(this.root, { withFileTypes: true })) {
        if (!e.isDirectory() || e.name.includes(".staging-")) continue;
        const dir = path.join(this.root, e.name);
        try {
          const manifest = verifyBundle(dir, this.keysFor);
          if (manifest.api.min <= this.apiVersion && this.apiVersion <= manifest.api.max) candidates.push({ manifest, dir });
        } catch {
          // Not ours, tampered, or for a key no extension trusts: never served.
        }
      }
    }
    this.candidates = candidates;
    return candidates;
  }

  /** The bundle served at /ui/ (selectBundle), or null. */
  active(): UiBundle | null {
    return selectBundle(this.verified(), this.owners);
  }

  /** The bundle a loaded extension serves at /ui/ext/<name>/ (selectOwnerBundle), or null. */
  own(name: string): UiBundle | null {
    const owner = this.owners.find((o) => o.name === name);
    return owner ? selectOwnerBundle(this.verified(), owner) : null;
  }

  /** Every loaded extension's own bundle, in load order, with the base it is served at. */
  extensionBundles(): UiBundleInfo[] {
    return this.owners.flatMap((o) => {
      const b = this.own(o.name);
      if (!b) return [];
      return [{ name: o.name, version: b.manifest.version, base: `/ui/ext/${encodeURIComponent(o.name)}/`, ...(b.manifest.module ? { module: b.manifest.module } : {}) }];
    });
  }

  /** The file a /ui/ or /ui/ext/<name>/ path names inside the bundle served there, or null. */
  resolve(urlPath: string): string | null {
    const ext = /^\/ui\/ext\/([^/]+)(?:\/(.*))?$/.exec(urlPath);
    if (!ext && /^\/ui\/ext(\/|$)/.test(urlPath)) return null;
    const bundle = ext ? this.own(decodeURIComponent(ext[1])) : this.active();
    if (!bundle) return null;
    let rel = decodeURIComponent(ext ? (ext[2] ?? "") : urlPath.replace(/^\/ui\/?/, ""));
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
  meta: { name: string; version: string; api: { min: number; max: number }; module?: { entry: string; host: string } },
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
