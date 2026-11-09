/**
 * blueprint(diff) with `against` (#1487), engine-free: what an against value
 * names, the package file an asset path is recorded under, the bytes a
 * checkpoint holds for a path including a save not yet folded into a
 * checkpoint, which forms are answered in process, and the bridge intercept.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { ShadowRepo } from "../../src/checkpoints/shadow-repo.js";
import { blueprintPackageKey } from "../../src/checkpoints/service.js";
import { interceptCheckpointDiff, resolveAgainst } from "../../src/tools/blueprint-diff.js";
import { runBridge } from "../../src/flow/run-action.js";
import type { FlowContext } from "../../src/flow/context.js";
import { blueprintTool } from "../../src/tools/blueprint.js";
import type { ToolContext } from "../../src/core/types.js";

let dir: string;

const write = (rel: string, content: string) => {
  const file = path.join(dir, rel);
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, content);
};

beforeEach(() => {
  dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-bpdiff-"));
});

afterEach(() => {
  fs.rmSync(dir, { recursive: true, force: true });
});

describe("resolveAgainst", () => {
  const none = async () => false;
  const all = async () => true;

  it("takes an explicit checkpoint: or revision: prefix at its word", async () => {
    expect(await resolveAgainst("checkpoint:abc1234", none)).toEqual({ kind: "checkpoint", id: "abc1234" });
    expect(await resolveAgainst("revision:abc1234", all)).toEqual({ kind: "revision", revision: "abc1234" });
    expect(await resolveAgainst("Revision: head ", all)).toEqual({ kind: "revision", revision: "head" });
  });

  it("tries a bare id as a checkpoint first, then as a revision", async () => {
    expect(await resolveAgainst("abc1234", all)).toEqual({ kind: "checkpoint", id: "abc1234" });
    expect(await resolveAgainst("abc1234", none)).toEqual({ kind: "revision", revision: "abc1234" });
    expect(await resolveAgainst("42", none)).toEqual({ kind: "revision", revision: "42" });
  });

  it("refuses a prefix with nothing after it", async () => {
    await expect(resolveAgainst("checkpoint:", none)).rejects.toThrow(/names no checkpoint/);
  });
});

describe("blueprintPackageKey", () => {
  it("maps /Game to Content, with or without the object name", () => {
    expect(blueprintPackageKey(dir, "/Game/BPs/BP_Door")).toBe("Content/BPs/BP_Door.uasset");
    expect(blueprintPackageKey(dir, "/Game/BPs/BP_Door.BP_Door")).toBe("Content/BPs/BP_Door.uasset");
    expect(blueprintPackageKey(dir, "/Game/BPs/BP_Door.BP_Door_C")).toBe("Content/BPs/BP_Door.uasset");
  });

  it("maps a plugin mount to that plugin's Content folder", () => {
    write("Plugins/Gameplay/Doors/Doors.uplugin", "{}");
    expect(blueprintPackageKey(dir, "/Doors/BP_Gate")).toBe("Plugins/Gameplay/Doors/Content/BP_Gate.uasset");
  });

  it("is null for a mount outside the project", () => {
    expect(blueprintPackageKey(dir, "/Engine/BasicShapes/Cube")).toBeNull();
    expect(blueprintPackageKey(dir, "BP_Door")).toBeNull();
  });
});

describe("ShadowRepo.exportAsOf", () => {
  it("writes the bytes a checkpoint holds, counting a save no checkpoint has folded in", async () => {
    const repo = new ShadowRepo(dir);
    const key = "Content/BP_Door.uasset";
    write(key, "state one");
    repo.recordBeforeWrite(path.join(dir, key));
    const first = await repo.checkpoint({ label: "start", trigger: "client" });

    // Saved after the checkpoint: the copy taken before the save is pending.
    repo.recordBeforeWrite(path.join(dir, key));
    write(key, "state two");

    const dest = path.join(dir, "out.uasset");
    const out = await repo.exportAsOf(first.info.id.slice(0, 10), key, dest);
    expect(out.state).toBe("written");
    expect(out.checkpoint).toMatchObject({ id: first.info.id, label: "start" });
    expect(fs.readFileSync(dest, "utf-8")).toBe("state one");
  });

  it("says when a path did not exist at the checkpoint, or was never recorded", async () => {
    const repo = new ShadowRepo(dir);
    write("Config/DefaultGame.ini", "[A]\n");
    repo.recordBeforeWrite(path.join(dir, "Config/DefaultGame.ini"));
    const first = await repo.checkpoint({ label: "start", trigger: "client" });

    const key = "Content/BP_New.uasset";
    repo.recordBeforeWrite(path.join(dir, key));
    write(key, "created after");
    const dest = path.join(dir, "out.uasset");
    expect((await repo.exportAsOf(first.info.id, key, dest)).state).toBe("absent");
    expect((await repo.exportAsOf(first.info.id, "Content/Never.uasset", dest)).state).toBe("unrecorded");
    expect(fs.existsSync(dest)).toBe(false);
  });

  it("refuses a project with no history", async () => {
    await expect(new ShadowRepo(dir).exportAsOf("abcd", "Content/X.uasset", path.join(dir, "o"))).rejects.toThrow(/no checkpoint history/);
  });
});

describe("blueprint(diff) dispatch", () => {
  const ctx = (call = vi.fn(async () => ({ success: true }))) => ({ bridge: { call } } as unknown as ToolContext);

  it("stays the diff_blueprint bridge action, with its recorded parameters and an intercept", () => {
    const spec = blueprintTool.actions.diff;
    expect(spec.kind).toBe("bridge");
    expect(spec.kind === "bridge" && spec.bridge).toBe("diff_blueprint");
    expect(spec.kind === "bridge" && typeof spec.intercept).toBe("function");
    expect(spec.paramSpec?.map((p) => p.name)).toEqual(["assetPath", "otherPath", "against"]);
    expect(spec.paramChoices).toEqual([{ mode: "exactlyOne", branches: [["otherPath"], ["against"]] }]);
  });

  it("leaves a two-asset diff and every revision form to the editor", async () => {
    expect(await interceptCheckpointDiff(ctx(), { assetPath: "/Game/A", otherPath: "/Game/B" })).toBeUndefined();
    expect(await interceptCheckpointDiff(ctx(), { assetPath: "/Game/A", against: "revision:1a2b3c4d" })).toBeUndefined();
    // No session, so no checkpoint history to try a bare id against.
    expect(await interceptCheckpointDiff(ctx(), { assetPath: "/Game/A", against: "head" })).toBeUndefined();
  });

  it("refuses a checkpoint without an editor session", async () => {
    await expect(interceptCheckpointDiff(ctx(), { assetPath: "/Game/A", against: "checkpoint:abcd123" }))
      .rejects.toThrow(/needs an editor session/);
  });
});

describe("runBridge intercept", () => {
  const flowCtx = (call: ReturnType<typeof vi.fn>) => ({ bridge: { call, capabilities: null } } as unknown as FlowContext);

  it("answers without the bridge when the intercept returns a value, and skips followUp", async () => {
    const call = vi.fn(async () => ({ success: true, from: "bridge" }));
    const followUp = vi.fn(async (_c: unknown, _p: unknown, a: unknown) => a);
    const out = await runBridge(flowCtx(call), "probe.diff", "probe_diff", undefined, undefined, { against: "x" }, undefined, {
      intercept: async () => ({ success: true, from: "intercept" }),
      followUp,
    });
    expect(out.data).toMatchObject({ from: "intercept" });
    expect(call).not.toHaveBeenCalled();
    expect(followUp).not.toHaveBeenCalled();
  });

  it("calls the bridge when the intercept returns undefined", async () => {
    const call = vi.fn(async () => ({ success: true, from: "bridge" }));
    const out = await runBridge(flowCtx(call), "probe.diff", "probe_diff", undefined, undefined, { against: "x" }, undefined, {
      intercept: async () => undefined,
    });
    expect(out.data).toMatchObject({ from: "bridge" });
    expect(call).toHaveBeenCalledWith("probe_diff", { against: "x" }, undefined);
  });
});
