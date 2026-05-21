#!/usr/bin/env node
import * as fs from "node:fs";
import * as path from "node:path";
import * as readline from "node:readline";
import { ProjectContext } from "./project.js";
import { deploy } from "./deployer.js";
import { installSkills, uninstallSkills } from "./skills.js";
import { warn as logWarn } from "./log.js";
import { BOLD, CYAN, DIM, GREEN, RED, RESET, fail, info, ok, warn } from "./ui/ansi.js";
import { checkboxSelect, singleSelect, type CheckboxItem } from "./ui/select.js";
import { installClaudeHooks, uninstallClaudeHooks } from "./hook-installer.js";
import { runFeedbackAuthStep } from "./auth-cli.js";

/* ------------------------------------------------------------------ */
/*  Tool categories                                                    */
/* ------------------------------------------------------------------ */

interface ToolCategory {
  name: string;
  label: string;
  description?: string;
  requiredPlugins?: string[];
  alwaysOn?: boolean;
}

const CATEGORIES: ToolCategory[] = [
  { name: "project",    label: "project",       alwaysOn: true },
  { name: "editor",     label: "editor",        alwaysOn: true },
  { name: "reflection", label: "reflection",    alwaysOn: true },
  { name: "level",      label: "levels",        description: "spawn/move/select actors, sublevels, world settings" },
  { name: "blueprint",  label: "blueprints",    description: "Blueprint classes: variables, functions, components, nodes" },
  { name: "material",   label: "materials",     description: "Materials, Material Functions, parameters" },
  { name: "asset",      label: "assets",        description: "import/move/rename/delete content browser assets" },
  { name: "animation",  label: "animation",     description: "anim sequences, montages, AnimBPs, skeletons" },
  { name: "niagara",    label: "vfx (niagara)", description: "VFX systems, emitters, modules", requiredPlugins: ["Niagara"] },
  { name: "landscape",  label: "landscape",     description: "terrain edit, layers, paint, sculpt" },
  { name: "pcg",        label: "pcg",           description: "procedural content graphs", requiredPlugins: ["PCG"] },
  { name: "foliage",    label: "foliage",       description: "instanced foliage placement and types" },
  { name: "audio",      label: "audio",         description: "MetaSounds, sound cues, sound classes" },
  { name: "widget",     label: "ui (widgets)",  description: "UMG widgets, editor utility widgets" },
  { name: "gameplay",   label: "gameplay / ai", description: "input mappings, collision, navmesh, PIE", requiredPlugins: ["EnhancedInput"] },
  { name: "gas",        label: "gas",           description: "Gameplay Ability System abilities, effects, attributes", requiredPlugins: ["GameplayAbilities"] },
  { name: "networking", label: "networking",    description: "replication, dormancy, RPCs" },
  { name: "demo",       label: "demo",          description: "tutorial / demo project scaffolding" },
  // feedback is intentionally NOT in the main category list — it has its
  // own toggle in the "Agent behavior" section below since enabling it
  // means giving the agent a tool that can post to a public issue tracker
  // (gated by user approval, but worth surfacing explicitly).
  { name: "feedback",   label: "feedback",      alwaysOn: true },
];

/* ------------------------------------------------------------------ */
/*  MCP client detection                                               */
/* ------------------------------------------------------------------ */

interface McpClient {
  name: string;
  configPath: string;
  detected: boolean;
}

/**
 * Project-scoped clients write their MCP config alongside the .uproject,
 * so enabling them only affects this project. Global/Desktop configs touch
 * every project the user opens — they should not be opted in by default.
 */
function isProjectScopedClient(clientName: string): boolean {
  return clientName.includes("(project)") || clientName === "Cursor";
}

function detectMcpClients(projectDir: string): McpClient[] {
  const home = process.env.HOME || process.env.USERPROFILE || "";
  const clients: McpClient[] = [];

  const claudeProjectMcp = path.join(projectDir, ".mcp.json");
  const claudeGlobalMcp = path.join(home, ".claude", ".mcp.json");
  // "Detected" for both Claude Code scopes means "Claude Code is installed
  // anywhere on this machine." If we gated project-scope detection on the
  // project's .mcp.json already existing, first-time users in a fresh
  // project would never see the project-scope checkbox and would be
  // funneled into global scope by elimination. Show both scopes whenever
  // Claude Code has been opened at least once; let the user pick.
  const claudeInstalled =
    fs.existsSync(claudeProjectMcp) ||
    fs.existsSync(path.dirname(claudeGlobalMcp));
  clients.push({
    name: "Claude Code (project)",
    configPath: claudeProjectMcp,
    detected: claudeInstalled,
  });
  clients.push({
    name: "Claude Code (global)",
    configPath: claudeGlobalMcp,
    detected: claudeInstalled,
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

  // Track every file/directory init mutates so the completion screen can
  // tell the user exactly where things landed. Push paths only after the
  // write actually succeeded.
  const wrote: Array<{ what: string; where: string }> = [];

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

  // On re-init, respect prior opt-outs in .ue-mcp.json so the user doesn't
  // have to re-uncheck categories they already disabled. project.config is
  // populated by ProjectContext.setProject above.
  const existingDisabled = new Set(project.config.disable ?? []);

  // 2. Tool category selection — interactive checkboxes with descriptions
  const optional = CATEGORIES.filter((c) => !c.alwaysOn);
  const checkboxItems: CheckboxItem[] = optional.map((c) => {
    const parts: string[] = [];
    if (c.description) parts.push(c.description);
    if (c.requiredPlugins) parts.push(`requires ${c.requiredPlugins.join(", ")}`);
    return {
      label: c.label,
      checked: !existingDisabled.has(c.name),
      suffix: parts.length > 0 ? parts.join(" — ") : undefined,
    };
  });

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
    // Hard failure: without the bridge plugin deployed, every subsequent step
    // is misleading at best — we would land on "Setup complete!" with nothing
    // actually wired up. `fail` only prints; abort explicitly.
    fail(`Plugin deployment failed: ${deployResult.error}`);
    process.exit(1);
  } else if (deployResult.cppPluginDeployed) {
    ok(
      `Plugin deployed to ${project.projectName}/Plugins/UE_MCP_Bridge/`,
    );
    wrote.push({
      what: "C++ bridge plugin",
      where: path.join(project.projectDir!, "Plugins", "UE_MCP_Bridge"),
    });
  } else {
    ok("Plugin already deployed");
  }

  // 5. Enable required plugins
  const enabled = ensurePluginsEnabled(project.projectPath!, [
    ...requiredPlugins,
  ]);
  if (enabled.length > 0) {
    ok(`Enabled: ${enabled.join(", ")}`);
    wrote.push({ what: "enabled plugins", where: project.projectPath! });
  } else {
    ok("Required plugins already enabled");
  }

  // .ue-mcp.json is written at the end of init() once all decisions
  // (categories, MCP clients, agent behavior) are settled — see step 10.

  // 7. Scaffold ue-mcp.yml if it doesn't exist
  const flowConfigPath = path.join(project.projectDir!, "ue-mcp.yml");
  if (!fs.existsSync(flowConfigPath)) {
    fs.writeFileSync(flowConfigPath, [
      "ue-mcp:",
      "  version: 1",
      "",
      "# Custom tasks — pre-fill options for built-in actions",
      "# All built-in actions are available without listing them here.",
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
    wrote.push({ what: "custom tasks & flows", where: flowConfigPath });
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
      // Global / Desktop configs affect every project on this machine, so
      // they default to UNCHECKED — opting them in should be an explicit
      // choice. Project-scoped configs default to checked since the user
      // running init for this project clearly wants ue-mcp here.
      checked: isProjectScopedClient(c.name),
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
        wrote.push({
          what: `${detected[i].name} MCP server entry`,
          where: detected[i].configPath,
        });
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

  // 9. Agent behavior — feedback toggle (always shown) plus the
  // Claude-Code-only rows (prompt hook + skills + OAuth). The hook and OAuth
  // are nested under feedback: if the user opts out of feedback, the hook
  // and the GitHub device flow are not even offered.
  //
  // Every checkbox in this section defaults OFF on a fresh install. A user
  // who blasts through with Enter gets nothing added — no tool registered,
  // no hook installed, no skill files copied. Re-init preserves prior
  // choices by reading state from .ue-mcp.json (categories, installed
  // hooks) and the filesystem (skills directory).
  const configuredClaudeCode = detected.some(
    (c, i) => c.name.startsWith("Claude Code") && clientStates[i],
  );

  console.log("");

  // .ue-mcp.json existing means init has been run before in this project.
  // We use that as the "this is a re-init" signal: prior choices should be
  // honored. On a fresh install (no config file yet), default all Agent
  // behavior off regardless of what the eventual disable[] would look like.
  const ueMcpJsonPathForSeed = path.join(project.projectDir!, ".ue-mcp.json");
  const isReInit = fs.existsSync(ueMcpJsonPathForSeed);

  const behaviorItems: CheckboxItem[] = [
    {
      label: "Enable feedback(submit) tool for filing tool-gap issues",
      // Fresh install: off. Re-init: on unless they previously disabled it.
      checked: isReInit && !existingDisabled.has("feedback"),
      suffix:
        "Recommended. Calls block on a user-approval prompt before anything is posted to a public tracker.",
    },
  ];
  if (configuredClaudeCode) {
    // Seed the prompt-hook and skills checkboxes from current install state
    // so re-init doesn't silently invert the user's prior choice — checking
    // installs, unchecking uninstalls (symmetric paths below), so a stale
    // default would silently un-do whatever the last init produced.
    const claudeSettingsPathForSeed = path.join(
      project.projectDir!,
      ".claude",
      "settings.json",
    );
    const installedHookSites = new Set(
      (project.config.installedHooks ?? []).map((p) => path.resolve(p)),
    );
    const hookCurrentlyInstalled = installedHookSites.has(
      path.resolve(claudeSettingsPathForSeed),
    );

    const skillsDir = path.join(project.projectDir!, ".claude", "skills");
    const skillsCurrentlyInstalled =
      fs.existsSync(skillsDir) && fs.readdirSync(skillsDir).length > 0;

    behaviorItems.push({
      label: "Auto-nudge agent to offer feedback after execute_python",
      // Fresh install: off (opt-in only). Re-init: respect prior install.
      checked: hookCurrentlyInstalled,
      suffix:
        "Opt-in. Installs a PostToolUse hook in .claude/settings.json; ignored if feedback is off.",
    });
    behaviorItems.push({
      label: "Install bundled Claude Code skills (workflow guides)",
      // Fresh install: off. Re-init: on if any skill file is currently
      // present (a non-empty skills dir means they were installed before).
      checked: skillsCurrentlyInstalled,
      suffix:
        "Recommended for Claude Code. Copies skill markdown into .claude/skills/.",
    });
  }

  const behaviorStates = await checkboxSelect("Agent behavior", behaviorItems);

  const feedbackEnabled = behaviorStates[0];
  const promptHookEnabled =
    configuredClaudeCode && (behaviorStates[1] ?? false) && feedbackEnabled;
  const installSkillsEnabled =
    configuredClaudeCode && (behaviorStates[2] ?? false);

  if (!feedbackEnabled) {
    disabled.push("feedback");
  }

  if (configuredClaudeCode) {
    const claudeSettingsPath = path.join(
      project.projectDir!,
      ".claude",
      "settings.json",
    );

    if (promptHookEnabled) {
      installClaudeHooks(claudeSettingsPath, project.projectDir!);
      ok("Claude Code feedback prompt hook installed");
      info(claudeSettingsPath);
      wrote.push({ what: "feedback prompt hook", where: claudeSettingsPath });
    } else {
      // Symmetric uninstall: purge any matcher we may have installed on a
      // prior init run when the user has now opted out.
      const removed = uninstallClaudeHooks(
        claudeSettingsPath,
        project.projectDir!,
      );
      if (removed) {
        ok("Claude Code feedback prompt hook removed");
        info(claudeSettingsPath);
      }
    }

    if (installSkillsEnabled) {
      const skillsResult = installSkills(project.projectDir!);
      if (skillsResult.error) {
        warn(`Skills install skipped: ${skillsResult.error}`);
      } else if (skillsResult.installed.length > 0) {
        ok(`Claude Code skills installed: ${skillsResult.installed.join(", ")}`);
        info(skillsResult.skillsDir);
        wrote.push({ what: "workflow skills", where: skillsResult.skillsDir });
      }
    } else {
      // Symmetric uninstall: opting out on re-init removes any skills we
      // copied in on a previous run. Leaves user-added skill files alone.
      const removed = uninstallSkills(project.projectDir!);
      if (removed.removed.length > 0) {
        ok(`Claude Code skills removed: ${removed.removed.join(", ")}`);
        info(removed.skillsDir);
      }
    }

    // OAuth only when the prompt hook is on — the user has explicitly opted
    // into a flow where the agent will routinely ask to file feedback. For
    // anyone else (feedback enabled but no hook, or feedback off), they can
    // run `npx ue-mcp auth` later if they ever want to author as their own
    // GitHub user instead of the bot.
    if (promptHookEnabled) {
      await runFeedbackAuthStep();
    }
  }

  // 10. Write .ue-mcp.json with the final disable[] — done last so the
  // feedback toggle from step 9 is captured. contentRoots seeding lives
  // inside writeProjectConfig.
  const ueMcpJsonPath = path.join(project.projectDir!, ".ue-mcp.json");
  writeProjectConfig(project.projectDir!, disabled);
  ok(".ue-mcp.json written");
  wrote.push({ what: "tool surface + content roots", where: ueMcpJsonPath });

  // 11. Done — recap what landed where so the user can find / undo anything.
  console.log("");
  console.log(`  ${BOLD}${GREEN}Setup complete!${RESET}`);
  console.log("");
  if (wrote.length > 0) {
    console.log(`  ${BOLD}Wrote:${RESET}`);
    const widest = Math.max(...wrote.map((w) => w.what.length));
    for (const w of wrote) {
      const padded = w.what.padEnd(widest);
      console.log(`    ${padded}  ${DIM}${w.where}${RESET}`);
    }
    console.log("");
  }
  console.log(
    `  ${DIM}Open (or restart) your editor to load the bridge plugin.`,
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
