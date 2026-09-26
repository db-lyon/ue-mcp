import { categoryTool, type ActionSpec, type ToolDef } from "../types.js";
import { z } from "zod";
import { CURSOR_PARAM } from "../pagination.js";
import { schema as specSchema } from "./specs/project.generated.js";
import { actions as epicActions, schema as epicSchema } from "./epic/project.generated.js";
import { sessionActions } from "./project/sessions.js";
import { configActions } from "./project/config.js";
import { sourceActions } from "./project/source.js";
import { engineActions } from "./project/engine.js";
import { surfaceActions } from "./project/surface.js";
import { installActions } from "./project/install.js";

export { envWarningsFor } from "./project/sessions.js";

/**
 * The order project advertises its actions in, which the golden baseline pins.
 * Each group module owns its actions; this only positions them, and a name
 * missing from either side fails at load.
 */
const ACTION_ORDER = [
  "get_status",
  "set_project",
  "list_editors",
  "use_editor",
  "add_editor",
  "drop_editor",
  "get_info",
  "read_config",
  "search_config",
  "list_config_tags",
  "read_cpp_header",
  "read_module",
  "list_modules",
  "search_cpp",
  "read_engine_header",
  "find_engine_symbol",
  "list_engine_modules",
  "search_engine_cpp",
  "search_tools",
  "describe_action",
  "list_available_actions",
  "list_content_assets",
  "check_install",
  "execute_python_report",
  "list_files",
  "set_config",
  "build",
  "generate_project_files",
  "create_cpp_class",
  "list_project_modules",
  "list_loaded_modules",
  "is_module_loaded",
  "list_available_plugins",
  "enable_plugin",
  "disable_plugin",
  "live_coding_compile",
  "live_coding_status",
  "resolve_collision_profile",
  "write_cpp_file",
  "read_cpp_source",
  "write_source_file",
  "read_source_file",
  "build_engine_index",
  "verify_symbols",
  "suggest_build_deps",
  "find_example_usage",
  "class_hierarchy",
  "find_references",
  "find_callers",
  "find_callees",
  "symbol_context",
  "lint_cpp_header",
  "add_module_dependency",
  "add_cpp_member",
] as const;

function inAdvertisedOrder(...groups: Record<string, ActionSpec>[]): Record<string, ActionSpec> {
  const all: Record<string, ActionSpec> = Object.assign({}, ...groups);
  const ordered: Record<string, ActionSpec> = {};
  for (const name of ACTION_ORDER) {
    if (!all[name]) throw new Error(`project: '${name}' is ordered but no group declares it`);
    ordered[name] = all[name];
  }
  const unordered = Object.keys(all).filter((n) => !(n in ordered));
  if (unordered.length > 0) throw new Error(`project: ${unordered.join(", ")} missing from ACTION_ORDER`);
  return ordered;
}

export const projectTool: ToolDef = categoryTool(
  "project",
  "Project status and editor connection: get_status (is the editor connected?), set_project (switch/redirect the bridge to another .uproject), get_info. Also config INI files, module load state, and C++ source inspection. Call project(get_status) first in any session.",
  {
    ...inAdvertisedOrder(sessionActions, configActions, sourceActions, engineActions, surfaceActions, installActions),
    ...epicActions,
  },
  {
    ...epicSchema,
    // #1057: every key a spec'd handler declares, generated from its C++
    // registration. A key listed again below is shared with hand-written
    // actions, and tests/unit/handler-specs.test.ts holds the two to one type.
    ...specSchema,
    projectPath: z.string().optional().describe("For set_project / add_editor / check_install: path to .uproject"),
    editorName: z.string().optional().describe("For add_editor: name to address the new session by (default the project name) (#817)"),
    editorTarget: z.string().optional().describe("For use_editor / drop_editor: session name, project name, or .uproject path (#817)"),
    start: z.boolean().optional().describe("For add_editor: launch the editor for that project and wait until it is ready (#817)"),
    timeout: z.number().optional().describe("For add_editor with start: seconds to wait for readiness (default 300)"),
    configName: z.string().optional().describe("For read_config/set_config: config file name"),
    query: z.string().optional().describe("For search_config/search_cpp: search text"),
    headerPath: z.string().optional().describe("For read_cpp_header: path to .h file"),
    moduleName: z.string().optional().describe("For read_module / is_module_loaded: module name. create_cpp_class: project module to add the class to (default the first)"),
    filter: z.string().optional().describe("For list_loaded_modules and list_available_plugins: case-insensitive name substring (#689)"),
    loadedOnly: z.boolean().optional().describe("For list_loaded_modules: only loaded modules (#689)"),
    limit: z.number().optional().describe("Max results: search_tools (default 20), find_example_usage (10), find_references (40), find_callers (25), find_callees (100), class_hierarchy descendants (100). The paged list actions: rows on this page (#704)"),
    // The paged list actions in this category resume on a cursor. `limit`
    // is already declared above and shared with the unpaged readers.
    cursor: CURSOR_PARAM,
    name: z.string().optional().describe("describe_action: the action to describe, as 'tool.action' or a bare action name"),
    category: z.string().optional().describe("describe_action / list_available_actions: narrow to one category instead of the whole surface"),
    includeNames: z.boolean().optional().describe("list_available_actions: list the action names, not just the counts (default false)"),
    // Stays a strict enum on purpose: listAvailableActions treats anything that
    // is neither "all" nor "available" as "blocked", so nothing rejects a typo.
    // Relaxing it would answer a request for the available half with the
    // blocked half and report success.
    state: z.enum(["available", "blocked", "all"]).optional().describe("list_available_actions: which side of the line to list when includeNames=true (default available)"),
    skipToolchain: z.boolean().optional().describe("check_install: skip the C++ toolchain probe, which shells out to vswhere or the compiler"),
    contentPath: z.string().optional().describe("list_content_assets: mount path to list, e.g. /Game or /Game/Characters (default /Game)"),
    namePattern: z.string().optional().describe("list_content_assets: case-insensitive substring the asset name must contain"),
    extensions: z.union([z.string(), z.array(z.string())]).optional().describe("For list_files: extension filter (#608)"),
    recursive: z.boolean().optional().describe("For list_files / list_content_assets: recurse into subdirectories (#608)"),
    directory: z.string().optional().describe("For search_cpp: subdirectory"),
    configuration: z.string().optional().describe("Build configuration: Development, Debug, Shipping"),
    platform: z.string().optional().describe("Target platform: Win64, Linux, Mac"),
    clean: z.boolean().optional().describe("Clean build"),
    symbol: z.string().optional().describe("Symbol name for find_engine_symbol / find_example_usage / class_hierarchy / find_references / find_callers / find_callees / symbol_context"),
    names: z.union([z.string(), z.array(z.string())]).optional().describe("verify_symbols / suggest_build_deps: engine symbol names, as an array or a comma-separated string (max 200)"),
    refresh: z.boolean().optional().describe("build_engine_index: rebuild even when a valid cache exists"),
    buildCsPath: z.string().optional().describe("suggest_build_deps / lint_cpp_header: absolute path to a .Build.cs (defaults to the one owning the target)"),
    modulePath: z.string().optional().describe("suggest_build_deps: a file or directory whose owning Build.cs to read"),
    trees: z.union([z.string(), z.array(z.string())]).optional().describe("find_example_usage / find_references / find_callers / find_callees: engine trees to search - Runtime|Editor|Developer|Plugins|all (default Runtime)"),
    // Stays a strict enum on purpose: classHierarchy tests the value against
    // "ancestors" and "descendants" by inequality, so any other string walks
    // both directions and the caller is never told its value was not understood.
    direction: z.enum(["ancestors", "descendants", "both"]).optional().describe("class_hierarchy: walk up, down, or both (default both)"),
    depth: z.number().optional().describe("class_hierarchy: generations of descendants to report (default 1; every transitive subclass of UObject is tens of thousands of names)"),
    includeProject: z.boolean().optional().describe("find_references / find_callers: also search this project's own Source and Plugins trees (default true)"),
    contextBefore: z.number().optional().describe("symbol_context: lines of source before the declaration (default 8)"),
    contextAfter: z.number().optional().describe("symbol_context: lines of source after the declaration (default 40)"),
    maxResults: z.number().optional().describe("Cap on find_engine_symbol / search_engine_cpp / list_content_assets hits (default 100 / 500 / 1000)"),
    tree: z.string().optional().describe("For search_engine_cpp: Runtime|Editor|Developer|Plugins|all (default Runtime)"),
    subdirectory: z.string().optional().describe("For search_engine_cpp: subdirectory within the chosen tree"),

    // v0.7.13 - native C++ authoring
    path: z.string().optional().describe("For write_cpp_file: path to write (relative to Source/ or absolute within Source/)."),
    content: z.string().optional().describe("For write_cpp_file: full file contents."),
    sourcePath: z.string().optional().describe("For read_cpp_source: path to .cpp (relative to Source/ or absolute)."),
    module: z.string().optional().describe("For write_source_file/read_source_file: module name (default project's primary module). Plugin modules are resolved too (#543)."),
    visibility: z.string().optional().describe("For write_source_file/read_source_file: Public or Private (default Private on write)."),
    fileName: z.string().optional().describe("For write_source_file/read_source_file: file name e.g. MyComponent.h."),
    dependency: z.string().optional().describe("For add_module_dependency: module name to add (e.g. 'UMG')."),
    declaration: z.string().optional().describe("For add_cpp_member: full UPROPERTY(...) / UFUNCTION(...) block plus the member or function signature."),
    memberName: z.string().optional().describe("For add_cpp_member: the identifier the declaration introduces (used for idempotency)."),
    // Deliberately a string, not z.enum. The MCP SDK validates arguments BEFORE
    // the tool callback runs, so a strict enum makes a typo fail at the transport
    // with a schema error, and the handler's own message, which names both valid
    // values, never reaches the caller. add_module_dependency rejects an unknown
    // access by name.
    access: z.string().optional().describe("For add_module_dependency: 'public' (PublicDependencyModuleNames) or 'private' (default)."),
    profileName: z.string().optional().describe("For resolve_collision_profile: the profile to resolve, e.g. 'Pawn', 'BlockAll', or one the project defined."),
    channel: z.string().optional().describe("For resolve_collision_profile: narrow the answer to one channel. Accepts the configured name ('Camera', 'Weapon'), the C++ enumerator ('ECC_Camera'), or the container index."),
    includeAllChannels: z.boolean().optional().describe("For resolve_collision_profile: include the unused GameTraceChannel slots as well as the engine channels and the project's own. Default false."),
  },
);
