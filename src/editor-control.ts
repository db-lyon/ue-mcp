import * as fs from "node:fs";
import * as path from "node:path";
import { spawn, execSync } from "child_process";
import type { ProjectContext } from "./project.js";

let editorProcess: ReturnType<typeof spawn> | null = null;

function findUEBuildTool(): string | null {
  const envPath = process.env.UE_BUILD_TOOL_PATH;
  if (envPath) return envPath;

  const versions = ["5.7", "5.6", "5.5", "5.4", "5.3"];
  const basePath = "C:/Program Files/Epic Games";

  for (const version of versions) {
    const buildToolPath = path.join(basePath, `UE_${version}`, "Engine", "Build", "BatchFiles", "Build.bat");
    if (fs.existsSync(buildToolPath)) {
      return buildToolPath;
    }
  }

  return null;
}

function findEditorExecutable(): string | null {
  const envPath = process.env.UE_EDITOR_PATH;
  if (envPath) return envPath;

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
  try {
    execSync('tasklist /FI "IMAGENAME eq UnrealEditor.exe" | find /I "UnrealEditor.exe"', { stdio: "pipe" });
    return true;
  } catch {
    return false;
  }
}

export async function startEditor(project: ProjectContext): Promise<{ success: boolean; message: string }> {
  if (isEditorRunning()) {
    return { success: false, message: "Editor is already running" };
  }

  const editorExe = findEditorExecutable();
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
    editorProcess = spawn(editorExe, [project.projectPath], {
      stdio: "ignore",
      detached: true,
    });

    editorProcess.unref();
    return { success: true, message: `Editor launched: ${editorExe}` };
  } catch (error) {
    return {
      success: false,
      message: `Failed to launch editor: ${error instanceof Error ? error.message : String(error)}`,
    };
  }
}

export async function stopEditor(): Promise<{ success: boolean; message: string }> {
  if (!isEditorRunning()) {
    return { success: false, message: "Editor is not running" };
  }

  try {
    // Graceful close - sends WM_CLOSE, allows save dialogs
    execSync('taskkill /IM UnrealEditor.exe', { stdio: "pipe" });

    // Wait up to 10 seconds for editor to close
    for (let i = 0; i < 10; i++) {
      await new Promise((resolve) => setTimeout(resolve, 1000));
      if (!isEditorRunning()) {
        editorProcess = null;
        return { success: true, message: "Editor closed successfully" };
      }
    }

    // Still running - user may have cancelled save dialog
    return {
      success: false,
      message: "Editor still running (user may have cancelled save dialog). Close manually if needed.",
    };
  } catch (error) {
    return {
      success: false,
      message: `Failed to stop editor: ${error instanceof Error ? error.message : String(error)}`,
    };
  }
}

export async function restartEditor(project: ProjectContext): Promise<{ success: boolean; message: string }> {
  const stopResult = await stopEditor();
  if (!stopResult.success && isEditorRunning()) {
    return { success: false, message: `Failed to stop editor: ${stopResult.message}` };
  }

  // Wait a bit for process to fully terminate
  await new Promise((resolve) => setTimeout(resolve, 2000));

  return startEditor(project);
}
