#!/usr/bin/env node
import * as fs from "node:fs";
import * as path from "node:path";
import { ProjectContext } from "./project.js";
import { deploy, deploySummary } from "./deployer.js";
import { installSkills } from "./skills.js";

const RESET = "\x1b[0m";
const BOLD = "\x1b[1m";
const GREEN = "\x1b[32m";
const RED = "\x1b[31m";
const DIM = "\x1b[2m";
const CYAN = "\x1b[36m";

const ok = (msg: string) => console.log(`  ${GREEN}\u2713${RESET} ${msg}`);
const fail = (msg: string) => console.log(`  ${RED}\u2717${RESET} ${msg}`);

async function deployCmd() {
  console.log("");
  console.log(`  ${BOLD}${CYAN}UE-MCP Deploy${RESET}`);
  console.log("");

  // Find project: CLI arg, then cwd
  let uprojectPath = process.argv[2] || "";

  if (!uprojectPath) {
    const cwd = process.cwd();
    const found = fs.readdirSync(cwd).filter((f) => f.endsWith(".uproject"));
    if (found.length > 0) {
      uprojectPath = path.join(cwd, found[0]);
    }
  }

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

  // Refresh Claude Code skills if the project is already using them
  if (fs.existsSync(path.join(project.projectDir!, ".claude"))) {
    const skillsResult = installSkills(project.projectDir!);
    if (!skillsResult.error && skillsResult.installed.length > 0) {
      ok(`Skills refreshed: ${skillsResult.installed.join(", ")}`);
    }
  }

  console.log("");
  if (result.cppPluginDeployed) {
    const projName = project.projectName ?? "<Project>";
    console.log(`  ${DIM}C++ sources changed - the plugin must be rebuilt before the editor will see new handlers.${RESET}`);
    console.log(`  ${DIM}From the project root:${RESET}`);
    console.log(`  ${DIM}  ue-mcp build${RESET}`);
    console.log(`  ${DIM}Then start (or restart) the editor.${RESET}`);
  } else {
    console.log(`  ${DIM}No changes needed.${RESET}`);
  }
  console.log("");
}

export { deployCmd };

deployCmd().catch((e) => {
  console.error(`\n  ${RED}Fatal error: ${e instanceof Error ? e.message : e}${RESET}\n`);
  process.exit(1);
});
