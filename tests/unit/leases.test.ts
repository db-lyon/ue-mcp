import { describe, expect, it } from "vitest";
import { LeaseRegistry, normalizeLeasePath, resolveLeaseConfig } from "../../src/dispatch/leases.js";
import { leasePathsOf } from "../../src/dispatch/lease-paths.js";
import { McpError } from "../../src/core/errors.js";

const A = { id: "session-a", label: "client A" };
const B = { id: "session-b", label: "client B" };

function registry(now = 1_000_000): LeaseRegistry {
  const r = new LeaseRegistry();
  r.now = () => now;
  return r;
}

describe("lease paths", () => {
  it("reduces an object path to its package and keeps a folder's trailing slash", () => {
    expect(normalizeLeasePath("/Game/A/BP_Hero.BP_Hero")).toBe("/Game/A/BP_Hero");
    expect(normalizeLeasePath("/Game/A/BP_Hero.BP_Hero:Sub")).toBe("/Game/A/BP_Hero");
    expect(normalizeLeasePath("\\Game\\A\\M_Rock")).toBe("/Game/A/M_Rock");
    expect(normalizeLeasePath("/Game/Characters/")).toBe("/Game/Characters/");
    expect(normalizeLeasePath("/Game/Characters/*")).toBe("/Game/Characters/");
    expect(normalizeLeasePath("C:/Projects/X")).toBeNull();
    expect(normalizeLeasePath("//server/share")).toBeNull();
    expect(normalizeLeasePath(42)).toBeNull();
  });

  it("finds the asset paths a mutating call names, and none for a read", () => {
    expect(leasePathsOf("blueprint.add_variable", { blueprintPath: "/Game/BP_Hero.BP_Hero", name: "Speed" })).toEqual(["/Game/BP_Hero"]);
    expect(leasePathsOf("asset.delete_batch", { assetPaths: ["/Game/A", "/Game/B"] }).sort()).toEqual(["/Game/A", "/Game/B"]);
    expect(leasePathsOf("asset.list", { path: "/Game/A" })).toEqual([]);
    expect(leasePathsOf("project.acquire_lease", { paths: ["/Game/A"] })).toEqual([]);
  });

  it("does not mistake a class path or a POSIX file path for an asset", () => {
    expect(leasePathsOf("blueprint.create", { assetPath: "/Game/BP_New", parentClassPath: "/Script/Engine.Actor" })).toEqual(["/Game/BP_New"]);
    expect(leasePathsOf("asset.import_static_mesh", { assetPath: "/Game/Imported/Mesh", sourceFilePath: "/home/me/mesh.fbx" })).toEqual(["/Game/Imported/Mesh"]);
  });
});

describe("the lease registry", () => {
  it("defaults to auto with a ten minute lease, and caps a long one", () => {
    expect(resolveLeaseConfig()).toEqual({ mode: "auto", ttlSeconds: 600 });
    expect(resolveLeaseConfig({ mode: "off", ttlSeconds: 10 })).toEqual({ mode: "off", ttlSeconds: 10 });
    expect(resolveLeaseConfig({ mode: "nonsense" as never }).mode).toBe("auto");
    expect(resolveLeaseConfig({ ttlSeconds: 10 ** 9 }).ttlSeconds).toBe(86_400);
  });

  it("refuses another session's claim, naming the holder and when it runs out", () => {
    const r = registry();
    r.acquire("ed", ["/Game/BP_Hero"], A, 60);
    expect(r.conflict("ed", ["/Game/BP_Hero"], A.id)).toBeNull();
    const hit = r.conflict("ed", ["/game/bp_hero"], B.id);
    expect(hit?.lease.holder).toBe(A.id);
    try {
      r.acquire("ed", ["/Game/BP_Hero", "/Game/Other"], B);
      expect.fail("a held path must refuse the whole request");
    } catch (e) {
      expect(e).toBeInstanceOf(McpError);
      expect((e as McpError).code).toBe("ASSET_LEASED");
      expect((e as McpError).message).toContain("client A");
      expect((e as McpError).message).toContain(new Date(1_000_000 + 60_000).toISOString());
    }
    // All or none: the free path was not taken either.
    expect(r.list().map((l) => l.path)).toEqual(["/Game/BP_Hero"]);
  });

  it("keeps editors apart, covers a folder, and lets a lease run out", () => {
    let now = 0;
    const r = new LeaseRegistry();
    r.now = () => now;
    r.acquire("one", ["/Game/Characters/"], A, 30);
    expect(r.conflict("two", ["/Game/Characters/BP_Hero"], B.id)).toBeNull();
    expect(r.conflict("one", ["/Game/Characters/BP_Hero"], B.id)?.lease.path).toBe("/Game/Characters/");
    expect(r.conflict("one", ["/Game/Characters"], B.id)).not.toBeNull();
    now = 31_000;
    expect(r.conflict("one", ["/Game/Characters/BP_Hero"], B.id)).toBeNull();
    expect(r.list()).toEqual([]);
  });

  it("releases only the holder's own leases, and everything a session held when it ends", () => {
    const r = registry();
    r.acquire("ed", ["/Game/A", "/Game/B"], A);
    r.acquire("ed", ["/Game/C"], B);
    expect(r.release(B.id, "ed", ["/Game/A"])).toEqual([]);
    expect(r.release(A.id, "ed", ["/Game/A"]).map((l) => l.path)).toEqual(["/Game/A"]);
    expect(r.releaseHolder(A.id)).toBe(1);
    expect(r.list().map((l) => l.holder)).toEqual([B.id]);
  });

  it("takes implicit leases only with more than one session in auto, always in on, never in off", () => {
    const r = registry();
    expect(r.implicit).toBe(false);
    r.liveSessions = () => 2;
    expect(r.implicit).toBe(true);
    r.config = { mode: "on", ttlSeconds: 600 };
    r.liveSessions = () => 1;
    expect(r.implicit).toBe(true);
    r.config = { mode: "off", ttlSeconds: 600 };
    expect(r.enforcing).toBe(false);
    expect(r.implicit).toBe(false);
  });

  it("never shortens an explicit lease when the holder's own change renews it", () => {
    const r = registry();
    r.acquire("ed", ["/Game/A"], A, 3600);
    const [renewed] = r.acquire("ed", ["/Game/A"], A, 60, true);
    expect(renewed.expiresAt).toBe(1_000_000 + 3_600_000);
    expect(renewed.implicit).toBe(false);
  });
});
