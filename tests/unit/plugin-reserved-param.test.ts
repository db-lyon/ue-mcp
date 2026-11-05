/** #1216: a plugin param named after a routing key can never arrive, so it is refused at load. */
import { describe, it, expect, beforeAll, afterAll } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { loadPlugins } from "../../src/extensions/loader.js";
import type { ToolDef } from "../../src/core/types.js";
import { categoryTool } from "../../src/surface/category-tool.js";

const MANIFEST = `
actionPrefix: recorder
nativeModule:
  uePluginName: Sample_Recorder
  minBridgeApi: 1
  source: ue/Plugins/Sample_Recorder
  category: recorder
  handlers:
    inject_input_tape:
      description: "Inject an input tape"
      schema:
        action: { type: string, description: "InputAction path" }
        values: { type: array }
    record_arm:
      description: "Arm the recorder"
      schema:
        name: { type: string }
`;

let projectDir: string;

beforeAll(() => {
  projectDir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-1216-"));
  const pkgDir = path.join(projectDir, "node_modules", "sample-recorder");
  fs.mkdirSync(pkgDir, { recursive: true });
  fs.writeFileSync(path.join(pkgDir, "package.json"), JSON.stringify({ name: "sample-recorder", version: "0.5.1" }));
  fs.writeFileSync(path.join(pkgDir, "ue-mcp.plugin.yml"), MANIFEST);
});

afterAll(() => {
  fs.rmSync(projectDir, { recursive: true, force: true });
});

const baseTools = (): ToolDef[] =>
  [categoryTool("gameplay", "Gameplay", { noop: { kind: "handler", effect: "read", description: "n", handler: async () => ({}) } })];

describe("a plugin param named 'action'", () => {
  it("drops the action that declares it, keeps the rest, and says why", async () => {
    const load = await loadPlugins(baseTools(), [{ name: "sample-recorder" }], projectDir, "1.2.3");
    const rec = load.records[0];
    expect(rec.status).toBe("active");
    expect(rec.provided.recorder).toEqual(["record_arm"]);
    const reason = rec.degraded.find((d) => d.startsWith("nativeModule.handlers.inject_input_tape"));
    expect(reason).toMatch(/parameter named 'action', which every category's dispatcher consumes/);
    expect(reason).toMatch(/Rename it/);
  });
});
