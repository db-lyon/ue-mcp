import { ALL_TOOLS } from "./tools.js";

/**
 * Counts and the category list, derived rather than transcribed (#817, plan
 * item 6.6).
 *
 * All three variants carried a hand-written "685+ actions" and a category list
 * missing `fab`, both of which had been wrong for many releases. A number a
 * human retypes on every surface change is a number that goes stale, and this
 * text is the first thing every agent reads, so being wrong here sends them
 * looking for actions that exist and past ones that do not. Reading the tool
 * graph makes both correct by construction.
 *
 * `tools.ts` is a leaf as far as this module is concerned: nothing in the tool
 * graph imports these instructions, so the import closes no cycle.
 */
const CATEGORY_COUNT = ALL_TOOLS.length;
const ACTION_COUNT = ALL_TOOLS.reduce((n, t) => n + Object.keys(t.actions).length, 0);

/** Every category name, wrapped so the block reads the way it always has. */
function categoryList(): string {
  const names = ALL_TOOLS.map((t) => t.name);
  const lines: string[] = [];
  let line = "";
  for (const name of names) {
    const next = line ? `${line}, ${name}` : name;
    if (next.length > 70) {
      lines.push(`${line},`);
      line = name;
    } else {
      line = next;
    }
  }
  if (line) lines.push(line);
  return lines.join("\n").replace(
    /\bepic\b(?![\w-])/,
    "epic (830 wrapped Unreal 5.8 tools; UE 5.8+)",
  );
}

const CATEGORIES = categoryList();

export const SERVER_INSTRUCTIONS = `UE-MCP: Unreal Engine editor bridge (C++ plugin) - ${CATEGORY_COUNT} category tools covering ${ACTION_COUNT} actions, plus 830 official Unreal 5.8 tools wrapped in-process (UE 5.8+; see the epic category).

Every tool takes an "action" parameter that selects the operation. Call project(action="get_status") first.

═══ QUICK START ═══
1. project(action="get_status") - check if the editor is connected
2. If not connected: editor(action="start_editor") to launch UE
3. level(action="get_outliner") - see what's in the current level
4. asset(action="list") - browse project assets
5. reflection(action="reflect_class", className="StaticMeshActor") - understand any UE class
6. demo(action="step", stepIndex=1) through 19 - run the Neon Shrine demo to see the bridge in action
7. demo(action="cleanup") - clean up after the demo

═══ TOOLS ═══

Every category tool lists its own actions (and each action's parameters) in
its description - read the description of the category you need. Categories:
${CATEGORIES}.

═══ TIPS ═══
• Start with level(action="get_outliner") or asset(action="list") to discover what's in the project.
• Use reflection(action="reflect_class") to understand any UE class's properties.
• asset(action="search", query="/Game/Characters/*") accepts wildcards.
• For BP scripting: blueprint(action="search_node_types") → blueprint(action="add_node") → blueprint(action="connect_pins").
• editor(action="execute_python") is the escape hatch for any Unreal Python API call.
• Animation tools need a skeleton path - use animation(action="list_skeletal_meshes") to find it.
• Editor lifecycle: editor(action="stop_editor") / editor(action="start_editor") / editor(action="restart_editor") manage the UE process. editor(action="build_project") builds the project C++ code (stop the editor first).
• editor(action="hot_reload") triggers Live Coding compilation without restarting the editor.
• editor(action="focus_on_actor", actorLabel="MyActor") snaps the viewport to any actor.
• Log output: editor(action="get_log", category="LogMCPBridge") to see bridge-specific logs.

═══ FLOWS - READ BEFORE ACTING ═══

Before you run bash/npm commands or chain 3+ category tool calls to
satisfy a user request, look at the \`flows\` field returned by
project(action="get_status").

That field lists named, pre-built sequences for this project. Each
entry has a name and description. If ANY flow's description matches
what the user asked for, you MUST run it instead of building the
sequence yourself.

Examples:
  User asks                          | Look for a flow like
  ---------------------------------- | ------------------------------
  "rebuild and relaunch the editor"  | rebuild
  "run the smoke tests"              | smoke
  "redeploy the plugin"              | deploy, redeploy
  "package the project"              | package

Run a matched flow with: flow(action="run", flowName="<name>")

DO NOT:
- Skip the get_status flows check before running bash/npm yourself.
- Author a new flow on your own. Only the user authors flows.
- Suggest a flow for a one-off task the user is unlikely to repeat.

DO suggest a new flow IF AND ONLY IF all three are true:
  1. You just finished a sequence with 3+ steps.
  2. The sequence had the same shape every run, with only 1-2 values
     changing.
  3. The user is likely to ask for the same shape again.
In that case say: "This sequence (X -> Y -> Z) might be worth registering
as a flow in ue-mcp.yml. Want me to draft one?" Then STOP. Wait.

═══ FEEDBACK ═══
If you had to use editor(action="execute_python") as a workaround because a native tool
couldn't handle the task, keep a mental note of what you did and why. When your task is
complete, tell the user:
  "I had to use custom Python scripts to [describe what]. Would you like to submit
   feedback to help improve ue-mcp?"
If the user agrees, call feedback(action="submit") with:
  • title - short, generic description of the gap (no project-specific details)
  • summary - what was attempted and why the native tool fell short
  • pythonWorkaround - the Python code that was used
  • idealTool - what tool/action should handle this natively
This creates a GitHub issue so the maintainers can add proper support.

Not every gap belongs to ue-mcp core. Plugins (PIE Studio, Perforce, Meshy, ...)
own their own surfaces and their own trackers. submit checks the plugin registry
and aims the issue at the owning repo on its own, and the approval prompt lets
the user change it - do NOT set the repo parameter yourself unless the user
names a repo. feedback(action="route") answers "where would this land?" without
posting anything.
`;

// Compact instructions used when context.strategy = "lean". The per-action
// catalog is intentionally omitted: agents pull it on demand via the `catalog`
// tool or a category's `describe` action. This keeps the initialize handshake
// small for token-constrained clients while preserving full capability.
export const SERVER_INSTRUCTIONS_LEAN = `UE-MCP (lean mode): Unreal Engine editor bridge (C++ plugin). ${CATEGORY_COUNT} category tools covering ${ACTION_COUNT} actions; the per-action catalog is loaded on demand to keep context small.

Every tool takes an "action" parameter that selects the operation. Start with project(action="get_status").

═══ DISCOVER ACTIONS ═══
Tool descriptions are trimmed in lean mode. Find the action you need with:
- catalog(action="search", query="spawn actor") - rank matching actions across every category
- catalog(action="list_categories") - the ${CATEGORY_COUNT} categories with one-line summaries
- <category>(action="describe") - every action in one category (e.g. blueprint(action="describe"))

Each category's "action" parameter is still a validated enum, so unknown actions are rejected up front. Call describe/search first when you are unsure of the exact action name.

═══ CATEGORIES ═══
${CATEGORIES}.

═══ FLOWS ═══
Before chaining 3+ tool calls, check the \`flows\` field from project(action="get_status")
and run a matching flow with flow(action="run", flowName="<name>") instead of rebuilding it.

═══ FEEDBACK ═══
If you had to fall back to editor(action="execute_python") because a native tool could not
do the job, tell the user when done and offer to feedback(action="submit") the gap. submit
routes the issue to the tracker that owns the surface (core, or the plugin that provides it)
by checking the plugin registry; feedback(action="route") previews that without posting.

Full mode (every action listed inline) is the default. This lean surface is selected by
context.strategy: lean in ue-mcp.yml or UE_MCP_CONTEXT_STRATEGY=lean.
`;

// Smallest surface (context.strategy = "micro"). The entire ue-mcp API is
// reached through one gateway tool, mirroring the native MCP toolset gateway
// (list_toolsets / describe_toolset / call_tool). Nothing else is advertised.
export const SERVER_INSTRUCTIONS_MICRO = `UE-MCP (micro mode): Unreal Engine editor bridge (C++ plugin). The entire surface (${CATEGORY_COUNT} categories, ${ACTION_COUNT} actions) is reached through a single gateway tool to keep context tiny.

═══ HOW TO USE ═══
- tools(action="list_categories") - list every category with a one-line summary
- tools(action="describe", category="blueprint") - list a category's actions and how to call them
- tools(action="call", category="blueprint", method="create", args={ ... }) - invoke any action

\`method\` is the action name; \`args\` is the object of that action's parameters.
Start with: tools(action="call", category="project", method="get_status").

═══ CATEGORIES ═══
${CATEGORIES}.

═══ FLOWS ═══
flow(action="run", flowName="<name>") runs a named sequence; see the \`flows\` field
from tools(action="call", category="project", method="get_status").

Full mode (every action listed inline) is the default. This micro surface is selected by
context.strategy: micro in ue-mcp.yml or UE_MCP_CONTEXT_STRATEGY=micro.
`;

/**
 * Targeting block, appended to whichever instruction variant is in use, and
 * only when this server drives more than one editor. At one editor the
 * initialize payload is exactly what it has always been.
 */
export function multiEditorInstructions(sessionNames: string[], activeName: string): string {
  return [
    "═══ MULTIPLE EDITORS ═══",
    `This server drives ${sessionNames.length} editors: ${sessionNames.join(", ")}.`,
    `Every call takes an optional \`editor\` parameter naming one of them. Calls without it run in the active editor (${activeName}).`,
    "",
    "- project(action=\"list_editors\") - every editor, its project, its bridge port and its state",
    "- project(action=\"use_editor\", editorTarget=\"<name>\") - move the default target",
    "- project(action=\"add_editor\", projectPath=\"...\") - register another project",
    "- project(action=\"drop_editor\", editorTarget=\"<name>\") - detach from an editor and leave it running",
    "",
    "Name the editor explicitly on anything that writes, and on every lifecycle call",
    "(start_editor / stop_editor / restart_editor / build_project), so the work lands where you intend.",
  ].join("\n");
}
