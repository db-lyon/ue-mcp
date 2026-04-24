#!/usr/bin/env node
import * as fs from "node:fs";
import * as path from "node:path";
import * as readline from "node:readline";
import { ProjectContext } from "./project.js";
import { deploy } from "./deployer.js";
import { installSkills } from "./skills.js";
import { warn as logWarn } from "./log.js";
import { BOLD, CYAN, DIM, GREEN, RED, RESET, YELLOW, fail, info, ok, warn } from "./ui/ansi.js";
import { checkboxSelect, multiSelect, singleSelect, type CheckboxItem } from "./ui/select.js";

/* ------------------------------------------------------------------ */
/*  Tool categories                                                    */
/* ------------------------------------------------------------------ */

interface ToolCategory {
  name: string;
  label: string;
  requiredPlugins?: string[];
  alwaysOn?: boolean;
}

const CATEGORIES: ToolCategory[] = [
  { name: "project", label: "project", alwaysOn: true },
  { name: "editor", label: "editor", alwaysOn: true },
  { name: "reflection", label: "reflection", alwaysOn: true },
  { name: "level", label: "levels" },
  { name: "blueprint", label: "blueprints" },
  { name: "material", label: "materials" },
  { name: "asset", label: "assets" },
  { name: "animation", label: "animation" },
  { name: "niagara", label: "vfx (niagara)", requiredPlugins: ["Niagara"] },
  { name: "landscape", label: "landscape" },
  { name: "pcg", label: "pcg", requiredPlugins: ["PCG"] },
  { name: "foliage", label: "foliage" },
  { name: "audio", label: "audio" },
  { name: "widget", label: "ui (widgets)" },
  { name: "gameplay", label: "gameplay / ai", requiredPlugins: ["EnhancedInput"] },
  { name: "gas", label: "gas", requiredPlugins: ["GameplayAbilities"] },
  { name: "networking", label: "networking" },
  { name: "demo", label: "demo" },
  { name: "feedback", label: "feedback", alwaysOn: true },
];

/* ------------------------------------------------------------------ */
/*  MCP client detection                                               */
/* ------------------------------------------------------------------ */

interface McpClient {
  name: string;
  configPath: string;
  detected: boolean;
}

function detectMcpClients(projectDir: string): McpClient[] {
  const home = process.env.HOME || process.env.USERPROFILE || "";
  const clients: McpClient[] = [];

  const claudeProjectMcp = path.join(projectDir, ".mcp.json");
  const claudeGlobalMcp = path.join(home, ".claude", ".mcp.json");
  clients.push({
    name: "Claude Code (project)",
    configPath: claudeProjectMcp,
    detected: fs.existsSync(claudeProjectMcp),
  });
  clients.push({
    name: "Claude Code (global)",
    configPath: claudeGlobalMcp,
    detected: fs.existsSync(path.dirname(claudeGlobalMcp)),
  });

  const appData =
    process.env.APPDATA || path.join(home, "AppData", "Roaming");
  const claudeDesktop = path.join(
    appData,
    "Claude",
    "claude_desktop_config.json",
  );
  clients.push({
    name: "Claude Desktop",
    configPath: claudeDesktop,
    detected: fs.existsSync(path.dirname(claudeDesktop)),
  });

  const cursorMcp = path.join(projectDir, ".cursor", "mcp.json");
  clients.push({
    name: "Cursor",
    configPath: cursorMcp,
    detected: fs.existsSync(path.join(projectDir, ".cursor")),
  });

  return clients;
}

function writeMcpConfig(configPath: string, uprojectPath: string): void {
  let existing: Record<string, unknown> = {};
  if (fs.existsSync(configPath)) {
    try {
      existing = JSON.parse(fs.readFileSync(configPath, "utf-8"));
    } catch (e) {
      logWarn("init", `MCP client config at ${configPath} was not valid JSON - overwriting with a fresh ue-mcp entry`, e);
    }
  }

  const mcpServers = (existing.mcpServers ?? {}) as Record<string, unknown>;
  mcpServers["ue-mcp"] = {
    command: "npx",
    args: ["ue-mcp", uprojectPath.replace(/\\/g, "/")],
  };
  existing.mcpServers = mcpServers;

  const dir = path.dirname(configPath);
  if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(configPath, JSON.stringify(existing, null, 2));
}

/* ------------------------------------------------------------------ */
/*  Claude Code hooks                                                  */
/* ------------------------------------------------------------------ */

interface ClaudeHookEntry {
  type: string;
  command: string;
}

interface ClaudeHookMatcher {
  matcher: string;
  hooks: ClaudeHookEntry[];
}

interface ClaudeSettings {
  hooks?: Record<string, ClaudeHookMatcher[]>;
  [key: string]: unknown;
}

function installClaudeHooks(settingsPath: string): void {
  let existing: ClaudeSettings = {};
  if (fs.existsSync(settingsPath)) {
    try {
      existing = JSON.parse(fs.readFileSync(settingsPath, "utf-8"));
    } catch (e) {
      logWarn("init", `Claude settings at ${settingsPath} was not valid JSON - rewriting hooks section only`, e);
    }
  }

  if (!existing.hooks) existing.hooks = {};
  if (!existing.hooks.PostToolUse) existing.hooks.PostToolUse = [];

  // Avoid duplicate: check if we already have a ue-mcp hook
  const already = existing.hooks.PostToolUse.some(
    (h) => h.matcher === "mcp__ue-mcp__editor",
  );
  if (!already) {
    existing.hooks.PostToolUse.push({
      matcher: "mcp__ue-mcp__editor",
      hooks: [
        {
          type: "command",
          command: "npx ue-mcp hook post-tool-use",
        },
      ],
    });
  }

  const dir = path.dirname(settingsPath);
  if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(settingsPath, JSON.stringify(existing, null, 2));
}

/* ------------------------------------------------------------------ */
/*  .ue-mcp.json config                                                */
/* ------------------------------------------------------------------ */

interface UeMcpInitConfig {
  contentRoots?: string[];
  disable?: string[];
}

function writeProjectConfig(projectDir: string, disabled: string[]): void {
  const configPath = path.join(projectDir, ".ue-mcp.json");
  let existing: UeMcpInitConfig = {};
  if (fs.existsSync(configPath)) {
    try {
      existing = JSON.parse(fs.readFileSync(configPath, "utf-8"));
    } catch (e) {
      logWarn("init", `.ue-mcp.json was not valid JSON - overwriting`, e);
    }
  }

  if (disabled.length > 0) {
    existing.disable = disabled;
  } else {
    delete existing.disable;
  }

  if (!existing.contentRoots) {
    existing.contentRoots = ["/Game/"];
  }

  fs.writeFileSync(configPath, JSON.stringify(existing, null, 2));
}

/* ------------------------------------------------------------------ */
/*  Plugin enablement                                                  */
/* ------------------------------------------------------------------ */

function ensurePluginsEnabled(
  uprojectPath: string,
  pluginNames: string[],
): string[] {
  const raw = fs.readFileSync(uprojectPath, "utf-8");
  const root = JSON.parse(raw);
  if (!root.Plugins) root.Plugins = [];

  const enabled: string[] = [];
  for (const name of pluginNames) {
    const existing = root.Plugins.find(
      (p: { Name?: string }) => p.Name?.toLowerCase() === name.toLowerCase(),
    );
    if (!existing) {
      root.Plugins.push({ Name: name, Enabled: true });
      enabled.push(name);
    } else if (!existing.Enabled) {
      existing.Enabled = true;
      enabled.push(name);
    }
  }

  if (enabled.length > 0) {
    fs.writeFileSync(uprojectPath, JSON.stringify(root, null, "\t"));
  }

  return enabled;
}

/* ------------------------------------------------------------------ */
/*  Ask for project path with readline                                 */
/* ------------------------------------------------------------------ */

function askPath(): Promise<string> {
  const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout,
  });
  return new Promise((resolve) => {
    rl.question(
      `  ${BOLD}?${RESET} UE project path (.uproject or directory): `,
      (answer) => {
        rl.close();
        resolve(answer.trim().replace(/^["']|["']$/g, ""));
      },
    );
  });
}

/* ------------------------------------------------------------------ */
/*  Main init flow                                                     */
/* ------------------------------------------------------------------ */

async function init() {
  console.log("");
  console.log(`  ${BOLD}${CYAN}UE-MCP Setup${RESET}`);
  console.log("");

  // 1. Get project path — check CLI arg, then cwd, then ask
  let uprojectPath = process.argv[2] || "";

  if (!uprojectPath) {
    // Auto-detect .uproject in current directory
    const cwd = process.cwd();
    const found = fs.readdirSync(cwd).filter((f) => f.endsWith(".uproject"));
    if (found.length > 0) {
      uprojectPath = path.join(cwd, found[0]);
      info(`Found ${found[0]} in current directory`);
    } else {
      uprojectPath = await askPath();
    }
  }

  const project = new ProjectContext();
  try {
    project.setProject(uprojectPath);
  } catch (e) {
    fail(e instanceof Error ? e.message : String(e));
    process.exit(1);
  }

  ok(
    `Found UE ${project.engineAssociation ?? "?"} project "${project.projectName}"`,
  );
  console.log("");

  // 2. Tool category selection — interactive checkboxes
  const optional = CATEGORIES.filter((c) => !c.alwaysOn);
  const checkboxItems: CheckboxItem[] = optional.map((c) => ({
    label: c.label,
    checked: true,
    suffix: c.requiredPlugins
      ? `requires ${c.requiredPlugins.join(", ")}`
      : undefined,
  }));

  const states = await checkboxSelect("Tool categories", checkboxItems);

  const disabled: string[] = [];
  for (let i = 0; i < optional.length; i++) {
    if (!states[i]) disabled.push(optional[i].name);
  }

  console.log("");

  // 3. Determine required plugins
  const requiredPlugins = new Set(["PythonScriptPlugin"]);
  for (const cat of CATEGORIES) {
    if (disabled.includes(cat.name)) continue;
    if (cat.requiredPlugins) {
      for (const p of cat.requiredPlugins) requiredPlugins.add(p);
    }
  }

  // 4. Deploy C++ plugin
  const deployResult = deploy(project);
  if (deployResult.error) {
    fail(`Plugin deployment failed: ${deployResult.error}`);
  } else if (deployResult.cppPluginDeployed) {
    ok(
      `Plugin deployed to ${project.projectName}/Plugins/UE_MCP_Bridge/`,
    );
  } else {
    ok("Plugin already deployed");
  }

  // 5. Enable required plugins
  const enabled = ensurePluginsEnabled(project.projectPath!, [
    ...requiredPlugins,
  ]);
  if (enabled.length > 0) {
    ok(`Enabled: ${enabled.join(", ")}`);
  } else {
    ok("Required plugins already enabled");
  }

  // 6. Write .ue-mcp.json
  writeProjectConfig(project.projectDir!, disabled);
  ok(".ue-mcp.json written");

  // 7. Scaffold ue-mcp.yml if it doesn't exist
  const flowConfigPath = path.join(project.projectDir!, "ue-mcp.yml");
  if (!fs.existsSync(flowConfigPath)) {
    fs.writeFileSync(flowConfigPath, [
      "ue-mcp:",
      "  version: 1",
      "",
      "# Custom tasks — pre-fill options for built-in actions",
      "# All 425+ built-in actions are available without listing them here.",
      "#",
      "# tasks:",
      "#   import_hero:",
      "#     class_path: ue-mcp.bridge",
      "#     options:",
      "#       method: import_skeletal_mesh",
      "#       filePath: Meshes/hero_sk.fbx",
      "#       packagePath: /Game/Characters/Hero",
      "",
      "tasks: {}",
      "",
      "# Flows compose tasks into repeatable multi-step sequences.",
      "#",
      "# flows:",
      "#   fresh_level:",
      "#     description: Blank level with basic lighting",
      "#     steps:",
      "#       1:",
      "#         task: level.create",
      "#         options:",
      "#           name: Sandbox",
      "#       2:",
      "#         task: level.spawn_light",
      "#         options:",
      "#           type: DirectionalLight",
      "",
      "flows: {}",
      "",
    ].join("\n"));
    ok("ue-mcp.yml created (custom tasks & flows)");
  } else {
    ok("ue-mcp.yml already exists");
  }

  console.log("");

  // 8. MCP client configuration — interactive
  const clients = detectMcpClients(project.projectDir!);
  const detected = clients.filter((c) => c.detected);
  let clientStates: boolean[] = [];

  if (detected.length > 0) {
    const clientItems: CheckboxItem[] = detected.map((c) => ({
      label: c.name,
      checked: true,
      suffix: c.configPath,
    }));

    clientStates = await checkboxSelect(
      "Configure MCP clients",
      clientItems,
    );

    for (let i = 0; i < detected.length; i++) {
      if (clientStates[i]) {
        writeMcpConfig(detected[i].configPath, project.projectPath!);
        ok(`${detected[i].name} configured`);
      }
    }
  } else {
    warn("No MCP clients detected. Add this to your MCP client config:");
    console.log("");
    console.log(`    ${DIM}{`);
    console.log(`      "mcpServers": {`);
    console.log(`        "ue-mcp": {`);
    console.log(`          "command": "npx",`);
    console.log(
      `          "args": ["ue-mcp", "${project.projectPath!.replace(/\\/g, "/")}"]`,
    );
    console.log(`        }`);
    console.log(`      }`);
    console.log(`    }${RESET}`);
    console.log("");
  }

  // 9. Claude Code hooks
  const configuredClaudeCode = detected.some(
    (c, i) => c.name.startsWith("Claude Code") && clientStates[i],
  );

  if (configuredClaudeCode) {
    console.log("");
    const hookItems: CheckboxItem[] = [
      {
        label: "Allow agents to fall back to execute_python when native tools can't do the job",
        checked: true,
        suffix: "Recommended",
      },
      {
        label: "Prompt agents to file a GitHub issue when they resort to Python workarounds",
        checked: true,
        suffix: "Recommended",
      },
    ];

    const hookStates = await checkboxSelect(
      "Agent behavior",
      hookItems,
    );

    if (hookStates[1]) {
      // Write hooks to the project-level Claude Code settings
      const claudeSettingsPath = path.join(
        project.projectDir!,
        ".claude",
        "settings.json",
      );
      installClaudeHooks(claudeSettingsPath);
      ok("Claude Code hooks installed");
      info(claudeSettingsPath);
    }

    // 9b. Claude Code skills — bundled workflow guides
    const skillsResult = installSkills(project.projectDir!);
    if (skillsResult.error) {
      warn(`Skills install skipped: ${skillsResult.error}`);
    } else if (skillsResult.installed.length > 0) {
      ok(`Claude Code skills installed: ${skillsResult.installed.join(", ")}`);
      info(skillsResult.skillsDir);
    }
  }

  // 10. Done
  console.log("");
  console.log(`  ${BOLD}${GREEN}Setup complete!${RESET}`);
  console.log("");
  console.log(
    `  ${DIM}Restart the editor to load the bridge plugin.`,
  );
  console.log(
    `  Then ask your AI: project(action="get_status")${RESET}`,
  );
  console.log("");
}

init().catch((e) => {
  console.error(
    `\n  ${RED}Fatal error: ${e instanceof Error ? e.message : e}${RESET}\n`,
  );
  process.exit(1);
});
