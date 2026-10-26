import { describe, expect, it } from "vitest";
import {
  assetDiffDeferred,
  assetDiffError,
  assetDiffFromBridge,
  describeRecord,
  nativeDiffOf,
  summarizeAssetDiff,
  type AssetDiff,
  type AssetDiffRecord,
} from "../../src/checkpoints/asset-diff.js";

const noPicture = () => undefined;

function modified(entries: AssetDiffRecord[], extra: Record<string, unknown> = {}): AssetDiff {
  return assetDiffFromBridge({
    status: "modified",
    assetClass: "Material",
    engine: { tool: "SMaterialDiff", note: "", native: "window", nativeAvailable: true },
    before: { class: "Material", facts: [] },
    after: { class: "Material", facts: [] },
    sections: [{ category: "Details", kind: "properties", engineSource: "DiffUtils::CompareUnrelatedObjects", entries }],
    ...extra,
  }, "Content/M.uasset", noPicture);
}

describe("an asset diff's summary", () => {
  it("leads with the count and the records that carry values", () => {
    const d = modified([
      { displayString: "Two Sided changed value in M", diffType: "PropertyValueChanged", path: "Two Sided", property: "TwoSided", before: "False", after: "True" },
      { displayString: "Added Node Constant", diffType: "NODE_ADDED" },
      { displayString: "Blend Mode changed value in M", diffType: "PropertyValueChanged", path: "Blend Mode", before: "BLEND_Opaque", after: "BLEND_Masked" },
    ]);
    expect(d.summary).toBe("3 differences: Two Sided False to True; Blend Mode BLEND_Opaque to BLEND_Masked; and 1 more");
    expect(d.nativeDiff).toEqual({ available: true });
  });

  it("reads a graph record's values from the property records behind it", () => {
    const d = modified([
      { displayString: "Added Node Constant", diffType: "NODE_ADDED" },
      { displayString: "Property Changed: Default Value", diffType: "NODE_PROPERTY", properties: [{ displayString: "x", diffType: "PropertyValueChanged", path: "Default Value", before: "0.2", after: "0.7" }] },
    ]);
    expect(d.summary).toBe("2 differences: Default Value 0.2 to 0.7; Added Node Constant");
  });

  it("names the node a graph record's values belong to, leads with removals, and names each change once", () => {
    const d = modified([
      { displayString: "Added Node Constant", diffType: "NODE_ADDED", category: "ADDITION" },
      { displayString: "Property Changed", diffType: "NODE_PROPERTY", node2: { guid: "g", name: "n", class: "MaterialGraphNode", title: "Roughness Param" }, properties: [{ displayString: "x", diffType: "PropertyValueChanged", path: "Default Value", before: "0.2", after: "0.7" }] },
      { displayString: "Removed actor QA_Doomed (PointLight)", diffType: "OBJECT_REMOVED", group: "QA_Doomed" },
    ]);
    expect(d.summary).toBe("3 differences: Removed actor QA_Doomed (PointLight); Roughness Param Default Value 0.2 to 0.7; and 1 more");
    const bp = modified([
      { displayString: "Added Variable Health", diffType: "PropertyAddedToB", group: "Health", path: "Health", before: null, after: "real" },
      { displayString: "Health added to Default__BP", diffType: "PropertyAddedToB", path: "Health", before: null, after: "100.0" },
      { displayString: "Added Node Lamp", diffType: "NODE_ADDED", group: "Lamp" },
    ]);
    expect(bp.summary).toBe("3 differences: Health added: real; Added Node Lamp; and 1 more");
  });

  it("names a group once, and an added or removed value", () => {
    expect(describeRecord({ displayString: "x", diffType: "PropertyAddedToB", group: "Health", path: "Health", before: null, after: "real" })).toBe("Health added: real");
    expect(describeRecord({ displayString: "x", diffType: "PropertyAddedToB", group: "RowB", path: "Dev Comment", before: null, after: "hi" })).toBe("RowB Dev Comment added: hi");
    expect(describeRecord({ displayString: "x", diffType: "PropertyAddedToA", path: "Speed", before: "1.0", after: null })).toBe("Speed removed: 1.0");
  });

  it("names what an added or removed node holds, past its class defaults", () => {
    const node = (properties: { name: string; displayName: string; value: string; isDefault: boolean }[]) => ({
      guid: "g", name: "MaterialGraphNode_3", class: "MaterialGraphNode", title: "Constant", fullTitle: "0.5",
      pins: [], propertiesOf: "MaterialExpressionConstant", properties,
    });
    const added: AssetDiffRecord = {
      displayString: "Added Node 'Constant'", diffType: "NODE_ADDED", category: "ADDITION",
      node1: node([{ name: "R", displayName: "Value", value: "0.500000", isDefault: false }, { name: "Desc", displayName: "Desc", value: "", isDefault: true }]),
    };
    expect(describeRecord(added)).toBe("Added Node 'Constant': Value 0.500000");
    expect(modified([added]).summary).toBe("1 difference: Added Node 'Constant': Value 0.500000");
    const many = node(["A", "B", "C", "D", "E"].map((n) => ({ name: n, displayName: n, value: "1", isDefault: false })));
    expect(describeRecord({ displayString: "Removed Node 'X'", diffType: "NODE_REMOVED", node1: many })).toBe("Removed Node 'X': A 1, B 1, C 1, and 2 more");
    expect(describeRecord({ displayString: "Added Node 'Constant'", diffType: "NODE_ADDED", node1: node([]) })).toBe("Added Node 'Constant'");
    expect(describeRecord({ displayString: "Added Node Constant", diffType: "NODE_ADDED" })).toBe("Added Node Constant");
  });

  it("counts records past the ceiling", () => {
    const d = modified([{ displayString: "a", diffType: "NODE_ADDED" }], { recordsDropped: 4 });
    expect(d.summary).toBe("5 differences: a; and 4 more");
  });

  it("says when only hidden properties or the file changed, never nothing", () => {
    const hiddenRecord = { displayString: "x", diffType: "PropertyValueChanged", path: "Lighting Guid", before: "A", after: "B", hidden: true };
    const hidden = assetDiffFromBridge({ status: "modified", assetClass: "StaticMesh", sections: [{ category: "Details", kind: "properties", engineSource: "", entries: [hiddenRecord, hiddenRecord], hiddenChanges: 2 }] }, "Content/S.uasset", noPicture);
    expect(hidden.summary).toBe("StaticMesh: 2 differences, all in properties the details panel does not show");
    const mixed = modified([{ displayString: "x", diffType: "PropertyValueChanged", path: "sRGB", before: "True", after: "False" }, hiddenRecord]);
    expect(mixed.summary).toBe("1 difference: sRGB True to False; plus 1 difference in properties the details panel does not show");
    const none = assetDiffFromBridge({ status: "modified", assetClass: "StaticMesh", sections: [] }, "Content/S.uasset", noPicture);
    expect(none.summary).toBe("StaticMesh: the file changed, and Unreal's diff finds no difference");
  });

  it("describes an added or deleted asset from its facts", () => {
    const added = assetDiffFromBridge({
      status: "added",
      assetClass: "Material",
      before: null,
      after: { class: "Material", facts: [{ name: "Blend mode", value: "BLEND_Opaque" }, { name: "Expressions", value: "2" }] },
      sections: [{ category: "Properties (against class defaults)", kind: "properties", engineSource: "", entries: [{ displayString: "x", diffType: "PropertyValueChanged", path: "Two Sided" }] }],
    }, "Content/M.uasset", noPicture);
    expect(added.before).toBeNull();
    expect(added.summary).toBe("New Material, 1 property set: Blend mode BLEND_Opaque; Expressions 2");
    const deleted = assetDiffFromBridge({ status: "deleted", assetClass: "Texture2D", before: { class: "Texture2D", facts: [] }, after: null, sections: [] }, "Content/T.uasset", noPicture);
    expect(deleted.summary).toBe("Deleted Texture2D");
  });

  it("turns the engine's CSV export into a unified diff", () => {
    const d = assetDiffFromBridge({
      status: "modified",
      assetClass: "DataTable",
      sections: [],
      engineText: { format: "csv", before: "---,Tag\nRowA,a\n", after: "---,Tag\nRowA,a\nRowB,b\n" },
    }, "Content/DT.uasset", noPicture);
    expect(d.textDiff?.format).toBe("csv");
    expect(d.textDiff?.diff).toContain("+RowB,b");
    expect(d.summary).toBe("DataTable: the engine's CSV export changed");
  });

  it("says why an item is not diffed, and why Unreal's view cannot open", () => {
    expect(assetDiffError("the package could not be loaded for diff: bad").summary).toBe("Could not diff: the package could not be loaded for diff: bad");
    expect(assetDiffDeferred().state).toBe("deferred");
    expect(nativeDiffOf({ state: "ready", engine: { tool: "t", note: "", native: "text", nativeAvailable: false, nativeReason: "no tool" } })).toEqual({ available: false, reason: "no tool" });
  });

  it("maps each side's picture through the caller", () => {
    const d = assetDiffFromBridge({
      status: "modified",
      assetClass: "Material",
      before: { class: "Material", facts: [], thumbnail: { file: "a.png", source: "rendered" } },
      after: { class: "Material", facts: [], thumbnail: { error: "no renderer" } },
      sections: [],
    }, "Content/M.uasset", (side, t) => (t && "file" in t ? { id: side.repeat(1), url: "", source: "rendered" } : t ? { error: String(t.error) } : undefined));
    expect(d.before?.thumbnail).toEqual({ id: "before", url: "", source: "rendered" });
    expect(d.after?.thumbnail).toEqual({ error: "no renderer" });
  });

  it("summarizes an error and a deferred diff in the same words everywhere", () => {
    expect(summarizeAssetDiff({ state: "deferred", summary: "", nativeDiff: { available: false } })).toMatch(/^Not diffed yet/);
  });
});
