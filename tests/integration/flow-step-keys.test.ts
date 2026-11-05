/**
 * A flow step key is its run order. A named key, in a project flow or a
 * plugin's, is refused with that rule instead of crashing inside the runner.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { McpServer, Sandbox } from "./harness.js";

let sb: Sandbox;
let server: McpServer;

beforeAll(async () => {
  sb = new Sandbox("ue-mcp-step-keys-");
  const uproject = sb.makeProject("Keys", {
    config: [
      "ue-mcp:",
      "  version: 1",
      "plugins:",
      "  - name: ue-mcp-named-steps",
      "flows:",
      "  named:",
      "    description: Steps keyed by name.",
      "    steps:",
      "      read:",
      "        task: project.get_info",
      "",
    ].join("\n"),
  });
  const pkg = path.join(path.dirname(uproject), "node_modules", "ue-mcp-named-steps");
  fs.mkdirSync(pkg, { recursive: true });
  fs.writeFileSync(path.join(pkg, "package.json"), JSON.stringify({ name: "ue-mcp-named-steps", version: "1.0.0", type: "module" }));
  fs.writeFileSync(
    path.join(pkg, "ue-mcp.plugin.yml"),
    ["actionPrefix: named", "flows:", "  named_pack:", "    description: Plugin flow keyed by name.", "    steps:", "      greet:", "        task: project.get_info", ""].join("\n"),
  );
  server = await McpServer.start(sb, { projects: [uproject], strategy: "full" });
});

afterAll(async () => {
  await server?.close();
  sb.cleanup();
});

describe("named flow step keys", () => {
  it("are refused by flow(plan) and flow(run) with the run-order rule", async () => {
    covers("flow.plan", "flow.run.yaml");
    for (const action of ["plan", "run"]) {
      const r = await server.call("flow", { action, flowName: "named" });
      expect(r.isError, `${action}: ${r.text}`).toBe(true);
      expect(r.text).toContain(`Flow 'named' has step key "read"`);
      expect(r.text).toContain("run order");
      expect(r.text).not.toContain("Cannot read properties");
    }
  });

  it("make a plugin's manifest invalid, and say why", async () => {
    covers("plugin.load.flows");
    const r = await server.call("plugins", { action: "list" });
    expect(r.text).toContain("ue-mcp-named-steps");
    expect(r.text).toContain("run order");
    const flows = await server.call("flow", { action: "list" });
    expect(flows.text).not.toContain("named_pack");
  });
});
