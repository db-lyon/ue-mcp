/**
 * GET /v1/flows/<name>/plan carries `inputs`, the parameters a run may pass,
 * derived on the daemon from the step schemas and options_schema (#1508).
 */
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { Sandbox, stopSandboxDaemons } from "./harness.js";
import { call, startDaemonCli, type Discovery } from "./daemon-fakes.js";

interface Input {
  name: string;
  type: string | string[];
  conflict?: true;
  description?: string;
  required: boolean;
  steps: Array<{ path: string; task: string; flow: string }>;
}

let sb: Sandbox;
let d: Discovery;

beforeAll(async () => {
  sb = new Sandbox("ue-mcp-plan-inputs-");
  const project = sb.makeProject("Inputs", {
    config: [
      "ue-mcp:",
      "  version: 1",
      "tasks:",
      "  greet:",
      "    class_path: shell",
      '    description: "Greet someone. Params: command, cwd?"',
      "    options_schema:",
      "      who:",
      "        type: string",
      "        required: true",
      "        description: Who to greet",
      "flows:",
      "  textures:",
      "    steps:",
      '      "1":',
      "        task: asset.list_textures",
      "        options:",
      "          directory: /Game",
      "  integ_inputs:",
      "    description: A shell step, a task with an options_schema and a nested flow.",
      "    steps:",
      '      "1":',
      "        task: greet",
      "        options:",
      "          command: echo hi",
      '      "2":',
      "        flow: textures",
      "",
    ].join("\n"),
  });
  d = await startDaemonCli(sb, project);
});

afterAll(async () => {
  await stopSandboxDaemons(sb);
  sb.cleanup();
});

describe("flow plan inputs over the daemon", () => {
  it("derives a project flow's inputs from its tasks, nested flow included", async () => {
    covers("flow.plan.inputs");
    const plan = await call(d, "GET", "/v1/flows/integ_inputs/plan");
    expect(plan.status, plan.text).toBe(200);
    const inputs = plan.body.inputs as Input[];
    expect(inputs.map((i) => [i.name, i.type, i.required, i.steps.map((s) => `${s.path} ${s.task} ${s.flow}`)])).toEqual([
      ["who", "string", true, ["1 greet integ_inputs"]],
      ["cwd", "any", false, ["1 greet integ_inputs"]],
      ["cursor", "string", false, ["2/1 asset.list_textures textures"]],
      ["limit", "integer", false, ["2/1 asset.list_textures textures"]],
    ]);
    expect(inputs[0].description).toBe("Who to greet");
    // The plan and the declared definition are still there beside it.
    expect(plan.body.definition.steps["2"]).toMatchObject({ flow: "textures" });
  });

  it("derives a built-in flow's inputs", async () => {
    covers("flow.plan.inputs");
    const plan = await call(d, "GET", "/v1/flows/playtest/plan");
    expect(plan.status, plan.text).toBe(200);
    const inputs = plan.body.inputs as Input[];
    expect(inputs.map((i) => [i.name, i.required])).toEqual([
      ["target", true],
      ["context", false],
      ["profile", false],
      ["name", false],
      ["package_path", false],
    ]);
  });
});
