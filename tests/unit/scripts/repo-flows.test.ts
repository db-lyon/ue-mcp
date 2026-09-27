/**
 * `npm run specs:record` and `npm run epic:record` are repo flows declared in
 * tests/ue_mcp/ue-mcp.yml and run through the shipped flow tool. Fed the
 * answers the committed recordings were made from, each writes those
 * recordings back byte for byte, which is what the scripts they replaced did.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, describe, expect, it } from "vitest";
import { runRepoFlow } from "../../../scripts/repo-flow.js";
import type { IBridge } from "../../../src/bridge/bridge.js";
import type { FlowConfig } from "../../../src/flow/schema.js";
import { repoPath } from "../../helpers/repo-root.js";

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-repo-flow-"));
afterAll(() => fs.rmSync(tmp, { recursive: true, force: true }));

function scripted(answers: Record<string, unknown>) {
  const calls: Array<{ method: string; params?: Record<string, unknown> }> = [];
  const bridge = {
    isConnected: true,
    capabilities: null,
    call: async (method: string, params?: Record<string, unknown>) => {
      calls.push({ method, params });
      return answers[method] ?? { success: false, error: `unscripted ${method}` };
    },
    connect: async () => {},
    getTarget: () => ({ projectPath: null, port: 0, portSource: "default", verified: true }),
  } as unknown as IBridge;
  return { bridge, calls };
}

/** Point one write task at a temp file instead of the committed recording. */
const writeTo = (task: string, out: string) => (config: FlowConfig) => {
  const def = (config.tasks as Record<string, { options?: Record<string, unknown> }>)[task];
  def.options = { ...(def.options ?? {}), out };
};

describe("specs_record", () => {
  const golden = repoPath("tests", "golden", "handler-specs.json");
  const recorded = JSON.parse(fs.readFileSync(golden, "utf8")) as { handlers: Record<string, unknown> };

  it("asks the bridge for its capabilities and writes the recording it was made from", async () => {
    // Registration order is not a diff, so the answer arrives unsorted.
    const specs = Object.fromEntries(Object.entries(recorded.handlers).reverse());
    const { bridge, calls } = scripted({ get_bridge_capabilities: { protocolVersion: 1, handlerSpecs: specs } });
    const out = path.join(tmp, "handler-specs.json");
    const result = await runRepoFlow("specs_record", bridge, writeTo("handler_specs_write", out));
    expect(result.success, result.summary).toBe(true);
    expect(calls).toEqual([{ method: "get_bridge_capabilities", params: {} }]);
    expect(fs.readFileSync(out, "utf8") === fs.readFileSync(golden, "utf8")).toBe(true);
  });

  it("writes nothing when the plugin published no specs", async () => {
    const { bridge } = scripted({ get_bridge_capabilities: { protocolVersion: 1 } });
    const out = path.join(tmp, "none.json");
    const result = await runRepoFlow("specs_record", bridge, writeTo("handler_specs_write", out));
    expect(result.success).toBe(false);
    expect(result.summary).toContain("published no handler specs");
    expect(fs.existsSync(out)).toBe(false);
  });
});

describe("epic_record", () => {
  const golden = repoPath("tests", "golden", "epic-catalog.json");
  const recorded = JSON.parse(fs.readFileSync(golden, "utf8")) as { engineAssociation: string; toolsets: Array<{ tools?: unknown[] }> };
  const uproject = JSON.parse(fs.readFileSync(repoPath("tests", "ue_mcp", "ue_mcp.uproject"), "utf8")) as { EngineAssociation: string };

  it("asks for the catalog with schemas and writes the recording it was made from", async () => {
    // The drift test keeps the committed catalog on the test project's engine.
    expect(recorded.engineAssociation).toBe(uproject.EngineAssociation);
    const shuffled = [...recorded.toolsets].reverse().map((ts) => ({ ...ts, tools: [...(ts.tools ?? [])].reverse() }));
    const { bridge, calls } = scripted({ epic_list_toolsets: { toolsets: shuffled } });
    const out = path.join(tmp, "epic-catalog.json");
    const result = await runRepoFlow("epic_record", bridge, writeTo("epic_catalog_write", out));
    expect(result.success, result.summary).toBe(true);
    expect(calls).toEqual([{ method: "epic_list_toolsets", params: { includeSchemas: true } }]);
    expect(fs.readFileSync(out, "utf8") === fs.readFileSync(golden, "utf8")).toBe(true);
  });

  it("writes nothing when the editor registered no toolsets", async () => {
    const { bridge } = scripted({ epic_list_toolsets: { toolsets: [] } });
    const out = path.join(tmp, "empty.json");
    const result = await runRepoFlow("epic_record", bridge, writeTo("epic_catalog_write", out));
    expect(result.success).toBe(false);
    expect(fs.existsSync(out)).toBe(false);
  });
});
