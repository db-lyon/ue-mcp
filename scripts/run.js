#!/usr/bin/env node

import { spawn } from 'child_process';
import path from 'path';
import fs from 'fs';
import { fileURLToPath } from 'url';
import { dirname } from 'path';
import { log, logSection, getProjectPaths, findUEBuildTool } from './build-utils.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

function findEditorExecutable() {
  // Check for environment variable override first
  const envPath = process.env.UE_EDITOR_PATH;
  if (envPath) {
    return envPath;
  }

  // Find build tool to get engine root
  const buildTool = findUEBuildTool();
  if (!buildTool) {
    return null;
  }

  // Extract engine root from build tool path
  // Build.bat is at Engine/Build/BatchFiles/Build.bat
  // Go up 4 levels to get to UE_X.X root, then Engine/Binaries/Win64/UnrealEditor.exe
  const engineRoot = path.resolve(buildTool, '..', '..', '..', '..');
  const editorExe = path.join(engineRoot, 'Engine', 'Binaries', 'Win64', 'UnrealEditor.exe');
  
  if (fs.existsSync(editorExe)) {
    return editorExe;
  }

  return null;
}

async function main() {
  logSection('UE-MCP Run');

  const { projectFile } = getProjectPaths();
  const editorExe = findEditorExecutable();

  if (!editorExe) {
    log('ERROR: Unreal Editor executable not found!', 'red');
    log('');
    log('Please either:');
    log('  1. Install UE5.3+ to default location, OR');
    log('  2. Set UE_EDITOR_PATH environment variable to your UnrealEditor.exe path');
    log('');
    log('Example: set UE_EDITOR_PATH=C:\\Program Files\\Epic Games\\UE_5.7\\Engine\\Binaries\\Win64\\UnrealEditor.exe');
    process.exit(1);
  }

  log(`Project File: ${projectFile}`);
  log(`Editor: ${editorExe}`);
  log('');

  log('Launching Unreal Editor...', 'green');
  
  // Launch editor with project file
  const proc = spawn(editorExe, [projectFile], {
    stdio: 'inherit',
    detached: true,
  });

  proc.unref(); // Allow parent process to exit
  log('Editor launched!', 'green');
}

main().catch((error) => {
  log(`\nUnexpected error: ${error.message}`, 'red');
  process.exit(1);
});
