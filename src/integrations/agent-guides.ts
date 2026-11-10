/**
 * The instruction files other agent clients read from a project: AGENTS.md
 * (Codex, Cursor), GEMINI.md (Gemini CLI) and .github/copilot-instructions.md
 * (VS Code). init writes one delimited block into each file a configured
 * client reads, and leaves every line outside the block as the user wrote it.
 *
 * The block is replaced whole on every run, so a rerun with nothing changed
 * writes nothing, and removing it takes out exactly what init put there.
 */
import * as fs from "node:fs";
import * as path from "node:path";
import { shippedSkills, skillResourceUri, type ShippedSkill } from "../extensions/skill-catalog.js";

export const GUIDE_BEGIN = "<!-- ue-mcp:begin (written by `npx ue-mcp init`; edit outside this block, it is replaced on every run) -->";
export const GUIDE_END = "<!-- ue-mcp:end -->";

export interface AgentGuideFile {
  /** Path from the project root, with forward slashes. */
  file: string;
  /** The client names (as detectMcpClients names them) that read this file. */
  clients: readonly string[];
}

/** Which client reads which file. Claude Code reads its skills and needs none of these. */
export const AGENT_GUIDE_FILES: readonly AgentGuideFile[] = [
  { file: "AGENTS.md", clients: ["Codex", "Codex (project)", "Cursor"] },
  { file: "GEMINI.md", clients: ["Gemini CLI"] },
  { file: ".github/copilot-instructions.md", clients: ["VS Code"] },
];

/** The guide files the selected clients read. */
export function guideFilesFor(selectedClients: readonly string[]): AgentGuideFile[] {
  const selected = new Set(selectedClients);
  return AGENT_GUIDE_FILES.filter((g) => g.clients.some((c) => selected.has(c)));
}

/** The text between the markers. */
export function guideBody(skills: readonly ShippedSkill[] = shippedSkills()): string {
  const lines = [
    "## Unreal Engine through ue-mcp",
    "",
    "This project's Unreal editor is driven through the `ue-mcp` MCP server.",
    "",
    "- Call `project(get_status)` first (in the default context, `tools(action=\"call\", category=\"project\", method=\"get_status\")`). It says whether the editor is connected and which project it has open.",
    "- Find an action with `tools(action=\"search\", query=\"...\")`, read its parameters with `tools(action=\"describe\", category=\"...\", method=\"...\")`, then call it. Prefer a dedicated action over `editor(execute_python)`.",
    "- Mutating actions return a `rollback` record; read state back after a change instead of assuming it landed.",
    "",
  ];
  if (skills.length > 0) {
    lines.push(
      "The server also serves these workflow guides as MCP prompts (prompts/get with the name) and resources (resources/read with the URI). Read the one that matches the task before starting it:",
      "",
    );
    for (const s of skills) {
      lines.push(`- \`${s.name}\` (\`${skillResourceUri(s.name)}\`): ${s.description.replace(/\s+/g, " ")}`);
    }
  }
  return lines.join("\n");
}

function eolOf(text: string): string {
  return text.includes("\r\n") ? "\r\n" : "\n";
}

/** The block's span in `text`, or null when it has none. */
function blockSpan(text: string): { start: number; end: number } | null {
  const start = text.indexOf(GUIDE_BEGIN);
  if (start < 0) return null;
  const endAt = text.indexOf(GUIDE_END, start);
  if (endAt < 0) return null;
  return { start, end: endAt + GUIDE_END.length };
}

/** `text` with the block written or replaced, in the file's own line endings. */
export function withGuideBlock(text: string, body: string): string {
  const eol = text === "" ? "\n" : eolOf(text);
  const block = [GUIDE_BEGIN, ...body.split("\n"), GUIDE_END].join(eol);
  const span = blockSpan(text);
  if (span) return text.slice(0, span.start) + block + text.slice(span.end);
  if (text.trim() === "") return block + eol;
  const trimmed = text.replace(/(\r?\n)*$/, "");
  return trimmed + eol + eol + block + eol;
}

/** `text` without the block and the blank line init put before it. */
export function withoutGuideBlock(text: string): string {
  const span = blockSpan(text);
  if (!span) return text;
  let before = text.slice(0, span.start);
  let after = text.slice(span.end);
  before = before.replace(/(\r?\n)+$/, "");
  after = after.replace(/^(\r?\n)+/, "");
  if (before === "") return after;
  const eol = eolOf(text);
  return after === "" ? before + eol : before + eol + eol + after;
}

export type GuideStatus = "changed" | "unchanged" | "removed";

/** Write or refresh the block in one file. Creates the file and its folder when absent. */
export function writeGuide(projectDir: string, file: string, body: string = guideBody()): GuideStatus {
  const target = path.join(projectDir, file);
  const before = fs.existsSync(target) ? fs.readFileSync(target, "utf8") : null;
  const after = withGuideBlock(before ?? "", body);
  if (after === before) return "unchanged";
  fs.mkdirSync(path.dirname(target), { recursive: true });
  fs.writeFileSync(target, after);
  return "changed";
}

/**
 * Take the block out of one file. A file left empty is deleted, since only
 * init's block was in it. Returns null when there was no block.
 */
export function removeGuide(projectDir: string, file: string): GuideStatus | null {
  const target = path.join(projectDir, file);
  if (!fs.existsSync(target)) return null;
  const before = fs.readFileSync(target, "utf8");
  if (!blockSpan(before)) return null;
  const after = withoutGuideBlock(before);
  if (after.trim() === "") {
    fs.unlinkSync(target);
    // .github goes too when the guide was all init put in it.
    const parent = path.dirname(target);
    if (path.resolve(parent) !== path.resolve(projectDir) && fs.readdirSync(parent).length === 0) fs.rmdirSync(parent);
  } else {
    fs.writeFileSync(target, after);
  }
  return "removed";
}

/** Whether the file holds init's block. */
export function hasGuide(projectDir: string, file: string): boolean {
  const target = path.join(projectDir, file);
  return fs.existsSync(target) && blockSpan(fs.readFileSync(target, "utf8")) !== null;
}
