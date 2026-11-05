/**
 * blueprint(open_diff_window) (#1483): routes `against` to the checkpoint
 * history or the source control provider, and the bridge keeps the earlier
 * version's /Temp package only while the window that shows it is open.
 *
 * The windows themselves only exist in an editor; these hold the routing and
 * the C++ lifetime contract.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { afterEach, describe, expect, it } from "vitest";
import { blueprintTool } from "../../src/tools/blueprint.js";
import { openDiffWindow, type DiffWindowDeps } from "../../src/tools/blueprint-diff-window.js";
import { declaredActionEffect } from "../../src/surface/action-effects.js";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const BRIDGE = path.join(ROOT, "plugin", "ue_mcp_bridge", "Source", "UE_MCP_Bridge");
const read = (rel: string) => fs.readFileSync(path.join(BRIDGE, rel), "utf8");

const dirs: string[] = [];
function project(files: string[]): string {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-diffwin-"));
  dirs.push(dir);
  for (const f of files) {
    fs.mkdirSync(path.dirname(path.join(dir, f)), { recursive: true });
    fs.writeFileSync(path.join(dir, f), "");
  }
  return dir;
}
afterEach(() => {
  for (const d of dirs.splice(0)) fs.rmSync(d, { recursive: true, force: true });
});

function deps(projectDir: string, checkpoints: string[]) {
  const calls: Array<{ kind: string; args: unknown[] }> = [];
  const d: DiffWindowDeps = {
    projectDir,
    hasCheckpoint: async (id) => checkpoints.some((c) => c.startsWith(id.toLowerCase())),
    openCheckpoint: async (changedPath, from) => {
      calls.push({ kind: "checkpoint", args: [changedPath, from] });
      return { opened: "window", windowTitle: "BP_Door", path: changedPath };
    },
    callBridge: async (method, params) => {
      calls.push({ kind: "bridge", args: [method, params] });
      return { success: true, editorDiffOpened: true, against: { revision: "abc1234def" }, unloadsOnClose: true };
    },
  };
  return { d, calls };
}

describe("blueprint(open_diff_window)", () => {
  it("is a read action on the blueprint category taking assetPath and against", () => {
    const action = blueprintTool.actions.open_diff_window;
    expect(action.kind).toBe("handler");
    expect(declaredActionEffect("blueprint", "open_diff_window")).toBe("read");
    expect(action.description).toMatch(/Params: assetPath, against/);
  });

  it("opens a checkpoint's version from the shadow history against the asset now", async () => {
    const dir = project(["Content/BP/BP_Door.uasset"]);
    const { d, calls } = deps(dir, ["0123456789abcdef0123456789abcdef01234567"]);
    const out = await openDiffWindow(d, { assetPath: "/Game/BP/BP_Door", against: "0123456" });
    expect(calls).toEqual([{ kind: "checkpoint", args: ["Content/BP/BP_Door.uasset", "0123456"] }]);
    expect(out).toMatchObject({ against: { kind: "checkpoint", id: "0123456" }, opened: "window" });
  });

  it("sends anything that is not a checkpoint to source_control_diff as a revision", async () => {
    const dir = project(["Content/BP/BP_Door.uasset"]);
    const { d, calls } = deps(dir, ["0123456789abcdef0123456789abcdef01234567"]);
    const out = await openDiffWindow(d, { assetPath: "/Game/BP/BP_Door.BP_Door", against: "abc1234" });
    expect(calls).toEqual([{ kind: "bridge", args: ["source_control_diff", { path: "/Game/BP/BP_Door.BP_Door", revision: "abc1234", openEditor: true }] }]);
    expect(out).toMatchObject({ against: { kind: "revision", revision: "abc1234def" }, opened: "window", unloadsOnClose: true });
  });

  it("takes checkpoint: and revision: prefixes to choose when an id could be either", async () => {
    const dir = project(["Content/BP/BP_Door.uasset"]);
    const { d, calls } = deps(dir, ["0123456789abcdef0123456789abcdef01234567"]);
    await openDiffWindow(d, { assetPath: "/Game/BP/BP_Door", against: "revision:0123456" });
    expect(calls[0]).toMatchObject({ kind: "bridge", args: ["source_control_diff", { revision: "0123456" }] });
    await openDiffWindow(d, { assetPath: "/Game/BP/BP_Door", against: "checkpoint:0123456" });
    expect(calls[1]).toEqual({ kind: "checkpoint", args: ["Content/BP/BP_Door.uasset", "0123456"] });
  });

  it("maps a plugin mount to its plugin's content, and refuses one outside the project", async () => {
    const dir = project(["Plugins/Group/MyPlugin/MyPlugin.uplugin"]);
    const { d, calls } = deps(dir, ["0123456789abcdef0123456789abcdef01234567"]);
    await openDiffWindow(d, { assetPath: "/MyPlugin/BP/BP_A.BP_A", against: "0123456" });
    expect(calls[0]).toEqual({ kind: "checkpoint", args: ["Plugins/Group/MyPlugin/Content/BP/BP_A.uasset", "0123456"] });
    await expect(openDiffWindow(d, { assetPath: "/Engine/BasicShapes/Cube", against: "0123456" })).rejects.toThrow(/not under \/Game/);
  });

  it("refuses a missing assetPath or against before calling anything", async () => {
    const { d, calls } = deps(project([]), []);
    await expect(openDiffWindow(d, { against: "abc" })).rejects.toThrow(/assetPath is required/);
    await expect(openDiffWindow(d, { assetPath: "/Game/BP/BP_Door" })).rejects.toThrow(/against is required/);
    expect(calls).toEqual([]);
  });

});

describe("diff package lifetime in the bridge", () => {
  const helper = read("Private/Handlers/Diff/DiffPackage.cpp");
  const checkpoint = read("Private/Handlers/Diff/DiffHandlers_Checkpoint.cpp");
  const sourceControl = read("Private/Handlers/SourceControl/SourceControlHandlers.cpp");

  it("unloads kept packages a tick after the window or asset editor closes", () => {
    expect(helper).toContain("GetOnWindowClosedEvent().AddLambda");
    expect(helper).toContain("OnAssetClosedInEditor().AddLambda");
    expect(helper).toContain("FTSTicker::GetCoreTicker().AddTicker");
    expect(helper).toContain("UPackageTools::UnloadPackages");
  });

  it("checkpoint_open_native_diff keeps only the packages it loaded from files, and unloads them on failure", () => {
    const open = checkpoint.slice(checkpoint.indexOf("FDiffHandlers::OpenCheckpointNativeDiff("));
    expect(open).toContain("ON_SCOPE_EXIT { if (!bKeep) MCPDiffPackage::Unload(Temp); }");
    expect(open).toContain("MCPDiffPackage::KeepUntilClosed(Window.ToSharedRef(), Temp)");
    expect(open).toContain("MCPDiffPackage::KeepUntilEditorCloses(Only)");
  });

  it("source_control_diff picks the named revision and opens the window on a /Temp copy it unloads on close", () => {
    const diff = sourceControl.slice(sourceControl.indexOf("FSourceControlHandlers::Diff("));
    expect(diff).toContain("MCPDiffPackage::FindRevision(State.ToSharedRef(), WantedRevision, FindError)");
    expect(diff).toContain("MCPDiffPackage::LoadRevision(Revision.ToSharedRef(), Path.PackageName)");
    expect(diff).toContain("MCPDiffPackage::KeepUntilClosed(Window.ToSharedRef(), { Old.Package })");
    expect(diff).not.toMatch(/\.DiffAgainstDepot\(/);
  });
});
