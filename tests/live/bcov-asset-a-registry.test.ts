// @live-editor headless
/**
 * Folders, the asset registry and asset hygiene, read back through list_assets
 * and the registry queries: create/delete folders, referencers and
 * dependencies, health and registry diagnosis, renames, redirector fix-up,
 * and the naming audit and fix. Refs #1223
 */
import { mkdirSync, rmSync, writeFileSync } from "node:fs";
import { randomUUID } from "node:crypto";
import * as path from "node:path";
import { TEST_PROJECT_DIR } from "../../scripts/bridge-target.mjs";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { callBridge, disconnectBridge, getBridge, type TestResult } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

const DIR = "/Game/MCPTest/BCov/AssetA/Registry";
const PARENT = `${DIR}/M_BCovParent`;
const CHILD = `${DIR}/MI_BCovChild`;
const MOVED = `${DIR}/M_BCovParentMoved`;
const NAMING_DIR = `${DIR}/Naming`;
// A local collection holding the parent. The editor polls this folder each second,
// and a collected asset always keeps a redirector at its old path when renamed.
const COLLECTION_DIR = `${path.join(TEST_PROJECT_DIR, "Saved", "Collections")}${path.sep}`;
const COLLECTION_FILE = `${COLLECTION_DIR}BCovAssetARegistry.collection`;

type Json = Record<string, unknown>;
const body = (r: TestResult): Json => (r.result && typeof r.result === "object" ? (r.result as Json) : {});
const succeeded = (r: TestResult): boolean => r.ok && body(r).success !== false;

let bridge: EditorBridge;

async function call(method: string, params: Json): Promise<Json> {
  const r = await callBridge(bridge, method, params);
  expect(succeeded(r), `${method}: ${r.error ?? JSON.stringify(r.result)}`).toBe(true);
  return body(r);
}

async function listNames(directory: string, classFilter?: string): Promise<string[]> {
  const r = await call("list_assets", { directory, recursive: false, limit: 500, ...(classFilter ? { classFilter } : {}) });
  return ((r.assets as Array<{ name: string }>) ?? []).map((a) => a.name);
}

beforeAll(async () => {
  bridge = await getBridge();
  await callBridge(bridge, "delete_folder", { path: DIR, force: true });
  await call("create_folder", { path: DIR });
  await call("create_material", { name: "M_BCovParent", packagePath: DIR });
  await call("create_material_instance", { name: "MI_BCovChild", packagePath: DIR, parentPath: PARENT });
  await call("save_asset", { assetPaths: [PARENT, CHILD] });
}, 180_000);

afterAll(async () => {
  rmSync(COLLECTION_FILE, { force: true });
  if (bridge) {
    await callBridge(bridge, "delete_folder", { path: DIR, force: true });
    disconnectBridge();
  }
}, 120_000);

describe("folders", () => {
  it("create_folder makes a folder that delete_folder removes", async () => {
    const folder = `${DIR}/Scratch/Nested`;
    const created = await call("create_folder", { path: folder });
    expect(created.createdCount).toBe(1);
    expect(created.created).toContain(folder);

    const again = await call("create_folder", { path: folder });
    expect(again.existedCount).toBe(1);
    expect(again.createdCount).toBe(0);

    const deleted = await call("delete_folder", { path: folder });
    expect(deleted.deletedCount).toBe(1);
    expect((deleted.entries as Array<{ status: string }>)[0].status).toBe("deleted");

    // Gone: creating it now is a create again, not an existed.
    const recreated = await call("create_folder", { path: folder });
    expect(recreated.createdCount).toBe(1);
    await call("delete_folder", { path: `${DIR}/Scratch`, force: true });
  }, 120_000);
});

describe("registry queries", () => {
  it("get_asset_referencers and get_asset_dependencies see the instance's parent link", async () => {
    const refs = await call("get_asset_referencers", { packagePath: PARENT });
    const byPkg = refs.referencersByPackage as Record<string, string[]>;
    expect(byPkg[PARENT]).toContain(CHILD);

    const deps = await call("get_asset_dependencies", { packagePath: CHILD });
    const depsByPkg = deps.dependenciesByPackage as Record<string, string[]>;
    expect(depsByPkg[CHILD]).toContain(PARENT);
    expect(Number(deps.totalDependencies)).toBeGreaterThan(0);
  }, 120_000);

  it("asset_health_check and diagnose_registry track an asset through its deletion", async () => {
    const doomed = `${DIR}/M_BCovDoomed`;
    await call("create_material", { name: "M_BCovDoomed", packagePath: DIR });
    await call("save_asset", { assetPath: doomed });

    const healthy = await call("asset_health_check", { assetPath: doomed });
    expect(healthy.onDisk).toBe(true);
    expect(healthy.inRegistry).toBe(true);
    expect(healthy.canLoad).toBe(true);
    expect(healthy.isStuck).toBe(false);
    expect(healthy.class).toBe("Material");

    const before = await call("diagnose_registry", { path: DIR, recursive: false, reconcile: true });
    expect(Number(before.onDiskCount)).toBeGreaterThanOrEqual(3);

    await call("delete_asset", { assetPath: doomed, force: true });
    const gone = await call("asset_health_check", { assetPath: doomed });
    expect(gone.onDisk).toBe(false);

    const after = await call("diagnose_registry", { path: DIR, recursive: false, reconcile: true });
    expect(Number(after.onDiskCount)).toBe(Number(before.onDiskCount) - 1);
    expect((after.ghostPaths as string[]).join(" ")).not.toContain("M_BCovDoomed");
  }, 120_000);
});

describe("asset hygiene", () => {
  it("audit_asset_hygiene reports a misnamed asset and fix_asset_hygiene renames it", async () => {
    await call("create_folder", { path: NAMING_DIR });
    await call("create_material", { name: "BCovBadName", packagePath: NAMING_DIR });
    await call("save_asset", { assetPath: `${NAMING_DIR}/BCovBadName` });

    const audit = await call("audit_asset_hygiene", { directory: NAMING_DIR, checks: ["naming"] });
    const naming = audit.naming as { count: number; assets: Array<{ name: string; suggestedName: string; expectedPrefix: string }> };
    const finding = naming.assets.find((a) => a.name === "BCovBadName");
    expect(finding, "the audit names the misnamed material").toBeDefined();
    expect(finding!.expectedPrefix).toBe("M_");
    expect(finding!.suggestedName).toBe("M_BCovBadName");

    const fixed = await call("fix_asset_hygiene", {
      fix: "naming", assetPaths: [`${NAMING_DIR}/BCovBadName`], directory: NAMING_DIR, dryRun: false,
    });
    const items = fixed.items as Array<{ name: string; status: string }>;
    expect(items.find((i) => i.name === "BCovBadName")?.status).toBe("renamed");

    const names = await listNames(NAMING_DIR);
    expect(names).toContain("M_BCovBadName");
    expect(names).not.toContain("BCovBadName");

    const reaudit = await call("audit_asset_hygiene", { directory: NAMING_DIR, checks: ["naming"] });
    expect((reaudit.naming as { count: number }).count).toBe(0);
  }, 180_000);

  it("bulk_rename_assets moves a referenced material and fixup_redirectors removes the redirector it leaves", async () => {
    mkdirSync(COLLECTION_DIR, { recursive: true });
    writeFileSync(COLLECTION_FILE, [
      "FileVersion:2", "Type:Static", `Guid:${randomUUID().toUpperCase()}`,
      "ParentGuid:00000000-0000-0000-0000-000000000000", "", `${PARENT}.M_BCovParent`, "",
    ].join("\r\n"));
    let renamed: Json;
    try {
      await new Promise((r) => setTimeout(r, 4000));
      renamed = await call("bulk_rename_assets", { renames: [{ sourcePath: PARENT, destinationPath: MOVED }] });
    } finally {
      rmSync(COLLECTION_FILE, { force: true });
    }
    expect(renamed.renamed).toBe(1);
    expect((renamed.results as Array<{ status: string }>)[0].status).toBe("renamed");

    const names = await listNames(DIR);
    expect(names).toContain("M_BCovParentMoved");

    expect(Number(renamed.redirectorsLeft), "a redirector to fix up").toBeGreaterThan(0);
    const audit = await call("audit_asset_hygiene", { directory: DIR, recursive: false, checks: ["redirectors"] });
    expect((audit.redirectors as { count: number }).count).toBeGreaterThan(0);

    const fixup = await call("fixup_redirectors", { paths: [DIR] });
    expect(Number(fixup.redirectorCount)).toBeGreaterThan(0);
    // The rename already repointed the loaded referencer; the collection is what held the redirector, so the
    // effect here is the redirector being deleted, with every referencer pointing at the destination below.
    expect(Number(fixup.deletedRedirectorCount)).toBeGreaterThan(0);

    expect(await listNames(DIR, "ObjectRedirector")).toHaveLength(0);
    const deps = await call("get_asset_dependencies", { packagePath: CHILD });
    expect((deps.dependenciesByPackage as Record<string, string[]>)[CHILD]).toContain(MOVED);
    const reaudit = await call("audit_asset_hygiene", { directory: DIR, recursive: false, checks: ["redirectors"] });
    expect((reaudit.redirectors as { count: number }).count).toBe(0);
  }, 300_000);
});

describe("saving", () => {
  it("save_asset force rewrites a clean asset saved the same second", async () => {
    const path = `${DIR}/M_BCovResave`;
    await call("create_material", { name: "M_BCovResave", packagePath: DIR });
    const saved = await call("save_asset", { assetPaths: [path], force: true });
    expect(saved.savedCount).toBe(1);
    expect(saved.failedCount).toBe(0);
    const row = (saved.results as Array<{ status: string; wasDirty: boolean; existsOnDisk: boolean }>)[0];
    expect(row.wasDirty).toBe(false);
    expect(row.status).toBe("saved");
    expect(row.existsOnDisk).toBe(true);
    await call("delete_asset", { assetPath: path, force: true });
  }, 120_000);
});
