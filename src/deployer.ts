import * as fs from "node:fs";
import * as path from "node:path";
import { execSync } from "node:child_process";
import type { ProjectContext } from "./project.js";

const BRIDGE_DIR_NAME = "ue_mcp_bridge";
const PYTHON_PLUGIN_NAME = "PythonScriptPlugin";
const STARTUP_INI_SECTION = "[/Script/PythonScriptPlugin.PythonScriptPluginSettings]";
const STARTUP_SCRIPT_LINE = "+StartupScripts=ue_mcp_bridge/startup_script.py";

export interface DeployResult {
  pluginEnabled: boolean;
  bridgeDeployed: boolean;
  startupConfigured: boolean;
  websocketsInstalled: boolean;
  error?: string;
}

export function deploy(context: ProjectContext): DeployResult {
  const result: DeployResult = {
    pluginEnabled: false,
    bridgeDeployed: false,
    startupConfigured: false,
    websocketsInstalled: false,
  };

  try {
    result.pluginEnabled = ensurePythonPlugin(context.projectPath!);
    result.bridgeDeployed = deployBridgeFiles(context.contentDir!);
    result.startupConfigured = ensureStartupConfig(context.projectPath!);
    result.websocketsInstalled = ensureWebsockets(context.engineAssociation, context.projectPath!);
  } catch (e) {
    result.error = e instanceof Error ? e.message : String(e);
  }

  return result;
}

export function deploySummary(r: DeployResult): string {
  if (r.error) return `Bridge deployment failed: ${r.error}`;
  const changes: string[] = [];
  if (r.pluginEnabled) changes.push("enabled PythonScriptPlugin");
  if (r.bridgeDeployed) changes.push("deployed bridge plugin");
  if (r.startupConfigured) changes.push("configured auto-start");
  if (r.websocketsInstalled) changes.push("installed websockets");
  if (changes.length === 0) return "Bridge already configured";
  return "Bridge setup: " + changes.join(", ");
}

function ensurePythonPlugin(uprojectPath: string): boolean {
  const raw = fs.readFileSync(uprojectPath, "utf-8");
  const root = JSON.parse(raw);

  if (!root.Plugins) root.Plugins = [];

  const already = root.Plugins.some(
    (p: { Name?: string }) =>
      p.Name?.toLowerCase() === PYTHON_PLUGIN_NAME.toLowerCase(),
  );
  if (already) return false;

  root.Plugins.unshift({ Name: PYTHON_PLUGIN_NAME, Enabled: true });
  fs.writeFileSync(uprojectPath, JSON.stringify(root, null, 2));
  return true;
}

function deployBridgeFiles(contentDir: string): boolean {
  const sourceDir = path.resolve(
    import.meta.dirname ?? path.dirname(new URL(import.meta.url).pathname),
    "..",
    "plugin",
    BRIDGE_DIR_NAME,
  );

  if (!fs.existsSync(sourceDir)) {
    console.error(`[ue-mcp] Bridge source not found at ${sourceDir}`);
    return false;
  }

  const targetDir = path.join(contentDir, "Python", BRIDGE_DIR_NAME);
  let anyDeployed = false;

  function copyRecursive(src: string, dest: string): void {
    if (!fs.existsSync(dest)) {
      fs.mkdirSync(dest, { recursive: true });
    }
    for (const entry of fs.readdirSync(src, { withFileTypes: true })) {
      const srcPath = path.join(src, entry.name);
      const destPath = path.join(dest, entry.name);

      if (entry.name === "__pycache__") continue;

      if (entry.isDirectory()) {
        copyRecursive(srcPath, destPath);
      } else {
        const srcBytes = fs.readFileSync(srcPath);
        let shouldWrite = true;
        if (fs.existsSync(destPath)) {
          const destBytes = fs.readFileSync(destPath);
          shouldWrite = !srcBytes.equals(destBytes);
        }
        if (shouldWrite) {
          fs.writeFileSync(destPath, srcBytes);
          anyDeployed = true;
        }
      }
    }
  }

  copyRecursive(sourceDir, targetDir);
  return anyDeployed;
}

function ensureStartupConfig(uprojectPath: string): boolean {
  const projectDir = path.dirname(uprojectPath);
  const iniPath = path.join(projectDir, "Config", "DefaultEngine.ini");

  if (!fs.existsSync(iniPath)) return false;

  let content = fs.readFileSync(iniPath, "utf-8");

  if (content.includes("/Game/Python/ue_mcp_bridge/startup_script.py")) {
    content = content.replace(
      "+StartupScripts=/Game/Python/ue_mcp_bridge/startup_script.py",
      "+StartupScripts=ue_mcp_bridge/startup_script.py",
    );
    fs.writeFileSync(iniPath, content);
    return true;
  }

  if (content.includes("ue_mcp_bridge/startup_script.py")) {
    return false;
  }

  const sectionIdx = content.indexOf(STARTUP_INI_SECTION);
  if (sectionIdx >= 0) {
    const insertAt = sectionIdx + STARTUP_INI_SECTION.length;
    content =
      content.slice(0, insertAt) +
      "\n" +
      STARTUP_SCRIPT_LINE +
      content.slice(insertAt);
  } else {
    content += "\n\n" + STARTUP_INI_SECTION + "\n" + STARTUP_SCRIPT_LINE + "\n";
  }

  fs.writeFileSync(iniPath, content);
  return true;
}

function ensureWebsockets(
  engineAssociation: string | null,
  uprojectPath: string,
): boolean {
  const pythonExe = findUePython(engineAssociation, uprojectPath);
  if (!pythonExe) return false;

  try {
    execSync(`"${pythonExe}" -c "import websockets"`, { stdio: "pipe" });
    return false; // already installed
  } catch {
    // not installed
  }

  try {
    execSync(
      `"${pythonExe}" -m pip install websockets --quiet --disable-pip-version-check`,
      { stdio: "pipe", timeout: 30_000 },
    );
    return true;
  } catch {
    return false;
  }
}

function findUePython(
  engineAssociation: string | null,
  _uprojectPath: string,
): string | null {
  const engineRoot = findEngineInstall(engineAssociation);
  if (!engineRoot) return null;

  const pythonExe = path.join(
    engineRoot,
    "Engine",
    "Binaries",
    "ThirdParty",
    "Python3",
    "Win64",
    "python.exe",
  );
  if (fs.existsSync(pythonExe)) return pythonExe;
  return null;
}

function findEngineInstall(
  engineAssociation: string | null,
): string | null {
  if (!engineAssociation) return null;

  // GUID-based custom builds: check Windows registry
  const guidRegex =
    /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
  if (guidRegex.test(engineAssociation)) {
    return findEngineByGuid(engineAssociation);
  }

  return findLauncherEngine(engineAssociation);
}

function findEngineByGuid(guid: string): string | null {
  try {
    const output = execSync(
      `reg query "HKCU\\SOFTWARE\\Epic Games\\Unreal Engine\\Builds" /v "${guid}"`,
      { stdio: "pipe", encoding: "utf-8" },
    );
    const match = output.match(/REG_SZ\s+(.+)/);
    if (match) {
      const p = match[1].trim();
      if (fs.existsSync(p)) return p;
    }
  } catch {
    // registry key not found
  }
  return null;
}

function findLauncherEngine(association: string): string | null {
  // Epic Games Launcher manifest
  const launcherDat = path.join(
    process.env.PROGRAMDATA || "C:\\ProgramData",
    "Epic",
    "UnrealEngineLauncher",
    "LauncherInstalled.dat",
  );

  if (fs.existsSync(launcherDat)) {
    try {
      const data = JSON.parse(fs.readFileSync(launcherDat, "utf-8"));
      for (const entry of data.InstallationList ?? []) {
        if (
          entry.AppName?.toLowerCase() ===
          `ue_${association}`.toLowerCase()
        ) {
          if (fs.existsSync(entry.InstallLocation)) {
            return entry.InstallLocation;
          }
        }
      }
    } catch {
      // malformed manifest
    }
  }

  // Common install paths
  for (const root of [
    "C:\\Program Files\\Epic Games",
    "D:\\Program Files\\Epic Games",
    "C:\\Epic Games",
    "D:\\Epic Games",
  ]) {
    const candidate = path.join(root, `UE_${association}`);
    if (fs.existsSync(candidate)) return candidate;
  }

  return null;
}
