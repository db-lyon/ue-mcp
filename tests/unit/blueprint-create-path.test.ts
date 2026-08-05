/**
 * `blueprint(create)` destination handling (#798).
 *
 * The reported failures: a name plus packagePath pair was answered with
 * "Missing required parameter 'path' (or 'assetPath')", naming an internal
 * field the schema never advertised, and a path carrying a .uasset suffix
 * reached the editor as an illegal asset name and came back as a bare
 * "Failed to create Blueprint".
 */
import { describe, expect, it, vi } from "vitest";
import { blueprintTool } from "../../src/tools/blueprint.js";
import type { ToolContext } from "../../src/types.js";

const ASSET = "/Game/_Project/UI/Computer/WBP_ComputerDesktop";

async function sent(params: Record<string, unknown>): Promise<Record<string, unknown>> {
  const call = vi.fn().mockResolvedValue({ success: true });
  const ctx = { bridge: { call } } as unknown as ToolContext;
  await blueprintTool.handler(ctx, { action: "create", ...params });
  return call.mock.calls[0][1] as Record<string, unknown>;
}

describe("blueprint.create destination", () => {
  it("accepts the canonical assetPath", async () => {
    expect(await sent({ assetPath: ASSET })).toEqual({ path: ASSET, parentClass: undefined });
  });

  it("accepts name plus packagePath as the same destination", async () => {
    const params = await sent({
      name: "WBP_ComputerDesktop",
      packagePath: "/Game/_Project/UI/Computer",
      parentClass: "/Script/UMG.UserWidget",
    });
    expect(params).toEqual({ path: ASSET, parentClass: "/Script/UMG.UserWidget" });
  });

  it("strips a .uasset suffix instead of sending an illegal asset name", async () => {
    expect(await sent({ assetPath: `${ASSET}.uasset` })).toMatchObject({ path: ASSET });
  });

  it("names the public parameter when the destination is missing", async () => {
    await expect(sent({ parentClass: "/Script/UMG.UserWidget" }))
      .rejects.toThrow(/Missing required parameter 'assetPath'/);
  });
});
