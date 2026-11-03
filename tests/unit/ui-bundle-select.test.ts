/**
 * Which UI bundle the daemon serves (#1618): a bundle named for a loaded
 * extension is served at that extension's version, or else the newest one the
 * extension declares compatible, and never one newer than the extension.
 */
import { describe, expect, it } from "vitest";
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { selectBundle, selectOwnerBundle, signBundle, UiBundleStore, verifyBundle, type UiManifest } from "../../src/daemon/ui-bundles.js";
import { DAEMON_API_VERSION } from "../../src/daemon/discovery.js";

const b = (name: string, version: string) => ({ manifest: { name, version, api: { min: 1, max: 1 }, files: {} } as UiManifest });
const versions = (xs: Array<{ manifest: UiManifest }> | null) => xs?.map((x) => `${x.manifest.name}@${x.manifest.version}`);
const pick = (cs: ReturnType<typeof b>[], owners: Parameters<typeof selectBundle>[1]) => {
  const s = selectBundle(cs, owners);
  return s ? `${s.manifest.name}@${s.manifest.version}` : null;
};

describe("selectBundle", () => {
  const installed = [b("studio", "0.1.23"), b("studio", "0.1.24-beta.3"), b("studio", "0.1.24-beta.19"), b("studio", "0.1.22")];

  it("serves the newest bundle when no extension is loaded", () => {
    expect(pick(installed, [])).toBe("studio@0.1.24-beta.19");
  });

  it("serves the bundle at the loaded extension's version, not a newer one", () => {
    expect(pick(installed, [{ name: "studio", version: "0.1.23" }])).toBe("studio@0.1.23");
    expect(pick(installed, [{ name: "studio", version: "0.1.24-beta.3" }])).toBe("studio@0.1.24-beta.3");
  });

  it("without an exact match, serves the newest older bundle", () => {
    expect(pick(installed, [{ name: "studio", version: "0.1.24-beta.10" }])).toBe("studio@0.1.24-beta.3");
  });

  it("never serves a bundle older than the extension's declared minimum", () => {
    expect(pick(installed, [{ name: "studio", version: "0.1.24-beta.10", uiMin: "0.1.24-beta.1" }])).toBe("studio@0.1.24-beta.3");
    expect(pick(installed, [{ name: "studio", version: "0.1.24-beta.2", uiMin: "0.1.24-beta.1" }])).toBeNull();
  });

  it("serves nothing rather than a newer bundle when every one of its bundles is newer", () => {
    expect(pick(installed, [{ name: "studio", version: "0.1.21" }])).toBeNull();
  });

  it("ranks a release above its own prereleases within the extension's range", () => {
    const cs = [b("studio", "0.1.24-beta.19"), b("studio", "0.1.24"), b("studio", "0.1.25-beta.1")];
    expect(pick(cs, [{ name: "studio", version: "0.1.24" }])).toBe("studio@0.1.24");
  });

  it("prefers the extension's own bundles and leaves other names unconstrained", () => {
    const cs = [...installed, b("other", "9.0.0")];
    expect(pick(cs, [{ name: "studio", version: "0.1.23" }])).toBe("studio@0.1.23");
    // No studio bundle qualifies, so a bundle no loaded extension names is used.
    expect(pick(cs, [{ name: "studio", version: "0.1.0" }])).toBe("other@9.0.0");
    // A loaded extension that ships no bundle holds nothing back.
    expect(pick(cs, [{ name: "probe", version: "0.0.1" }])).toBe("other@9.0.0");
  });

  it("does not reorder its input", () => {
    const cs = [...installed];
    selectBundle(cs, []);
    expect(versions(cs)).toEqual(versions(installed));
  });
});

describe("UiBundleStore with an extension loaded", () => {
  it("serves the bundle at the extension's version and follows a change of extension", () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-ui-select-"));
    try {
      const keys = crypto.generateKeyPairSync("ed25519");
      const pem = keys.privateKey.export({ type: "pkcs8", format: "pem" }).toString();
      const store = new UiBundleStore(DAEMON_API_VERSION, path.join(root, "ui"));
      store.trustKey(keys.publicKey.export({ type: "spki", format: "pem" }).toString());
      for (const v of ["1.0.0", "1.1.0", "2.0.0"]) {
        const dir = path.join(root, "build", v);
        fs.mkdirSync(dir, { recursive: true });
        fs.writeFileSync(path.join(dir, "index.html"), v);
        signBundle(dir, { name: "studio", version: v, api: { min: DAEMON_API_VERSION, max: DAEMON_API_VERSION } }, pem);
        store.install(dir);
      }
      expect(store.active()?.manifest.version).toBe("2.0.0");
      store.setOwners([{ name: "studio", version: "1.1.0" }]);
      expect(store.active()?.manifest.version).toBe("1.1.0");
      expect(fs.readFileSync(store.resolve("/ui/")!, "utf-8")).toBe("1.1.0");
      store.setOwners([{ name: "studio", version: "1.5.0" }]);
      expect(store.active()?.manifest.version).toBe("1.1.0");
      store.setOwners([]);
      expect(store.active()?.manifest.version).toBe("2.0.0");
    } finally {
      fs.rmSync(root, { recursive: true, force: true });
    }
  });
});

describe("UiBundleStore keys scoped to their extension", () => {
  const pair = () => {
    const k = crypto.generateKeyPairSync("ed25519");
    return { pub: k.publicKey.export({ type: "spki", format: "pem" }).toString(), priv: k.privateKey.export({ type: "pkcs8", format: "pem" }).toString() };
  };
  const api = { min: DAEMON_API_VERSION, max: DAEMON_API_VERSION };
  const build = (root: string, name: string, version: string, priv: string) => {
    const dir = path.join(root, "build", `${name}-${version}`);
    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(path.join(dir, "index.html"), `${name}@${version}`);
    signBundle(dir, { name, version, api }, priv);
    return dir;
  };
  const withStore = (fn: (root: string, store: UiBundleStore) => void) => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-ui-keys-"));
    try {
      const store = new UiBundleStore(DAEMON_API_VERSION, path.join(root, "ui"));
      store.setOwners([{ name: "studio", version: "1.0.0" }, { name: "pro", version: "1.0.0" }]);
      fn(root, store);
    } finally {
      fs.rmSync(root, { recursive: true, force: true });
    }
  };

  it("a key trusted by pro cannot install or serve a bundle named studio", () => {
    withStore((root, store) => {
      const pro = pair();
      store.trustKey(pro.pub, "pro");
      const forged = build(root, "studio", "1.0.0", pro.priv);
      expect(() => store.install(forged)).toThrow(/no trusted UI signing key is registered for studio/);
      // Placed in the store by hand, it is still never served.
      fs.cpSync(forged, path.join(root, "ui", "studio-1.0.0"), { recursive: true });
      expect(store.active()).toBeNull();
      // Once studio trusts its own key, a bundle pro signed still does not verify.
      store.trustKey(pair().pub, "studio");
      expect(() => store.install(forged)).toThrow(/signature/);
      expect(store.active()).toBeNull();
    });
  });

  it("a bundle no loaded extension names verifies with any trusted key", () => {
    withStore((root, store) => {
      const pro = pair();
      store.trustKey(pro.pub, "pro");
      expect(store.install(build(root, "free-ui", "2.0.0", pro.priv)).name).toBe("free-ui");
      expect(store.active()?.manifest.name).toBe("free-ui");
    });
  });

  it("an extension's own key installs and serves its own bundle", () => {
    withStore((root, store) => {
      const studio = pair();
      store.trustKey(studio.pub, "studio");
      store.install(build(root, "studio", "1.0.0", studio.priv));
      expect(fs.readFileSync(store.resolve("/ui/")!, "utf-8")).toBe("studio@1.0.0");
    });
  });

  it("a key the daemon trusts with no owner verifies every bundle", () => {
    withStore((root, store) => {
      const any = pair();
      store.trustKey(any.pub);
      expect(store.install(build(root, "studio", "1.0.0", any.priv)).name).toBe("studio");
      expect(store.install(build(root, "free-ui", "1.0.0", any.priv)).name).toBe("free-ui");
    });
  });
});

describe("one bundle per extension, plus a root", () => {
  const studio = { name: "studio", version: "0.1.19" };
  const pro = { name: "pro", version: "1.0.0", root: false };
  const cs = [b("studio", "0.1.18"), b("studio", "0.1.19"), b("studio", "0.1.20"), b("pro", "0.9.0"), b("pro", "1.0.0"), b("pro", "1.1.0"), b("free", "5.0.0")];

  it("serves a root:false extension's bundle only at its own base, never at /ui/", () => {
    expect(pick(cs, [studio, pro])).toBe("studio@0.1.19");
    // Without root:false the newer pro bundle takes /ui/, which is why the flag exists.
    expect(pick(cs, [studio, { name: "pro", version: "1.0.0" }])).toBe("pro@1.0.0");
    // With no root owner left, /ui/ falls back to free bundles, never to pro's.
    expect(pick(cs, [pro])).toBe("free@5.0.0");
  });

  it("keeps each extension to its own version window", () => {
    const own = (o: Parameters<typeof selectOwnerBundle>[1]) => selectOwnerBundle(cs, o)?.manifest.version ?? null;
    expect(own(studio)).toBe("0.1.19");
    expect(own(pro)).toBe("1.0.0");
    expect(own({ name: "pro", version: "1.0.0", uiMin: "1.0.0" })).toBe("1.0.0");
    expect(own({ name: "pro", version: "0.8.0" })).toBeNull();
    expect(own({ name: "nobody", version: "1.0.0" })).toBeNull();
  });

  it("resolves /ui/ext/<name>/ inside that extension's bundle and nowhere else", () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-ui-ext-"));
    try {
      const keys = crypto.generateKeyPairSync("ed25519");
      const pem = keys.privateKey.export({ type: "pkcs8", format: "pem" }).toString();
      const store = new UiBundleStore(DAEMON_API_VERSION, path.join(root, "ui"));
      store.trustKey(keys.publicKey.export({ type: "spki", format: "pem" }).toString());
      for (const [name, version] of [["studio", "0.1.19"], ["studio", "0.1.20"], ["pro", "1.0.0"], ["pro", "1.1.0"]]) {
        const dir = path.join(root, "build", `${name}-${version}`);
        fs.mkdirSync(dir, { recursive: true });
        fs.writeFileSync(path.join(dir, "index.html"), `${name}@${version}`);
        fs.writeFileSync(path.join(dir, "index.js"), `export const v = "${name}@${version}";`);
        signBundle(dir, { name, version, api: { min: DAEMON_API_VERSION, max: DAEMON_API_VERSION }, ...(name === "pro" ? { module: { entry: "index.js", host: "studio" } } : {}) }, pem);
        store.install(dir);
      }
      store.setOwners([studio, pro]);
      const read = (url: string) => { const f = store.resolve(url); return f ? fs.readFileSync(f, "utf-8") : null; };
      expect(read("/ui/")).toBe("studio@0.1.19");
      expect(read("/ui/ext/pro/index.js")).toBe('export const v = "pro@1.0.0";');
      expect(read("/ui/ext/pro")).toBe("pro@1.0.0");
      expect(read("/ui/ext/studio/")).toBe("studio@0.1.19");
      expect(read("/ui/ext/nobody/index.html")).toBeNull();
      expect(read("/ui/ext/")).toBeNull();
      expect(read("/ui/ext/pro/manifest.json")).toBeNull();
      expect(store.own("pro")?.manifest.module).toEqual({ entry: "index.js", host: "studio" });
      expect(store.extensionBundles()).toEqual([
        { name: "studio", version: "0.1.19", base: "/ui/ext/studio/" },
        { name: "pro", version: "1.0.0", base: "/ui/ext/pro/", module: { entry: "index.js", host: "studio" } },
      ]);
    } finally {
      fs.rmSync(root, { recursive: true, force: true });
    }
  });

  it("signs the module, so changing it after signing fails verification", () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-ui-mod-"));
    try {
      const keys = crypto.generateKeyPairSync("ed25519");
      const pem = keys.privateKey.export({ type: "pkcs8", format: "pem" }).toString();
      const pub = keys.publicKey.export({ type: "spki", format: "pem" }).toString();
      const dir = path.join(root, "pro");
      fs.mkdirSync(dir, { recursive: true });
      fs.writeFileSync(path.join(dir, "index.js"), "export {}");
      fs.writeFileSync(path.join(dir, "other.js"), "export {}");
      const api = { min: DAEMON_API_VERSION, max: DAEMON_API_VERSION };
      signBundle(dir, { name: "pro", version: "1.0.0", api, module: { entry: "index.js", host: "studio" } }, pem);
      const key = crypto.createPublicKey(pub);
      expect(verifyBundle(dir, [key]).module).toEqual({ entry: "index.js", host: "studio" });
      const manifestPath = path.join(dir, "manifest.json");
      const signed = fs.readFileSync(manifestPath, "utf-8");
      fs.writeFileSync(manifestPath, signed.replace('"host": "studio"', '"host": "other"'));
      expect(() => verifyBundle(dir, [key])).toThrow(/signature/);
      fs.writeFileSync(manifestPath, signed.replace('"entry": "index.js"', '"entry": "other.js"'));
      expect(() => verifyBundle(dir, [key])).toThrow(/signature/);
      // A module whose entry is not a listed file is refused even when signed.
      signBundle(dir, { name: "pro", version: "1.0.0", api, module: { entry: "missing.js", host: "studio" } }, pem);
      expect(() => verifyBundle(dir, [key])).toThrow(/module must name/);
    } finally {
      fs.rmSync(root, { recursive: true, force: true });
    }
  });
});
