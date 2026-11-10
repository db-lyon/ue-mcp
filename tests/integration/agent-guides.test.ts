/**
 * Agent guides and skills for every client, through the real CLI and server:
 * init writes its delimited block into AGENTS.md, GEMINI.md and
 * .github/copilot-instructions.md for the clients it configures, keeps the
 * user's own text, leaves a rerun unchanged and takes the block back out; the
 * server lists and serves the shipped skills as MCP prompts and resources
 * with no editor running.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { covers } from "../helpers/covers.js";
import { GUIDE_BEGIN, GUIDE_END } from "../../src/integrations/agent-guides.js";
import { listSkills, packagedSkillsRoot } from "../../src/extensions/skills.js";
import { McpServer, runCli, Sandbox, type CliOptions, type CliResult } from "./harness.js";

let sb: Sandbox;
const SKILLS = listSkills(packagedSkillsRoot());

beforeAll(() => {
  sb = new Sandbox("ue-mcp-guides-");
});
afterAll(() => sb.cleanup());

function cli(args: string[], opts: CliOptions = {}): Promise<CliResult> {
  return runCli(sb, args, { ...opts, env: { UE_MCP_TEST_ENGINE_ROOT: sb.path("no-engine"), ...opts.env } });
}

type Step = { id: string; status: string; target: string };
const guideSteps = (r: CliResult): Step[] => (JSON.parse(r.stdout).steps as Step[]).filter((s) => s.id.startsWith("guide:"));
const read = (file: string) => fs.readFileSync(file, "utf8");

describe("init agent guides", () => {
  it("writes a block per configured client's file, keeps user text, reruns unchanged and removes the block", async () => {
    covers("cli.init.agent-guides");
    const uproject = sb.makeProject("GuideGame");
    const dir = path.dirname(uproject);
    const agents = path.join(dir, "AGENTS.md");
    const gemini = path.join(dir, "GEMINI.md");
    const copilot = path.join(dir, ".github", "copilot-instructions.md");
    fs.writeFileSync(agents, "# Team rules\n\nRun the linter.\n");

    const first = await cli(["init", uproject, "--json", "--install", "source", "--clients", "cursor,gemini,vscode"]);
    expect(first.code, first.stdout + first.stderr).toBe(0);
    expect(guideSteps(first).map((s) => [s.id, s.status])).toEqual([
      ["guide:AGENTS.md", "changed"],
      ["guide:GEMINI.md", "changed"],
      ["guide:.github/copilot-instructions.md", "changed"],
    ]);
    const text = read(agents);
    expect(text.startsWith("# Team rules\n\nRun the linter.\n\n" + GUIDE_BEGIN)).toBe(true);
    expect(text.trimEnd().endsWith(GUIDE_END)).toBe(true);
    for (const name of SKILLS) expect(text).toContain(`skill://ue-mcp/${name}`);
    expect(read(gemini)).toContain(GUIDE_BEGIN);
    expect(read(copilot)).toContain(GUIDE_BEGIN);

    // A rerun with the same clients writes nothing.
    const again = await cli(["init", uproject, "--json", "--install", "source", "--clients", "cursor,gemini,vscode"]);
    expect(guideSteps(again).map((s) => s.status)).toEqual(["unchanged", "unchanged", "unchanged"]);
    expect(read(agents)).toBe(text);

    // Dropping clients removes their blocks, and a file only the block was in goes with it.
    const fewer = await cli(["init", uproject, "--json", "--install", "source", "--clients", "cursor"]);
    expect(guideSteps(fewer).map((s) => [s.id, s.status])).toEqual([
      ["guide:AGENTS.md", "unchanged"],
      ["guide:GEMINI.md", "removed"],
      ["guide:.github/copilot-instructions.md", "removed"],
    ]);
    expect(fs.existsSync(gemini)).toBe(false);
    expect(fs.existsSync(copilot)).toBe(false);

    // --no-agent-guides takes the last block out and leaves the user's text exactly.
    const off = await cli(["init", uproject, "--json", "--install", "source", "--clients", "cursor", "--no-agent-guides"]);
    expect(guideSteps(off).map((s) => [s.id, s.status])).toEqual([["guide:AGENTS.md", "removed"]]);
    expect(read(agents)).toBe("# Team rules\n\nRun the linter.\n");
  });

  it("writes no guide for a client that reads none", async () => {
    covers("cli.init.agent-guides");
    const uproject = sb.makeProject("ClaudeOnly");
    const r = await cli(["init", uproject, "--json", "--install", "source", "--clients", "claude-code"]);
    expect(r.code, r.stdout + r.stderr).toBe(0);
    expect(guideSteps(r)).toEqual([]);
    for (const f of ["AGENTS.md", "GEMINI.md", path.join(".github", "copilot-instructions.md")]) {
      expect(fs.existsSync(path.join(path.dirname(uproject), f)), f).toBe(false);
    }
  });
});

describe("skills over MCP", () => {
  it("lists and serves every shipped skill as a prompt and a resource with no editor", async () => {
    covers("server.skill-prompts");
    const server = await McpServer.start(sb, { projects: [sb.makeProject("PromptGame")] });
    try {
      const { prompts } = await server.client.listPrompts();
      expect(prompts.map((p) => p.name).sort()).toEqual(SKILLS);
      expect(prompts.every((p) => (p.description ?? "").length > 20)).toBe(true);

      const got = await server.client.getPrompt({ name: "ue-mcp-workflow" });
      expect(got.messages).toHaveLength(1);
      expect(got.messages[0].role).toBe("user");
      const body = (got.messages[0].content as { type: string; text: string }).text;
      expect(body.startsWith("---")).toBe(false);
      expect(body).toContain("get_status");

      const { resources } = await server.client.listResources();
      expect(resources.map((r) => r.uri).sort()).toEqual(SKILLS.map((n) => `skill://ue-mcp/${n}`));
      expect(resources.every((r) => r.mimeType === "text/markdown")).toBe(true);

      const resource = await server.client.readResource({ uri: "skill://ue-mcp/ue-mcp-workflow" });
      expect(resource.contents).toHaveLength(1);
      expect(resource.contents[0]).toMatchObject({ uri: "skill://ue-mcp/ue-mcp-workflow", mimeType: "text/markdown" });
      expect((resource.contents[0] as { text: string }).text).toBe(
        fs.readFileSync(path.join(packagedSkillsRoot(), "ue-mcp-workflow", "SKILL.md"), "utf8"),
      );
      await expect(server.client.readResource({ uri: "skill://ue-mcp/no-such-skill" })).rejects.toThrow();
    } finally {
      await server.close();
    }
  }, 60_000);
});
