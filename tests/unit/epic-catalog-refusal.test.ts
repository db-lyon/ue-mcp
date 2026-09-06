/**
 * An editor that could not be asked is not an editor with no tools.
 *
 * `epic_list_toolsets` is refused like anything else while a modal is up, and
 * the refusal carries no `toolsets`, so it read as "this editor advertises
 * none" and the surface silently fell back to a cache with nothing said.
 */
import { describe, expect, it } from "vitest";
import { catalogOrRefusal } from "../../src/epic-cache.js";

describe("reading a toolset catalog", () => {
  it("recognises a dialog refusal rather than reading it as an empty catalog", () => {
    const refusal = {
      success: false,
      dialogBlocking: true,
      refusedMethod: "epic_list_toolsets",
      dialogTitle: "Save Content",
    };
    const read = catalogOrRefusal(refusal);
    expect(read.refusedByDialog).toBe(true);
    expect(read.catalog).toBeNull();
  });

  it("passes a real catalog through untouched", () => {
    const catalog = { toolsets: [{ name: "Editor", tools: [] }] };
    const read = catalogOrRefusal(catalog);
    expect(read.refusedByDialog).toBe(false);
    expect(read.catalog).toBe(catalog);
  });

  it("does not mistake an editor that genuinely has no toolsets for a refusal", () => {
    // The case the two used to be indistinguishable from.
    const read = catalogOrRefusal({ toolsets: [] });
    expect(read.refusedByDialog).toBe(false);
    expect(read.catalog).toEqual({ toolsets: [] });
  });

  it("handles null and non-objects without throwing", () => {
    expect(catalogOrRefusal(null)).toEqual({ catalog: null, refusedByDialog: false });
    expect(catalogOrRefusal("nope").refusedByDialog).toBe(false);
  });
});
