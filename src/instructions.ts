export const SERVER_INSTRUCTIONS = `UE-MCP: Unreal Engine editor bridge (C++ plugin) - 23 category tools covering 630+ actions, plus 830 official Unreal 5.8 tools wrapped in-process (UE 5.8+; see the epic category).

Every tool takes an "action" parameter that selects the operation. Call project(action="get_status") first.

═══ QUICK START ═══
1. project(action="get_status") — check if the editor is connected
2. If not connected: editor(action="start_editor") to launch UE
3. level(action="get_outliner") — see what's in the current level
4. asset(action="list") — browse project assets
5. reflection(action="reflect_class", className="StaticMeshActor") — understand any UE class
6. demo(action="step", stepIndex=1) through 19 — run the Neon Shrine demo to see the bridge in action
7. demo(action="cleanup") — clean up after the demo

═══ TOOLS ═══

Every category tool lists its own actions (and each action's parameters) in
its description - read the description of the category you need. Categories:
project, asset, blueprint, level, material, animation, landscape, pcg, foliage,
niagara, audio, widget, editor, reflection, gameplay, gas, networking, demo,
feedback, statetree, chooser, plugins, epic (830 wrapped Unreal 5.8 tools; UE 5.8+).

═══ TIPS ═══
• Start with level(action="get_outliner") or asset(action="list") to discover what's in the project.
• Use reflection(action="reflect_class") to understand any UE class's properties.
• asset(action="search", query="/Game/Characters/*") accepts wildcards.
• For BP scripting: blueprint(action="search_node_types") → blueprint(action="add_node") → blueprint(action="connect_pins").
• editor(action="execute_python") is the escape hatch for any Unreal Python API call.
• Animation tools need a skeleton path — use animation(action="list_skeletal_meshes") to find it.
• Editor lifecycle: editor(action="stop_editor") / editor(action="start_editor") / editor(action="restart_editor") manage the UE process. editor(action="build_project") builds the project C++ code (stop the editor first).
• editor(action="hot_reload") triggers Live Coding compilation without restarting the editor.
• editor(action="focus_on_actor", actorLabel="MyActor") snaps the viewport to any actor.
• Log output: editor(action="get_log", category="LogMCPBridge") to see bridge-specific logs.

═══ FLOWS — READ BEFORE ACTING ═══

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
  • title — short, generic description of the gap (no project-specific details)
  • summary — what was attempted and why the native tool fell short
  • pythonWorkaround — the Python code that was used
  • idealTool — what tool/action should handle this natively
This creates a GitHub issue so the maintainers can add proper support.
`;

// Compact instructions used when context.strategy = "lean". The per-action
// catalog is intentionally omitted: agents pull it on demand via the `catalog`
// tool or a category's `describe` action. This keeps the initialize handshake
// small for token-constrained clients while preserving full capability.
export const SERVER_INSTRUCTIONS_LEAN = `UE-MCP (lean mode): Unreal Engine editor bridge (C++ plugin). 23 category tools; the per-action catalog is loaded on demand to keep context small.

Every tool takes an "action" parameter that selects the operation. Start with project(action="get_status").

═══ DISCOVER ACTIONS ═══
Tool descriptions are trimmed in lean mode. Find the action you need with:
- catalog(action="search", query="spawn actor") - rank matching actions across every category
- catalog(action="list_categories") - the 22 categories with one-line summaries
- <category>(action="describe") - every action in one category (e.g. blueprint(action="describe"))

Each category's "action" parameter is still a validated enum, so unknown actions are rejected up front. Call describe/search first when you are unsure of the exact action name.

═══ CATEGORIES ═══
project, asset, blueprint, level, material, animation, landscape, pcg, foliage,
niagara, audio, widget, editor, reflection, gameplay, gas, networking, demo,
feedback, statetree, chooser, plugins, epic (830 wrapped Unreal 5.8 tools; UE 5.8+).

═══ FLOWS ═══
Before chaining 3+ tool calls, check the \`flows\` field from project(action="get_status")
and run a matching flow with flow(action="run", flowName="<name>") instead of rebuilding it.

═══ FEEDBACK ═══
If you had to fall back to editor(action="execute_python") because a native tool could not
do the job, tell the user when done and offer to feedback(action="submit") the gap.

Full mode (every action listed inline) is the default. This lean surface is selected by
context.strategy: lean in ue-mcp.yml or UE_MCP_CONTEXT_STRATEGY=lean.
`;

// Smallest surface (context.strategy = "micro"). The entire ue-mcp API is
// reached through one gateway tool, mirroring the native MCP toolset gateway
// (list_toolsets / describe_toolset / call_tool). Nothing else is advertised.
export const SERVER_INSTRUCTIONS_MICRO = `UE-MCP (micro mode): Unreal Engine editor bridge (C++ plugin). The entire surface (22 categories, 600+ actions) is reached through a single gateway tool to keep context tiny.

═══ HOW TO USE ═══
- tools(action="list_categories") - list every category with a one-line summary
- tools(action="describe", category="blueprint") - list a category's actions and how to call them
- tools(action="call", category="blueprint", method="create", args={ ... }) - invoke any action

\`method\` is the action name; \`args\` is the object of that action's parameters.
Start with: tools(action="call", category="project", method="get_status").

═══ CATEGORIES ═══
project, asset, blueprint, level, material, animation, landscape, pcg, foliage,
niagara, audio, widget, editor, reflection, gameplay, gas, networking, demo,
feedback, statetree, chooser, plugins, epic (830 wrapped Unreal 5.8 tools; UE 5.8+).

═══ FLOWS ═══
flow(action="run", flowName="<name>") runs a named sequence; see the \`flows\` field
from tools(action="call", category="project", method="get_status").

Full mode (every action listed inline) is the default. This micro surface is selected by
context.strategy: micro in ue-mcp.yml or UE_MCP_CONTEXT_STRATEGY=micro.
`;
