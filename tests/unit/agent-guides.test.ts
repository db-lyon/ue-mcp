/**
 * The block init writes into AGENTS.md, GEMINI.md and
 * .github/copilot-instructions.md: written once, replaced whole, never
 * touching the user's own lines, and removable without a trace. The shipped
 * skills it names are the same ones the server serves as prompts and resources.
 */
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { afterAll, describe, expect, it } from "vitest";
import {
  AGENT_GUIDE_FILES,
  GUIDE_BEGIN,
  GUIDE_END,
  guideBody,
  guideFilesFor,
  hasGuide,
  removeGuide,
  withGuideBlock,
  withoutGuideBlock,
  writeGuide,
} from "../../src/integrations/agent-guides.js";
import { listSkills, packagedSkillsRoot } from "../../src/extensions/skills.js";
import { parseSkill, shippedSkills, skillResourceUri } from "../../src/extensions/skill-catalog.js";
import { skillResources } from "../../src/server/skill-prompts.js";

const dir = fs.mkdtempSync(path.join(os.tmpdir(), "ue-mcp-guides-"));
afterAll(() => fs.rmSync(dir, { recursive: true, force: true }));

describe("guide block text", () => {
  it("appends to user text after a blank line and replaces itself on the next write", () => {
    const user = "# My project\n\nKeep tests green.\n";
    const once = withGuideBlock(user, "first");
    expect(once).toBe(`# My project\n\nKeep tests green.\n\n${GUIDE_BEGIN}\nfirst\n${GUIDE_END}\n`);
    const twice = withGuideBlock(once, "second");
    expect(twice).toBe(once.replace("first", "second"));
    expect(withGuideBlock(twice, "second")).toBe(twice);
  });

  it("keeps text after the block and the file's CRLF line endings", () => {
    const text = `intro\r\n\r\n${GUIDE_BEGIN}\r\nold\r\n${GUIDE_END}\r\n\r\nafter\r\n`;
    expect(withGuideBlock(text, "a\nb")).toBe(`intro\r\n\r\n${GUIDE_BEGIN}\r\na\r\nb\r\n${GUIDE_END}\r\n\r\nafter\r\n`);
    expect(withoutGuideBlock(text)).toBe("intro\r\n\r\nafter\r\n");
  });

  it("removes exactly what it added", () => {
    const user = "# Mine\n\nline\n";
    expect(withoutGuideBlock(withGuideBlock(user, "x"))).toBe(user);
    expect(withoutGuideBlock(withGuideBlock("", "x")).trim()).toBe("");
    expect(withoutGuideBlock(user)).toBe(user);
  });
});

describe("guide files", () => {
  it("maps each client to the file it reads", () => {
    expect(guideFilesFor(["Codex (project)"]).map((g) => g.file)).toEqual(["AGENTS.md"]);
    expect(guideFilesFor(["Gemini CLI", "VS Code"]).map((g) => g.file)).toEqual(["GEMINI.md", ".github/copilot-instructions.md"]);
    expect(guideFilesFor(["Claude Code (project)", "Claude Desktop"])).toEqual([]);
    expect(AGENT_GUIDE_FILES.map((g) => g.file)).toEqual(["AGENTS.md", "GEMINI.md", ".github/copilot-instructions.md"]);
  });

  it("writes, rewrites as unchanged, and removes, deleting a file only init's block was in", () => {
    const project = fs.mkdtempSync(path.join(dir, "p-"));
    expect(writeGuide(project, ".github/copilot-instructions.md", "body")).toBe("changed");
    expect(writeGuide(project, ".github/copilot-instructions.md", "body")).toBe("unchanged");
    expect(hasGuide(project, ".github/copilot-instructions.md")).toBe(true);
    expect(removeGuide(project, ".github/copilot-instructions.md")).toBe("removed");
    expect(fs.existsSync(path.join(project, ".github", "copilot-instructions.md"))).toBe(false);
    expect(removeGuide(project, ".github/copilot-instructions.md")).toBeNull();

    fs.writeFileSync(path.join(project, "AGENTS.md"), "# Team rules\n");
    writeGuide(project, "AGENTS.md", "body");
    expect(removeGuide(project, "AGENTS.md")).toBe("removed");
    expect(fs.readFileSync(path.join(project, "AGENTS.md"), "utf8")).toBe("# Team rules\n");
  });

  it("names every shipped skill with its resource URI", () => {
    const body = guideBody();
    for (const name of listSkills(packagedSkillsRoot())) expect(body).toContain(`\`${name}\` (\`${skillResourceUri(name)}\`)`);
    expect(body).not.toMatch(/\u2014/);
  });
});

describe("shipped skills", () => {
  it("reads name, description and body from each SKILL.md", () => {
    const skills = shippedSkills();
    expect(skills.map((s) => s.name)).toEqual(listSkills(packagedSkillsRoot()));
    for (const s of skills) {
      expect(s.description.length, s.name).toBeGreaterThan(20);
      expect(s.body.startsWith("---"), s.name).toBe(false);
      expect(s.text.startsWith("---"), s.name).toBe(true);
    }
    expect(parseSkill("no frontmatter")).toEqual({ body: "no frontmatter" });
  });

  it("serves each as a markdown resource", () => {
    const resources = skillResources();
    expect(resources.map((r) => r.uri)).toEqual(shippedSkills().map((s) => skillResourceUri(s.name)));
    expect(resources.every((r) => r.mimeType === "text/markdown" && r.text.length > 100)).toBe(true);
  });
});
