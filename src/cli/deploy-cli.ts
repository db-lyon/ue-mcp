import * as path from "node:path";
import { ProjectContext } from "../config/project.js";
import { deploy } from "../editor/deployer.js";
import { coreSkillsInstalled, conflictMessages, installCoreSkills, syncPluginSkills } from "../extensions/skills.js";
import { takeEditorTarget, EditorFlagError } from "./editor-flag.js";
import { findUProject } from "../config/uproject-path.js";
import { RESET, BOLD, RED, DIM, CYAN, ok, fail } from "./ui/ansi.js";

async function deployCmd(argv: string[]) {
  console.log("");
  console.log(`  ${BOLD}${CYAN}UE-MCP Deploy${RESET}`);
  console.log("");

  // Find project: --editor, then CLI arg, then cwd.
  let target: { projectPath?: string; rest: string[] };
  try {
    target = takeEditorTarget(argv);
  } catch (e) {
    fail(e instanceof EditorFlagError ? e.message : String(e));
    process.exit(1);
  }
  const uprojectPath = target.projectPath || target.rest[0] || findUProject(process.cwd());

  if (!uprojectPath) {
    fail("No .uproject found. Run from your project directory or pass the path.");
    process.exit(1);
  }

  const project = new ProjectContext();
  try {
    project.setProject(uprojectPath);
  } catch (e) {
    fail(e instanceof Error ? e.message : String(e));
    process.exit(1);
  }

  ok(`Project: ${project.projectName} (UE ${project.engineAssociation ?? "?"})`);

  const result = deploy(project);

  if (result.error) {
    fail(`Deploy failed: ${result.error}`);
    process.exit(1);
  }

  if (result.cppPluginDeployed) {
    ok("Plugin sources updated - rebuild required");
  } else {
    ok("Plugin already up to date");
  }

  if (result.pythonPluginEnabled) ok("Enabled PythonScriptPlugin");
  if (result.cppPluginEnabled) ok("Enabled UE_MCP_Bridge");

  // Refresh ue-mcp's skills if the project uses them, and every plugin's.
  const skillResults = coreSkillsInstalled(project.projectDir!)
    ? { "ue-mcp": installCoreSkills(project.projectDir!) }
    : {};
  const pluginSkills = syncPluginSkills(project.projectDir!, path.join(project.projectDir!, "ue-mcp.yml"));
  for (const [owner, r] of Object.entries({ ...skillResults, ...pluginSkills.plugins })) {
    for (const line of conflictMessages(r)) fail(line);
    if (r.installed.length > 0) ok(`${owner} skills refreshed: ${r.installed.join(", ")}`);
  }

  console.log("");
  if (result.cppPluginDeployed) {
    console.log(`  ${DIM}C++ sources changed - the plugin must be rebuilt before the editor will see new handlers.${RESET}`);
    console.log(`  ${DIM}From the project root:${RESET}`);
    console.log(`  ${DIM}  ue-mcp build${RESET}`);
    console.log(`  ${DIM}Then start (or restart) the editor.${RESET}`);
  } else {
    console.log(`  ${DIM}No changes needed.${RESET}`);
  }
  console.log("");
}

/** Entry point for `ue-mcp deploy [project] [--editor <name-or-path>]`. */
export async function run(argv: string[]): Promise<number | void> {
  try {
    await deployCmd(argv);
  } catch (e) {
    console.error(`\n  ${RED}Fatal error: ${e instanceof Error ? e.message : e}${RESET}\n`);
    return 1;
  }
}
