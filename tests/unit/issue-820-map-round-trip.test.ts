/**
 * #820: a read-then-write round trip on a struct-keyed TMap used to empty it.
 *
 * The repair itself is in the C++ bridge (property values are read and written
 * through FScriptMapHelper instead of export text, and a write that cannot
 * store every entry fails instead of leaving the map empty). What is testable
 * without an engine is the contract the server advertises to callers: the map
 * shapes the setter accepts, the round-trip signal the readers return, and the
 * force_reload parameter that keeps a dirty package from being discarded.
 */
import { describe, it, expect } from "vitest";
import { ALL_TOOLS } from "../../src/tools.js";
import type { ToolDef } from "../../src/core/types.js";
import { handlerSpecs } from "../../src/tools/specs/asset.generated.js";

function tool(name: string): ToolDef {
  const found = ALL_TOOLS.find((t) => t.name === name);
  if (!found) throw new Error(`no such tool: ${name}`);
  return found;
}

describe("#820 set_property documents both TMap shapes", () => {
  it("editor(set_property) names the object form and the key/value pair form", () => {
    const desc = tool("editor").actions.set_property.description ?? "";
    expect(desc).toContain("TMap");
    expect(desc).toContain("key");
    expect(desc).toContain("value");
    // The failure mode callers must be able to rely on.
    expect(desc.toLowerCase()).toContain("fails");
  });

  it("asset(set_property) names the pair form too, so both write paths agree", () => {
    const desc = tool("asset").actions.set_property.description ?? "";
    expect(desc).toContain("TMap");
    expect(desc).toContain("#820");
  });
});

describe("#820 read actions advertise the round-trip signal", () => {
  it("editor(get_property) points callers at `value` over `valueText`", () => {
    const desc = tool("editor").actions.get_property.description ?? "";
    expect(desc).toContain("valueTextRoundTrips");
    expect(desc).toContain("value");
  });

  it("editor(describe_object) carries the same flag", () => {
    expect(tool("editor").actions.describe_object.description ?? "").toContain("valueTextRoundTrips");
  });

  it("editor(get_object_properties) reports map properties structurally", () => {
    const desc = tool("editor").actions.get_object_properties.description ?? "";
    expect(desc).toContain("values");
    expect(desc).toContain("#820");
  });
});

describe("#820 asset(force_reload) parameter mapping", () => {
  const spec = tool("asset").actions.force_reload;

  // #1057: force_reload_asset declares its parameters in C++, and the action
  // forwards the bag as sent rather than rebuilding it.
  const declared = handlerSpecs.force_reload_asset.params;

  it("passes discardUnsaved through to the bridge", () => {
    expect(spec.mapParams).toBeUndefined();
    expect(declared.find((p) => p.name === "discardUnsaved")?.type).toBe("boolean");
  });

  it("still accepts `path` as an alias for assetPath", () => {
    expect(declared.find((p) => p.name === "assetPath")?.aliases).toContain("path");
  });

  it("leaves discardUnsaved optional, so the bridge default (false) holds when it is omitted", () => {
    expect(declared.find((p) => p.name === "discardUnsaved")?.required).toBe(false);
  });

  it("declares discardUnsaved as an optional boolean in the tool schema", () => {
    const schema = tool("asset").schema.discardUnsaved;
    expect(schema).toBeDefined();
    expect(schema.safeParse(true).success).toBe(true);
    expect(schema.safeParse(undefined).success).toBe(true);
    expect(schema.safeParse("yes").success).toBe(false);
  });
});
