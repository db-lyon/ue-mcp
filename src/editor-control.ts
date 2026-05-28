import * as fs from "node:fs";
import * as path from "node:path";
import { spawn, execSync } from "child_process";
import * as net from "net";
import type { ProjectContext } from "./project.js";
import { findEngineInstall } from "./deployer.js";

// editor-control relies on Windows-only tools (tasklist/taskkill, Build.bat).
// The MCP server itself is cross-platform; only process control is gated.
const IS_WINDOWS = process.platform === "win32";

const WINDOWS_ONLY_MSG =
  "editor start/stop/restart is Windows-only. On macOS/Linux, start and stop the Unreal Editor manually; ue-mcp will reconnect when the bridge is reachable.";

function findUEBuildTool(): string | null {
  const envPath = process.env.UE_BUILD_TOOL_PATH;
  if (envPath) return envPath;

  const versions = ["5.7", "5.6", "5.5", "5.4", "5.3"];
  const scriptName = IS_WINDOWS ? "Build.bat" : "Build.sh";

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

  const buildTool = findUEBuildTool();
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

export async function stopEditor(force = false): Promise<{ success: boolean; message: string }> {
  if (!IS_WINDOWS) return { success: false, message: WINDOWS_ONLY_MSG };
  const processRunning = isEditorRunning();
  const bridgeUp = await isBridgeAvailable();

  if (!processRunning && !bridgeUp) {
    return { success: false, message: "Editor is not running" };
  }

  try {
    if (force) {
      execSync('taskkill /F /IM UnrealEditor.exe', { stdio: "pipe" });
      return { success: true, message: "Editor force-killed" };
    }

    // Graceful close - sends WM_CLOSE, allows save dialogs
    execSync('taskkill /IM UnrealEditor.exe', { stdio: "pipe" });

    // Wait up to 10 seconds for editor to close gracefully
    for (let i = 0; i < 10; i++) {
      await new Promise((resolve) => setTimeout(resolve, 1000));
      if (!isEditorRunning()) {
        return { success: true, message: "Editor closed successfully" };
      }
    }

    // Graceful close failed — force kill
    execSync('taskkill /F /IM UnrealEditor.exe', { stdio: "pipe" });
    await new Promise((resolve) => setTimeout(resolve, 2000));

    if (!isEditorRunning()) {
      return { success: true, message: "Editor force-killed after graceful close timed out" };
    }

    return {
      success: false,
      message: "Editor still running after force kill attempt. Close manually.",
    };
  } catch (error) {
    return {
      success: false,
      message: `Failed to stop editor: ${error instanceof Error ? error.message : String(error)}`,
    };
  }
}

export async function restartEditor(project: ProjectContext, bridge?: { connect: (timeoutMs?: number) => Promise<void> }): Promise<{ success: boolean; message: string }> {
  const stopResult = await stopEditor();
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
  const buildTool = findUEBuildTool();
  if (!buildTool) {
    return {
      success: false,
      exitCode: null,
      message:
        "Unreal Engine build tool not found. Set UE_BUILD_TOOL_PATH or install UE5.3+ to a default location.",
    };
  }

  const resolvedPath = path.resolve(projectPath);
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
