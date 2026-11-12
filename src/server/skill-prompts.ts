/**
 * The shipped skills as MCP prompts and resources. A client that reads
 * neither `.claude/skills` nor an instruction file can still list the
 * workflow guides and pull one in: prompts/get returns the guide as a user
 * message, resources/read returns the SKILL.md as markdown.
 *
 * They come from this package and do not depend on an editor, so they are
 * listed with or without one.
 */
import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { shippedSkills, skillResourceUri, type ShippedSkill } from "../extensions/skill-catalog.js";
import type { StaticResource } from "./editor-resources.js";

/** A skill as a read-only resource. */
export function skillResources(skills: readonly ShippedSkill[] = shippedSkills()): StaticResource[] {
  return skills.map((s) => ({
    uri: skillResourceUri(s.name),
    name: s.name,
    title: `ue-mcp skill: ${s.name}`,
    description: s.description,
    mimeType: "text/markdown",
    text: s.text,
  }));
}

/** Register one argument-free prompt per skill. */
export function registerSkillPrompts(server: McpServer, skills: readonly ShippedSkill[] = shippedSkills()): void {
  for (const skill of skills) {
    server.registerPrompt(
      skill.name,
      { title: `ue-mcp skill: ${skill.name}`, description: skill.description },
      () => ({
        description: skill.description,
        messages: [{ role: "user" as const, content: { type: "text" as const, text: skill.body } }],
      }),
    );
  }
}
