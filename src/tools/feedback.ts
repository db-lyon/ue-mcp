import { z } from "zod";
import { categoryTool, type ToolDef, type ToolContext } from "../types.js";
import { submitFeedback } from "../github-app.js";

export const feedbackTool: ToolDef = categoryTool(
  "feedback",
  "Submit feedback to improve ue-mcp when native tools fall short and execute_python was used as a workaround.",
  {
    submit: {
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

        sections.push("", "---", "*Submitted via ue-mcp agent feedback*");

        const body = sections.join("\n");
        const result = await submitFeedback(title, body);

        return {
          message: "Feedback submitted successfully!",
          issue_url: result.url,
          issue_number: result.number,
        };
      },
    },
  },
  `- submit: Submit feedback about a tool gap. Params: title, summary, pythonWorkaround?, idealTool?`,
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
