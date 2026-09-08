import { describe, it, expect, beforeAll, afterAll } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { loadPlugins } from "../../src/plugin/loader.js";
import { categoryTool, type ToolDef } from "../../src/types.js";

/**
 * #892: a plugin manifest with one malformed param used to fail validation as a
 * whole, which took every action the plugin provided off the surface with it.
 * A defect confined to one handler must now cost one handler.
 */

const MANIFEST = `
actionPrefix: pie
nativeModule:
  uePluginName: PIE_Studio
  minBridgeApi: 1
  source: ue/Plugins/PIE_Studio
  category: pie
  categoryDescription: "PIE record, replay, observe"
  handlers:
    record_arm:
      description: "Arm the recorder"
      schema:
        name: { type: string, description: "Recording name" }
    actor_set:
      description: "Write a property on a live PIE actor"
      schema:
        target: { type: string }
        value:  { type: wobbly }
    snapshot:
      description: "Snapshot the live world"
`;

let projectDir: string;

beforeAll(() => {
  projectDir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-892-"));
  const pkgDir = path.join(projectDir, "node_modules", "pie-studio");
  fs.mkdirSync(pkgDir, { recursive: true });
  fs.writeFileSync(
    path.join(pkgDir, "package.json"),
    JSON.stringify({ name: "pie-studio", version: "0.5.1" }),
  );
  fs.writeFileSync(path.join(pkgDir, "ue-mcp.plugin.yml"), MANIFEST);
});

afterAll(() => {
  fs.rmSync(projectDir, { recursive: true, force: true });
});

function baseTools(): ToolDef[] {
  return [categoryTool("gameplay", "Gameplay", { noop: { kind: "handler", effect: "read", description: "n", handler: async () => ({}) } })];
}

describe("#892 one malformed param does not take the category down", () => {
  it("keeps the plugin active and provisions its category", async () => {
    const load = await loadPlugins(
      baseTools(),
      [{ name: "pie-studio" }],
      projectDir,
      "1.2.3",
    );
    const rec = load.records[0];
    expect(rec.status).toBe("active");
    expect(rec.statusReason).toBeUndefined();
    expect(Object.keys(rec.provided)).toEqual(["pie"]);
  });

  it("surfaces every sound handler and only drops the malformed one", async () => {
    const load = await loadPlugins(
      baseTools(),
      [{ name: "pie-studio" }],
      projectDir,
      "1.2.3",
    );
    expect(load.records[0].provided.pie).toEqual(["record_arm", "snapshot"]);

    const pieTool = load.tools.find((t) => t.name === "pie");
    expect(pieTool).toBeDefined();
    expect(Object.keys(pieTool!.actions).sort()).toEqual(["record_arm", "snapshot"]);
  });

  it("names the dropped unit and the reason on the record", async () => {
    const load = await loadPlugins(
      baseTools(),
      [{ name: "pie-studio" }],
      projectDir,
      "1.2.3",
    );
    const degraded = load.records[0].degraded;
    expect(degraded).toHaveLength(1);
    expect(degraded[0]).toContain("nativeModule.handlers.actor_set");
    expect(degraded[0]).toContain("value.type");
  });
});
