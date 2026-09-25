/**
 * #1057: a key the server mirrored under another name is not blamed on the
 * caller when the editor read the other name.
 */
import { describe, expect, it } from "vitest";
import { attachNotRead } from "../../src/call-pipeline.js";
import { WIDGET_PARAM_GROUPS } from "../../src/tools/widget-params.js";

const pipeline = (sent: string[]) => ({ paramGroups: WIDGET_PARAM_GROUPS, sentKeys: sent, params: {} });

describe("paramsNotRead alias groups", () => {
  it("drops mirrored aliases when a group member was read", () => {
    const out = attachNotRead({ success: true }, { paramsNotRead: ["assetPath", "path"] },
      pipeline(["assetPath", "path", "name", "packagePath"])) as Record<string, unknown>;
    expect(out.paramsNotRead).toBeUndefined();
  });

  it("reports the group when no member was read", () => {
    const out = attachNotRead({ success: true }, { paramsNotRead: ["assetPath", "path"] },
      pipeline(["assetPath", "path"])) as { paramsNotRead?: { params: string[] } };
    expect(out.paramsNotRead?.params).toEqual(["assetPath", "path"]);
  });

  it("still reports a key outside every group", () => {
    const out = attachNotRead({ success: true }, { paramsNotRead: ["bogus"] },
      pipeline(["assetPath", "bogus"])) as { paramsNotRead?: { params: string[] } };
    expect(out.paramsNotRead?.params).toEqual(["bogus"]);
  });
});
