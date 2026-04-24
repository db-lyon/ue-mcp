import { z } from "zod";
import { categoryTool, type ToolDef, type ToolContext } from "../types.js";
import { submitFeedback } from "../github-app.js";
import { getWorkarounds, clearWorkarounds } from "../workaround-tracker.js";

// Map tool category names to GitHub labels
const CATEGORY_LABELS: Record<string, string[]> = {
  level:      ["level"],
  blueprint:  ["blueprint"],
  asset:      ["asset"],
  material:   ["material"],
  animation:  ["animation"],
  editor:     ["editor"],
  gameplay:   ["gameplay"],
  niagara:    ["niagara"],
  widget:     ["widget"],
  landscape:  ["landscape"],
  pcg:        ["pcg"],
  audio:      ["audio"],
  foliage:    ["foliage"],
  gas:        ["gas"],
  networking: ["networking"],
  reflection: ["reflection"],
  project:    ["project"],
  input:      ["input", "gameplay"],
};

function inferLabels(title: string, summary: string, idealTool?: string): string[] {
  const labels = new Set<string>(["agent-feedback"]);

  // Parse category from idealTool — e.g. "blueprint(action=foo)" or "asset(action=bar)"
  if (idealTool) {
    const match = idealTool.match(/^(\w+)\s*\(/);
    if (match) {
      const cat = match[1].toLowerCase();
      const mapped = CATEGORY_LABELS[cat];
      if (mapped) mapped.forEach((l) => labels.add(l));
    }
  }

  // Scan title + summary for category keywords as fallback
  const text = `${title} ${summary}`.toLowerCase();
  const keywords: [RegExp, string[]][] = [
    [/\bblueprint|bp\b|add_variable|add_node|set_class_default/,  ["blueprint"]],
    [/\blevel|actor|move_actor|place_actor|outliner/,              ["level"]],
    [/\basset|datatable|datatab|static.?mesh|texture|import/,     ["asset"]],
    [/\bmaterial|expression|shad/,                                 ["material"]],
    [/\bniagara|vfx|emitter|particle/,                            ["niagara"]],
    [/\bwidget|umg|ui\b|editor.?utility/,                         ["widget"]],
    [/\bgameplay|collision|nav.?mesh|physics|input|imc|pie\b/,    ["gameplay"]],
    [/\binput.?action|input.?mapping|enhanced.?input|imc\b/,      ["input", "gameplay"]],
    [/\banimation|anim.?bp|montage|skeleton|ik\b/,                ["animation"]],
    [/\blandscape|terrain|heightmap/,                              ["landscape"]],
    [/\bpcg|procedural/,                                          ["pcg"]],
    [/\baudio|sound|metasound/,                                   ["audio"]],
    [/\bgas\b|gameplay.?ability|gameplay.?effect/,                 ["gas"]],
    [/\breplicat|network|dormancy/,                               ["networking"]],
    [/\breflect|uclass|ustruct|uenum/,                            ["reflection"]],
    [/\bcrash|assert|exception/,                                  ["bug"]],
  ];
  for (const [re, cats] of keywords) {
    if (re.test(text)) cats.forEach((l) => labels.add(l));
  }

  // If we still only have agent-feedback, add enhancement as default type
  if (labels.size === 1) labels.add("enhancement");

  return [...labels];
}

export const feedbackTool: ToolDef = categoryTool(
  "feedback",
  "Submit feedback to improve ue-mcp when native tools fall short and execute_python was used as a workaround.",
  {
    submit: {
      description: "Submit feedback about a tool gap. Params: title, summary, pythonWorkaround?, idealTool?",
      handler: async (_ctx: ToolContext, params: Record<string, unknown>) => {
        const title = params.title as string;
        const summary = params.summary as string;
        const pythonWorkaround = params.pythonWorkaround as string | undefined;
        const idealTool = params.idealTool as string | undefined;

        const sections: string[] = ["## Summary", summary];

        if (idealTool) {
          sections.push("", "## Ideal Tool/Action", idealTool);
        }

        if (pythonWorkaround) {
          sections.push(
            "",
            "## Python Workaround Used",
            "```python",
            pythonWorkaround,
            "```",
          );
        }

        // Append session workaround log if any calls were tracked
        const workarounds = getWorkarounds();
        if (workarounds.length > 0) {
          sections.push("", "## Session Workaround Log", `${workarounds.length} execute_python call(s) this session:`, "");
          for (const w of workarounds) {
            sections.push(
              `### ${w.timestamp}`,
              "```python",
              w.code,
              "```",
              w.resultSnippet ? `> Result: \`${w.resultSnippet}\`` : "",
              "",
            );
          }
          clearWorkarounds();
        }

        sections.push("", "---", "*Submitted via ue-mcp agent feedback*");

        const body = sections.join("\n");
        const labels = inferLabels(title, summary, idealTool);
        const result = await submitFeedback(title, body, labels);

        return {
          message: "Feedback submitted successfully!",
          issue_url: result.url,
          issue_number: result.number,
          labels,
        };
      },
    },
  },
  undefined,
  {
    title: z
      .string()
      .describe("Short title describing the tool gap (do not include project-specific details)"),
    summary: z
      .string()
      .describe("What the user was trying to accomplish and why the native tool couldn't handle it"),
    pythonWorkaround: z
      .string()
      .optional()
      .describe("The execute_python code that was used as a workaround"),
    idealTool: z
      .string()
      .optional()
      .describe("What tool/action should have handled this natively (e.g. 'blueprint(action=set_variable_default)')"),
  },
);
