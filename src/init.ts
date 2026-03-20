#!/usr/bin/env node
import * as fs from "node:fs";
import * as path from "node:path";
import * as readline from "node:readline";
import { ProjectContext } from "./project.js";
import { deploy } from "./deployer.js";

/* ------------------------------------------------------------------ */
/*  Terminal helpers                                                    */
/* ------------------------------------------------------------------ */

const RESET = "\x1b[0m";
const BOLD = "\x1b[1m";
const DIM = "\x1b[2m";
const GREEN = "\x1b[32m";
const YELLOW = "\x1b[33m";
const CYAN = "\x1b[36m";
const RED = "\x1b[31m";

const ok = (msg: string) => console.log(`  ${GREEN}✓${RESET} ${msg}`);
const warn = (msg: string) => console.log(`  ${YELLOW}!${RESET} ${msg}`);
const fail = (msg: string) => console.log(`  ${RED}✗${RESET} ${msg}`);
const info = (msg: string) => console.log(`  ${DIM}${msg}${RESET}`);

const HIDE_CURSOR = "\x1b[?25l";
const SHOW_CURSOR = "\x1b[?25h";
const CLEAR_LINE = "\x1b[2K";
const MOVE_UP = (n: number) => `\x1b[${n}A`;

/* ------------------------------------------------------------------ */
/*  Interactive checkbox selector                                      */
/* ------------------------------------------------------------------ */

interface CheckboxItem {
  label: string;
  checked: boolean;
  suffix?: string;
}

function checkboxSelect(
  title: string,
  items: CheckboxItem[],
): Promise<boolean[]> {
  return new Promise((resolve) => {
    let cursor = 0;
    const states = items.map((i) => i.checked);

    process.stdout.write(HIDE_CURSOR);

    function render(firstRender = false) {
      if (!firstRender) {
        // Move cursor back up to redraw
        process.stdout.write(MOVE_UP(items.length + 1));
      }

      console.log(
        `  ${BOLD}?${RESET} ${title} ${DIM}(↑↓ move, space toggle, enter confirm)${RESET}`,
      );

      for (let i = 0; i < items.length; i++) {
        process.stdout.write(CLEAR_LINE);
        const pointer = i === cursor ? `${CYAN}❯${RESET}` : " ";
        const check = states[i]
          ? `${GREEN}◉${RESET}`
          : `${DIM}○${RESET}`;
        const label =
          i === cursor ? `${BOLD}${items[i].label}${RESET}` : items[i].label;
        const suffix = items[i].suffix
          ? ` ${DIM}${items[i].suffix}${RESET}`
          : "";
        console.log(`   ${pointer} ${check} ${label}${suffix}`);
      }
    }

    render(true);

    const stdin = process.stdin;
    stdin.setRawMode(true);
    stdin.resume();
    stdin.setEncoding("utf-8");

    function onData(key: string) {
      // Ctrl+C
      if (key === "\x03") {
        process.stdout.write(SHOW_CURSOR);
        stdin.setRawMode(false);
        stdin.removeListener("data", onData);
        process.exit(0);
      }

      // Enter
      if (key === "\r" || key === "\n") {
        process.stdout.write(SHOW_CURSOR);
        stdin.setRawMode(false);
        stdin.pause();
        stdin.removeListener("data", onData);

        // Show final state
        process.stdout.write(MOVE_UP(items.length + 1));
        const enabled = items
          .filter((_, i) => states[i])
          .map((i) => i.label);
        const disabled = items
          .filter((_, i) => !states[i])
          .map((i) => i.label);
        process.stdout.write(CLEAR_LINE);

        if (disabled.length === 0) {
          console.log(`  ${GREEN}✓${RESET} All ${items.length} categories enabled`);
        } else {
          console.log(
            `  ${GREEN}✓${RESET} ${enabled.length} enabled, ${disabled.length} disabled: ${DIM}${disabled.join(", ")}${RESET}`,
          );
        }

        // Clear the item lines
        for (let i = 0; i < items.length; i++) {
          process.stdout.write(CLEAR_LINE + "\n");
        }
        process.stdout.write(MOVE_UP(items.length));

        resolve(states);
        return;
      }

      // Space — toggle
      if (key === " ") {
        states[cursor] = !states[cursor];
        render();
        return;
      }

      // Arrow up / k
      if (key === "\x1b[A" || key === "k") {
        cursor = (cursor - 1 + items.length) % items.length;
        render();
        return;
      }

      // Arrow down / j
      if (key === "\x1b[B" || key === "j") {
        cursor = (cursor + 1) % items.length;
        render();
        return;
      }

      // 'a' — toggle all
      if (key === "a") {
        const allChecked = states.every(Boolean);
        for (let i = 0; i < states.length; i++) states[i] = !allChecked;
        render();
        return;
      }
    }

    stdin.on("data", onData);
  });
}

/* ------------------------------------------------------------------ */
/*  Interactive single-select                                          */
/* ------------------------------------------------------------------ */

function singleSelect(
  title: string,
  items: string[],
): Promise<number> {
  return new Promise((resolve) => {
    let cursor = 0;

    process.stdout.write(HIDE_CURSOR);

    function render(firstRender = false) {
      if (!firstRender) {
        process.stdout.write(MOVE_UP(items.length + 1));
      }

      console.log(
        `  ${BOLD}?${RESET} ${title} ${DIM}(↑↓ move, enter select)${RESET}`,
      );

      for (let i = 0; i < items.length; i++) {
        process.stdout.write(CLEAR_LINE);
        const pointer = i === cursor ? `${CYAN}❯${RESET}` : " ";
        const label =
          i === cursor ? `${BOLD}${items[i]}${RESET}` : items[i];
        console.log(`   ${pointer} ${label}`);
      }
    }

    render(true);

    const stdin = process.stdin;
    stdin.setRawMode(true);
    stdin.resume();
    stdin.setEncoding("utf-8");

    function onData(key: string) {
      if (key === "\x03") {
        process.stdout.write(SHOW_CURSOR);
        stdin.setRawMode(false);
        stdin.removeListener("data", onData);
        process.exit(0);
      }

      if (key === "\r" || key === "\n") {
        process.stdout.write(SHOW_CURSOR);
        stdin.setRawMode(false);
        stdin.pause();
        stdin.removeListener("data", onData);

        process.stdout.write(MOVE_UP(items.length + 1));
        process.stdout.write(CLEAR_LINE);
        console.log(
          `  ${GREEN}✓${RESET} ${title}: ${BOLD}${items[cursor]}${RESET}`,
        );
        for (let i = 0; i < items.length; i++) {
          process.stdout.write(CLEAR_LINE + "\n");
        }
        process.stdout.write(MOVE_UP(items.length));

        resolve(cursor);
        return;
      }

      if (key === "\x1b[A" || key === "k") {
        cursor = (cursor - 1 + items.length) % items.length;
        render();
      }
      if (key === "\x1b[B" || key === "j") {
        cursor = (cursor + 1) % items.length;
        render();
      }
    }

    stdin.on("data", onData);
  });
}

/* ------------------------------------------------------------------ */
/*  Multi-select (for MCP clients)                                     */
/* ------------------------------------------------------------------ */

function multiSelect(
  title: string,
  items: string[],
): Promise<boolean[]> {
  const checkItems = items.map((label) => ({
    label,
    checked: true,
  }));
  return checkboxSelect(title, checkItems);
}

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
    } catch {
      // corrupt file, overwrite
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
    } catch {
      // overwrite
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

  // 1. Get project path
  let uprojectPath = process.argv[2] || "";

  if (!uprojectPath) {
    uprojectPath = await askPath();
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

  console.log("");

  // 7. MCP client configuration — interactive
  const clients = detectMcpClients(project.projectDir!);
  const detected = clients.filter((c) => c.detected);

  if (detected.length > 0) {
    const clientItems: CheckboxItem[] = detected.map((c) => ({
      label: c.name,
      checked: true,
      suffix: c.configPath,
    }));

    const clientStates = await checkboxSelect(
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

  // 8. Done
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
