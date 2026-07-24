import * as fs from "node:fs";
import * as path from "node:path";
import { spawn, execSync } from "child_process";
import * as net from "net";
import WebSocket from "ws";
import type { ProjectContext } from "./project.js";
import { findEngineInstall } from "./deployer.js";

// editor-control relies on Windows-only tools (tasklist/taskkill, Build.bat).
// The MCP server itself is cross-platform; only process control is gated.
const IS_WINDOWS = process.platform === "win32";

const WINDOWS_ONLY_MSG =
  "editor start/stop/restart is Windows-only. On macOS/Linux, start and stop the Unreal Editor manually; ue-mcp will reconnect when the bridge is reachable.";

/** Read EngineAssociation from a .uproject, or null if unreadable. */
function readEngineAssociation(projectPath: string): string | null {
  try {
    const parsed = JSON.parse(fs.readFileSync(projectPath, "utf-8"));
    return typeof parsed?.EngineAssociation === "string" ? parsed.EngineAssociation : null;
  } catch {
    return null;
  }
}

function findUEBuildTool(engineAssociation?: string | null): string | null {
  const envPath = process.env.UE_BUILD_TOOL_PATH;
  if (envPath) return envPath;

  const scriptName = IS_WINDOWS ? "Build.bat" : "Build.sh";

  // Prefer the engine the project's EngineAssociation actually points at, so a
  // 5.7 project builds with 5.7's Build tool - not whatever version happens to
  // sort first in the fallback search below. The editor launch already respects
  // the association (findEditorExecutable); without this the CLI build could
  // silently compile against a different engine than the editor runs, masking
  // API incompatibilities until the editor's own rebuild fails.
  const associatedRoot = findEngineInstall(engineAssociation ?? null);
  if (associatedRoot) {
    const associatedTool = path.join(associatedRoot, "Engine", "Build", "BatchFiles", scriptName);
    if (fs.existsSync(associatedTool)) return associatedTool;
  }

  const versions = ["5.8", "5.7", "5.6", "5.5", "5.4", "5.3"];

  const searchRoots: string[] = IS_WINDOWS
    ? [
        "C:/Program Files/Epic Games",
        "D:/Program Files/Epic Games",
        "E:/Program Files/Epic Games",
        "C:/Epic Games",
        "D:/Epic Games",
        "E:/Epic Games",
      ]
    : process.platform === "darwin"
      ? ["/Users/Shared/Epic Games"]
      : [
          path.join(process.env.HOME ?? "/home", "UnrealEngine"),
          "/opt/UnrealEngine",
        ];

  for (const basePath of searchRoots) {
    for (const version of versions) {
      const buildToolPath = path.join(basePath, `UE_${version}`, "Engine", "Build", "BatchFiles", scriptName);
      if (fs.existsSync(buildToolPath)) {
        return buildToolPath;
      }
    }
  }

  // Linux source builds: ~/UnrealEngine/Engine/Build/BatchFiles/Build.sh (no version subdir)
  if (!IS_WINDOWS && process.platform !== "darwin") {
    const home = process.env.HOME ?? "/home";
    const sourceBuild = path.join(home, "UnrealEngine", "Engine", "Build", "BatchFiles", "Build.sh");
    if (fs.existsSync(sourceBuild)) return sourceBuild;
  }

  return null;
}

function findEditorExecutable(project?: ProjectContext): string | null {
  const envPath = process.env.UE_EDITOR_PATH;
  if (envPath) return envPath;

  const associatedEngineRoot = findEngineInstall(project?.engineAssociation ?? null);
  if (associatedEngineRoot) {
    const associatedEditorExe = path.join(associatedEngineRoot, "Engine", "Binaries", "Win64", "UnrealEditor.exe");
    if (fs.existsSync(associatedEditorExe)) {
      return associatedEditorExe;
    }
  }

  const buildTool = findUEBuildTool(project?.engineAssociation ?? null);
  if (!buildTool) return null;

  const engineRoot = path.resolve(buildTool, "..", "..", "..", "..");
  const editorExe = path.join(engineRoot, "Engine", "Binaries", "Win64", "UnrealEditor.exe");

  if (fs.existsSync(editorExe)) {
    return editorExe;
  }

  return null;
}

function isEditorRunning(): boolean {
  if (!IS_WINDOWS) return false;
  try {
    // Use /NH (no header) and check output directly — avoids pipe/find issues
    const output = execSync('tasklist /NH /FI "IMAGENAME eq UnrealEditor.exe"', {
      stdio: "pipe",
      encoding: "utf-8",
    });
    // tasklist returns "INFO: No tasks..." when not found, or the process line when found
    return output.toLowerCase().includes("unrealeditor.exe");
  } catch {
    return false;
  }
}

async function isBridgeAvailable(host = process.env.UE_MCP_HOST ?? "127.0.0.1", port = 9877, timeoutMs = 1000): Promise<boolean> {
  return new Promise((resolve) => {
    const socket = new net.Socket();
    let resolved = false;

    const timer = setTimeout(() => {
      if (!resolved) {
        resolved = true;
        socket.destroy();
        resolve(false);
      }
    }, timeoutMs);

    socket.once("connect", () => {
      if (!resolved) {
        resolved = true;
        clearTimeout(timer);
        socket.destroy();
        resolve(true);
      }
    });

    socket.once("error", () => {
      if (!resolved) {
        resolved = true;
        clearTimeout(timer);
        resolve(false);
      }
    });

    socket.connect(port, host);
  });
}

async function waitForBridge(maxWaitSeconds = 120, checkIntervalMs = 2000): Promise<boolean> {
  const startTime = Date.now();
  const maxWaitMs = maxWaitSeconds * 1000;

  while (Date.now() - startTime < maxWaitMs) {
    if (await isBridgeAvailable()) {
      return true;
    }
    await new Promise((resolve) => setTimeout(resolve, checkIntervalMs));
  }

  return false;
}

export async function startEditor(project: ProjectContext): Promise<{ success: boolean; message: string }> {
  if (!IS_WINDOWS) return { success: false, message: WINDOWS_ONLY_MSG };
  if (isEditorRunning()) {
    return { success: false, message: "Editor is already running" };
  }

  const editorExe = findEditorExecutable(project);
  if (!editorExe) {
    return {
      success: false,
      message: "Unreal Editor executable not found. Set UE_EDITOR_PATH environment variable or install UE5.3+ to default location.",
    };
  }

  if (!project.projectPath) {
    return { success: false, message: "No project loaded. Use project(action='set_project') first." };
  }

  try {
    const editorProcess = spawn(editorExe, [project.projectPath], {
      stdio: "ignore",
      detached: true,
    });

    editorProcess.unref();

    // Wait for bridge to become available (editor fully started)
    const bridgeAvailable = await waitForBridge(120, 2000);
    if (!bridgeAvailable) {
      return {
        success: false,
        message: "Editor launched but bridge did not become available within 120 seconds. Editor may still be starting up.",
      };
    }

    return { success: true, message: `Editor launched and bridge available: ${editorExe}` };
  } catch (error) {
    return {
      success: false,
      message: `Failed to launch editor: ${error instanceof Error ? error.message : String(error)}`,
    };
  }
}

// Ask the editor to quit ITSELF, on the game thread, via a deferred slate tick
// so the bridge can reply before the process exits. This is a clean in-process
// exit, not an OS kill.
const EDITOR_SELF_QUIT_PY = [
  "import unreal",
  "def _ue_mcp_quit(dt):",
  "    try:",
  "        unreal.SystemLibrary.quit_editor()",
  "    except Exception as e:",
  "        unreal.log_error('ue-mcp quit_editor failed: ' + str(e))",
  "unreal.register_slate_post_tick_callback(_ue_mcp_quit)",
].join("\n");

/** Read the project's live bridge port from its lockfile, else env, else 9877. */
function resolveBridgePort(projectDir?: string): number {
  if (projectDir) {
    try {
      const raw = fs.readFileSync(path.join(projectDir, "Saved", "UE_MCP_Bridge", "port.json"), "utf-8");
      const p = JSON.parse(raw) as { port?: unknown };
      if (typeof p.port === "number" && p.port > 0) return p.port;
    } catch { /* fall through to defaults */ }
  }
  const env = Number(process.env.UE_MCP_PORT);
  return Number.isFinite(env) && env > 0 ? env : 9877;
}

/**
 * Ask the editor to quit itself via the bridge (`execute_python` -> quit_editor).
 * Returns true if the request was delivered. Never touches the OS process table.
 */
function requestEditorSelfQuit(port: number): Promise<boolean> {
  return new Promise<boolean>((resolve) => {
    let settled = false;
    const ws = new WebSocket(`ws://127.0.0.1:${port}`);
    const finish = (v: boolean) => {
      if (settled) return;
      settled = true;
      try { ws.close(); } catch { /* ignore */ }
      resolve(v);
    };
    const timer = setTimeout(() => finish(false), 8000);
    ws.on("open", () => ws.send(JSON.stringify({ id: "ue-mcp-stop", method: "execute_python", params: { code: EDITOR_SELF_QUIT_PY } })));
    ws.on("message", () => { clearTimeout(timer); finish(true); });
    ws.on("error", () => { clearTimeout(timer); finish(false); });
  });
}

/**
 * Stop the editor by asking it to quit ITSELF through the bridge. ue-mcp NEVER
 * issues an OS kill: `taskkill /IM UnrealEditor.exe` matches by image name and
 * would also close the user's other editors (e.g. their real project). `force`
 * is accepted for back-compat but there is deliberately no force-kill path.
 * Success is confirmed by the project's own bridge port going quiet, so it is
 * specific to this editor even when others are open.
 */
export async function stopEditor(force = false, projectDir?: string): Promise<{ success: boolean; message: string }> {
  void force;
  if (!IS_WINDOWS) return { success: false, message: WINDOWS_ONLY_MSG };

  const port = resolveBridgePort(projectDir);
  const bridgeUp = await isBridgeAvailable("127.0.0.1", port);
  if (!bridgeUp && !isEditorRunning()) {
    return { success: false, message: "Editor is not running" };
  }
  if (!bridgeUp) {
    return {
      success: false,
      message: "Editor appears to be running but its bridge is unreachable, so it cannot be asked to quit cleanly. Close it manually - ue-mcp never force-kills processes.",
    };
  }

  const quitSent = await requestEditorSelfQuit(port);
  if (!quitSent) {
    return {
      success: false,
      message: "Could not deliver a quit request to the editor bridge. Close the editor manually - ue-mcp never force-kills processes.",
    };
  }

  // Confirm via the project's own bridge port closing - specific to this editor.
  for (let i = 0; i < 20; i++) {
    await new Promise((resolve) => setTimeout(resolve, 1000));
    if (!(await isBridgeAvailable("127.0.0.1", port))) {
      return { success: true, message: "Editor quit itself via the bridge" };
    }
  }
  return {
    success: false,
    message: "Asked the editor to quit but its bridge is still up after 20s. Close it manually - ue-mcp never force-kills processes.",
  };
}

export async function restartEditor(project: ProjectContext, bridge?: { connect: (timeoutMs?: number) => Promise<void> }): Promise<{ success: boolean; message: string }> {
  const stopResult = await stopEditor(false, project.projectDir ?? undefined);
  if (!stopResult.success && isEditorRunning()) {
    return { success: false, message: `Failed to stop editor: ${stopResult.message}` };
  }

  // Wait for process to fully terminate and release locks
  await new Promise((resolve) => setTimeout(resolve, 3000));

  const startResult = await startEditor(project);
  if (!startResult.success) {
    return startResult;
  }

  // Reconnect the bridge if provided
  if (bridge) {
    try {
      await bridge.connect(5000);
    } catch {
      // Bridge reconnect timer will handle it
    }
  }

  return startResult;
}

export interface BuildResult {
  success: boolean;
  message: string;
  exitCode: number | null;
}

function getPlatformString(): string {
  if (IS_WINDOWS) return "Win64";
  if (process.platform === "darwin") return "Mac";
  return "Linux";
}

export async function buildProject(
  projectPath: string,
  opts: { onOutput?: (line: string) => void } = {},
): Promise<BuildResult> {
  const resolvedPath = path.resolve(projectPath);
  const buildTool = findUEBuildTool(readEngineAssociation(resolvedPath));
  if (!buildTool) {
    return {
      success: false,
      exitCode: null,
      message:
        "Unreal Engine build tool not found. Set UE_BUILD_TOOL_PATH or install UE5.3+ to a default location.",
    };
  }

  if (!fs.existsSync(resolvedPath)) {
    return { success: false, exitCode: null, message: `Project file not found: ${resolvedPath}` };
  }

  const projectName = path.basename(resolvedPath, ".uproject");
  const target = `${projectName}Editor`;
  const platform = getPlatformString();

  const buildArgs = [target, platform, "Development", `-Project="${resolvedPath}"`, "-WaitMutex", "-FromMsBuild"];

  return new Promise((resolve) => {
    let proc;
    if (IS_WINDOWS) {
      const quotedCommand = `"${buildTool}"`;
      const fullCommand = `cmd /c "${quotedCommand} ${buildArgs.join(" ")}"`;
      proc = spawn(fullCommand, [], { shell: true, stdio: "pipe" });
    } else {
      proc = spawn(buildTool, buildArgs, { stdio: "pipe" });
    }

    const forward = (data: Buffer) => {
      const text = data.toString();
      if (opts.onOutput) opts.onOutput(text);
      else process.stdout.write(text);
    };

    if (proc.stdout) proc.stdout.on("data", forward);
    if (proc.stderr) proc.stderr.on("data", forward);

    proc.on("close", (code) => {
      resolve(
        code === 0
          ? { success: true, exitCode: 0, message: "Build succeeded" }
          : { success: false, exitCode: code, message: `Build failed with exit code ${code}` },
      );
    });

    proc.on("error", (err) => {
      resolve({ success: false, exitCode: null, message: `Build error: ${err.message}` });
    });
  });
}
