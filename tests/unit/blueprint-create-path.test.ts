/**
 * `blueprint(create)` destination handling (#798).
 *
 * The reported failures: a name plus packagePath pair was answered with
 * "Missing required parameter 'path' (or 'assetPath')", naming an internal
 * field the schema never advertised, and a path carrying a .uasset suffix
 * reached the editor as an illegal asset name and came back as a bare
 * "Failed to create Blueprint".
 *
 * The destination is now a spec choice (#1057): assetPath OR name +
 * packagePath, checked before anything is sent. The handler folds the path
 * spellings itself, so the bag reaches the bridge exactly as the caller wrote it.
 */
import { describe, expect, it, vi } from "vitest";
import { blueprintTool } from "../../src/tools/blueprint.js";
import type { ToolContext } from "../../src/core/types.js";

const ASSET = "/Game/_Project/UI/Computer/WBP_ComputerDesktop";

async function sent(params: Record<string, unknown>): Promise<Record<string, unknown>> {
  const call = vi.fn().mockResolvedValue({ success: true });
  const ctx = { bridge: { call } } as unknown as ToolContext;
  await blueprintTool.handler(ctx, { action: "create", ...params });
  return call.mock.calls[0][1] as Record<string, unknown>;
}

describe("blueprint.create destination", () => {
  it("forwards the canonical assetPath", async () => {
    expect(await sent({ assetPath: ASSET })).toEqual({ assetPath: ASSET });
  });

  it("forwards name plus packagePath as the same destination", async () => {
    const params = {
      name: "WBP_ComputerDesktop",
      packagePath: "/Game/_Project/UI/Computer",
      parentClass: "/Script/UMG.UserWidget",
    };
    expect(await sent(params)).toEqual(params);
  });

  it("leaves a .uasset suffix for the handler to fold", async () => {
    expect(await sent({ assetPath: `${ASSET}.uasset` })).toEqual({ assetPath: `${ASSET}.uasset` });
  });

  it("names the public parameters when the destination is missing", async () => {
    await expect(sent({ parentClass: "/Script/UMG.UserWidget" }))
      .rejects.toThrow(/assetPath \(or path\) OR name \+ packagePath/);
  });

  it("refuses both spellings of the destination at once", async () => {
    await expect(sent({ assetPath: ASSET, name: "WBP_ComputerDesktop", packagePath: "/Game/_Project/UI/Computer" }))
      .rejects.toThrow(/takes one side/);
  });

  it("refuses a name without its packagePath", async () => {
    await expect(sent({ name: "WBP_ComputerDesktop" })).rejects.toThrow(/without packagePath/);
  });
});
