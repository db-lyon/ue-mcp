// Regression: #820 - writing a property whose value is a TMap with a struct
// key emptied the map and reported success. Reading a value and writing it
// straight back is the workflow that destroyed the data, so that is what this
// exercises, end to end, counting pairs at every step.
//
// The fixture class lives in the test project (UMCPStructKeyMapFixture), which
// holds a TMap<FMCPTestMapKey, TSoftObjectPtr<UObject>> both inside a wrapping
// struct (Config) and reachable directly (Config.Entries).
import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { getBridge, disconnectBridge, callBridge } from "../setup.js";
import type { EditorBridge } from "../../src/bridge/bridge.js";

let bridge: EditorBridge;

const FIXTURE_CLASS = "/Script/ue_mcp.MCPStructKeyMapFixture";
const PACKAGE_PATH = "/Game/MCPTest";
const ASSET_NAME = "DA_MCP820_StructKeyMap";
const ASSET_PATH = `${PACKAGE_PATH}/${ASSET_NAME}.${ASSET_NAME}`;

/**
 * Two pairs, in the [{key, value}] shape a struct key needs. The tag half is
 * filled from tags the project already has: reflection(create_tag) writes the
 * ini and only takes effect on the next editor start, and the key is a struct
 * either way, which is what this test is about. Slot keeps the keys distinct.
 */
let TWO_ENTRIES: Array<{ key: { Tag: string; Slot: number }; value: string }> = [];

function asRecord(value: unknown): Record<string, unknown> {
  return (value ?? {}) as Record<string, unknown>;
}

/** Pair count of the Entries map as the read side reports it. */
function entryCount(propertyValue: unknown): number {
  const entries = asRecord(propertyValue).Entries;
  return Array.isArray(entries) ? entries.length : -1;
}

async function readConfig(): Promise<Record<string, unknown>> {
  const r = await callBridge(bridge, "get_property", { objectPath: ASSET_PATH, propertyName: "Config" });
  expect(r.ok, r.error).toBe(true);
  return asRecord(r.result);
}

beforeAll(async () => {
  bridge = await getBridge();

  const tagList = await callBridge(bridge, "list_gameplay_tags", {});
  const known = (asRecord(tagList.result).tags as unknown[] | undefined) ?? [];
  const tagA = typeof known[0] === "string" ? known[0] : "";
  const tagB = typeof known[1] === "string" ? known[1] : "";
  TWO_ENTRIES = [
    { key: { Tag: tagA, Slot: 1 }, value: "/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial" },
    { key: { Tag: tagB, Slot: 2 }, value: "/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial" },
  ];

  const created = await callBridge(bridge, "create_asset_by_class", {
    name: ASSET_NAME,
    className: FIXTURE_CLASS,
    packagePath: PACKAGE_PATH,
    onConflict: "replace",
  });
  expect(created.ok, created.error).toBe(true);
  expect(
    asRecord(created.result).success,
    `fixture class ${FIXTURE_CLASS} is missing - rebuild the test project C++ before running this test`,
  ).not.toBe(false);
});

afterAll(async () => {
  if (bridge) await callBridge(bridge, "delete_asset", { assetPath: ASSET_PATH, force: true });
  disconnectBridge();
});

describe("editor(set_property) - struct-keyed TMap (#820)", () => {
  it("writes a struct-keyed map through the wrapping struct and stores every pair", async () => {
    const w = await callBridge(bridge, "set_property", {
      objectPath: ASSET_PATH,
      propertyName: "Config",
      value: { Entries: TWO_ENTRIES, Revision: 1 },
    });
    expect(w.ok, w.error).toBe(true);
    expect(asRecord(w.result).success).not.toBe(false);
    expect(asRecord(w.result).mapPairCount).toBe(2);

    const read = await readConfig();
    expect(read.mapPairCount).toBe(2);
    expect(entryCount(read.value)).toBe(2);
  });

  it("reads a value and writes it straight back without losing a pair", async () => {
    const before = await readConfig();
    expect(entryCount(before.value)).toBe(2);

    const rewrite = await callBridge(bridge, "set_property", {
      objectPath: ASSET_PATH,
      propertyName: "Config",
      value: before.value,
    });
    expect(rewrite.ok, rewrite.error).toBe(true);
    expect(asRecord(rewrite.result).success).not.toBe(false);
    expect(asRecord(rewrite.result).mapPairCount).toBe(2);

    const after = await readConfig();
    expect(after.mapPairCount).toBe(2);
    expect(entryCount(after.value)).toBe(2);
  });

  it("never empties the map when handed its own export text back", async () => {
    const before = await readConfig();
    const valueText = before.valueText as string;
    expect(typeof valueText).toBe("string");

    const w = await callBridge(bridge, "set_property", {
      objectPath: ASSET_PATH,
      propertyName: "Config",
      value: valueText,
    });

    // Either the text imports faithfully or the write is refused. The one
    // outcome this test exists to forbid is success with an emptied map.
    const result = asRecord(w.result);
    const succeeded = w.ok && result.success !== false;
    if (succeeded) {
      expect(result.mapPairCount).toBe(2);
    }

    const after = await readConfig();
    expect(after.mapPairCount).toBe(2);
    expect(entryCount(after.value)).toBe(2);
  });

  it("writes the inner map directly, by its dotted path", async () => {
    const w = await callBridge(bridge, "set_property", {
      objectPath: ASSET_PATH,
      propertyName: "Config.Entries",
      value: TWO_ENTRIES,
    });
    expect(w.ok, w.error).toBe(true);
    expect(asRecord(w.result).success).not.toBe(false);
    expect(asRecord(w.result).elementCount).toBe(2);

    const after = await readConfig();
    expect(entryCount(after.value)).toBe(2);
  });

  it("rejects a duplicate key and leaves the stored map untouched", async () => {
    const w = await callBridge(bridge, "set_property", {
      objectPath: ASSET_PATH,
      propertyName: "Config.Entries",
      value: [TWO_ENTRIES[0], TWO_ENTRIES[0]],
    });
    const failed = !w.ok || asRecord(w.result).success === false;
    expect(failed).toBeTruthy();

    const after = await readConfig();
    expect(after.mapPairCount).toBe(2);
  });

  it("rejects an unusable key and leaves the stored map untouched", async () => {
    const w = await callBridge(bridge, "set_property", {
      objectPath: ASSET_PATH,
      propertyName: "Config.Entries",
      value: [{ key: { Tag: "MCP.Test.Map.NoSuchTagAnywhere", Slot: 9 }, value: "/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial" }],
    });
    const failed = !w.ok || asRecord(w.result).success === false;
    expect(failed).toBeTruthy();

    const after = await readConfig();
    expect(after.mapPairCount).toBe(2);
    expect(entryCount(after.value)).toBe(2);
  });

  it("clears the map only when asked, with an empty list", async () => {
    const w = await callBridge(bridge, "set_property", {
      objectPath: ASSET_PATH,
      propertyName: "Config.Entries",
      value: [],
    });
    expect(w.ok, w.error).toBe(true);
    expect(asRecord(w.result).elementCount).toBe(0);

    const after = await readConfig();
    expect(after.mapPairCount).toBe(0);

    // Put the pairs back for any later assertion in this file.
    await callBridge(bridge, "set_property", {
      objectPath: ASSET_PATH,
      propertyName: "Config.Entries",
      value: TWO_ENTRIES,
    });
  });
});

describe("editor(set_property) - text-keyed TMap still takes the object shape (#820)", () => {
  it("round-trips { key: value } for a name-keyed map", async () => {
    const w = await callBridge(bridge, "set_property", {
      objectPath: ASSET_PATH,
      propertyName: "NamedCounts",
      value: { Alpha: 3, Beta: 5 },
    });
    expect(w.ok, w.error).toBe(true);
    expect(asRecord(w.result).elementCount).toBe(2);

    const r = await callBridge(bridge, "get_property", { objectPath: ASSET_PATH, propertyName: "NamedCounts" });
    expect(r.ok, r.error).toBe(true);
    const value = asRecord(asRecord(r.result).value);
    expect(value.Alpha).toBe(3);
    expect(value.Beta).toBe(5);

    const rewrite = await callBridge(bridge, "set_property", {
      objectPath: ASSET_PATH,
      propertyName: "NamedCounts",
      value,
    });
    expect(rewrite.ok, rewrite.error).toBe(true);
    expect(asRecord(rewrite.result).elementCount).toBe(2);
  });
});

describe("asset(read_properties) hands back a writable map (#820)", () => {
  it("reports the structured form and the pair count next to the export text", async () => {
    const r = await callBridge(bridge, "read_asset_properties", {
      assetPath: ASSET_PATH,
      propertyName: "Config",
    });
    expect(r.ok, r.error).toBe(true);
    const result = asRecord(r.result);
    expect(result.mapPairCount).toBe(2);
    expect(typeof result.valueTextRoundTrips).toBe("boolean");
    expect(entryCount(result.valueJson)).toBe(2);

    // Whatever read_properties calls writable must be writable.
    const w = await callBridge(bridge, "set_property", {
      objectPath: ASSET_PATH,
      propertyName: "Config",
      value: result.valueJson,
    });
    expect(w.ok, w.error).toBe(true);
    expect(asRecord(w.result).mapPairCount).toBe(2);
  });
});
