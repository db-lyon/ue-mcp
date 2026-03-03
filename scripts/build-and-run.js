#!/usr/bin/env node

import { execSync } from 'child_process';
import path from 'path';
import { fileURLToPath } from 'url';
import { dirname } from 'path';
import { log, logSection } from './build-utils.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

function killEditor() {
  try {
    // Check if editor is running
    execSync('tasklist /FI "IMAGENAME eq UnrealEditor.exe" | find /I "UnrealEditor.exe"', { stdio: 'pipe' });
  } catch (e) {
    // Editor not running, that's fine
    return true;
  }

  // Editor is running - try graceful close first (allows save dialog)
  log('Requesting Unreal Editor to close (save your work if prompted)...', 'yellow');
  try {
    // Graceful close - sends WM_CLOSE, allows save dialogs
    execSync('taskkill /IM UnrealEditor.exe', { stdio: 'pipe' });
  } catch (e) {
    // Graceful kill failed, user may have cancelled
  }

  // Wait for editor to close (up to 30 seconds)
  for (let i = 0; i < 30; i++) {
    try {
      execSync('timeout /t 1 /nobreak >nul 2>&1', { stdio: 'pipe' });
      execSync('tasklist /FI "IMAGENAME eq UnrealEditor.exe" | find /I "UnrealEditor.exe"', { stdio: 'pipe' });
      // Still running, keep waiting
    } catch (e) {
      // Editor closed
      log('Unreal Editor closed', 'yellow');
      return true;
    }
  }

  // Still running after 30 seconds - user likely cancelled save dialog
  log('Editor still running - build cancelled. Save your work and try again.', 'red');
  return false;
}

async function main() {
  logSection('UE-MCP Build and Run');

  const buildScript = path.join(__dirname, 'build.js');
  const runScript = path.join(__dirname, 'run.js');

  try {
    // Close editor if running to avoid Live Coding conflicts
    if (!killEditor()) {
      process.exit(1);
    }
    
    // Run build script
    log('Running build...', 'green');
    execSync(`node "${buildScript}"`, { stdio: 'inherit' });
    
    // If build succeeded, run the project
    log('');
    log('Running project...', 'green');
    execSync(`node "${runScript}"`, { stdio: 'inherit' });
    
    process.exit(0);
  } catch (error) {
    // Error output is already handled by the individual scripts
    process.exit(1);
  }
}

// Run the script
main().catch((error) => {
  log(`\nUnexpected error: ${error.message}`, 'red');
  process.exit(1);
});
