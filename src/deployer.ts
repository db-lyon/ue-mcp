import * as fs from "node:fs";
import * as path from "node:path";
import { execSync } from "node:child_process";
import type { ProjectContext } from "./project.js";
import { debug, warn } from "./log.js";
import { UPluginSchema } from "./schemas.js";

export interface DeployResult {
  pythonPluginEnabled: boolean;
  cppPluginDeployed: boolean;
  cppPluginEnabled: boolean;
  error?: string;
}

export interface AttachResult {
  pythonPluginEnabled: boolean;
  cppPluginEnabled: boolean;
  cppPluginPresent: boolean;
  packagedVersion: string | null;
  installedVersion: string | null;
  versionMatch: boolean | null;
  error?: string;
}

/**
 * Deploy the C++ bridge plugin to the target UE project.
 *
 * Copies plugin source from plugin/ue_mcp_bridge/ into the target
 * project's Plugins/UE_MCP_Bridge/ directory (skipping build artifacts).
 * Also enables PythonScriptPlugin in the .uproject because the C++
 * bridge's `execute_python` handler calls into it at runtime.
 */
export function deploy(context: ProjectContext): DeployResult {
  const result: DeployResult = {
    pythonPluginEnabled: false,
    cppPluginDeployed: false,
    cppPluginEnabled: false,
  };

  try {
    result.pythonPluginEnabled = ensurePythonPlugin(context.projectPath!);
    result.cppPluginDeployed = deployCppPlugin(context.projectPath!);
    result.cppPluginEnabled = ensureCppPluginEnabled(context.projectPath!);
  } catch (e) {
    result.error = e instanceof Error ? e.message : String(e);
  }

  return result;
}

export function deploySummary(r: DeployResult): string {
  if (r.error) return `Bridge deployment failed: ${r.error}`;
  const changes: string[] = [];
  if (r.pythonPluginEnabled) changes.push("enabled PythonScriptPlugin");
  if (r.cppPluginDeployed) changes.push("deployed C++ bridge plugin");
  if (r.cppPluginEnabled) changes.push("enabled UE_MCP_Bridge in .uproject");
  if (changes.length === 0) return "Bridge already configured";
  return "Bridge setup: " + changes.join(", ");
}

/**
 * Non-destructive attach used on normal MCP server startup.
 *
 * Unlike `deploy()`, this NEVER overwrites bridge source under
 * `Plugins/UE_MCP_Bridge/Source/` — so local forks/edits and
 * project-tracked bridge revisions are preserved. It only:
 *   - ensures PythonScriptPlugin is listed in the .uproject
 *   - ensures UE_MCP_Bridge is listed in the .uproject
 *   - reports plugin presence + version for a warning-level check
 *
 * If the plugin is missing or a version mismatch is detected, callers
 * should surface that to the user and ask them to run `ue-mcp init`
 * or `ue-mcp deploy` explicitly.
 */
export function attach(context: ProjectContext): AttachResult {
  const result: AttachResult = {
    pythonPluginEnabled: false,
    cppPluginEnabled: false,
    cppPluginPresent: false,
    packagedVersion: null,
    installedVersion: null,
    versionMatch: null,
  };

  try {
    const uprojectPath = context.projectPath!;
    result.pythonPluginEnabled = ensurePythonPlugin(uprojectPath);
    result.cppPluginEnabled = ensureCppPluginEnabled(uprojectPath);

    const projectDir = path.dirname(uprojectPath);
    const installedUplugin = path.join(
      projectDir,
      "Plugins",
      "UE_MCP_Bridge",
      "UE_MCP_Bridge.uplugin",
    );
    result.cppPluginPresent = fs.existsSync(installedUplugin);
    result.installedVersion = readUpluginVersion(installedUplugin);
    result.packagedVersion = readUpluginVersion(packagedUpluginPath());

    if (result.installedVersion && result.packagedVersion) {
      result.versionMatch = result.installedVersion === result.packagedVersion;
    }
  } catch (e) {
    result.error = e instanceof Error ? e.message : String(e);
  }

  return result;
}

export function attachSummary(r: AttachResult): string {
  if (r.error) return `Bridge attach failed: ${r.error}`;

  const notes: string[] = [];
  if (r.pythonPluginEnabled) notes.push("enabled PythonScriptPlugin in .uproject");
  if (r.cppPluginEnabled) notes.push("enabled UE_MCP_Bridge in .uproject");

  if (!r.cppPluginPresent) {
    notes.push(
      `UE_MCP_Bridge plugin NOT installed — run \`ue-mcp init <uproject>\` to deploy (packaged v${r.packagedVersion ?? "?"})`,
    );
  } else if (r.versionMatch === false) {
    notes.push(
      `bridge version mismatch — installed v${r.installedVersion}, packaged v${r.packagedVersion}. Source left untouched; run \`ue-mcp deploy <uproject>\` to upgrade.`,
    );
  } else if (r.versionMatch === true) {
    notes.push(`bridge v${r.installedVersion} present (source untouched)`);
  } else {
    notes.push("bridge present (version unreadable, source untouched)");
  }

  return "Bridge attach: " + notes.join("; ");
}

function packagedUpluginPath(): string {
  return path.resolve(
    import.meta.dirname ?? path.dirname(new URL(import.meta.url).pathname),
    "..",
    "plugin",
    "ue_mcp_bridge",
    "UE_MCP_Bridge.uplugin",
  );
}

function readUpluginVersion(upluginPath: string): string | null {
  try {
    if (!fs.existsSync(upluginPath)) return null;
    const raw = fs.readFileSync(upluginPath, "utf-8");
    const parsed = UPluginSchema.safeParse(JSON.parse(raw));
    if (!parsed.success) return null;
    return parsed.data.VersionName ?? null;
  } catch (e) {
    warn("deployer", `could not read VersionName from ${upluginPath}`, e);
    return null;
  }
}

/* ------------------------------------------------------------------ */
/*  PythonScriptPlugin — still needed for execute_python escape hatch */
/* ------------------------------------------------------------------ */

function ensurePythonPlugin(uprojectPath: string): boolean {
  const raw = fs.readFileSync(uprojectPath, "utf-8");
  const root = JSON.parse(raw);

  if (!root.Plugins) root.Plugins = [];

  const already = root.Plugins.some(
    (p: { Name?: string }) =>
      p.Name?.toLowerCase() === "pythonscriptplugin",
  );
  if (already) return false;

  root.Plugins.unshift({ Name: "PythonScriptPlugin", Enabled: true });
  fs.writeFileSync(uprojectPath, JSON.stringify(root, null, "\t"));
  return true;
}

/* ------------------------------------------------------------------ */
/*  C++ Plugin deployment                                             */
/* ------------------------------------------------------------------ */

function deployCppPlugin(uprojectPath: string): boolean {
  const projectDir = path.dirname(uprojectPath);
  const pluginsDir = path.join(projectDir, "Plugins");

  const sourcePluginDir = path.resolve(
    import.meta.dirname ?? path.dirname(new URL(import.meta.url).pathname),
    "..",
    "plugin",
    "ue_mcp_bridge",
  );

  if (!fs.existsSync(sourcePluginDir)) {
    console.error(`[ue-mcp] C++ plugin source not found at ${sourcePluginDir}`);
    return false;
  }

  const targetPluginDir = path.join(pluginsDir, "UE_MCP_Bridge");
  let anyDeployed = false;

  function copyRecursive(src: string, dest: string): void {
    if (!fs.existsSync(dest)) {
      fs.mkdirSync(dest, { recursive: true });
    }
    for (const entry of fs.readdirSync(src, { withFileTypes: true })) {
      const srcPath = path.join(src, entry.name);
      const destPath = path.join(dest, entry.name);

      // Skip build artifacts
      if (
        entry.name === "Binaries" ||
        entry.name === "Intermediate" ||
        entry.name === "Saved"
      ) {
        continue;
      }

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

  copyRecursive(sourcePluginDir, targetPluginDir);
  return anyDeployed;
}

function ensureCppPluginEnabled(uprojectPath: string): boolean {
  const raw = fs.readFileSync(uprojectPath, "utf-8");
  const root = JSON.parse(raw);

  if (!root.Plugins) root.Plugins = [];

  const already = root.Plugins.some(
    (p: { Name?: string }) => p.Name === "UE_MCP_Bridge",
  );
  if (already) return false;

  root.Plugins.push({
    Name: "UE_MCP_Bridge",
    Enabled: true,
  });
  fs.writeFileSync(uprojectPath, JSON.stringify(root, null, "\t"));
  return true;
}

/* ------------------------------------------------------------------ */
/*  Engine discovery (used by editor-control)                         */
/* ------------------------------------------------------------------ */

export function findEngineInstall(
  engineAssociation: string | null,
): string | null {
  if (!engineAssociation) return null;

  const guidRegex =
    /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
  if (guidRegex.test(engineAssociation)) {
    return findEngineByGuid(engineAssociation);
  }

  return findLauncherEngine(engineAssociation);
}

function findEngineByGuid(guid: string): string | null {
  if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(guid)) {
    debug("deployer", `refusing registry lookup for non-GUID engine association '${guid}'`);
    return null;
  }
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
  } catch (e) {
    debug("deployer", `no registry entry for GUID ${guid}`, e);
  }
  return null;
}

function findLauncherEngine(association: string): string | null {
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
    } catch (e) {
      warn("deployer", `LauncherInstalled.dat at ${launcherDat} could not be parsed - falling back to drive-letter scan`, e);
    }
  }

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
